#include "test.h"
#include "client/ui/chat_i18n.h"
#include "client/ui/i18n.h"
using namespace flix;
TEST(hyper_spawn_announcements_translate_rarity_and_mob_in_both_locations) {
    const auto before=ui::language();
    for(const auto tail:{"!"," somewhere!"}) {
        const std::string line=std::string("<b style=\"color: #ff0000;\">A Hyper Spider has spawned")+tail+"</b>";
        ui::setLanguage(ui::Language::Zh);const auto result=ui::localizeChatText(line);
        CHECK(result.find("超凡")!=std::string::npos);CHECK(result.find("蜘蛛")!=std::string::npos);
        CHECK(result.find("Hyper")==std::string::npos);CHECK(result.find("</b>")!=std::string::npos);
        CHECK(result.find(std::string(tail)=="!"?"已出现":"已在某处出现")!=std::string::npos);
        ui::setLanguage(ui::Language::En);CHECK_EQ(ui::localizeChatText(line),line);
    }
    ui::setLanguage(before);
}
