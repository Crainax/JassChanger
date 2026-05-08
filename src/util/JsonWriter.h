#pragma once

#include <iosfwd>
#include <string>
#include <string_view>

namespace vjassc {

std::string jsonEscape(std::string_view text);
void writeJsonString(std::ostream& out, std::string_view text);

} // namespace vjassc
