#include "server/squads.h"

#include <algorithm>

namespace flix {

namespace {

/// `'squad_' + Math.random().toString(36).substr(2, 9)`, which is what the
/// reference mints. The alphabet and the length matter because the id is typed
/// back in as `/squad-join <id>`.
std::string mintSquadId(Rng& rng) {
    static const char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string id = "squad_";
    id.reserve(15);
    for (int i = 0; i < 9; ++i) id.push_back(kAlphabet[rng.below(36)]);
    return id;
}

} // namespace

bool Squad::contains(SquadMemberId who) const {
    return std::find(members.begin(), members.end(), who) != members.end();
}

Squad* SquadRoster::find(const std::string& id) {
    const auto it = squads_.find(id);
    return it == squads_.end() ? nullptr : &it->second;
}

Squad* SquadRoster::forMember(SquadMemberId who) {
    if (!who.valid()) return nullptr;
    for (auto& entry : squads_) {
        if (entry.second.contains(who)) return &entry.second;
    }
    return nullptr;
}

Squad* SquadRoster::create(SquadMemberId leader, bool isPublic, Rng& rng) {
    if (!leader.valid()) return nullptr;
    if (forMember(leader) != nullptr) return nullptr;

    Squad squad;
    // Minted until it is unused. One collision in 36^9 is not a thing that
    // happens, but a squad silently replacing another one if it did is.
    do {
        squad.id = mintSquadId(rng);
    } while (squads_.count(squad.id) != 0);
    squad.leader = leader;
    squad.members.push_back(leader);
    // A bot-led squad is always public, so humans can find and join it.
    squad.isPublic = leader.bot() ? true : isPublic;

    const std::string id = squad.id;
    squads_[id] = std::move(squad);
    return &squads_[id];
}

std::string SquadRoster::joinPublic(const std::string& squadId, SquadMemberId who) {
    Squad* squad = find(squadId);
    if (squad == nullptr) return "Squad not found.";
    if (!squad->isPublic) return "That squad is private.";
    if (!who.valid()) return "Squad not found.";
    if (squad->full()) return "Squad is full.";
    if (forMember(who) != nullptr) return "You are already in a squad.";
    squad->members.push_back(who);
    invites_.erase(who.connection);
    return {};
}

const Squad* SquadRoster::findByCode(const std::string& code) const {
    if (code.size() != 3) return nullptr;
    for (const auto& [id, squad] : squads_) {
        if (squad.code == code) return &squad;
    }
    return nullptr;
}

const std::string& SquadRoster::issueCode(Squad& squad, Rng& rng) {
    if (!squad.code.empty()) return squad.code;
    const unsigned start = rng.below(1000);
    for (unsigned offset = 0; offset < 1000; ++offset) {
        const unsigned value = (start + offset) % 1000;
        std::string code = std::to_string(1000 + value).substr(1);
        if (findByCode(code)) continue;
        squad.code = std::move(code);
        break;
    }
    return squad.code;
}

std::string SquadRoster::joinWithCode(const std::string& code, SquadMemberId who,
                                     Departure& leftPrevious) {
    leftPrevious = {};
    const Squad* target = findByCode(code);
    if (!target || !who.valid()) return "Squad not found.";
    if (target->contains(who)) return "You are already in a squad.";
    if (target->full()) return "Squad is full.";
    Squad* squad = find(target->id);
    squad->members.reserve(squad->members.size() + 1);
    leftPrevious = leave(who);
    squad->members.push_back(who);
    return {};
}

std::string SquadRoster::addBot(const std::string& squadId, SquadMemberId bot) {
    Squad* squad = find(squadId);
    if (squad == nullptr) return "Squad not found.";
    if (squad->full()) return "Squad is full.";
    if (forMember(bot) != nullptr) return "Player already has a squad.";
    squad->members.push_back(bot);
    return {};
}

std::string SquadRoster::setVisibility(SquadMemberId leader, bool isPublic, Squad** out) {
    Squad* squad = forMember(leader);
    if (squad == nullptr) return "You are not in a squad.";
    if (!(squad->leader == leader)) return "Only the squad leader can change visibility.";
    squad->isPublic = isPublic;
    if (out != nullptr) *out = squad;
    return {};
}

std::vector<const Squad*> SquadRoster::publicSquads() const {
    std::vector<const Squad*> out;
    for (const auto& entry : squads_) {
        if (entry.second.isPublic && !entry.second.full()) out.push_back(&entry.second);
    }
    // The map's own order is an implementation detail and the listing is read
    // by a person, so it is sorted into something stable.
    std::sort(out.begin(), out.end(),
              [](const Squad* a, const Squad* b) { return a->id < b->id; });
    return out;
}

std::string SquadRoster::invite(SquadMemberId from, SquadMemberId to, std::int64_t nowMillis) {
    Squad* squad = forMember(from);
    if (squad == nullptr) return "You are not in a squad.";
    // Any member may invite — AshFlor: filling a party from every seat.
    if (squad->full()) return "Squad is full (max 4 players).";
    // Already in *this* squad: refuse. Someone in another party can still be
    // invited; accept() will pull them across.
    if (squad->contains(to)) return "That player is already in a squad.";

    const auto pending = invites_.find(to.connection);
    if (pending != invites_.end() && pending->second.expiresAtMillis > nowMillis) {
        return "That player already has a pending invite.";
    }

    invites_[to.connection] = Invite{squad->id, nowMillis + kSquadInviteMillis};
    return {};
}

const Squad* SquadRoster::pendingInviteSquad(SquadMemberId target,
                                            std::int64_t nowMillis) const {
    const auto it = invites_.find(target.connection);
    if (it == invites_.end() || nowMillis > it->second.expiresAtMillis) return nullptr;
    const auto squad = squads_.find(it->second.squadId);
    return squad == squads_.end() ? nullptr : &squad->second;
}

std::string SquadRoster::accept(SquadMemberId target, std::int64_t nowMillis,
                                std::string& squadIdOut, Departure* leftPrevious) {
    if (leftPrevious != nullptr) *leftPrevious = {};
    const auto it = invites_.find(target.connection);
    if (it == invites_.end()) return "No pending invite.";
    if (nowMillis > it->second.expiresAtMillis) {
        invites_.erase(it);
        return "Invite has expired.";
    }

    const std::string squadId = it->second.squadId;
    invites_.erase(it);

    Squad* squad = find(squadId);
    if (squad == nullptr) return "Squad no longer exists.";
    if (squad->full()) return "Squad is full.";
    if (squad->contains(target)) {
        squadIdOut = squadId;
        return {};
    }

    // Switching parties: leave the old one first. Invite is already consumed,
    // so leave()'s invite erase is a no-op here.
    if (Squad* previous = forMember(target); previous != nullptr) {
        const Departure left = leave(target);
        if (leftPrevious != nullptr) *leftPrevious = left;
    }

    // Re-find: leave() may have erased an empty map entry and moved others,
    // but this squad still had members (the inviters), so its id stays put.
    squad = find(squadId);
    if (squad == nullptr) return "Squad no longer exists.";
    if (squad->full()) return "Squad is full.";

    squad->members.push_back(target);
    squadIdOut = squadId;
    return {};
}

bool SquadRoster::decline(SquadMemberId target) {
    return invites_.erase(target.connection) != 0;
}

SquadRoster::Departure SquadRoster::leave(SquadMemberId who) {
    Departure out;
    Squad* squad = forMember(who);
    // An invitation belongs to whoever was invited, not to the squad, so it is
    // dropped here as well: a player who left has not accepted anything.
    invites_.erase(who.connection);
    if (squad == nullptr) return out;

    out.wasMember = true;
    out.squadId = squad->id;
    squad->members.erase(std::remove(squad->members.begin(), squad->members.end(), who),
                         squad->members.end());

    if (squad->members.empty()) {
        out.disbanded = true;
        squads_.erase(out.squadId);
        return out;
    }

    if (squad->leader == who) {
        squad->leader = squad->members.front();
        // A bot that inherits a squad keeps it public, or the humans left in it
        // would be the last ones who could ever join.
        if (squad->leader.bot()) squad->isPublic = true;
        out.promoted = squad->leader;
    }
    return out;
}

void SquadRoster::expire(std::int64_t nowMillis) {
    for (auto it = invites_.begin(); it != invites_.end();) {
        if (it->second.expiresAtMillis <= nowMillis) it = invites_.erase(it);
        else ++it;
    }
}

} // namespace flix
