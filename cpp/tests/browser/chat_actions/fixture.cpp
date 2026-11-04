#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "canvas.h"
#include "svg.h"
#include "window.h"
#include "client/camera.h"
#include "client/net_client.h"
#include "client/ui/context_menu.h"
#include "client/ui/text_select.h"
#include "client/render/darkness.h"
#include "client/render/sprites.h"
#include "client/render/world_renderer.h"
#include "client/ui/menus.h"
#include "client/ui/mobile_controls.h"
#include "client/ui/tutorial.h"
#include "shared/game/map_elements.h"
#include "shared/core/types.h"
// Test-only access to inspect hit targets and drafts after real pointer events.
#define private public
#include "client/app.h"
#undef private
#include "client/ui/text.h"
#include <emscripten.h>
using namespace flix;
App app;
void tick() {
    app.window_.pump();
    EM_ASM({window.pointer=Array($0,$1,$2,$3);},app.window_.mouseX(),app.window_.mouseY(),app.window_.mousePressed(MouseButton::Right),app.contextMenu_.isOpen());
    app.timeSeconds_ = emscripten_get_now()/1000;
    auto& c=app.window_.canvas(); c.clear(Color{50,125,60});
    ui::TextSelect::instance().beginFrame();
    app.drawChat(c,app.timeSeconds_);
    app.updateTextSelection(c);
    app.window_.present();
    EM_ASM({window.uiReady=true;window.chatHits=[];});
    for(const auto& hit:app.chatNameHits_)EM_ASM({window.chatHits.push({x:$0,y:$1,w:$2,h:$3,name:UTF8ToString($4)});},hit.first.x,hit.first.y,hit.first.w,hit.first.h,hit.second.c_str());
    EM_ASM({window.chatDraft=UTF8ToString($0);window.chatCaret=$1;},app.chatDraft_.c_str(),app.chatField_.selection.end());
}
int main(){std::string error;loadContent("/data",error);ui::Fonts::init("/data",error);app.window_.open(1000,800,"Chat actions",error);app.window_.setDesignSize(1000,800);const_cast<Profile&>(app.net_.profile()).username="viewer";app.net_.addLocalChat("Divitv","没有叶，右键名字邀请组队",net::ChatChannel::Squad);app.net_.addLocalChat("LongPlayerNameThatWrapsAcrossTheChatColumnAndContinuesMuchFurtherBeyondItsAvailableWidth","wrapped author",net::ChatChannel::Global);app.net_.addLocalChat("viewer","自己的名字不能邀请自己",net::ChatChannel::Global);app.net_.addLocalChat("hqyx","正文提到 Divitv 不应变成快捷目标",net::ChatChannel::Global);emscripten_set_main_loop(tick,60,1);}
