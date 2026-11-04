#include "test.h"
#include "client/ui/gallery_drop_preview.h"
#include "client/ui/text.h"
#include "client/net_client.h"
#include "shared/core/json.h"
#include "shared/game/config.h"
#include "shared/game/drop_rates.h"
using namespace flix;

TEST(gallery_magic_drop_preview_follows_all_authored_conversions_and_orb_cap) {
    std::string error; CHECK(loadContent("/data",error)); ui::Fonts::init("/data",error);
    Json data; CHECK(Json::parseFile("/data/mob_drops.json",data,error)); int checked=0;
    for(const auto& mob:data.keys())for(const auto& entry:data[mob]["drops"].items()){
        const auto source=content().petalIndex(entry["itemType"].asString());
        const auto magic=entry.contains("magicItemType")?content().petalIndex(entry["magicItemType"].asString()):content().magicFormOf(source);
        if(magic==kInvalidIndex)continue;
        const auto plain=galleryDropPreview(content().mobIndex(mob),Rarity::Apex,kRarityCount);
        const auto converted=galleryDropPreview(content().mobIndex(mob),Rarity::Apex,rarityIndex(Rarity::Unique));
        bool seen=false;
        for(const auto& d:plain)CHECK(d.petalIndex!=magic);
        for(const auto& d:converted)if(d.petalIndex==magic){seen=true;CHECK(rarityIndex(d.rarity)<=8);CHECK(d.probability>0);}
        CHECK(seen);++checked;
    }
    CHECK(checked>=20);
    DropRates conditional;
    for(const auto& entry:data["golden_leafbug"]["drops"].items())
        if(entry.contains("magicRarityChances"))CHECK(parseDropRates(entry["magicRarityChances"],conditional));
    CHECK(!conditional.empty());
    double expected=0,shown=0;
    for(const auto& r:ratesFor(conditional,Rarity::Unique))expected+=r.probability*100;
    for(const auto& d:galleryDropPreview(content().mobIndex("golden_leafbug"),Rarity::Unique,5))
        if(d.petalIndex==content().petalIndex("magic_leaf")){shown+=d.probability;CHECK(rarityIndex(d.rarity)<=5);}
    CHECK_NEAR(shown,expected,1e-7);
    for(const char* mob:{"ladybug","jungle_ladybug","desert_ladybug"}){
        bool found=false;
        for(const auto& d:galleryDropPreview(content().mobIndex(mob),Rarity::Apex,5))
            if(d.petalIndex==content().petalIndex("mana_orb")){found=true;CHECK(rarityIndex(d.rarity)<=5);}
        CHECK(found);
    }
}
TEST(gallery_magic_orb_checks_active_slots_and_resolves_mimic) {
    std::string error; CHECK(loadContent("/data",error));
    Profile p;p.loadout.resize(kLoadoutSlots);
    const auto orb=content().petalIndex("mana_orb");
    p.loadout[kLoadoutActiveSlots]={orb,Rarity::Apex};CHECK_EQ(galleryOrbTier(p),kRarityCount);
    p.loadout[0]={orb,Rarity::Mythic};CHECK_EQ(galleryOrbTier(p),5);
    p.loadout[1]={content().petalIndex("mimic"),Rarity::Unique};CHECK_EQ(galleryOrbTier(p),8);
}

TEST(gallery_hyper_eternal_cells_share_the_reduced_budget) {
    std::string error;CHECK(loadContent("/data",error));
    double total=0;
    for(const auto& d:galleryDropPreview(content().mobIndex("ladybug"),Rarity::Apex,kRarityCount))
        if(d.rarity==Rarity::Unique)total+=d.probability;
    CHECK_NEAR(total,100.0*0.35/30.0,1e-8);
}
