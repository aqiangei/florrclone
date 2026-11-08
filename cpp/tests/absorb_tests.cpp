#include "test.h"

#include "server_harness.h"
#include "shared/game/absorb.h"
#include "shared/game/components.h"
#include "shared/game/constants.h"
#include "shared/game/shop.h"

using namespace flix;
using flix::testsupport::awaitProfile;
using flix::testsupport::connectClient;
using flix::testsupport::dataDir;
using flix::testsupport::Harness;
using flix::testsupport::loginAs;
using flix::testsupport::onlyPlayer;
using flix::testsupport::seedStack;
using flix::testsupport::seedUser;

// Absorbing: the craft window's Absorb side, which destroys petals for XP.
// The price is shared/game/absorb.h's; the requests go over a real socket,
// because the Profile that comes back is what the window redraws from.

namespace {

/// Seeds an account's Absorption talent, for the cases about the multiplier.
void seedAbsorbing(const std::string& path, const std::string& username, Rarity tier) {
    Database db;
    std::string error;
    db.load(path, error);
    const Account* account = db.findUser(username);
    if (account == nullptr) return;
    db.progress(account->id).skills.set(SkillId::Absorbing, rarityIndex(tier));
    db.markDirty();
    db.save();
}

/// Sends one Absorb and hands its answer back with `pending` cleared, as the
/// panel reads it.
bool absorb(Harness& h, NetClient& client, const std::vector<NetClient::AbsorbStack>& stacks,
            AbsorbOutcome& out) {
    client.requestAbsorb(stacks);
    if (!h.stepUntil({&client}, [&] { return client.absorbOutcome().pending; }, 200)) return false;
    out = client.absorbOutcome();
    client.absorbOutcome().pending = false;
    return true;
}

} // namespace

TEST(absorb_curve_rewards_new_and_veteran_stock_without_replacing_hunting) {
    CHECK_EQ(absorbXpPerPetal(Rarity::Common),1.0);
    CHECK_EQ(absorbXpPerPetal(Rarity::Super),140000.0);
    CHECK_EQ(absorbXpPerPetal(Rarity::Unique),840000.0);
    CHECK_EQ(absorbXpPerPetal(Rarity::Apex),0.0);
    // Veteran stock remains much more valuable than low-tier duplicates.
    for(int tier=1;tier<=8;++tier)
        CHECK(absorbXpPerPetal(clampRarity(tier))>=4*absorbXpPerPetal(clampRarity(tier-1)));
    // Even with the highest existing talent, one high-tier petal pays less
    // than a same-tier standard kill, so drops supplement hunting.
    SkillSet max;max.set(SkillId::Absorbing,9);
    const double killXp[]={32000,150000,1200000,6800000};
    for(int tier=5;tier<=8;++tier)
        CHECK(absorbXpTotal(absorbUnits(clampRarity(tier)),max)<killXp[tier-5]);
    // Buying and burning cannot turn one star into anything close to the XP
    // earned alongside that star (even a Mythic Rock grants 3,100 XP).
    std::string error;CHECK(loadContent(dataDir(),error));
    for(std::uint16_t i=0;i<content().petalCount();++i)if(shopSellsPetal(i))
        for(int tier=0;tier<=7;++tier) {
            const auto rarity=clampRarity(tier);
            CHECK(absorbXpTotal(absorbUnits(rarity),max)/shopOfferPrice(i,rarity,30)<1000);
        }
}

TEST(absorb_request_rounds_talent_once_and_single_common_never_pays_zero) {
    const SkillSet none;
    CHECK_EQ(absorbXpTotal(absorbUnits(Rarity::Common), none), 1.0);
    CHECK_EQ(absorbXpTotal(49.0 * absorbUnits(Rarity::Common), none), 49.0);
    CHECK_EQ(absorbXpTotal(7.0 * absorbUnits(Rarity::Uncommon), none), 28.0);
    CHECK_EQ(absorbXpTotal(3.0 * absorbUnits(Rarity::Epic) + absorbUnits(Rarity::Rare), none),
             256.0);

    // The talent multiplies before the floor, and a total that is whole on
    // paper stays whole: 50 rares at 2.52x is 2016, not 2015.
    SkillSet legendary;
    legendary.set(SkillId::Absorbing, rarityIndex(Rarity::Legendary));
    CHECK_EQ(absorbXpTotal(50.0 * absorbUnits(Rarity::Rare), legendary), 2016.0);
    SkillSet apex;
    apex.set(SkillId::Absorbing, rarityIndex(Rarity::Apex));
    CHECK_EQ(absorbXpTotal(absorbUnits(Rarity::Rare), apex), 128.0);
}

TEST(the_basic_hyper_and_universals_cannot_be_absorbed) {
    PetalConfig basic;
    basic.id = "basic";
    PetalConfig rose;
    rose.id = "rose";
    CHECK(!absorbable(basic, Rarity::Common));
    CHECK(!absorbable(basic, Rarity::Mythic));
    CHECK(absorbable(rose, Rarity::Common));
    CHECK(absorbable(rose, Rarity::Unique));
    CHECK(!absorbable(rose, Rarity::Apex));
    CHECK(!absorbable(rose, Rarity::Universal));
}

TEST(absorbing_on_the_title_screen_pays_the_account) {
    Harness h("absorb-title", [](const std::string& path) {
        seedUser(path, "eater", "password7");
        seedStack(path, "eater", "petal_rose", Rarity::Rare, 10);
        seedStack(path, "eater", "petal_light", Rarity::Epic, 3);
        seedStack(path, "eater", "petal_light", Rarity::Common, 100);
    });
    if (!h.ready) { CHECK(false); return; }
    const std::uint16_t rose = content().petalIndex("rose");
    const std::uint16_t light = content().petalIndex("light");
    if (rose == kInvalidIndex || light == kInvalidIndex) { CHECK(false); return; }

    NetClient client;
    CHECK(loginAs(h, client, "eater", "password7"));
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Rare) == 10u &&
               p.stackCount(light, Rarity::Common) == 100u;
    }));
    const double before = client.profile().totalXp;

    // Every stack in one request, priced once: 10 x 16 + 2 x 80 + 60.
    AbsorbOutcome outcome;
    CHECK(absorb(h, client, {{light,Rarity::Common,1}},outcome));
    CHECK(outcome.success);CHECK_EQ(outcome.xpGained,1.0);
    CHECK(awaitProfile(h,client,[&](const Profile& p){return p.totalXp==before+1.0;}));
    CHECK(absorb(h, client,
                 {{rose, Rarity::Rare, 10}, {light, Rarity::Epic, 2}, {light, Rarity::Common, 60}},
                 outcome));
    CHECK(outcome.success);
    CHECK_EQ(outcome.xpGained, 380.0);
    CHECK_EQ(outcome.absorbed, 72u);
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Rare) == 0u && p.stackCount(light, Rarity::Epic) == 1u &&
               p.stackCount(light, Rarity::Common) == 39u && p.totalXp == before + 381.0;
    }));
}

TEST(an_absorb_short_of_one_stack_takes_nothing) {
    Harness h("absorb-short", [](const std::string& path) {
        seedUser(path, "eater", "password7");
        seedStack(path, "eater", "petal_rose", Rarity::Rare, 10);
        seedStack(path, "eater", "petal_light", Rarity::Epic, 3);
        seedStack(path, "eater", "petal_basic", Rarity::Rare, 5);
        seedStack(path, "eater", "petal_rose", Rarity::Universal, 1);
    });
    if (!h.ready) { CHECK(false); return; }
    const std::uint16_t rose = content().petalIndex("rose");
    const std::uint16_t light = content().petalIndex("light");
    const std::uint16_t basic = content().petalIndex("basic");
    if (rose == kInvalidIndex || light == kInvalidIndex || basic == kInvalidIndex) {
        CHECK(false);
        return;
    }

    NetClient client;
    CHECK(loginAs(h, client, "eater", "password7"));
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Rare) == 10u;
    }));
    const double before = client.profile().totalXp;

    // One stack the bag cannot cover refuses the lot.
    AbsorbOutcome outcome;
    CHECK(absorb(h, client, {{rose, Rarity::Rare, 10}, {light, Rarity::Epic, 4}}, outcome));
    CHECK(!outcome.success);
    CHECK_EQ(outcome.xpGained, 0.0);
    CHECK(!outcome.reason.empty());
    // Naming a stack twice asks for the sum, and the sum is checked.
    CHECK(absorb(h, client, {{rose, Rarity::Rare, 6}, {rose, Rarity::Rare, 6}}, outcome));
    CHECK(!outcome.success);
    // A basic, at any tier, and a universal are never absorbed.
    CHECK(absorb(h, client, {{rose, Rarity::Rare, 1}, {basic, Rarity::Rare, 5}}, outcome));
    CHECK(!outcome.success);
    CHECK(absorb(h, client, {{rose, Rarity::Universal, 1}}, outcome));
    CHECK(!outcome.success);

    h.step(10, {&client});
    const Profile& p = client.profile();
    CHECK_EQ(p.stackCount(rose, Rarity::Rare), 10u);
    CHECK_EQ(p.stackCount(light, Rarity::Epic), 3u);
    CHECK_EQ(p.stackCount(basic, Rarity::Rare), 5u);
    CHECK_EQ(p.stackCount(rose, Rarity::Universal), 1u);
    CHECK_EQ(p.totalXp, before);
}

TEST(the_absorption_talent_multiplies_what_absorbing_pays) {
    Harness h("absorb-talent", [](const std::string& path) {
        seedUser(path, "eater", "password7");
        seedStack(path, "eater", "petal_rose", Rarity::Rare, 50);
        seedAbsorbing(path, "eater", Rarity::Legendary);
    });
    if (!h.ready) { CHECK(false); return; }
    const std::uint16_t rose = content().petalIndex("rose");
    if (rose == kInvalidIndex) { CHECK(false); return; }

    NetClient client;
    CHECK(loginAs(h, client, "eater", "password7"));
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Rare) == 50u;
    }));
    AbsorbOutcome outcome;
    CHECK(absorb(h, client, {{rose, Rarity::Rare, 50}}, outcome));
    CHECK(outcome.success);
    CHECK_EQ(outcome.xpGained, 2016.0);
}

TEST(absorbing_in_the_world_levels_the_flower_up_on_the_spot) {
    Harness h(
        "absorb-live",
        [](const std::string& path) {
            seedUser(path, "eater", "password7");
            seedStack(path, "eater", "petal_rose", Rarity::Legendary, 2);
        },
        dataDir(), 0);
    if (!h.ready) { CHECK(false); return; }
    const std::uint16_t rose = content().petalIndex("rose");
    if (rose == kInvalidIndex) { CHECK(false); return; }

    NetClient client;
    CHECK(loginAs(h, client, "eater", "password7"));
    client.joinGame(1920, 1080, {}, "Eater");
    CHECK(h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::Playing; },
                      200));
    World& world = h.server.world();
    const Entity body = onlyPlayer(world);
    CHECK(body != NULL_ENTITY);
    if (body == NULL_ENTITY) return;
    const double before = world.get<PlayerProgress>(body).totalXp;
    const int levelBefore = world.get<PlayerProgress>(body).level;

    // Two legendaries grant several levels to a fresh flower immediately.
    constexpr double earnedXp = 1000.0;
    AbsorbOutcome outcome;
    CHECK(absorb(h, client, {{rose, Rarity::Legendary, 2}}, outcome));
    CHECK(outcome.success);
    CHECK_EQ(outcome.xpGained, earnedXp);
    const PlayerProgress& progress = world.get<PlayerProgress>(body);
    CHECK_EQ(progress.totalXp, before + earnedXp);
    CHECK(progress.level > levelBefore);
    CHECK_EQ(progress.level, levelFromTotalXp(before + earnedXp).level);
    // The level-up landed with the XP: the new level's health, full.
    const Health& health = world.get<Health>(body);
    CHECK(health.max > maxHealthForLevel(levelBefore));
    CHECK_EQ(health.current, health.max);
    // And the record was brought level with the body straight away, so the
    // Profile that followed already counts it.
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.totalXp == before + earnedXp && p.stackCount(rose, Rarity::Legendary) == 0u;
    }));
}
