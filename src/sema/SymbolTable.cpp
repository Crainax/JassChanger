#include "sema/SymbolTable.h"

#include "util/PathUtil.h"

#include <cctype>
#include <sstream>

namespace vjassc {
namespace {

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool isIdentPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

std::string joinScope(const std::vector<std::string>& scopePath) {
    std::string out;
    for (size_t i = 0; i < scopePath.size(); ++i) {
        if (i > 0) {
            out += "_";
        }
        out += scopePath[i];
    }
    return out;
}

std::string parseAccess(std::string& line) {
    std::string t = trim(line);
    if (startsWithWord(t, "public")) {
        line = trim(std::string_view(t).substr(6));
        return "public";
    }
    if (startsWithWord(t, "private")) {
        line = trim(std::string_view(t).substr(7));
        return "private";
    }
    line = t;
    return {};
}

} // namespace

std::string extractGlobalName(std::string line, std::string& access) {
    access = parseAccess(line);
    std::istringstream in(line);
    std::string word;
    std::string maybeType;
    in >> word;
    if (word == "constant") {
        in >> maybeType;
    }
    std::string name;
    in >> name;
    if (name == "array") {
        in >> name;
    }
    size_t bracket = name.find('[');
    if (bracket != std::string::npos) {
        name = name.substr(0, bracket);
    }
    size_t assign = name.find('=');
    if (assign != std::string::npos) {
        name = name.substr(0, assign);
    }
    return name;
}

std::string stripAccessPrefixFromLine(std::string line) {
    (void)parseAccess(line);
    return line;
}

void SymbolTable::build(const Program& program) {
    scoped_.clear();
    std::vector<std::string> scopePath;
    for (const auto& decl : program.decls) {
        if (decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) {
            buildInContainer(decl, scopePath);
        }
    }
}

const ScopedSymbolMap* SymbolTable::symbolsFor(const Decl* container) const {
    auto it = scoped_.find(container);
    return it == scoped_.end() ? nullptr : &it->second;
}

std::string SymbolTable::renameForDecl(const Decl& decl, const Decl* container) const {
    if (decl.access.empty() || !container) {
        return decl.name;
    }
    auto symbols = symbolsFor(container);
    if (!symbols) {
        return decl.name;
    }
    auto it = symbols->replacements.find(decl.name);
    return it == symbols->replacements.end() ? decl.name : it->second;
}

std::string SymbolTable::rewriteLine(const std::string& line, const Decl* container) const {
    auto symbols = symbolsFor(container);
    if (!symbols || symbols->replacements.empty()) {
        return line;
    }

    std::string out;
    out.reserve(line.size());
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    for (size_t i = 0; i < line.size();) {
        if (!inString && !inRaw && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
            out.append(line.substr(i));
            break;
        }
        char c = line[i];
        if (inString) {
            out.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            ++i;
            continue;
        }
        if (inRaw) {
            out.push_back(c);
            if (c == '\'') {
                inRaw = false;
            }
            ++i;
            continue;
        }
        if (c == '"') {
            inString = true;
            out.push_back(c);
            ++i;
            continue;
        }
        if (c == '\'') {
            inRaw = true;
            out.push_back(c);
            ++i;
            continue;
        }
        if (isIdentStart(c)) {
            size_t start = i++;
            while (i < line.size() && isIdentPart(line[i])) {
                ++i;
            }
            std::string ident = line.substr(start, i - start);
            auto it = symbols->replacements.find(ident);
            out += it == symbols->replacements.end() ? ident : it->second;
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

void SymbolTable::buildInContainer(const Decl& container, std::vector<std::string>& scopePath) {
    scopePath.push_back(container.name);
    ScopedSymbolMap map;
    for (const auto& child : container.children) {
        if (child.kind == DeclKind::Function && !child.access.empty()) {
            map.replacements[child.name] = makeScopedName(scopePath, child.access, child.name);
        } else if (child.kind == DeclKind::GlobalBlock) {
            collectGlobalNames(child, scopePath, map);
        }
    }
    scoped_[&container] = std::move(map);

    for (const auto& child : container.children) {
        if (child.kind == DeclKind::Scope) {
            buildInContainer(child, scopePath);
        }
    }
    scopePath.pop_back();
}

std::string SymbolTable::makeScopedName(const std::vector<std::string>& scopePath, std::string_view access, std::string_view name) const {
    std::string scope = joinScope(scopePath);
    if (access == "private") {
        return scope + "___" + std::string(name);
    }
    return scope + "_" + std::string(name);
}

void SymbolTable::collectGlobalNames(const Decl& globalBlock, const std::vector<std::string>& scopePath, ScopedSymbolMap& map) {
    for (auto line : globalBlock.lines) {
        std::string access;
        std::string name = extractGlobalName(line, access);
        if (!access.empty() && !name.empty()) {
            map.replacements[name] = makeScopedName(scopePath, access, name);
        }
    }
}

} // namespace vjassc
