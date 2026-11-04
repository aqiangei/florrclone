#include "client/ui/menus.h"
#include "client/ui/text.h"
#include <emscripten.h>
using namespace flix;
Window window;SpriteCache sprites;NetClient client;WorldRenderer renderer;ClientSettings settings;DragState drag;SkinsPanel panel;
void tick(){window.pump();auto& c=window.canvas();c.clear(Color{40,55,45});MenuContext ctx{c,window,client,sprites,renderer,settings,drag};ctx.timeSeconds=emscripten_get_now()/1000;ctx.dt=1./60;ctx.bounds={20,20,SkinsPanel::preferredWidth(),SkinsPanel::preferredHeight()};panel.render(ctx);window.present();EM_ASM({window.uiReady=true;});}
int main(){std::string e;loadContent("/data",e);ui::Fonts::init("/data",e);sprites.build(content(),"/data");const_cast<Profile&>(client.profile()).username="author";
 auto& skins=const_cast<std::vector<CustomSkin>&>(client.skinCatalog());
 for(int i=0;i<3;++i){CustomSkin s;s.id=std::to_string(i);s.name=i==0?"My private":i==1?"Public skin":"Hidden foreign";s.author=i==0?"author":"viewer";s.published=i==1;SkinShape shape;shape.t=SkinShapeType::Circle;shape.r=20;shape.fill=i==0?"#e011cc":"#00cccc";s.shapes={shape};skins.push_back(s);}
 window.open(820,700,"Private skins",e);window.setDesignSize(820,700);emscripten_set_main_loop(tick,60,1);}
