#pragma once
// Localize server system / squad chat lines and notices for the active UI language.
#include <string>
namespace flix {
namespace ui {
/// Rewrite known English system/squad phrases when language is Zh. Markup kept.
std::string localizeChatText(std::string text);
} // namespace ui
} // namespace flix
