#include "test.h"
#include "test_data.h"
#include "server/hyper_forge.h"
#include "server/timed_hammers.h"
#include "shared/game/constants.h"
#include <cstdio>
using namespace flix;
namespace {
struct Fixture {
    std::string path=testsupport::tempUnique("hyper-forge",".json");Database db;
    std::string a,b;
    Fixture(){std::string error;db.load(path,error);db.setPasswordCost(4);
        a=db.createUser("one","password8").account->id;
        b=db.createUser("two","password8").account->id;}
    ~Fixture(){db.save();std::remove(path.c_str());}
    void give(const std::string& id,const std::string& petal,int n=5){db.progress(id).addItem(Rarity::Unique,"petal_"+petal,n);}
};
}
TEST(hyper_forge_price_and_protection_curve_match_the_requested_progression){
    CHECK_EQ(titanForgeCost(0,"rose"),10);CHECK_EQ(titanForgeCost(1,"rose"),15);CHECK_EQ(titanForgeCost(2000,"rose"),15);
    CHECK_EQ(titanForgeCost(0,"fragment"),5);CHECK_EQ(titanForgeCost(2000,"fragment"),5);
    CHECK_EQ(hyperProtectionMillis(1),15LL*3600000);CHECK_EQ(hyperProtectionMillis(2),14LL*3600000);
    CHECK_EQ(hyperProtectionMillis(15),3600000);CHECK_EQ(hyperProtectionMillis(10000),3600000);
}
TEST(hyper_forge_is_unique_blocks_early_theft_and_refunds_four_inputs){
    Fixture f;f.give(f.a,"rose");f.give(f.b,"rose");
    CHECK(forgeHyper(f.db,f.a,"rose",1000).ok);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_rose"),0);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_rose"),1);
    const auto until=hyperForgeState(f.db,"rose").protectedUntil;
    const auto before=f.db.serialise();CHECK(!forgeHyper(f.db,f.b,"rose",until-1).ok);
    CHECK_EQ(before,f.db.serialise());
    CHECK(forgeHyper(f.db,f.b,"rose",until).ok);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_rose"),0);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_rose"),4);
    CHECK_EQ(f.db.progress(f.b).itemCount(Rarity::Apex,"petal_rose"),1);
    CHECK_EQ(hyperForgeState(f.db,"rose").protectedUntil,until+14LL*3600000);
    CHECK(!forgeHyper(f.db,f.b,"rose",until+100000000).ok);
}
TEST(hyper_forge_removes_equipped_and_preset_copies_without_duplicate_refunds){
    Fixture f;f.give(f.a,"rose");f.give(f.b,"rose");CHECK(forgeHyper(f.db,f.a,"rose",0).ok);
    auto& record=f.db.progress(f.a);record.addItem(Rarity::Apex,"petal_rose",-1);
    StoredItem item;item.petalType="rose";item.rarity=Rarity::Apex;
    record.loadout.resize(20);record.loadout[0]=item;record.loadoutPresets["K1"]={item};
    CHECK(forgeHyper(f.db,f.b,"rose",15LL*3600000).ok);
    CHECK(!f.db.progress(f.a).loadout[0]);CHECK(!f.db.progress(f.a).loadoutPresets["K1"][0]);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_rose"),4);
}
TEST(hyper_forge_persists_owner_counter_and_protection_across_restart){
    Fixture f;f.give(f.a,"rose");f.give(f.b,"rose");CHECK(forgeHyper(f.db,f.a,"rose",1000).ok);
    Database restored;std::string error;CHECK(restored.load(f.path,error));
    CHECK_EQ(hyperForgeState(restored,"rose").owner,f.a);
    CHECK_EQ(hyperForgeState(restored,"rose").claims,1);
    CHECK(!forgeHyper(restored,f.b,"rose",1001).ok);
    CHECK(forgeHyper(restored,f.b,"rose",1000+15LL*3600000).ok);
    CHECK_EQ(hyperForgeState(restored,"rose").claims,2);
}
TEST(hyper_hammer_uses_eternal_fragments_and_does_not_share_timed_hammer_expiry){
    Fixture f;f.give(f.a,"fragment");f.give(f.b,"fragment");
    addHammerLease(f.db.progress(f.a),0);addHammerLease(f.db.progress(f.a),1000);
    CHECK(forgeHyper(f.db,f.a,"fragment",0).ok);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_mjolnir"),1);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_fragment"),0);
    CHECK_EQ(expireHammerLeases(f.db.progress(f.a),3602000),2);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_mjolnir"),1);
    CHECK(forgeHyper(f.db,f.b,"fragment",15LL*3600000).ok);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_fragment"),4);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_mjolnir"),0);
    CHECK_EQ(f.db.progress(f.b).itemCount(Rarity::Apex,"petal_mjolnir"),1);
}
TEST(hyper_forge_requires_five_inputs_and_keeps_unrelated_inventory_intact){
    Fixture f;f.give(f.a,"rose",4);f.give(f.a,"leaf",20);
    CHECK(!forgeHyper(f.db,f.a,"rose",0).ok);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_rose"),4);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_leaf"),20);
    CHECK_EQ(hyperForgeState(f.db,"rose").claims,0);
}
TEST(hyper_forge_race_has_one_winner_and_steals_reach_the_one_hour_floor){
    Fixture f;f.give(f.a,"rose",100);f.give(f.b,"rose",100);
    const auto third=f.db.createUser("three","password8").account->id;f.give(third,"rose",5);
    CHECK(forgeHyper(f.db,f.a,"rose",0).ok);
    auto now=hyperForgeState(f.db,"rose").protectedUntil;
    CHECK(forgeHyper(f.db,f.b,"rose",now).ok);
    CHECK(!forgeHyper(f.db,third,"rose",now).ok);
    CHECK_EQ(f.db.progress(third).itemCount(Rarity::Unique,"petal_rose"),5);
    std::string next=f.a;
    for(int claims=3;claims<=18;++claims){
        now=hyperForgeState(f.db,"rose").protectedUntil;
        CHECK(forgeHyper(f.db,next,"rose",now).ok);
        const auto state=hyperForgeState(f.db,"rose");
        CHECK_EQ(state.claims,claims);CHECK_EQ(state.protectedUntil-now,hyperProtectionMillis(claims));
        next=next==f.a?f.b:f.a;
    }
}
TEST(hyper_cannot_be_obtained_through_regular_crafting_or_oracle){
    CHECK(craftsOutOf(Rarity::Super));CHECK(!craftsOutOf(Rarity::Unique));
    CHECK(!craftsOutOf(Rarity::Apex));CHECK_EQ(oracleCraftCost(Rarity::Unique),0);
}
TEST(hyper_forge_failed_save_rolls_back_inputs_output_and_protection){
    Database db;std::string error;db.load("/proc/florr-hyper-test-cannot-write.json",error);db.setPasswordCost(4);
    const auto id=db.createUser("unsaved","password8").account->id;
    db.progress(id).addItem(Rarity::Unique,"petal_rose",5);
    CHECK(!forgeHyper(db,id,"rose",1000).ok);
    CHECK_EQ(db.progress(id).itemCount(Rarity::Unique,"petal_rose"),5);
    CHECK_EQ(db.progress(id).itemCount(Rarity::Apex,"petal_rose"),0);
    CHECK_EQ(hyperForgeState(db,"rose").claims,0);
}
