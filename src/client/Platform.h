#pragma once

#include <string>

namespace vw::client {

bool systemShare();
void shareText(const std::string& title, const std::string& text);
std::string takeJoinLink();
std::string inviteLink(const std::string& code);

} // namespace vw::client
