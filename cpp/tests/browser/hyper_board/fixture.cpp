#include "client/ui/menus.h"
#include "client/ui/text.h"
#include <emscripten.h>
using namespace flix;
Window window;SpriteCache sprites;NetClient client;WorldRenderer renderer;ClientSettings settings;DragState drag;LeaderboardPanel board;
void tick(){window.pump();auto& c=window.canvas();c.clear(Color{40,55,45});MenuContext ctx{c,window,client,sprites,renderer,settings,drag};ctx.timeSeconds=emscripten_get_now()/1000;ctx.dt=1./60;ctx.bounds={20,20,500,500};board.render(ctx);window.present();EM_ASM({window.uiReady=true;});}
int main(){std::string e;loadContent("/data",e);ui::Fonts::init("/data",e);sprites.build(content(),"/data");auto& rows=const_cast<std::vector<HyperHoldingRow>&>(client.hyperBoard());rows={{"示例玩家","beetle_egg",2,5},{"示例玩家","golden_leaf",3,5},{"another-player","lightning",1,1}};window.open(550,550,"Hyper board",e);window.setDesignSize(550,550);emscripten_set_main_loop(tick,60,1);}
