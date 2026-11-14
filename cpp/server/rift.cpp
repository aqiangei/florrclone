#include "server/game_server.h"
#include "shared/game/config.h"
#include "server/systems/spawning.h"
#include "server/systems/loot.h"

#include <algorithm>
#include <cmath>

namespace flix {

namespace {

constexpr double kRiftMinute = 60000.0;
constexpr double kRiftAdmission = 3 * kRiftMinute;
constexpr double kRiftMatch = 60 * kRiftMinute;
constexpr double kRiftResults = 3 * kRiftMinute;
constexpr std::size_t kRiftCapacity = 150;
constexpr int kRiftEternalCount = 10;
constexpr Rarity kRiftPrizeRarity = Rarity::Unique;
constexpr const char* kRiftClaimHint =
    "走到地上的花瓣旁拾取冠军奖励，捡齐后自动返回。";

bool isRiftPrize(const Json& prize) {
    return prize["remainingEternals"].isArray() ||
        (prize.isObject() && prize["eternals"].items().size() == kRiftEternalCount) ||
        (prize.isArray() && prize.items().size() == kRiftEternalCount);
}

} // namespace

bool GameServer::isRiftRealm(Realm realm) const {
    const auto* map = worldMaps_.forRealm(realm);
    return map && map->id().rfind("rift_", 0) == 0;
}

bool GameServer::riftAdmissionOpen() const {
    return riftPhase_ == RiftPhase::Open && wallClockMillis_ < riftDeadline_ &&
           riftEntrants_.size() < kRiftCapacity;
}

void GameServer::sendRiftStatus(net::Connection& peer) {
    ByteWriter message;
    message.u8(static_cast<std::uint8_t>(net::ServerMessage::RiftStatus));
    message.boolean(riftAdmissionOpen());
    peer.send(message);
}

void GameServer::broadcastRiftStatus() {
    for (const auto& entry : sessions_) {
        if (!entry.second.authenticated()) continue;
        if (auto* peer = listener_.find(entry.second.connection)) sendRiftStatus(*peer);
    }
}

void GameServer::riftTeleport(Entity entity, Realm realm, Vec2 position) {
    const auto* at = world_.tryGet<Transform>(entity);
    if (!at) return;
    if (!isRiftRealm(at->realm) && isRiftRealm(realm)) {
        Session* session = sessionForEntity(entity);
        const auto* map = worldMaps_.forRealm(realm);
        if (session && session->entity == entity && map && map->id() == "rift_main")
            riftArrivals_.emplace_back(entity, position);
        return;
    }
    if (const auto* map = worldMaps_.forRealm(realm); map && map->id() == "rift_pyramid") {
        bool found = false;
        const Realm variant = worldMaps_.realmOfId(riftPyramid_, found);
        if (found) {
            realm = variant;
            position = worldMaps_.forRealm(realm)->defaultSpawn(rng_, *terrain_);
        }
    }
    moveEntityToRealm(entity, realm, position);
}

void GameServer::enterRift(Session& session, Vec2 position) {
    if (riftPhase_ != RiftPhase::Open || wallClockMillis_ >= riftDeadline_ ||
        riftEntrants_.size() >= kRiftCapacity || riftEntrants_.count(session.userId) ||
        !session.playing() || !isStanding(world_, session.entity) || session.inRift ||
        session.arena || session.split() || session.controlling != 0 || session.controlledBy != 0)
        return;
    bool found = false;
    const Realm main = worldMaps_.realmOfId("rift_main", found);
    if (!found) return;
    const Transform origin = world_.get<Transform>(session.entity);
    const auto viewport = world_.get<PlayerLocation>(session.entity).viewport;
    const NetId netId = world_.get<NetId>(session.entity);
    despawnPlayer(session, true);
    session.inRift = true;
    session.riftReturnRealm = origin.realm;
    session.riftReturnPosition = origin.position;
    session.arena = startArenaRun(PlayerRecord{});
    session.arena->loadout[4] = StoredItem{};
    session.arena->loadout[4]->type = "petal";
    session.arena->loadout[4]->petalType = "rose";
    session.arena->loadout[4]->rarity = Rarity::Common;
    riftEntrants_.insert(session.userId);
    if (riftEntrants_.size() == kRiftCapacity) broadcastRiftStatus();
    session.runLoot.clear();
    const Entity flower = finishPlayerSpawn(session, main, position);
    world_.get<NetId>(flower) = netId;
    world_.get<PlayerLocation>(flower).viewport = viewport;
    sendRealmChange(session, world_.get<Transform>(flower).position);
    if (auto* peer = listener_.find(session.connection)) {
        sendProfile(session, *peer);
        sendSystem(*peer, "已进入裂隙。对局物品独立结算。输入 /rift 查看状态。");
    }
}

void GameServer::recordRiftDeath(Session& session, Entity killer) {
    if (session.riftEliminated || riftPhase_ == RiftPhase::Results) return;
    session.riftEliminated = true;
    const double score = world_.get<ArenaScore>(session.entity).score;
    Session* other = sessionForEntity(killer);
    if (other && other != &session && other->inRift && !other->riftEliminated &&
        other->realm == session.realm && isStanding(world_, other->entity)) {
        world_.get<ArenaScore>(other->entity).score += score * 0.9;
    }
}

void GameServer::leaveRift(Session& session) {
    if (!session.inRift || !world_.isAlive(session.entity)) return;
    const Vec2 viewport = world_.get<PlayerLocation>(session.entity).viewport;
    const NetId netId = world_.get<NetId>(session.entity);
    const Vec2 position = session.riftReturnPosition;
    const Realm realm = session.riftReturnRealm;
    despawnPlayer(session, false);
    const Entity flower = spawnPlayerNear(session, realm, position);
    world_.get<NetId>(flower) = netId;
    world_.get<PlayerLocation>(flower).viewport = viewport;
    world_.ensure<TeleporterState>(flower).cooldownUntilMillis = clockMillis_ + kTeleporterCooldownMillis;
    session.runLoot.clear();
    sendRealmChange(session, world_.get<Transform>(flower).position);
    if (auto* peer = listener_.find(session.connection)) {
        sendProfile(session, *peer);
        ByteWriter w;
        w.u8(static_cast<std::uint8_t>(net::ServerMessage::Revived));
        w.str("");
        peer->send(w);
    }
}

void GameServer::serviceRift(bool settle) {
    if (riftDeadline_ == 0) {
        bool found = false;
        worldMaps_.realmOfId("rift_main", found);
        spawning_->setRiftSpawning(0.0, {});
        riftDeadline_ = found ? wallClockMillis_ + rng_.range(120, 240) * kRiftMinute
                              : std::numeric_limits<double>::infinity();
    }
    if (!settle) {
        if (riftPhase_ == RiftPhase::Running)
            spawning_->setRiftSpawning(1.0 - (riftDeadline_ - wallClockMillis_) / kRiftMatch, riftPyramid_);
        if (wallClockMillis_ < riftDeadline_) return;
        if (riftPhase_ == RiftPhase::Results) {
            for (auto& entry : sessions_) {
                if (!entry.second.riftPrizeRoom) leaveRift(entry.second);
            }
            bool victoryFound = false;
            const Realm victory = worldMaps_.realmOfId("rift_victory", victoryFound);
            Query<Transform>{world_}.each([&](Entity e, Transform& at) {
                if ((!victoryFound || at.realm != victory) && isRiftRealm(at.realm)) commands_.destroy(e);
            });
            commands_.flush();
            spawning_->clearRiftPopulation();
            spawning_->setRiftSpawning(0.0, {});
            riftPhase_ = RiftPhase::Cooldown;
            riftDeadline_ = wallClockMillis_ + rng_.range(120, 240) * kRiftMinute;
        } else if (riftPhase_ == RiftPhase::Cooldown) {
            static constexpr const char* layouts[] = {
                "rift_pyramid", "rift_pyramid_alt", "rift_pyramid_west", "rift_pyramid_east"
            };
            riftPyramid_ = layouts[rng_.below(4)];
            spawning_->setRiftSpawning(0.0, riftPyramid_);
            riftPhase_ = RiftPhase::Open;
            riftDeadline_ = wallClockMillis_ + kRiftAdmission;
            broadcastRiftStatus();
            broadcastChat(net::ChatChannel::System, "",
                "裂隙开放 3 分钟。请从花园、沙漠或丛林的裂隙传送门进入。");
        } else if (riftPhase_ == RiftPhase::Open) {
            riftPhase_ = RiftPhase::Running;
            riftDeadline_ = wallClockMillis_ + kRiftMatch;
            broadcastRiftStatus();
        }
        return;
    }
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        if (!session.riftPrizeComplete) continue;
        spawnRiftPrizes(session);
        if (session.riftPrizeDrops.empty()) leaveRift(session);
    }
    if (riftPhase_ == RiftPhase::Cooldown || riftPhase_ == RiftPhase::Results) return;
    bool helFound = false;
    const Realm hel = worldMaps_.realmOfId("rift_hel", helFound);
    Session* last = nullptr;
    double highScore = -1;
    bool scoreTied = false;
    int standing = 0;
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        if (!session.inRift || session.riftPrizeRoom || !session.playing()) continue;
        if (!session.riftEliminated && isStanding(world_, session.entity)) {
            const double score = world_.get<ArenaScore>(session.entity).score;
            if (score > highScore) {
                last = &session;
                highScore = score;
                scoreTied = false;
            } else if (score == highScore) {
                scoreTied = true;
            }
            ++standing;
            continue;
        }
        if (!helFound || session.realm == hel) continue;

        // A defeated player may look around Hel with an empty kit. Their
        // original flower stays eliminated even after rebuilding the body.
        const NetId netId = world_.get<NetId>(session.entity);
        const Vec2 viewport = world_.get<PlayerLocation>(session.entity).viewport;
        destroyBody(session.entity);
        session.arena = std::make_unique<PlayerRecord>();
        const Vec2 arrival = worldMaps_.forRealm(hel)->defaultSpawn(rng_, *terrain_);
        const Entity flower = finishPlayerSpawn(session, hel, arrival);
        world_.get<NetId>(flower) = netId;
        world_.get<PlayerLocation>(flower).viewport = viewport;
        world_.get<Health>(flower).invulnerableUntilMillis = std::numeric_limits<double>::infinity();
        sendRealmChange(session, world_.get<Transform>(flower).position);
        if (auto* peer = listener_.find(session.connection)) {
            sendProfile(session, *peer);
            ByteWriter w;
            w.u8(static_cast<std::uint8_t>(net::ServerMessage::Revived));
            w.str("");
            peer->send(w);
            sendSystem(*peer, "你已被淘汰。输入 /rift leave 返回原地图。");
        }
    }
    if (riftPhase_ != RiftPhase::Running ||
        (wallClockMillis_ < riftDeadline_ && standing > 1)) return;
    riftPhase_ = RiftPhase::Results;
    riftDeadline_ = wallClockMillis_ + kRiftResults;
    const bool hasWinner = last && !scoreTied && riftEntrants_.size() >= 2;
    riftEntrants_.clear();
    if (!hasWinner) {
        for (auto& entry : sessions_) {
            if (!entry.second.riftPrizeRoom) leaveRift(entry.second);
        }
        return;
    }

    PlayerRecord& account = database_.progress(last->userId);
    // Save both rolls before the champion can claim or reconnect.
    Json& pending = account.extra["riftPrizes"];
    if (!pending.isArray()) pending = Json::array();
    std::vector<std::uint16_t> pool;
    for (std::size_t i = 0; i < content().petalCount(); ++i) {
        const auto& petal = content().petal(i);
        if (petal.isAdminPetal || petalHasOwnSource(petal) || !petalComesAt(petal, kRiftPrizeRarity)) continue;
        pool.push_back(static_cast<std::uint16_t>(i));
    }
    Json rolled = Json::array();
    for (int i = 0; i < kRiftEternalCount && !pool.empty(); ++i)
        rolled.push(Json("petal_" + content().petal(pool[rng_.below(static_cast<std::uint32_t>(pool.size()))]).id));
    if (rolled.items().size() == kRiftEternalCount) {
        Json prize = Json::object();
        prize["eternals"] = std::move(rolled);
        prize["permanentHyper"] = "petal_" +
            content().petal(pool[rng_.below(static_cast<std::uint32_t>(pool.size()))]).id;
        pending.push(std::move(prize));
    }
    database_.markDirty();

    if (const Entity prizeBody = enterRiftPrizeRoom(*last); prizeBody != NULL_ENTITY) {
        sendRealmChange(*last, world_.get<Transform>(prizeBody).position);
        if (auto* peer = listener_.find(last->connection)) {
            sendProfile(*last, *peer);
            sendSystem(*peer, std::string("你赢得了裂隙试炼！") + kRiftClaimHint);
        }
    } else {
        leaveRift(*last);
    }
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        if (&session != last && session.inRift && !session.riftPrizeRoom && session.realm != hel)
            leaveRift(session);
    }
}

Entity GameServer::enterRiftPrizeRoom(Session& session) {
    bool found = false;
    const Realm victory = worldMaps_.realmOfId("rift_victory", found);
    if (!found || session.userId.empty()) return NULL_ENTITY;
    const auto* account = database_.findProgress(session.userId);
    if (!account) return NULL_ENTITY;
    const auto& prizes = account->extra["riftPrizes"].items();
    if (prizes.empty() || !isRiftPrize(prizes.front())) return NULL_ENTITY;

    const Transform origin = world_.get<Transform>(session.entity);
    const Vec2 viewport = world_.get<PlayerLocation>(session.entity).viewport;
    const NetId id = world_.get<NetId>(session.entity);
    const Realm returnRealm = session.inRift ? session.riftReturnRealm : origin.realm;
    const Vec2 returnPosition = session.inRift ? session.riftReturnPosition : origin.position;
    despawnPlayer(session, !session.inRift);
    session.inRift = true;
    session.riftPrizeRoom = true;
    session.riftEliminated = true; // No combat or score while collecting prizes.
    session.riftReturnRealm = returnRealm;
    session.riftReturnPosition = returnPosition;
    session.arena = std::make_unique<PlayerRecord>();
    const Vec2 arrival = worldMaps_.forRealm(victory)->defaultSpawn(rng_, *terrain_);
    const Entity body = finishPlayerSpawn(session, victory, arrival);
    world_.get<NetId>(body) = id;
    world_.get<PlayerLocation>(body).viewport = viewport;
    world_.get<Health>(body).invulnerableUntilMillis = std::numeric_limits<double>::infinity();
    spawnRiftPrizes(session);
    return body;
}

void GameServer::spawnRiftPrizes(Session& session) {
    session.riftPrizeDrops.clear();
    session.riftPrizeComplete = false;
    auto& pending = database_.progress(session.userId).extra["riftPrizes"].items();
    if (pending.empty() || !isRiftPrize(pending.front())) return;
    Json& prize = pending.front();
    const Json& savedPrize = prize;
    if (!savedPrize["remainingEternals"].isArray()) {
        Json current = Json::object();
        current["remainingEternals"] = savedPrize.isObject() ? savedPrize["eternals"] : savedPrize;
        current["remainingEternals"].push(Json("petal_fission"));
        if (savedPrize.isObject()) current["permanentHyper"] = savedPrize["permanentHyper"];
        prize = std::move(current);
        database_.markDirty();
    }

    const auto& eternals = prize["remainingEternals"].items();
    const std::string hyper = static_cast<const Json&>(prize)["permanentHyper"].asString();
    const auto count = eternals.size() + (hyper.empty() ? 0 : 1);
    const Vec2 entry = world_.get<Transform>(session.entity).position;
    const Vec2 centre = entry + Vec2{0, 470};
    for (std::size_t i = 0; i < count; ++i) {
        const Rarity rarity = i < eternals.size() ? kRiftPrizeRarity : Rarity::PermanentHyper;
        const std::string key = i < eternals.size() ? eternals[i].asString() : hyper;
        const auto petal = key.rfind("petal_", 0) == 0 ? content().petalIndex(key.substr(6)) : kInvalidIndex;
        if (petal == kInvalidIndex) continue;
        const double angle = 6.283185307179586 * i / count;
        const Vec2 position = centre + Vec2{std::cos(angle), std::sin(angle)} * 225.0;
        const Entity drop = loot_->spawnDrop(world_, petal, rarity, position, session.realm, {session.entity});
        auto& item = world_.get<DropItem>(drop);
        item.riftPrize = item.singleCopy = item.humansOnly = true;
        world_.get<Lifetime>(drop).remainingSeconds = std::numeric_limits<double>::infinity();
        session.riftPrizeDrops.push_back(drop);
    }
}

bool GameServer::collectRiftPrize(Entity player, std::uint16_t petal, Rarity rarity) {
    Session* session = sessionForEntity(player);
    if (!session || !session->riftPrizeRoom || session->entity != player) return false;
    auto& account = database_.progress(session->userId);
    auto& pending = account.extra["riftPrizes"].items();
    if (pending.empty()) return false;
    auto& prize = pending.front();
    auto& eternals = prize["remainingEternals"].items();
    const std::string key = "petal_" + content().petal(petal).id;
    const auto match = std::find_if(eternals.begin(), eternals.end(),
        [&](const Json& item) { return item.asString() == key; });
    if (rarity == Rarity::PermanentHyper) {
        if (prize["permanentHyper"].asString() != key) return false;
    } else if (rarity != kRiftPrizeRarity || match == eternals.end()) {
        return false;
    }

    if (account.itemCount(rarity, key) >= kMaxStackCount) {
        if (!session->riftPrizeFullNotice) {
            session->riftPrizeFullNotice = true;
            if (auto* peer = listener_.find(session->connection))
                sendSystem(*peer, "这件花瓣的库存已满。奖励会保留，清理库存后重新进入可继续拾取。");
        }
        return false;
    }
    account.addItem(rarity, key, 1);
    if (rarity == Rarity::PermanentHyper) prize["permanentHyper"] = Json();
    else eternals.erase(match);
    if (eternals.empty() && prize["permanentHyper"].asString().empty()) {
        pending.erase(pending.begin());
        session->riftPrizeComplete = true;
    }
    database_.markDirty();
    return true;
}

void GameServer::clearRiftPrizes(Session& session) {
    for (const Entity drop : session.riftPrizeDrops) world_.destroy(drop);
    session.riftPrizeDrops.clear();
    session.riftPrizeRoom = false;
    session.riftPrizeComplete = false;
    session.riftPrizeFullNotice = false;
}

void GameServer::handleRiftCommand(Session& session, net::Connection& peer, const std::string& argument) {
    if (argument == "leave" && session.inRift) {
        leaveRift(session);
        return;
    }
    if (session.riftPrizeRoom) {
        sendSystem(peer, kRiftClaimHint);
        return;
    }

    if (!std::isfinite(riftDeadline_)) {
        sendSystem(peer, "本服未启用裂隙。");
        return;
    }
    const auto minutes = static_cast<int>(std::ceil(std::max(0.0, riftDeadline_ - wallClockMillis_) / kRiftMinute));
    if (riftPhase_ == RiftPhase::Open)
        sendSystem(peer, "裂隙可报名，剩余 " + std::to_string(minutes) + " 分钟。"
            "从花园、沙漠或丛林的裂隙传送门进入。");
    else if (riftPhase_ == RiftPhase::Running)
        sendSystem(peer, "裂隙进行中，剩余 " + std::to_string(minutes) +
            " 分钟。输入 /rift leave 可退出。");
    else if (riftPhase_ == RiftPhase::Results)
        sendSystem(peer, "本局裂隙已结束。输入 /rift leave 可离开。");
    else
        sendSystem(peer, "距离 裂隙开放约 " + std::to_string(minutes) + " 分钟。");

}

} // namespace flix
