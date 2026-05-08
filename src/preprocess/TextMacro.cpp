#include "preprocess/TextMacro.h"

namespace vjassc {

const char* modeName(SyntaxMode mode) {
    return mode == SyntaxMode::Zinc ? "Zinc" : "JassLike";
}

} // namespace vjassc
