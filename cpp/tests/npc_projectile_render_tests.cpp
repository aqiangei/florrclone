#include "test.h"
#include "test_data.h"
#include "client/render/world_renderer.h"
#include "client/render/sprites.h"
#include "client/world_view.h"
#include "render_rig.h"
using namespace flix;
namespace {
const ContentRegistry& data(){return testsupport::shippedContent();}
const SpriteCache& sprites(){static SpriteCache cache;static bool ready=false;if(!ready){CHECK(cache.build(data(),testsupport::dataDir()));ready=true;}return cache;}
std::vector<std::uint8_t> frame(const RemoteEntity* entity){
    Canvas canvas=Canvas::createVirtual(300,300);WorldView view;view.setRealm(Realm::Overworld);if(entity)view.seedForTest(*entity);
    WorldRenderer renderer;renderer.setContent(&data());renderer.setSprites(&sprites());
    renderer.draw(canvas,view,testsupport::frameCamera(300,{}),{},0);
    return canvas.getImageData(0,0,300,300);
}
RemoteEntity mob(const char* id){RemoteEntity e;e.netId=5;e.kind=net::EntityKind::Mob;e.typeIndex=data().mobIndex(id);e.rarity=Rarity::Common;e.radius=data().mobStats(e.typeIndex,e.rarity).radius;e.needsSnap=false;return e;}
}
TEST(eternal_assembler_art_size_is_independent_of_the_small_collision_body){
    for(const char* id:{"assembler","cyber_assembler"}){
        auto e=mob(id);e.kind=net::EntityKind::Npc;e.rarity=Rarity::Unique;e.radius=60;
        const auto small=frame(&e);e.radius=data().mobStats(e.typeIndex,e.rarity).radius;
        CHECK(e.radius>60);CHECK_EQ(testsupport::differingPixels(small,frame(&e)),0);
        CHECK(testsupport::differingPixels(small,frame(nullptr))>1000);
    }
}
TEST(shooter_art_has_no_loaded_ammo_overlay_but_live_projectiles_still_draw){
    for(const char* id:{"hornet","wasp","hel_wasp","mecha_wasp"}){
        auto e=mob(id);const auto loaded=frame(&e);e.state=net::StateUnloaded;
        CHECK_EQ(testsupport::differingPixels(loaded,frame(&e)),0);
        e.kind=net::EntityKind::Projectile;e.state=0;e.typeIndex=data().mob(e.typeIndex).projectile.ammoPetalIndex;e.radius=15;
        CHECK(testsupport::differingPixels(frame(&e),frame(nullptr))>50);
    }
}

TEST(mechanical_spider_steps_by_distance_and_holds_still_when_idle){
    const auto index=data().mobIndex("mecha_spider");
    auto paint=[&](double distance,double time){Canvas canvas=Canvas::createVirtual(160,160);MobMotion motion;motion.distance=distance;
        sprites().drawMob(canvas,index,Rarity::Common,80,80,120,0,time,false,30,{1,0},&motion);
        return canvas.getImageData(0,0,160,160);};
    const auto idle=paint(0,0),walking=paint(25,0);
    CHECK(testsupport::differingPixels(idle,walking)>100);CHECK_EQ(testsupport::differingPixels(idle,paint(0,100)),0);
    int bodyChanges=0;for(int y=70;y<90;++y)for(int x=70;x<90;++x){const auto at=(y*160+x)*4;
        for(int c=0;c<3;++c)bodyChanges+=idle[at+c]!=walking[at+c];}CHECK_EQ(bodyChanges,0);
}
