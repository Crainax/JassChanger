#include "codegen/CodeWriter.h"

namespace vjassc {

void CodeWriter::writeln(const std::string& line) {
    for (int i = 0; i < indent_; ++i) {
        out_ << "    ";
    }
    out_ << line << '\n';
}

void CodeWriter::indent() {
    ++indent_;
}

void CodeWriter::dedent() {
    if (indent_ > 0) {
        --indent_;
    }
}

std::string CodeWriter::str() const {
    return out_.str();
}

} // namespace vjassc
