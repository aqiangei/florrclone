#include "client/ui/menus.h"
#include "client/ui/text.h"
#include <emscripten.h>
using namespace flix;
Window window;SpriteCache sprites;NetClient client;WorldRenderer renderer;ClientSettings settings;DragState drag;GalleryPanel board;
void tick(){window.pump();auto& c=window.canvas();c.clear(Color{40,55,45});MenuContext ctx{c,window,client,sprites,renderer,settings,drag};ctx.timeSeconds=emscripten_get_now()/1000;ctx.dt=1./60;ctx.bounds={20,20,GalleryPanel::preferredWidth(),GalleryPanel::preferredHeight()};board.render(ctx);window.present();EM_ASM({window.uiReady=true;});}
int main(){std::string e;loadContent("/data",e);ui::Fonts::init("/data",e);sprites.build(content(),"/data");auto& profile=const_cast<Profile&>(client.profile());profile.mobKills.assign(content().mobCount()*kRarityCount,1);profile.loadout.resize(kLoadoutSlots);profile.loadout[0]={content().petalIndex("mana_orb"),Rarity::Unique};window.open(1000,800,"Hyper board",e);window.setDesignSize(1000,800);emscripten_set_main_loop(tick,60,1);}

extern "C" EMSCRIPTEN_KEEPALIVE void removeOrb(){const_cast<Profile&>(client.profile()).loadout[0]={};}
