#include "test.h"
#include "client/render/sprites.h"
#include "shared/game/config.h"
using namespace flix;
TEST(dice_art_is_visible_at_every_rarity){
    ContentRegistry c;std::string error;CHECK(c.loadFiles("/data/mobs.json","/data/petals.json",error));
    SpriteCache sprites;CHECK(sprites.build(c));
    for(const auto& w:sprites.warnings())if(w.find("dice")!=std::string::npos)std::printf("%s\n",w.c_str());
    const auto dice=c.petalIndex("dice");CHECK(sprites.petalDrawable(dice));
    for(int tier=0;tier<kLadderRarityCount;++tier){Canvas canvas=Canvas::createVirtual(100,100);
        sprites.drawPetal(canvas,dice,static_cast<Rarity>(tier),50,50,60,0,0);
        const auto px=canvas.getImageData(0,0,100,100);int visible=0;
        for(std::size_t i=3;i<px.size();i+=4)visible+=px[i]>64;CHECK(visible>500);
    }
}

#include "client/ui/item_tile.h"
#include "render_rig.h"
TEST(equipped_dice_is_a_projected_cube_that_turns_over_time){
 std::string error;CHECK(loadContent("/data",error));SpriteCache sprites;CHECK(sprites.build(content()));
 auto frame=[&](double time,int face){Canvas canvas=Canvas::createVirtual(120,120);ui::ItemTile tile;tile.petalIndex=content().petalIndex("dice");tile.rarity=Rarity::Apex;tile.diceFace=face;tile.timeSeconds=time;tile.showName=false;ui::drawItemTile(canvas,sprites,{10,10,100,100},tile);return canvas.getImageData(0,0,120,120);};
 CHECK_EQ(testsupport::differingPixels(frame(1.2,6),frame(1.5,6)),0);
 const auto first=frame(0,6);CHECK(testsupport::differingPixels(first,frame(.5,6))>200);CHECK(testsupport::differingPixels(first,frame(0,1))>40);
}

#include "shared/game/skills.h"
TEST(duplicator_keeps_each_equipment_icon_authored_layout) {
 std::string error;CHECK(loadContent("/data",error));SpriteCache sprites;CHECK(sprites.build(content()));
 for(const char* id:{"orange","peas","dust","stinger","light","dahlia"}){
  auto frame=[&](const SkillSet* skills){ui::setViewerSkills(skills);Canvas c=Canvas::createVirtual(120,120);
   ui::ItemTile tile;tile.petalIndex=content().petalIndex(id);tile.rarity=Rarity::Super;tile.loadoutSlot=0;tile.showName=false;
   ui::drawItemTile(c,sprites,{10,10,100,100},tile);return c.getImageData(0,0,120,120);};
  SkillSet skills;skills.set(SkillId::Loadout,4);skills.set(SkillId::Duplicator,3);
  CHECK_EQ(testsupport::differingPixels(frame(nullptr),frame(&skills)),0);
 }
 ui::setViewerSkills(nullptr);
}
TEST(dust_source_art_contains_one_particle_for_one_simulated_body) {
 std::string error;CHECK(loadContent("/data",error));SpriteCache sprites;CHECK(sprites.build(content()));
 Canvas c=Canvas::createVirtual(100,100);sprites.drawPetal(c,content().petalIndex("dust"),Rarity::Common,50,50,60,0,0);
 auto px=c.getImageData(0,0,100,100);int groups=0;std::vector<int> pending;
 for(int i=0;i<10000;++i){if(px[i*4+3]<128)continue;++groups;px[i*4+3]=0;pending.push_back(i);
 while(!pending.empty()){int n=pending.back();pending.pop_back();for(int next:{n-1,n+1,n-100,n+100})if(next>=0&&next<10000&&std::abs(next%100-n%100)<=1&&px[next*4+3]>=128){px[next*4+3]=0;pending.push_back(next);}}}
 CHECK_EQ(groups,1);CHECK_EQ(content().petalStats(content().petalIndex("dust"),Rarity::Common).count,3);
}
