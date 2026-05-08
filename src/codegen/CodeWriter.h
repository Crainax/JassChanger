#pragma once

#include <sstream>
#include <string>

namespace vjassc {

class CodeWriter {
public:
    void writeln(const std::string& line = {});
    void indent();
    void dedent();
    std::string str() const;

private:
    std::ostringstream out_;
    int indent_ = 0;
};

} // namespace vjassc
