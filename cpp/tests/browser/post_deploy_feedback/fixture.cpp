#include "client/ui/menus.h"
#include "client/ui/text.h"
#include "client/web/persist.h"
#include <emscripten.h>
using namespace flix;
Window win; NetClient client;SpriteCache sprites;WorldRenderer renderer;ClientSettings settings;DragState drag;CraftingPanel craft;GuildPanel guild;int mode=0;
extern "C" EMSCRIPTEN_KEEPALIVE void setMode(int n){mode=n;}
void tick(){win.pump();auto& c=win.canvas();c.clear(Color{48,83,58});MenuContext ctx{c,win,client,sprites,renderer,settings,drag};ctx.timeSeconds=emscripten_get_now()/1000;ctx.dt=1./60;
if(mode==0){ctx.bounds={20,20,CraftingPanel::size(client.profile()).x,650};craft.render(ctx);}else if(mode==1){ctx.bounds=GuildPanel::bounds(1000,760);guild.render(ctx);}else if(mode==4){
    renderer.setContent(&content());renderer.setSprites(&sprites);
    static bool emitted=false;if(!emitted){WorldView v;ViewEvent a;a.kind=net::EventKind::Damage;a.netId=999;a.position={0,0};a.amount=100;v.events().push_back(a);a.amount=3500;a.flag=net::DamageCritical;v.events().push_back(a);renderer.ingestEvents(v);emitted=true;}
    Camera camera;camera.setViewport(1000,760);camera.snapTo({0,0});WorldRenderer::EntityMap entities;renderer.draw(c,entities,camera,{},ctx.timeSeconds);
}else if(mode==3){
    Camera camera;camera.setViewport(1000,760);camera.snapTo({0,0});
    WorldRenderer::EntityMap entities;
    for(int i=0;i<4;++i){RemoteEntity e;e.netId=i+1;e.kind=net::EntityKind::Projectile;e.typeIndex=content().petalIndex("mecha_missile");e.rarity=Rarity::Rare;e.radius=25;e.position={-300.0+i*200,0};e.angle=i*kPi/2;entities.emplace(e.netId,e);}
    renderer.setContent(&content());renderer.setSprites(&sprites);renderer.draw(c,entities,camera,{},ctx.timeSeconds);
}else{const char* ids[]={"centipede","jungle_centipede","jungle_centipede_body","hel_centipede","hel_centipede_body"};for(int i=0;i<5;++i)sprites.drawMob(c,content().mobIndex(ids[i]),Rarity::Unique,100+i*180,330,140*content().mob(content().mobIndex(ids[i])).visualScale,0,0);}
if(settings.craftBlacklistDirty){settings.save("/persist/settings");settings.craftBlacklistDirty=false;}
win.present();EM_ASM({window.uiReady=true;window.excluded=$0;},settings.craftBlacklist.size());}
int main(){std::string e;loadContent("/data",e);ui::Fonts::init("/data",e);sprites.build(content(),"/data");web::mountStorage({"settings"},"feedback90/");settings.load("/persist/settings");ui::setLanguage(ui::Language::Zh);auto& p=const_cast<Profile&>(client.profile());p.username="leader";p.loadout.resize(kLoadoutSlots);for(auto id:{"rock","leaf","pollen"})p.inventory.push_back({content().petalIndex(id),Rarity::Rare,100});auto& g=const_cast<GuildState&>(client.guild());g.joined=true;g.leader="leader";g.name="Test guild";g.tag="TEST";g.members={{"leader",true,"Garden"},{"Hala",true,"Jungle"},{"offline",false,""}};win.open(1000,760,"Feedback90",e);win.setDesignSize(1000,760);emscripten_set_main_loop(tick,60,1);}
