#pragma once

#include "core/Diagnostics.h"
#include "parser/Ast.h"
#include "preprocess/TextMacro.h"

#include <vector>

namespace vjassc {

class Parser {
public:
    Parser(Diagnostics& diagnostics);
    Program parse(const std::vector<LogicalLine>& lines);

private:
    void preScanUnsupported(const std::vector<LogicalLine>& lines);
    std::vector<Decl> parseJassRange(const std::vector<LogicalLine>& lines, size_t& index, const std::string& terminator);
    Decl parseJassFunction(const std::vector<LogicalLine>& lines, size_t& index);
    Decl parseJassGlobalBlock(const std::vector<LogicalLine>& lines, size_t& index);
    Decl parseJassLibraryOrScope(const std::vector<LogicalLine>& lines, size_t& index, bool isScope);
    Decl parseUnsupportedBlock(const std::vector<LogicalLine>& lines, size_t& index, const std::string& feature, const std::string& endWord);

    Decl parseZincLibrary(const std::vector<LogicalLine>& lines, size_t& index);
    std::vector<Decl> parseZincMembers(const std::vector<LogicalLine>& lines);
    Decl parseZincFunction(const std::vector<LogicalLine>& lines, size_t& index);
    Decl parseZincUnsupportedBlock(const std::vector<LogicalLine>& lines, size_t& index, const std::string& feature);

    Diagnostics& diagnostics_;
    ParserStats stats_;
};

} // namespace vjassc
