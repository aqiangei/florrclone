#include "test.h"
#include "server_harness.h"
using namespace flix;using namespace flix::testsupport;
TEST(chat_shortcuts_invite_without_existing_squad_and_whisper_to_account) {
    Harness h("chat-actions");CHECK(h.ready);if(!h.ready)return;
    NetClient a,b;CHECK(connectClient(h,a));a.requestRegister("sender","hunter2!");
    CHECK(h.stepUntil({&a},[&]{return a.status()==NetClient::Status::LoggedIn;}));
    CHECK(connectClient(h,b));b.requestRegister("target","hunter2!");
    CHECK(h.stepUntil({&a,&b},[&]{return b.status()==NetClient::Status::LoggedIn;}));
    a.sendChat("/squad-invite target");
    CHECK(h.stepUntil({&a,&b},[&]{return a.squad().inSquad && b.squadInvite().waiting;}));
    a.sendChat("/w target shortcut-test");
    CHECK(h.stepUntil({&a,&b},[&]{for(const auto& line:b.chat())if(line.channel==net::ChatChannel::Whisper && line.text.find("shortcut-test")!=std::string::npos)return true;return false;}));
}
