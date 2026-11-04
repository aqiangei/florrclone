#include <emscripten.h>
#include "client/ui/menus.h"
#include "client/ui/text.h"
#include "shared/game/config.h"
using namespace flix;
Window window;NetClient client;SpriteCache sprites;WorldRenderer renderer;ClientSettings settings;DragState drag;TitanPanel panel;int sent=0;
EM_JS(void,stats,(int count),{window.forgeSent=count;window.uiReady=true;});
extern "C" EMSCRIPTEN_KEEPALIVE void testOutcome(int hammer){auto& o=client.titanForgeOutcome();o.success=true;o.pending=true;o.petalIndex=content().petalIndex(hammer?"timed_mjolnir":"fragment");}
void tick(){window.pump();auto& canvas=window.canvas();canvas.clear(Color{30,60,40});
 MenuContext ctx{canvas,window,client,sprites,renderer,settings,drag};ctx.timeSeconds=emscripten_get_now()/1000;ctx.dt=1.0/60;ctx.bounds={50,20,610,500};panel.render(ctx);window.present();
 std::uint32_t in,out;std::vector<NetClient::WireEvent> events;client.takeWireStats(in,out,events,256);
 for(const auto& e:events)if(std::string(e.name)=="titanForge"&&!e.incoming)sent+=e.bytes/8;stats(sent);
}
int main(){std::string error;if(!loadContent("/data",error)){printf("%s",error.c_str());return 1;}ui::Fonts::init("/data",error);sprites.build(content());
 auto& p=const_cast<Profile&>(client.profile());p.loadout.resize(kLoadoutSlots);p.inventory.push_back({content().petalIndex("fragment"),Rarity::Super,60});p.inventory.push_back({content().petalIndex("fragment"),Rarity::Unique,20});
 if(!window.open(800,650,"Forge test",error))return 2;window.setDesignSize(800,650);emscripten_set_main_loop(tick,60,1);
}
