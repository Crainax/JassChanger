#include "parser/Parser.h"

#include "util/PathUtil.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace vjassc {
namespace {

std::string firstWord(const std::string& text) {
    std::istringstream in(trim(text));
    std::string word;
    in >> word;
    return word;
}

bool isCommentOrEmpty(const std::string& text) {
    std::string t = trim(text);
    return t.empty() || t.rfind("//", 0) == 0;
}

bool startsAnyTypeDecl(const std::string& t) {
    static const std::unordered_set<std::string> types = {
        "integer", "real", "boolean", "string", "code", "handle", "unit", "player", "timer",
        "trigger", "effect", "group", "force", "rect", "location", "item", "destructable"
    };
    std::string word = firstWord(t);
    return types.contains(word);
}

std::string parseAccessPrefix(std::string& text) {
    std::string t = trim(text);
    if (startsWithWord(t, "public")) {
        text = trim(std::string_view(t).substr(6));
        return "public";
    }
    if (startsWithWord(t, "private")) {
        text = trim(std::string_view(t).substr(7));
        return "private";
    }
    text = t;
    return {};
}

std::string parseNameAfterWord(const std::string& text, const std::string& word) {
    std::string t = trim(text);
    if (!startsWithWord(t, word)) {
        return {};
    }
    t = trim(std::string_view(t).substr(word.size()));
    std::istringstream in(t);
    std::string name;
    in >> name;
    return name;
}

std::string parseZincFunctionName(const std::string& text) {
    std::string t = trim(text);
    std::string accessText = t;
    (void)parseAccessPrefix(accessText);
    if (!startsWithWord(accessText, "function")) {
        return {};
    }
    accessText = trim(std::string_view(accessText).substr(8));
    size_t open = accessText.find('(');
    if (open == std::string::npos) {
        std::istringstream in(accessText);
        std::string name;
        in >> name;
        return name;
    }
    return trim(std::string_view(accessText).substr(0, open));
}

void parseRequirementsAndInitializer(const std::string& tail, Decl& decl) {
    static const std::vector<std::string> depWords = {"requires", "needs", "uses"};
    std::string clean = tail;
    size_t brace = clean.find('{');
    if (brace != std::string::npos) {
        clean = clean.substr(0, brace);
    }

    size_t initPos = clean.find("initializer");
    if (initPos != std::string::npos) {
        std::istringstream initIn(clean.substr(initPos + 11));
        initIn >> decl.initializer;
        clean = clean.substr(0, initPos);
    }

    for (const auto& depWord : depWords) {
        size_t pos = clean.find(depWord);
        if (pos == std::string::npos) {
            continue;
        }
        std::string list = clean.substr(pos + depWord.size());
        for (const auto& otherWord : depWords) {
            size_t next = list.find(otherWord);
            if (next != std::string::npos) {
                list = list.substr(0, next);
            }
        }
        for (auto part : splitCommaListRespectingQuotes(list)) {
            part = trim(part);
            if (part.empty()) {
                continue;
            }
            bool optional = false;
            if (startsWithWord(part, "optional")) {
                optional = true;
                part = trim(std::string_view(part).substr(8));
            }
            std::istringstream in(part);
            std::string name;
            in >> name;
            if (!name.empty()) {
                decl.requirements.push_back(Requirement{name, optional});
            }
        }
    }
}

int braceDelta(const std::string& text) {
    int delta = 0;
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    for (char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (inRaw) {
            if (c == '\'') {
                inRaw = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '\'') {
            inRaw = true;
        } else if (c == '{') {
            ++delta;
        } else if (c == '}') {
            --delta;
        }
    }
    return delta;
}

std::string stripTrailingSemicolon(std::string text) {
    text = trim(text);
    if (!text.empty() && text.back() == ';') {
        text.pop_back();
    }
    return trim(text);
}

} // namespace

const char* declKindName(DeclKind kind) {
    switch (kind) {
    case DeclKind::GlobalBlock:
        return "GlobalBlock";
    case DeclKind::Native:
        return "Native";
    case DeclKind::TypeDecl:
        return "TypeDecl";
    case DeclKind::Function:
        return "Function";
    case DeclKind::Library:
        return "Library";
    case DeclKind::Scope:
        return "Scope";
    case DeclKind::Unsupported:
        return "Unsupported";
    }
    return "Unsupported";
}

bool Program::hasUnsupported() const {
    auto walk = [](const auto& self, const std::vector<Decl>& decls) -> bool {
        for (const auto& decl : decls) {
            if (decl.kind == DeclKind::Unsupported) {
                return true;
            }
            if (self(self, decl.children)) {
                return true;
            }
        }
        return false;
    };
    return walk(walk, decls);
}

Parser::Parser(Diagnostics& diagnostics) : diagnostics_(diagnostics) {}

Program Parser::parse(const std::vector<LogicalLine>& lines) {
    Program program;
    stats_ = ParserStats{};
    preScanUnsupported(lines);
    size_t index = 0;
    program.decls = parseJassRange(lines, index, {});
    program.stats = stats_;
    return program;
}

void Parser::preScanUnsupported(const std::vector<LogicalLine>& lines) {
    for (const auto& logical : lines) {
        std::string t = trim(logical.text);
        if (isCommentOrEmpty(t)) {
            continue;
        }
        std::string accessText = t;
        (void)parseAccessPrefix(accessText);
        if (startsWithWord(accessText, "optional")) {
            accessText = trim(std::string_view(accessText).substr(8));
        }
        if (startsWithWord(accessText, "struct")) {
            ++stats_.structsUnsupported;
            diagnostics_.unsupported(logical.loc, "struct");
        } else if (startsWithWord(accessText, "static method") || startsWithWord(accessText, "method")) {
            ++stats_.methodsUnsupported;
            diagnostics_.unsupported(logical.loc, "method");
        } else if (startsWithWord(accessText, "module")) {
            ++stats_.modulesUnsupported;
            diagnostics_.unsupported(logical.loc, "module");
        } else if (startsWithWord(accessText, "static if")) {
            ++stats_.staticIfUnsupported;
            diagnostics_.unsupported(logical.loc, "static if");
        } else if (startsWithWord(accessText, "function interface")) {
            ++stats_.functionInterfacesUnsupported;
            diagnostics_.unsupported(logical.loc, "function interface");
        }
    }
}

std::vector<Decl> Parser::parseJassRange(const std::vector<LogicalLine>& lines, size_t& index, const std::string& terminator) {
    std::vector<Decl> decls;
    while (index < lines.size()) {
        const auto& logical = lines[index];
        std::string t = trim(logical.text);
        if (!terminator.empty() && startsWithWord(t, terminator)) {
            ++index;
            break;
        }
        if (isCommentOrEmpty(t)) {
            ++index;
            continue;
        }

        if (logical.mode == SyntaxMode::Zinc) {
            if (startsWithWord(t, "library") || startsWithWord(t, "scope")) {
                decls.push_back(parseZincLibrary(lines, index));
                continue;
            }
            if (startsWithWord(t, "function")) {
                decls.push_back(parseZincFunction(lines, index));
                continue;
            }
            if (startsWithWord(t, "struct")) {
                decls.push_back(parseZincUnsupportedBlock(lines, index, "struct"));
                continue;
            }
            ++index;
            continue;
        }

        if (startsWithWord(t, "library") || startsWithWord(t, "library_once")) {
            decls.push_back(parseJassLibraryOrScope(lines, index, false));
        } else if (startsWithWord(t, "scope")) {
            decls.push_back(parseJassLibraryOrScope(lines, index, true));
        } else if (startsWithWord(t, "globals")) {
            decls.push_back(parseJassGlobalBlock(lines, index));
        } else {
            std::string accessText = t;
            std::string access = parseAccessPrefix(accessText);
            (void)access;
            if (startsWithWord(accessText, "function")) {
                decls.push_back(parseJassFunction(lines, index));
            } else if (startsWithWord(t, "native")) {
                Decl decl;
                decl.kind = DeclKind::Native;
                decl.mode = logical.mode;
                decl.loc = logical.loc;
                decl.name = parseNameAfterWord(t, "native");
                decl.lines.push_back(logical.text);
                ++stats_.natives;
                decls.push_back(std::move(decl));
                ++index;
            } else if (startsWithWord(t, "type")) {
                Decl decl;
                decl.kind = DeclKind::TypeDecl;
                decl.mode = logical.mode;
                decl.loc = logical.loc;
                decl.name = parseNameAfterWord(t, "type");
                decl.lines.push_back(logical.text);
                ++stats_.types;
                decls.push_back(std::move(decl));
                ++index;
            } else if (startsWithWord(t, "struct")) {
                decls.push_back(parseUnsupportedBlock(lines, index, "struct", "endstruct"));
            } else if (startsWithWord(t, "module")) {
                decls.push_back(parseUnsupportedBlock(lines, index, "module", "endmodule"));
            } else if (startsWithWord(t, "static if")) {
                Decl decl;
                decl.kind = DeclKind::Unsupported;
                decl.loc = logical.loc;
                decl.unsupportedFeature = "static if";
                decl.lines.push_back(logical.text);
                decls.push_back(std::move(decl));
                ++index;
            } else if (startsWithWord(t, "function interface")) {
                Decl decl;
                decl.kind = DeclKind::Unsupported;
                decl.loc = logical.loc;
                decl.unsupportedFeature = "function interface";
                decl.lines.push_back(logical.text);
                decls.push_back(std::move(decl));
                ++index;
            } else {
                ++index;
            }
        }
    }
    return decls;
}

Decl Parser::parseJassFunction(const std::vector<LogicalLine>& lines, size_t& index) {
    const auto& start = lines[index];
    std::string header = trim(start.text);
    std::string accessText = header;
    std::string access = parseAccessPrefix(accessText);

    Decl decl;
    decl.kind = DeclKind::Function;
    decl.mode = start.mode;
    decl.loc = start.loc;
    decl.access = access;
    decl.name = parseNameAfterWord(accessText, "function");
    decl.lines.push_back(start.text);
    ++stats_.functions;
    ++index;
    while (index < lines.size()) {
        decl.lines.push_back(lines[index].text);
        if (startsWithWord(trim(lines[index].text), "endfunction")) {
            ++index;
            break;
        }
        ++index;
    }
    return decl;
}

Decl Parser::parseJassGlobalBlock(const std::vector<LogicalLine>& lines, size_t& index) {
    Decl decl;
    decl.kind = DeclKind::GlobalBlock;
    decl.mode = lines[index].mode;
    decl.loc = lines[index].loc;
    ++stats_.globalsBlocks;
    ++index;
    while (index < lines.size()) {
        std::string t = trim(lines[index].text);
        if (startsWithWord(t, "endglobals")) {
            ++index;
            break;
        }
        decl.lines.push_back(lines[index].text);
        ++index;
    }
    return decl;
}

Decl Parser::parseJassLibraryOrScope(const std::vector<LogicalLine>& lines, size_t& index, bool isScope) {
    const auto& start = lines[index];
    std::string t = trim(start.text);
    Decl decl;
    decl.kind = isScope ? DeclKind::Scope : DeclKind::Library;
    decl.mode = start.mode;
    decl.loc = start.loc;
    decl.libraryOnce = startsWithWord(t, "library_once");
    decl.name = parseNameAfterWord(t, isScope ? "scope" : (decl.libraryOnce ? "library_once" : "library"));
    size_t namePos = t.find(decl.name);
    if (namePos != std::string::npos) {
        parseRequirementsAndInitializer(t.substr(namePos + decl.name.size()), decl);
    }
    if (isScope) {
        ++stats_.scopes;
    } else if (decl.libraryOnce) {
        ++stats_.libraryOnce;
    } else {
        ++stats_.libraries;
    }
    ++index;
    decl.children = parseJassRange(lines, index, isScope ? "endscope" : "endlibrary");
    return decl;
}

Decl Parser::parseUnsupportedBlock(const std::vector<LogicalLine>& lines, size_t& index, const std::string& feature, const std::string& endWord) {
    Decl decl;
    decl.kind = DeclKind::Unsupported;
    decl.mode = lines[index].mode;
    decl.loc = lines[index].loc;
    decl.unsupportedFeature = feature;
    while (index < lines.size()) {
        std::string t = trim(lines[index].text);
        decl.lines.push_back(lines[index].text);
        ++index;
        if (startsWithWord(t, endWord)) {
            break;
        }
    }
    return decl;
}

Decl Parser::parseZincLibrary(const std::vector<LogicalLine>& lines, size_t& index) {
    const auto& start = lines[index];
    std::string header = trim(start.text);
    bool isScope = startsWithWord(header, "scope");
    Decl decl;
    decl.kind = isScope ? DeclKind::Scope : DeclKind::Library;
    decl.mode = SyntaxMode::Zinc;
    decl.loc = start.loc;
    decl.name = parseNameAfterWord(header, isScope ? "scope" : "library");
    size_t namePos = header.find(decl.name);
    if (namePos != std::string::npos) {
        parseRequirementsAndInitializer(header.substr(namePos + decl.name.size()), decl);
    }
    if (isScope) {
        ++stats_.scopes;
    } else {
        ++stats_.libraries;
    }

    std::vector<LogicalLine> body;
    int depth = 0;
    bool seenOpen = false;
    while (index < lines.size()) {
        std::string text = lines[index].text;
        int delta = braceDelta(text);
        if (!seenOpen) {
            size_t brace = text.find('{');
            if (brace != std::string::npos) {
                seenOpen = true;
                std::string after = text.substr(brace + 1);
                if (!trim(after).empty()) {
                    body.push_back(LogicalLine{SyntaxMode::Zinc, lines[index].loc, after});
                }
            }
        } else {
            if (depth + delta <= 0) {
                size_t close = text.rfind('}');
                if (close != std::string::npos) {
                    std::string before = text.substr(0, close);
                    if (!trim(before).empty()) {
                        body.push_back(LogicalLine{SyntaxMode::Zinc, lines[index].loc, before});
                    }
                }
                ++index;
                break;
            }
            body.push_back(lines[index]);
        }
        depth += delta;
        ++index;
    }
    decl.children = parseZincMembers(body);
    return decl;
}

std::vector<Decl> Parser::parseZincMembers(const std::vector<LogicalLine>& lines) {
    std::vector<Decl> decls;
    size_t index = 0;
    while (index < lines.size()) {
        std::string t = trim(lines[index].text);
        if (isCommentOrEmpty(t) || t == "}") {
            ++index;
            continue;
        }
        std::string accessText = t;
        std::string access = parseAccessPrefix(accessText);
        if (startsWithWord(accessText, "function")) {
            Decl fn = parseZincFunction(lines, index);
            fn.access = access;
            decls.push_back(std::move(fn));
        } else if (startsWithWord(accessText, "struct") || startsWithWord(accessText, "method")) {
            decls.push_back(parseZincUnsupportedBlock(lines, index, startsWithWord(accessText, "struct") ? "struct" : "method"));
        } else if (startsAnyTypeDecl(accessText) || startsWithWord(accessText, "constant")) {
            Decl globals;
            globals.kind = DeclKind::GlobalBlock;
            globals.mode = SyntaxMode::Zinc;
            globals.loc = lines[index].loc;
            globals.access = access;
            while (index < lines.size()) {
                std::string line = trim(lines[index].text);
                std::string globalAccessText = line;
                (void)parseAccessPrefix(globalAccessText);
                if (!(startsAnyTypeDecl(globalAccessText) || startsWithWord(globalAccessText, "constant"))) {
                    break;
                }
                globals.lines.push_back(stripTrailingSemicolon(lines[index].text));
                ++index;
            }
            ++stats_.globalsBlocks;
            decls.push_back(std::move(globals));
        } else {
            ++index;
        }
    }
    return decls;
}

Decl Parser::parseZincFunction(const std::vector<LogicalLine>& lines, size_t& index) {
    const auto& start = lines[index];
    Decl decl;
    decl.kind = DeclKind::Function;
    decl.mode = SyntaxMode::Zinc;
    decl.loc = start.loc;
    std::string header = trim(start.text);
    std::string accessText = header;
    decl.access = parseAccessPrefix(accessText);
    decl.name = parseZincFunctionName(header);
    decl.lines.push_back(header);
    ++stats_.functions;

    int depth = braceDelta(header);
    ++index;
    while (index < lines.size()) {
        std::string line = lines[index].text;
        int delta = braceDelta(line);
        if (depth + delta <= 0) {
            size_t close = line.rfind('}');
            if (close != std::string::npos) {
                std::string before = line.substr(0, close);
                if (!trim(before).empty()) {
                    decl.lines.push_back(before);
                }
            }
            ++index;
            break;
        }
        decl.lines.push_back(line);
        depth += delta;
        ++index;
    }
    return decl;
}

Decl Parser::parseZincUnsupportedBlock(const std::vector<LogicalLine>& lines, size_t& index, const std::string& feature) {
    Decl decl;
    decl.kind = DeclKind::Unsupported;
    decl.mode = SyntaxMode::Zinc;
    decl.loc = lines[index].loc;
    decl.unsupportedFeature = feature;
    int depth = 0;
    bool seenBrace = false;
    while (index < lines.size()) {
        std::string t = trim(lines[index].text);
        decl.lines.push_back(lines[index].text);
        int delta = braceDelta(t);
        if (t.find('{') != std::string::npos) {
            seenBrace = true;
        }
        depth += delta;
        ++index;
        if ((seenBrace && depth <= 0) || (!seenBrace && !t.empty() && t.back() == ';')) {
            break;
        }
    }
    return decl;
}

} // namespace vjassc
