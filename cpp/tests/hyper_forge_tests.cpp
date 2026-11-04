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
TEST(hyper_temporary_hammer_preserves_protected_owner_and_materials_expires_and_cannot_regrant){
    Fixture f;f.give(f.a,"fragment",10);f.give(f.b,"timed_mjolnir");
    CHECK(forgeHyper(f.db,f.a,"fragment",1000).ok);
    const auto protectedState=f.db.storedTable("hyperForges").dump();
    CHECK(grantTemporaryHyperHammer(f.db,f.b,2000,"compensation").ok);
    CHECK_EQ(f.db.storedTable("hyperForges").dump(),protectedState);
    CHECK_EQ(f.db.progress(f.b).itemCount(Rarity::Unique,"petal_timed_mjolnir"),5);
    CHECK_EQ(f.db.progress(f.b).itemCount(Rarity::Apex,"petal_mjolnir"),1);
    CHECK(grantTemporaryHyperHammer(f.db,f.b,3000,"compensation").ok);
    CHECK_EQ(f.db.progress(f.b).itemCount(Rarity::Apex,"petal_mjolnir"),1);
    auto& record=f.db.progress(f.b);record.addItem(Rarity::Apex,"petal_mjolnir",-1);
    StoredItem hammer;hammer.petalType="mjolnir";hammer.rarity=Rarity::Apex;
    record.loadout.resize(20);record.loadout[4]=hammer;record.loadoutPresets["K1"]={hammer};
    CHECK(!expireTemporaryHyperHammer(record,2000+15LL*3600000-1));
    CHECK(expireTemporaryHyperHammer(record,2000+15LL*3600000));
    CHECK(!record.loadout[4]);CHECK(!record.loadoutPresets["K1"][0]);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_mjolnir"),1);
    CHECK(grantTemporaryHyperHammer(f.db,f.b,2000+16LL*3600000,"compensation").ok);
    CHECK_EQ(record.itemCount(Rarity::Apex,"petal_mjolnir"),0);
}
TEST(hyper_forge_price_and_protection_curve_match_the_requested_progression){
    for(int n=0;n<20;++n){CHECK_EQ(hyperForgeCost(n),5);CHECK_EQ(hyperForgeCost(n,"fragment"),10);}
    CHECK_EQ(hyperForgeRefund(5),4);CHECK_EQ(hyperForgeRefund(6),5);CHECK_EQ(hyperForgeRefund(10),9);
    CHECK_EQ(titanForgeCost(0,"rose"),10);CHECK_EQ(titanForgeCost(1,"rose"),15);CHECK_EQ(titanForgeCost(2000,"rose"),15);
    CHECK_EQ(titanForgeCost(0,"fragment"),15);CHECK_EQ(titanForgeCost(2000,"fragment"),15);
    CHECK_EQ(hyperProtectionMillis(1),15LL*3600000);CHECK_EQ(hyperProtectionMillis(2),14LL*3600000);
    CHECK_EQ(hyperProtectionMillis(15),3600000);CHECK_EQ(hyperProtectionMillis(10000),3600000);
}
TEST(hyper_forge_fixed_five_cost_and_four_refund_preserve_one_net_loss){
    Fixture f;f.give(f.a,"rose",200);f.give(f.b,"rose",200);
    std::string winner=f.a;std::int64_t now=0;int previousPaid=0;
    for(int n=0;n<10;++n){
        const auto previous=winner==f.a?f.b:f.a;
        const auto oldWinner=f.db.progress(winner).itemCount(Rarity::Unique,"petal_rose");
        const auto oldLoser=f.db.progress(previous).itemCount(Rarity::Unique,"petal_rose");
        const auto result=forgeHyper(f.db,winner,"rose",now);CHECK(result.ok);
        CHECK_EQ(result.consumed,5);
        CHECK_EQ(f.db.progress(winner).itemCount(Rarity::Unique,"petal_rose"),oldWinner-result.consumed);
        if(n>0)CHECK_EQ(f.db.progress(previous).itemCount(Rarity::Unique,"petal_rose"),oldLoser+4);
        CHECK_EQ(f.db.storedTable("hyperForges")["rose"]["paid"].asInt(),result.consumed);
        previousPaid=result.consumed;now=hyperForgeState(f.db,"rose").protectedUntil;winner=previous;
    }
    CHECK_EQ(hyperForgeState(f.db,"leaf").claims,0);
}
TEST(hyper_two_titans_are_independent_even_when_one_account_owns_both_copies){
    Fixture f;f.give(f.a,"rose",30);f.give(f.b,"rose",10);
    CHECK(forgeHyper(f.db,f.a,"rose",1000).ok);
    const auto jungle=f.db.storedTable("hyperForges")["rose"].dump();
    CHECK(forgeHyper(f.db,f.a,"rose",1000,{},"garden").ok);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_rose"),2);
    CHECK_EQ(f.db.storedTable("hyperForges")["rose"].dump(),jungle);
    CHECK_EQ(hyperForgeCost(hyperForgeState(f.db,"rose","garden").claims),5);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_rose"),20);
    const auto until=hyperForgeState(f.db,"rose","garden").protectedUntil;
    CHECK(forgeHyper(f.db,f.b,"rose",until,{},"garden").ok);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_rose"),1);
    CHECK_EQ(f.db.progress(f.b).itemCount(Rarity::Apex,"petal_rose"),1);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_rose"),24);
    CHECK_EQ(f.db.storedTable("hyperForges")["rose"].dump(),jungle);
    CHECK_EQ(hyperForgeState(f.db,"rose","garden").claims,2);
    CHECK_EQ(hyperForgeState(f.db,"rose").claims,1);
    Database restored;std::string error;CHECK(restored.load(f.path,error));
    CHECK_EQ(hyperForgeState(restored,"rose").owner,f.a);
    CHECK_EQ(hyperForgeState(restored,"rose","garden").owner,f.b);
}
TEST(hyper_two_titans_only_remove_one_equipped_copy_and_keep_presets_for_the_other){
    Fixture f;f.give(f.a,"rose",10);f.give(f.b,"rose",6);
    CHECK(forgeHyper(f.db,f.a,"rose",0).ok);CHECK(forgeHyper(f.db,f.a,"rose",0,{},"garden").ok);
    auto& owner=f.db.progress(f.a);owner.addItem(Rarity::Apex,"petal_rose",-2);
    StoredItem item;item.petalType="rose";item.rarity=Rarity::Apex;
    owner.loadout.resize(20);owner.loadout[0]=item;owner.loadout[1]=item;owner.loadoutPresets["K1"]={item};
    CHECK(forgeHyper(f.db,f.b,"rose",15LL*3600000,{},"garden").ok);
    int worn=0;for(const auto& slot:owner.loadout)if(slot&&slot->rarity==Rarity::Apex&&slot->petalType=="rose")++worn;
    CHECK_EQ(worn,1);CHECK(owner.loadoutPresets["K1"][0].has_value());
    CHECK_EQ(hyperForgeState(f.db,"rose").owner,f.a);CHECK_EQ(hyperForgeState(f.db,"rose","garden").owner,f.b);
}
TEST(hyper_forge_is_unique_blocks_early_theft_and_refunds_four_inputs){
    Fixture f;f.give(f.a,"rose");f.give(f.b,"rose",6);
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
    Fixture f;f.give(f.a,"rose");f.give(f.b,"rose",6);CHECK(forgeHyper(f.db,f.a,"rose",0).ok);
    auto& record=f.db.progress(f.a);record.addItem(Rarity::Apex,"petal_rose",-1);
    StoredItem item;item.petalType="rose";item.rarity=Rarity::Apex;
    record.loadout.resize(20);record.loadout[0]=item;record.loadoutPresets["K1"]={item};
    CHECK(forgeHyper(f.db,f.b,"rose",15LL*3600000).ok);
    CHECK(!f.db.progress(f.a).loadout[0]);CHECK(!f.db.progress(f.a).loadoutPresets["K1"][0]);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_rose"),4);
}
TEST(hyper_forge_persists_owner_counter_and_protection_across_restart){
    Fixture f;f.give(f.a,"rose");f.give(f.b,"rose",6);CHECK(forgeHyper(f.db,f.a,"rose",1000).ok);
    Database restored;std::string error;CHECK(restored.load(f.path,error));
    CHECK_EQ(hyperForgeState(restored,"rose").owner,f.a);
    CHECK_EQ(hyperForgeState(restored,"rose").claims,1);
    CHECK(!forgeHyper(restored,f.b,"rose",1001).ok);
    CHECK(forgeHyper(restored,f.b,"rose",1000+15LL*3600000).ok);
    CHECK_EQ(hyperForgeState(restored,"rose").claims,2);
}
TEST(hyper_hammer_uses_ten_eternal_fragments_and_ignores_combat_hammer_expiry){
    Fixture f;f.give(f.a,"fragment",10);f.give(f.b,"fragment",10);
    for(int n=0;n<7;++n)addHammerLease(f.db.progress(f.a),0);
    CHECK(forgeHyper(f.db,f.a,"fragment",0).ok);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_mjolnir"),1);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_fragment"),0);
    CHECK_EQ(hyperForgeState(f.db,"mjolnir").protectedUntil,15LL*3600000);
    CHECK_EQ(expireHammerLeases(f.db.progress(f.a),3602000),7);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_mjolnir"),1);
    CHECK(forgeHyper(f.db,f.b,"fragment",15LL*3600000).ok);
    CHECK_EQ(hyperForgeState(f.db,"mjolnir").protectedUntil,29LL*3600000);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_fragment"),9);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_mjolnir"),0);
}
TEST(hyper_hammer_rejects_timed_hammers_and_super_fragments_without_charging){
    Fixture f;f.give(f.a,"fragment",9);
    f.db.progress(f.a).addItem(Rarity::Super,"petal_fragment",25);
    for(int n=0;n<5;++n)addHammerLease(f.db.progress(f.a),0);
    const auto before=f.db.progress(f.a).inventory.dump();
    CHECK(!forgeHyper(f.db,f.a,"timed_mjolnir",0).ok);
    CHECK(!forgeHyper(f.db,f.a,"mjolnir",0).ok);
    CHECK(!forgeHyper(f.db,f.a,"fragment",0).ok);
    CHECK_EQ(f.db.progress(f.a).inventory.dump(),before);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_mjolnir"),0);
}
TEST(hyper_hammer_legacy_holder_receives_original_material_without_opening_a_new_conversion){
    Fixture f;f.db.progress(f.a).addItem(Rarity::Apex,"petal_mjolnir",1);f.give(f.b,"fragment",10);
    Json entry=Json::object();entry["owner"]=f.a;entry["input"]="timed_mjolnir";
    entry["claims"]=1;entry["protectedUntil"]=Json(15.0*3600000);
    f.db.rawTable("hyperForges")["mjolnir"]=entry;
    CHECK(forgeHyper(f.db,f.b,"fragment",15LL*3600000).ok);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_fragment"),0);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_timed_mjolnir"),4);
    CHECK(!forgeHyper(f.db,f.a,"timed_mjolnir",30LL*3600000).ok);
}
TEST(hyper_forge_requires_five_inputs_and_keeps_unrelated_inventory_intact){
    Fixture f;f.give(f.a,"rose",4);f.give(f.a,"leaf",20);
    CHECK(!forgeHyper(f.db,f.a,"rose",0).ok);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_rose"),4);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_leaf"),20);
    CHECK_EQ(hyperForgeState(f.db,"rose").claims,0);
}
TEST(hyper_account_cap_counts_both_titans_equipment_and_unmatched_claims){
    Fixture f;f.give(f.a,"rose",50);
    CHECK(forgeHyper(f.db,f.a,"rose",0).ok);
    CHECK(forgeHyper(f.db,f.a,"rose",0,{},"garden").ok);
    CHECK_EQ(hyperHoldingCount(f.db,f.a),2);
    auto& record=f.db.progress(f.a);record.addItem(Rarity::Apex,"petal_leaf",2);
    StoredItem item;item.petalType="basic";item.rarity=Rarity::Apex;record.loadout.resize(20);record.loadout[11]=item;
    CHECK_EQ(hyperHoldingCount(f.db,f.a),5);
    f.give(f.a,"basic");const auto before=f.db.serialise();
    CHECK(!forgeHyper(f.db,f.a,"basic",0).ok);CHECK_EQ(before,f.db.serialise());
    // Discarding physical copies cannot erase a still-owned titan claim.
    record.addItem(Rarity::Apex,"petal_rose",-2);CHECK_EQ(hyperHoldingCount(f.db,f.a),5);
    record.addItem(Rarity::Apex,"petal_stinger",4);const auto old=f.db.serialise();
    CHECK_EQ(hyperHoldingCount(f.db,f.a),9);CHECK(!forgeHyper(f.db,f.a,"basic",0).ok);CHECK_EQ(old,f.db.serialise());
}
TEST(hyper_cap_reopens_after_one_copy_is_stolen_and_preserves_existing_five){
    Fixture f;f.give(f.a,"rose",10);f.give(f.a,"basic");f.give(f.b,"rose");
    CHECK(forgeHyper(f.db,f.a,"rose",0).ok);f.db.progress(f.a).addItem(Rarity::Apex,"petal_leaf",4);
    CHECK(!forgeHyper(f.db,f.a,"basic",0).ok);
    CHECK(forgeHyper(f.db,f.b,"rose",15LL*3600000).ok);CHECK_EQ(hyperHoldingCount(f.db,f.a),4);
    CHECK(forgeHyper(f.db,f.a,"basic",15LL*3600000).ok);CHECK_EQ(hyperHoldingCount(f.db,f.a),5);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_leaf"),4);
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

TEST(hyper_hammer_old_five_fragment_claim_refunds_only_its_actual_paid_minus_one){
    Fixture f;f.give(f.b,"fragment",10);f.db.progress(f.a).addItem(Rarity::Apex,"petal_mjolnir",1);
    Json old=Json::object();old["owner"]=f.a;old["input"]="fragment";old["paid"]=5;old["claims"]=1;old["protectedUntil"]=0;
    f.db.rawTable("hyperForges")["mjolnir"]=old;
    const auto result=forgeHyper(f.db,f.b,"fragment",1);CHECK(result.ok);CHECK_EQ(result.consumed,10);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_fragment"),4);
    CHECK_EQ(f.db.storedTable("hyperForges")["mjolnir"]["paid"].asInt(),10);
}
TEST(hyper_hammer_ten_fragment_cost_applies_independently_to_both_titans_and_counts_in_cap){
    Fixture f;f.give(f.a,"fragment",20);
    CHECK(forgeHyper(f.db,f.a,"fragment",0).ok);CHECK(forgeHyper(f.db,f.a,"fragment",0,{},"garden").ok);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Unique,"petal_fragment"),0);
    CHECK_EQ(f.db.progress(f.a).itemCount(Rarity::Apex,"petal_mjolnir"),2);CHECK_EQ(hyperHoldingCount(f.db,f.a),2);
    f.db.progress(f.a).addItem(Rarity::Apex,"petal_leaf",3);f.give(f.a,"rose");
    const auto before=f.db.serialise();CHECK(!forgeHyper(f.db,f.a,"rose",0).ok);CHECK_EQ(f.db.serialise(),before);
}
