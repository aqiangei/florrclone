#include "client/ui/menus.h"
#include <emscripten.h>
#include "client/ui/item_tile.h"
#include "client/ui/text.h"
#include "shared/game/config.h"
#include "shared/game/map_elements.h"
using namespace flix;
Window window;SpriteCache sprites;NetClient client;WorldRenderer renderer;ClientSettings settings;DragState drag;TalentsPanel talents;int mode=0;CraftingPanel craft;WorldMaps maps;WorldView view;Camera camera;
extern "C" EMSCRIPTEN_KEEPALIVE void showTalents(int n){mode=n;talents.setOpen(1,false);}
void tick(){window.pump();auto& c=window.canvas();c.clear(Color{40,55,45});double t=emscripten_get_now()/1000;
 if(mode>=3){const char* ids[]={"garden","desert","computer"};bool found=false;auto realm=maps.realmOfId(ids[mode-3],found);view.setRealm(realm);camera.setViewport(700,760);camera.userZoom=.25;const Vec2 positions[]={{22784,13283},{11260,3656},{3840,22476}};camera.snapTo(positions[mode-3]);renderer.draw(c,view,camera,{},t);window.present();return;}
 if(mode){MenuContext ctx{c,window,client,sprites,renderer,settings,drag};ctx.timeSeconds=t;ctx.dt=1.0/60;ctx.bounds={20,20,650,650};if(mode==2)craft.render(ctx);else talents.render(ctx);window.present();return;}
 for(int i=0;i<6;++i){ui::ItemTile tile;tile.petalIndex=content().petalIndex("dice");tile.rarity=Rarity::Apex;tile.diceFace=i+1;tile.timeSeconds=t+i*.4;tile.showName=false;ui::drawItemTile(c,sprites,{20.+i*110,25,90,90},tile);}
 const char* names[]={"orange","peas","dust","stinger","light","dahlia"};
 for(int row=0;row<2;++row){SkillSet skills;if(row){skills.set(SkillId::Loadout,4);skills.set(SkillId::Duplicator,3);}ui::setViewerSkills(&skills);
 for(int i=0;i<6;++i){ui::ItemTile tile;tile.petalIndex=content().petalIndex(names[i]);tile.rarity=Rarity::Super;tile.loadoutSlot=0;ui::drawItemTile(c,sprites,{20.+i*110,150.+row*120,90,90},tile);}}ui::setViewerSkills(nullptr);
 const char* heads[]={"centipede","desert_centipede","evil_centipede","jungle_centipede","hel_centipede"};
 for(int row=0;row<5;++row){auto head=content().mobIndex(heads[row]);auto body=content().mob(head).segmentBodyIndex;
 for(int i=0;i<5;++i)sprites.drawMob(c,i?body:head,Rarity::Common,150+i*65,420+row*65,65,0,0);}
 window.present();EM_ASM({window.uiReady=true;});}
int main(){std::string e;loadContent("/data",e);ui::Fonts::init("/data",e);sprites.build(content(),"/data");maps.load("/data",nullptr,e);renderer.setWorldMaps(&maps);renderer.setContent(&content());renderer.setSprites(&sprites);window.open(700,760,"Icons",e);window.setDesignSize(700,760);emscripten_set_main_loop(tick,60,1);}
