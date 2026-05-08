#pragma once

#include "core/Diagnostics.h"
#include "lexer/Token.h"
#include "preprocess/TextMacro.h"

#include <vector>

namespace vjassc {

struct LexerResult {
    std::vector<Token> tokens;
};

class Lexer {
public:
    Lexer(Diagnostics& diagnostics);
    LexerResult lex(const std::vector<LogicalLine>& lines);

private:
    Diagnostics& diagnostics_;
};

} // namespace vjassc
