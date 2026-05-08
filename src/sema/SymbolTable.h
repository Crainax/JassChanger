#pragma once

#include "parser/Ast.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace vjassc {

struct ScopedSymbolMap {
    std::unordered_map<std::string, std::string> replacements;
};

class SymbolTable {
public:
    void build(const Program& program);
    const ScopedSymbolMap* symbolsFor(const Decl* container) const;
    std::string renameForDecl(const Decl& decl, const Decl* container) const;
    std::string rewriteLine(const std::string& line, const Decl* container) const;

private:
    void buildInContainer(const Decl& container, std::vector<std::string>& scopePath);
    std::string makeScopedName(const std::vector<std::string>& scopePath, std::string_view access, std::string_view name) const;
    void collectGlobalNames(const Decl& globalBlock, const std::vector<std::string>& scopePath, ScopedSymbolMap& map);

    std::unordered_map<const Decl*, ScopedSymbolMap> scoped_;
};

std::string extractGlobalName(std::string line, std::string& access);
std::string stripAccessPrefixFromLine(std::string line);

} // namespace vjassc
