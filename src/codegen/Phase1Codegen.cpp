#include "codegen/Phase1Codegen.h"

#include "codegen/CodeWriter.h"
#include "util/PathUtil.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace vjassc {
namespace {

std::string rewriteOutsideProtected(const std::string& line, const std::function<std::string(std::string)>& rewrite);
std::string replaceRegex(std::string text, const std::string& pattern, const std::string& replacement);

std::string removeSemicolon(std::string text) {
    text = trim(text);
    if (!text.empty() && text.back() == ';') {
        text.pop_back();
    }
    return trim(text);
}

std::string removeSemicolonsOutsideProtected(const std::string& line) {
    std::string out;
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (!inString && !inRaw && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
            out += line.substr(i);
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
            continue;
        }
        if (inRaw) {
            out.push_back(c);
            if (c == '\'') {
                inRaw = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            out.push_back(c);
            continue;
        }
        if (c == '\'') {
            inRaw = true;
            out.push_back(c);
            continue;
        }
        if (c != ';') {
            out.push_back(c);
        }
    }
    return rtrim(out);
}

std::string normalizeOperatorsOutsideProtected(const std::string& line) {
    return rewriteOutsideProtected(line, [](std::string text) {
        text = replaceRegex(text, R"(\s*&&\s*)", " and ");
        text = replaceRegex(text, R"(\s*\|\|\s*)", " or ");
        text = replaceRegex(text, R"((^|[^=!<>])!\s*(?!=))", "$1not ");
        text = replaceRegex(text, R"(\bnot\s+=)", "!=");
        return text;
    });
}

std::string sanitizeGeneratedLine(const std::string& line) {
    return normalizeOperatorsOutsideProtected(removeSemicolonsOutsideProtected(line));
}

std::string parseNativeName(const std::string& line) {
    std::istringstream in(trim(line));
    std::string word;
    std::string name;
    in >> word >> name;
    return name;
}

bool isTypeWord(const std::string& word) {
    static const std::unordered_set<std::string> types = {
        "integer", "real", "boolean", "string", "code", "handle", "unit", "player", "timer",
        "trigger", "effect", "group", "force", "rect", "location", "item", "destructable",
        "widget", "image", "sound", "region", "hashtable", "boolexpr", "dialog", "button",
        "thistype"
    };
    return types.contains(word);
}

bool isLocalDecl(const std::string& line) {
    std::string t = trim(line);
    if (startsWithWord(t, "local")) {
        return true;
    }
    if (startsWithWord(t, "call") || startsWithWord(t, "set") || startsWithWord(t, "return") ||
        startsWithWord(t, "if") || startsWithWord(t, "while") || startsWithWord(t, "for") ||
        startsWithWord(t, "loop") || startsWithWord(t, "exitwhen") || startsWithWord(t, "else")) {
        return false;
    }
    std::istringstream in(t);
    std::string word;
    std::string name;
    in >> word >> name;
    if (word == "constant") {
        in >> word >> name;
    }
    if (word.empty() || name.empty()) {
        return false;
    }
    if (!std::all_of(word.begin(), word.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
        })) {
        return false;
    }
    if (!std::isalpha(static_cast<unsigned char>(word[0])) && word[0] != '_' && word[0] != '$') {
        return false;
    }
    return (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_' || name[0] == '$') ||
           isTypeWord(word);
}

std::string parenContent(const std::string& text) {
    size_t open = text.find('(');
    size_t close = text.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) {
        return {};
    }
    return trim(std::string_view(text).substr(open + 1, close - open - 1));
}

std::string conditionFromHeader(const std::string& text) {
    return "(" + parenContent(text) + ")";
}

bool isAssignmentLike(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '=') {
            continue;
        }
        char prev = i > 0 ? text[i - 1] : '\0';
        char next = i + 1 < text.size() ? text[i + 1] : '\0';
        if (prev != '=' && prev != '!' && prev != '<' && prev != '>' && next != '=') {
            return true;
        }
    }
    return text.find("+=") != std::string::npos || text.find("-=") != std::string::npos ||
           text.find("*=") != std::string::npos || text.find("/=") != std::string::npos;
}

std::vector<std::string> splitSemicolons(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    int parens = 0;
    bool inString = false;
    for (char c : text) {
        if (c == '"') {
            inString = !inString;
        } else if (!inString && c == '(') {
            ++parens;
        } else if (!inString && c == ')' && parens > 0) {
            --parens;
        }
        if (!inString && parens == 0 && c == ';') {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(trim(cur));
    return out;
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool isIdentPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool containsIdentifierOutsideProtected(const std::string& text, const std::string& ident) {
    if (ident.empty()) {
        return false;
    }
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    for (size_t i = 0; i < text.size();) {
        if (!inString && !inRaw && i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
            return false;
        }
        char c = text[i];
        if (inString) {
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
            if (c == '\'') {
                inRaw = false;
            }
            ++i;
            continue;
        }
        if (c == '"') {
            inString = true;
            ++i;
            continue;
        }
        if (c == '\'') {
            inRaw = true;
            ++i;
            continue;
        }
        if (i + ident.size() <= text.size() && text.compare(i, ident.size(), ident) == 0) {
            char before = i > 0 ? text[i - 1] : '\0';
            char after = i + ident.size() < text.size() ? text[i + ident.size()] : '\0';
            if (!isIdentPart(before) && before != '.' && !isIdentPart(after)) {
                return true;
            }
        }
        ++i;
    }
    return false;
}

std::optional<std::pair<size_t, size_t>> findAnonymousFunctionStart(const std::string& text) {
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    for (size_t i = 0; i < text.size();) {
        if (!inString && !inRaw && i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
            return std::nullopt;
        }
        char c = text[i];
        if (inString) {
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
            if (c == '\'') {
                inRaw = false;
            }
            ++i;
            continue;
        }
        if (c == '"') {
            inString = true;
            ++i;
            continue;
        }
        if (c == '\'') {
            inRaw = true;
            ++i;
            continue;
        }
        if (i + 8 <= text.size() && text.compare(i, 8, "function") == 0) {
            char before = i > 0 ? text[i - 1] : '\0';
            char afterWord = i + 8 < text.size() ? text[i + 8] : '\0';
            if (!isIdentPart(before) && !isIdentPart(afterWord)) {
                size_t after = i + 8;
                while (after < text.size() && std::isspace(static_cast<unsigned char>(text[after]))) {
                    ++after;
                }
                if (after < text.size() && text[after] == '(') {
                    return std::make_pair(i, after);
                }
            }
        }
        ++i;
    }
    return std::nullopt;
}

std::vector<std::string> declaredNamesInZincStatements(const std::string& raw) {
    std::vector<std::string> names;
    for (auto statement : splitSemicolons(raw)) {
        statement = removeSemicolon(trim(statement));
        if (statement.empty()) {
            continue;
        }
        if (startsWithWord(statement, "local")) {
            statement = trim(std::string_view(statement).substr(5));
        }
        if (!isLocalDecl(statement)) {
            continue;
        }
        if (startsWithWord(statement, "constant")) {
            statement = trim(std::string_view(statement).substr(8));
        }
        size_t assign = statement.find('=');
        std::string declPart = assign == std::string::npos ? statement : trim(std::string_view(statement).substr(0, assign));
        std::istringstream in(declPart);
        std::string type;
        std::string name;
        in >> type >> name;
        if (name == "array") {
            in >> name;
        }
        size_t bracket = name.find('[');
        if (bracket != std::string::npos) {
            name = name.substr(0, bracket);
        }
        if (!type.empty() && !name.empty()) {
            names.push_back(name);
        }
    }
    return names;
}

std::string regexEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (std::string_view(R"(\.^$|()[]{}*+?)").find(c) != std::string_view::npos) {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::string rewriteOutsideProtected(const std::string& line, const std::function<std::string(std::string)>& rewrite) {
    std::string out;
    std::string normal;
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    auto flush = [&]() {
        if (!normal.empty()) {
            out += rewrite(normal);
            normal.clear();
        }
    };
    for (size_t i = 0; i < line.size();) {
        if (!inString && !inRaw && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
            flush();
            out += line.substr(i);
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
            flush();
            inString = true;
            out.push_back(c);
            ++i;
            continue;
        }
        if (c == '\'') {
            flush();
            inRaw = true;
            out.push_back(c);
            ++i;
            continue;
        }
        normal.push_back(c);
        ++i;
    }
    flush();
    return out;
}

std::string replaceRegex(std::string text, const std::string& pattern, const std::string& replacement) {
    return std::regex_replace(text, std::regex(pattern), replacement);
}

std::vector<std::string> splitCommaList(const std::string& text) {
    return splitCommaListRespectingQuotes(text);
}

std::string joinParams(const std::vector<std::string>& params) {
    std::string out;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += params[i];
    }
    return out.empty() ? "nothing" : out;
}

std::string joinArgs(const std::vector<std::string>& args) {
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += args[i];
    }
    return out;
}

std::vector<ParamDecl> parseCodegenParamList(const std::string& text, SourceLocation loc) {
    std::vector<ParamDecl> params;
    for (auto part : splitCommaList(text)) {
        part = trim(part);
        if (part.empty() || part == "nothing") {
            continue;
        }
        std::istringstream in(part);
        std::string type;
        std::string name;
        in >> type >> name;
        if (!type.empty() && !name.empty()) {
            params.push_back(ParamDecl{TypeRef{type, loc, false}, name, loc});
        }
    }
    return params;
}

std::string compactSetSpacing(std::string line) {
    line = std::regex_replace(line, std::regex(R"(\s*=\s*)"), "=");
    line = std::regex_replace(line, std::regex(R"(\s*\+\s*)"), "+");
    return line;
}

size_t findMatchingParen(const std::string& text, size_t open) {
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        char c = text[i];
        if (!inString && !inRaw && i + 1 < text.size() && c == '/' && text[i + 1] == '/') {
            return std::string::npos;
        }
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
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

std::optional<size_t> findTopLevelAssignment(const std::string& text) {
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    int parens = 0;
    int brackets = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (!inString && !inRaw && i + 1 < text.size() && c == '/' && text[i + 1] == '/') {
            return std::nullopt;
        }
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
        } else if (c == '(') {
            ++parens;
        } else if (c == ')' && parens > 0) {
            --parens;
        } else if (c == '[') {
            ++brackets;
        } else if (c == ']' && brackets > 0) {
            --brackets;
        } else if (c == '=' && parens == 0 && brackets == 0) {
            char prev = i > 0 ? text[i - 1] : '\0';
            char next = i + 1 < text.size() ? text[i + 1] : '\0';
            if (prev != '=' && prev != '!' && prev != '<' && prev != '>' && next != '=') {
                return i;
            }
        }
    }
    return std::nullopt;
}

std::string receiverFromPossibleAssignment(const std::string& rawLine) {
    std::string line = trim(rawLine);
    if (auto eq = findTopLevelAssignment(line)) {
        std::string lhs = trim(std::string_view(line).substr(0, *eq));
        if (startsWithWord(lhs, "set")) {
            lhs = trim(std::string_view(lhs).substr(3));
        }
        if (isLocalDecl(lhs)) {
            if (startsWithWord(lhs, "constant")) {
                lhs = trim(std::string_view(lhs).substr(8));
            }
            std::istringstream in(lhs);
            std::string type;
            std::string name;
            in >> type >> name;
            if (name == "array") {
                in >> name;
            }
            return name;
        }
        return lhs;
    }
    return {};
}

std::vector<std::string> expandLeadingDotChains(const std::vector<std::string>& lines) {
    std::vector<std::string> out;
    std::string receiver;
    for (const auto& raw : lines) {
        std::string t = trim(raw);
        if (!t.empty() && t.front() == '.' && !receiver.empty()) {
            out.push_back(receiver + t);
            continue;
        }
        out.push_back(raw);
        std::string nextReceiver = receiverFromPossibleAssignment(t);
        if (!nextReceiver.empty()) {
            receiver = nextReceiver;
        } else if (!t.empty() && t.front() != '.') {
            receiver.clear();
        }
    }
    return out;
}

std::vector<std::string> splitZincStructuralLines(const std::vector<std::string>& lines) {
    std::vector<std::string> out;
    for (const auto& raw : lines) {
        std::string current;
        bool inString = false;
        bool inRaw = false;
        bool escaped = false;
        int parens = 0;
        int brackets = 0;
        auto flush = [&]() {
            std::string t = trim(current);
            if (!t.empty()) {
                out.push_back(t);
            }
            current.clear();
        };
        for (size_t i = 0; i < raw.size(); ++i) {
            char c = raw[i];
            if (!inString && !inRaw && i + 1 < raw.size() && c == '/' && raw[i + 1] == '/') {
                current += raw.substr(i);
                break;
            }
            if (inString) {
                current.push_back(c);
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
                current.push_back(c);
                if (c == '\'') {
                    inRaw = false;
                }
                continue;
            }
            if (c == '"') {
                inString = true;
                current.push_back(c);
                continue;
            }
            if (c == '\'') {
                inRaw = true;
                current.push_back(c);
                continue;
            }
            if (c == '(') {
                ++parens;
            } else if (c == ')' && parens > 0) {
                --parens;
            } else if (c == '[') {
                ++brackets;
            } else if (c == ']' && brackets > 0) {
                --brackets;
            }
            if (parens == 0 && brackets == 0 && c == '{') {
                current.push_back(c);
                flush();
                continue;
            }
            if (parens == 0 && brackets == 0 && c == '}') {
                flush();
                std::string close = "}";
                size_t j = i + 1;
                while (j < raw.size() && std::isspace(static_cast<unsigned char>(raw[j]))) {
                    ++j;
                }
                if (j + 4 <= raw.size() && raw.compare(j, 4, "else") == 0 &&
                    (j + 4 == raw.size() || !isIdentPart(raw[j + 4]))) {
                    close += " else";
                    j += 4;
                    while (j < raw.size() && std::isspace(static_cast<unsigned char>(raw[j]))) {
                        ++j;
                    }
                    while (j < raw.size()) {
                        char ec = raw[j];
                        close.push_back(ec);
                        if (ec == '{') {
                            break;
                        }
                        ++j;
                    }
                    i = j;
                }
                out.push_back(trim(close));
                continue;
            }
            if (parens == 0 && brackets == 0 && c == ';') {
                current.push_back(c);
                flush();
                continue;
            }
            current.push_back(c);
        }
        flush();
    }
    return out;
}

struct RangeForParts {
    bool ok = false;
    std::string start;
    std::string var;
    std::string upperOp;
    std::string end;
};

RangeForParts parseRangeForParts(const std::string& content) {
    RangeForParts result;
    std::regex pattern(R"(^\s*(.+?)\s*(?:<=|<)\s*([A-Za-z_$][A-Za-z0-9_$]*)\s*(<=|<)\s*(.+?)\s*$)");
    std::smatch match;
    if (std::regex_match(content, match, pattern)) {
        result.ok = true;
        result.start = trim(match[1].str());
        result.var = match[2].str();
        result.upperOp = match[3].str();
        result.end = trim(match[4].str());
    }
    return result;
}

bool findMethodCallSpan(const std::string& text,
                        const std::string& method,
                        size_t& receiverStart,
                        size_t& dotPos,
                        size_t& open,
                        size_t& close) {
    std::string needle = "." + method;
    size_t search = 0;
    while ((dotPos = text.find(needle, search)) != std::string::npos) {
        size_t methodEnd = dotPos + needle.size();
        size_t p = methodEnd;
        while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) {
            ++p;
        }
        if (p >= text.size() || text[p] != '(') {
            search = methodEnd;
            continue;
        }
        size_t left = dotPos;
        while (left > 0 && std::isspace(static_cast<unsigned char>(text[left - 1]))) {
            --left;
        }
        size_t start = left;
        int brackets = 0;
        while (start > 0) {
            char c = text[start - 1];
            if (c == ']') {
                ++brackets;
            } else if (c == '[' && brackets > 0) {
                --brackets;
            }
            if (brackets == 0 &&
                !(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '.' || c == ']' || c == '[')) {
                break;
            }
            --start;
        }
        close = findMatchingParen(text, p);
        if (close == std::string::npos) {
            search = methodEnd;
            continue;
        }
        receiverStart = start;
        open = p;
        return true;
    }
    return false;
}

std::string sanitizeName(std::string text) {
    for (char& c : text) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            c = '_';
        }
    }
    return text;
}

std::vector<int> parseArrayDimensions(std::string_view suffix) {
    std::vector<int> dims;
    size_t pos = 0;
    while (pos < suffix.size()) {
        while (pos < suffix.size() && std::isspace(static_cast<unsigned char>(suffix[pos]))) {
            ++pos;
        }
        if (pos >= suffix.size() || suffix[pos] != '[') {
            break;
        }
        size_t close = suffix.find(']', pos + 1);
        if (close == std::string_view::npos) {
            break;
        }
        std::string dimText = trim(suffix.substr(pos + 1, close - pos - 1));
        int dim = 0;
        if (!dimText.empty()) {
            try {
                dim = std::stoi(dimText);
            } catch (...) {
                dim = 0;
            }
        }
        dims.push_back(dim);
        pos = close + 1;
    }
    return dims;
}

int arrayFlatSize(const std::vector<int>& dims) {
    int total = 1;
    bool any = false;
    for (int dim : dims) {
        if (dim <= 0) {
            return 0;
        }
        total *= dim;
        any = true;
    }
    return any ? total : 0;
}

std::string stripLineCommentPreservingLiterals(const std::string& line) {
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        char c = line[i];
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
            continue;
        }
        if (c == '\'') {
            inRaw = true;
            continue;
        }
        if (c == '/' && line[i + 1] == '/') {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string composeFlatIndex(const std::vector<std::string>& indexes,
                             const std::vector<int>& dimensions,
                             size_t startIndex) {
    if (startIndex >= indexes.size()) {
        return {};
    }
    std::string expr = "(" + trim(indexes[startIndex]) + ")";
    for (size_t i = startIndex + 1; i < indexes.size() && i - startIndex < dimensions.size(); ++i) {
        int dim = dimensions[i - startIndex];
        expr = "(" + expr + " * " + std::to_string(dim) + " + (" + trim(indexes[i]) + "))";
    }
    return expr;
}

size_t findMatchingBracketOutsideProtected(const std::string& text, size_t open) {
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        char c = text[i];
        if (!inString && !inRaw && i + 1 < text.size() && c == '/' && text[i + 1] == '/') {
            return std::string::npos;
        }
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
        } else if (c == '[') {
            ++depth;
        } else if (c == ']') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

struct ArrayDeclRewrite {
    bool matched = false;
    std::string name;
    std::vector<int> dimensions;
    std::string line;
};

ArrayDeclRewrite rewriteMultiDimDeclarationLine(const std::string& line) {
    std::string code = stripLineCommentPreservingLiterals(line);
    std::string comment;
    if (code.size() < line.size()) {
        comment = line.substr(code.size());
    }
    std::string leading = line.substr(0, line.find_first_not_of(" \t") == std::string::npos ? 0 : line.find_first_not_of(" \t"));
    std::string t = trim(code);
    if (t.empty()) {
        return {};
    }
    bool hasLocal = false;
    if (startsWithWord(t, "local")) {
        hasLocal = true;
        t = trim(std::string_view(t).substr(5));
    }
    bool hasConstant = false;
    if (startsWithWord(t, "constant")) {
        hasConstant = true;
        t = trim(std::string_view(t).substr(8));
    }
    size_t assign = t.find('=');
    std::string declPart = assign == std::string::npos ? t : trim(std::string_view(t).substr(0, assign));
    std::string init = assign == std::string::npos ? "" : trim(std::string_view(t).substr(assign + 1));
    std::istringstream in(declPart);
    std::string type;
    std::string name;
    in >> type >> name;
    bool arrayWord = false;
    if (name == "array") {
        in >> name;
        arrayWord = true;
    }
    if (type.empty() || name.empty()) {
        return {};
    }
    size_t bracket = name.find('[');
    std::string suffix;
    if (bracket != std::string::npos) {
        suffix = name.substr(bracket);
        name = name.substr(0, bracket);
    } else {
        std::string rest;
        std::getline(in, rest);
        suffix = trim(rest);
    }
    std::vector<int> dims = parseArrayDimensions(suffix);
    if (dims.empty()) {
        return {};
    }
    ArrayDeclRewrite result;
    result.matched = true;
    result.name = name;
    result.dimensions = std::move(dims);
    result.line = leading;
    if (hasLocal) {
        result.line += "local ";
    }
    if (hasConstant) {
        result.line += "constant ";
    }
    result.line += type + " array " + name;
    if (!init.empty()) {
        result.line += "=" + init;
    }
    if (!comment.empty()) {
        result.line += comment;
    }
    (void)arrayWord;
    return result;
}

} // namespace

Phase1Codegen::Phase1Codegen(Diagnostics& diagnostics, CodegenOptions options)
    : diagnostics_(diagnostics), options_(options) {}

CodegenResult Phase1Codegen::generate(const Program& program) {
    if (program.hasUnsupported() && !options_.scanOnly) {
        diagnostics_.error(SourceLocation{}, "unsupported declarations prevent code generation");
        return makeResult(false);
    }
    symbols_.build(program);
    structs_.clear();
    structIndexByDecl_.clear();
    structIndexByName_.clear();
    globalArrayShapes_.clear();
    functionInterfaces_.clear();
    functionInterfaceIndexByName_.clear();
    functions_.clear();
    functionIndexByName_.clear();
    lambdas_.clear();
    processedZincFunctionBodies_.clear();
    processedZincMethodBodies_.clear();
    nextLambdaId_ = 1;
    lambdasCodeContext_ = 0;
    lambdasBoolexprContext_ = 0;
    lambdasFunctionInterfaceContext_ = 0;
    lambdasNativeCallbackContext_ = 0;
    lambdasMethodCallbackContext_ = 0;
    lambdasUnknownContext_ = 0;
    lambdasCapturing_ = 0;
    lambdasRejected_ = 0;
    lambdaContextStruct_ = nullptr;
    lambdaContextContainer_ = nullptr;
    functionInterfaceCalls_ = 0;
    functionObjectCalls_ = 0;
    functionInterfaceMaxEvaluateDepth_ = 0;
    structInitializers_.clear();
    mainFunction_ = nullptr;
    mainContainer_ = nullptr;
    collectStructs(program.decls, nullptr);
    collectFunctionInterfaces(program.decls, nullptr);
    collectFunctions(program.decls, nullptr);
    LibraryGraph graph(diagnostics_);
    LibraryGraphResult graphResult = graph.sort(program);
    if (diagnostics_.hasErrors()) {
        return makeResult(false);
    }

    writer_.writeln("// Generated by vjassc phase5");
    writer_.writeln();
    emitGlobals(program, graphResult);
    writer_.writeln();
    emitTypesAndNatives(program, graphResult);
    writer_.writeln();
    emitFunctions(program, graphResult);

    return makeResult(!diagnostics_.hasErrors());
}

CodegenResult Phase1Codegen::makeResult(bool ok) {
    CodegenResult result;
    result.ok = ok;
    if (ok) {
        std::istringstream in(writer_.str());
        std::string line;
        while (std::getline(in, line)) {
            result.output += sanitizeGeneratedLine(line);
            result.output += '\n';
        }
    }
    result.lambdasLowered = lambdas_.size();
    result.lambdasCodeContext = lambdasCodeContext_;
    result.lambdasBoolexprContext = lambdasBoolexprContext_;
    result.lambdasFunctionInterfaceContext = lambdasFunctionInterfaceContext_;
    result.lambdasNativeCallbackContext = lambdasNativeCallbackContext_;
    result.lambdasMethodCallbackContext = lambdasMethodCallbackContext_;
    result.lambdasUnknownContext = lambdasUnknownContext_;
    result.lambdasCapturing = lambdasCapturing_;
    result.lambdasRejected = lambdasRejected_;
    result.lambdasGeneratedFunctions = lambdas_.size();
    for (const auto& iface : functionInterfaces_) {
        result.functionInterfaceTargets += iface.targets.size();
    }
    result.functionInterfaceCalls = functionInterfaceCalls_;
    result.functionObjectCalls = functionObjectCalls_;
    result.functionInterfaceMaxEvaluateDepth = functionInterfaceMaxEvaluateDepth_;
    result.functionInterfaceEvaluateTempLimit = functionInterfaceEvaluateTempLimit_;
    return result;
}

void Phase1Codegen::emitGlobals(const Program& program, const LibraryGraphResult& graph) {
    writer_.writeln("globals");
    writer_.indent();
    for (const Decl* library : graph.sortedLibraries) {
        writer_.writeln("constant boolean LIBRARY_" + library->name + "=true");
    }
    emitStructGlobals();
    emitFunctionInterfaceGlobals();
    for (const Decl* library : graph.sortedLibraries) {
        emitDeclGlobals(*library, nullptr);
    }
    for (const auto& decl : program.decls) {
        if (decl.kind != DeclKind::Library) {
            emitDeclGlobals(decl, nullptr);
        }
    }
    writer_.dedent();
    writer_.writeln("endglobals");
}

void Phase1Codegen::emitDeclGlobals(const Decl& decl, const Decl* container) {
    if (decl.kind == DeclKind::Struct || decl.kind == DeclKind::Module || decl.kind == DeclKind::FunctionInterface) {
        return;
    }
    if (decl.kind == DeclKind::GlobalBlock) {
        for (auto line : decl.lines) {
            std::string out = rewriteGlobalLine(line, container);
            if (!trim(out).empty()) {
                writer_.writeln(out);
            }
        }
        return;
    }
    const Decl* nextContainer = (decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) ? &decl : container;
    for (const auto& child : decl.children) {
        emitDeclGlobals(child, nextContainer);
    }
}

void Phase1Codegen::emitFunctionInterfaceGlobals() {
    for (const auto& iface : functionInterfaces_) {
        std::string prefix = interfaceGlobalPrefix(iface);
        writer_.writeln("trigger array " + prefix + "_trigger");
        for (size_t i = 0; i < iface.signature.paramTypes.size(); ++i) {
            writer_.writeln(rewriteTypeName(iface.signature.paramTypes[i], nullptr) + " " + prefix + "_arg" + std::to_string(i));
        }
        if (iface.signature.returnType != "nothing") {
            writer_.writeln(rewriteTypeName(iface.signature.returnType, nullptr) + " " + prefix + "_result");
            for (int i = 1; i <= 8; ++i) {
                writer_.writeln(rewriteTypeName(iface.signature.returnType, nullptr) + " " + prefix + "_tmp" + std::to_string(i));
            }
        }
    }
}

void Phase1Codegen::emitStructGlobals() {
    for (const auto& info : structs_) {
        emitStructGlobalBlock(info);
    }
}

void Phase1Codegen::emitStructGlobalBlock(const StructInfo& info) {
    if (!info.isArrayStruct) {
        writer_.writeln("integer array si__" + info.generatedName + "_F");
        writer_.writeln("integer si__" + info.generatedName + "_I=0");
        writer_.writeln("integer array si__" + info.generatedName + "_V");
    }
    for (const auto& field : info.fields) {
        std::string typeName = rewriteTypeName(field.typeName, &info);
        if (field.isFixedArray && field.fixedArraySize > 0) {
            writer_.writeln("constant integer s___" + info.generatedName + "_" + field.name + "_size=" + std::to_string(field.fixedArraySize));
        }
        if (field.isStatic) {
            if (field.isArray) {
                writer_.writeln(typeName + " array " + field.generatedName);
            } else if (!field.decl->initializer.empty()) {
                writer_.writeln(typeName + " " + field.generatedName + "=" + rewriteStructExpression(field.decl->initializer, &info, {}));
            } else {
                writer_.writeln(typeName + " " + field.generatedName);
            }
        } else {
            writer_.writeln(typeName + " array " + field.generatedName);
        }
    }
}

void Phase1Codegen::emitTypesAndNatives(const Program& program, const LibraryGraphResult& graph) {
    for (const auto& decl : program.decls) {
        if (decl.kind != DeclKind::Library) {
            emitTypeOrNative(decl);
        }
    }
    for (const Decl* library : graph.sortedLibraries) {
        emitTypeOrNative(*library);
    }
}

void Phase1Codegen::emitTypeOrNative(const Decl& decl) {
    if (decl.kind == DeclKind::Module || decl.kind == DeclKind::FunctionInterface) {
        return;
    }
    if (decl.kind == DeclKind::TypeDecl) {
        writer_.writeln(trim(decl.lines.front()));
    } else if (decl.kind == DeclKind::Native) {
        std::string name = parseNativeName(decl.lines.front());
        if (emittedNatives_.insert(name).second) {
            writer_.writeln(trim(decl.lines.front()));
        } else {
            diagnostics_.warning(decl.loc, "duplicate native '" + name + "' ignored");
        }
    }
    for (const auto& child : decl.children) {
        emitTypeOrNative(child);
    }
}

void Phase1Codegen::emitFunctions(const Program& program, const LibraryGraphResult& graph) {
    initializers_.clear();
    collectRootInitializers(program.decls);
    emitStructFunctions();
    for (const Decl* library : graph.sortedLibraries) {
        collectInitializers(*library, library);
        emitDeclFunctions(*library, library);
    }
    for (const auto& decl : program.decls) {
        if (decl.kind != DeclKind::Library) {
            emitDeclFunctions(decl, nullptr);
        }
    }
    emitGeneratedLambdas();
    emitFunctionInterfaceRuntime();
    emitInitHelper(graph, program);
    if (mainFunction_) {
        emitJassFunction(*mainFunction_, mainContainer_, true);
    }
}

void Phase1Codegen::emitDeclFunctions(const Decl& decl, const Decl* container) {
    if (decl.kind == DeclKind::Struct || decl.kind == DeclKind::Module || decl.kind == DeclKind::FunctionInterface) {
        return;
    }
    if (decl.kind == DeclKind::Function) {
        if (decl.name == "main") {
            mainFunction_ = &decl;
            mainContainer_ = container;
            return;
        }
        if (decl.mode == SyntaxMode::Zinc) {
            emitZincFunction(decl, container);
        } else {
            emitJassFunction(decl, container, false);
        }
        writer_.writeln();
        return;
    }
    const Decl* nextContainer = (decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) ? &decl : container;
    for (const auto& child : decl.children) {
        emitDeclFunctions(child, nextContainer);
    }
}

void Phase1Codegen::emitJassFunction(const Decl& decl, const Decl* container, bool injectInit) {
    if (decl.lines.empty()) {
        return;
    }
    std::string header = rewriteFunctionHeader(decl, container);
    writer_.writeln(header);
    writer_.indent();
    std::unordered_map<std::string, std::string> localTypes;
    seedFunctionLocalTypes(stripAccessPrefixFromLine(decl.lines.front()), localTypes);
    std::vector<std::string> tempLocals;
    std::unordered_map<std::string, ArrayShape> localArrayShapes;
    LoweringContext ctx{nullptr, container, &localTypes, &localArrayShapes, &tempLocals, 0};
    bool injected = false;
    std::vector<std::string> locals;
    std::vector<std::string> body;
    std::vector<std::string> rawLines(decl.lines.begin() + 1, decl.lines.end());
    const StructInfo* prevLambdaStruct = lambdaContextStruct_;
    const Decl* prevLambdaContainer = lambdaContextContainer_;
    lambdaContextStruct_ = nullptr;
    lambdaContextContainer_ = container;
    rawLines = extractZincLambdas(rawLines, decl.loc);
    lambdaContextStruct_ = prevLambdaStruct;
    lambdaContextContainer_ = prevLambdaContainer;
    for (const auto& rawLine : rawLines) {
        std::string t = trim(rawLine);
        if (startsWithWord(t, "endfunction")) {
            break;
        }
        std::string line = trim(rewriteForContainer(rawLine, container));
        std::vector<std::string> extraLines;
        if (startsWithWord(line, "local")) {
            locals.push_back(rewriteLocalDeclLine(line, nullptr, localTypes, ctx.localArrayShapes, extraLines));
            for (const auto& extra : extraLines) {
                for (const auto& lowered : lowerStatementLine(extra, ctx)) {
                    body.push_back(trim(lowered));
                }
            }
        } else {
            for (const auto& lowered : lowerStatementLine(line, ctx)) {
                body.push_back(trim(lowered));
            }
        }
        if (injectInit && !injected && t.find("InitBlizzard") != std::string::npos) {
            body.push_back("call vjassc__init_structs()");
            if (!functionInterfaces_.empty()) {
                body.push_back("call vjassc__init_function_interfaces()");
            }
            body.push_back("call vjassc__init_libraries()");
            injected = true;
        }
    }
    if (injectInit && !injected) {
        body.push_back("call vjassc__init_structs()");
        if (!functionInterfaces_.empty()) {
            body.push_back("call vjassc__init_function_interfaces()");
        }
        body.push_back("call vjassc__init_libraries()");
    }
    for (const auto& local : locals) {
        writer_.writeln(local);
    }
    for (const auto& line : body) {
        writer_.writeln(line);
    }
    writer_.dedent();
    writer_.writeln("endfunction");
}

void Phase1Codegen::emitZincFunction(const Decl& decl, const Decl* container) {
    if (decl.lines.empty()) {
        return;
    }
    std::string header = trim(decl.lines.front());
    std::string accessHeader = header;
    (void)stripAccessPrefixFromLine(accessHeader);
    size_t functionPos = header.find("function");
    size_t open = header.find('(', functionPos);
    size_t close = header.find(')', open);
    std::string functionName = decl.name;
    std::string finalName = renameInContainer(functionName, container);
    std::string params = (open != std::string::npos && close != std::string::npos) ? header.substr(open + 1, close - open - 1) : "";
    std::string returns = "nothing";
    size_t arrow = header.find("->", close == std::string::npos ? 0 : close);
    if (arrow != std::string::npos) {
        std::string after = trim(std::string_view(header).substr(arrow + 2));
        size_t brace = after.find('{');
        if (brace != std::string::npos) {
            after = trim(std::string_view(after).substr(0, brace));
        }
        if (!after.empty()) {
            returns = after;
        }
    }
    params = trim(params);
    if (params.empty()) {
        params = "nothing";
    } else {
        std::vector<std::string> rewrittenParams;
        for (auto part : splitCommaList(params)) {
            std::istringstream in(trim(part));
            std::string type;
            std::string name;
            in >> type >> name;
            if (!type.empty() && !name.empty()) {
                rewrittenParams.push_back(rewriteTypeName(type, nullptr) + " " + name);
            }
        }
        params = joinParams(rewrittenParams);
    }
    writer_.writeln("function " + finalName + " takes " + params + " returns " + rewriteTypeName(returns, nullptr));
    writer_.indent();
    std::unordered_map<std::string, std::string> localTypes;
    FunctionSignature fnSig = parseFunctionSignatureFromHeader(header, SyntaxMode::Zinc);
    size_t paramIndex = 0;
    for (auto part : splitCommaList(params == "nothing" ? "" : params)) {
        std::istringstream in(trim(part));
        std::string type;
        std::string name;
        in >> type >> name;
        if (!type.empty() && !name.empty()) {
            if (findStruct(type) || findFunctionInterface(type)) {
                localTypes[name] = type;
            } else if (paramIndex < fnSig.paramTypes.size() && findFunctionInterface(fnSig.paramTypes[paramIndex])) {
                localTypes[name] = fnSig.paramTypes[paramIndex];
            }
        }
        ++paramIndex;
    }
    std::vector<std::string> rawBodyLines(decl.lines.begin() + 1, decl.lines.end());
    const StructInfo* prevLambdaStruct = lambdaContextStruct_;
    const Decl* prevLambdaContainer = lambdaContextContainer_;
    lambdaContextStruct_ = nullptr;
    lambdaContextContainer_ = container;
    std::vector<std::string> bodyLines = extractZincLambdas(rawBodyLines, decl.loc);
    lambdaContextStruct_ = prevLambdaStruct;
    lambdaContextContainer_ = prevLambdaContainer;
    std::vector<std::string> tempLocals;
    std::unordered_map<std::string, ArrayShape> localArrayShapes;
    LoweringContext ctx{nullptr, container, &localTypes, &localArrayShapes, &tempLocals, 0};
    for (const auto& line : lowerZincBody(bodyLines)) {
        std::string out = rewriteForContainer(line, container);
        std::vector<std::string> extraLines;
        if (startsWithWord(trim(out), "local")) {
            writer_.writeln(rewriteLocalDeclLine(out, nullptr, localTypes, ctx.localArrayShapes, extraLines));
            for (const auto& extra : extraLines) {
                for (const auto& lowered : lowerStatementLine(extra, ctx)) {
                    writer_.writeln(lowered);
                }
            }
        } else {
            for (const auto& lowered : lowerStatementLine(out, ctx)) {
                writer_.writeln(lowered);
            }
        }
    }
    writer_.dedent();
    writer_.writeln("endfunction");
}

void Phase1Codegen::emitGeneratedLambdas() {
    for (const auto& lambda : lambdas_) {
        writer_.writeln();
        writer_.writeln("function " + lambda.name + " takes " + rewriteParamList(lambda.params, false, nullptr) +
                        " returns " + rewriteTypeName(lambda.returnType.name, lambda.currentStruct));
        writer_.indent();
        std::unordered_map<std::string, std::string> localTypes;
        for (const auto& param : lambda.params) {
            if (findStruct(param.type.name) || findFunctionInterface(param.type.name)) {
                localTypes[param.name] = param.type.name;
            }
        }
        std::vector<std::string> lines = lowerZincBody(lambda.bodyLines);
        std::vector<std::string> tempLocals;
        std::unordered_map<std::string, ArrayShape> localArrayShapes;
        LoweringContext ctx{lambda.currentStruct, lambda.container, &localTypes, &localArrayShapes, &tempLocals, 0};
        for (const auto& raw : lines) {
            std::string line = trim(rewriteForContainer(raw, lambda.container));
            if (line.empty()) {
                continue;
            }
            std::vector<std::string> extraLines;
            if (startsWithWord(line, "local")) {
                writer_.writeln(rewriteLocalDeclLine(line, nullptr, localTypes, ctx.localArrayShapes, extraLines));
                for (const auto& extra : extraLines) {
                    for (const auto& lowered : lowerStatementLine(extra, ctx)) {
                        writer_.writeln(lowered);
                    }
                }
            } else {
                for (const auto& lowered : lowerStatementLine(line, ctx)) {
                    writer_.writeln(lowered);
                }
            }
        }
        writer_.dedent();
        writer_.writeln("endfunction");
    }
    if (!lambdas_.empty()) {
        writer_.writeln();
    }
}

void Phase1Codegen::emitFunctionInterfaceRuntime() {
    bool emittedAny = false;
    for (const auto& iface : functionInterfaces_) {
        std::string prefix = interfaceGlobalPrefix(iface);
        for (const auto& target : iface.targets) {
            writer_.writeln("function " + prefix + "__" + sanitizeName(target.finalName) + "__wrapper takes nothing returns nothing");
            writer_.indent();
            std::vector<std::string> args;
            for (size_t i = 0; i < iface.signature.paramTypes.size(); ++i) {
                args.push_back(prefix + "_arg" + std::to_string(i));
            }
            std::string call = target.finalName + "(" + joinArgs(args) + ")";
            if (iface.signature.returnType == "nothing") {
                writer_.writeln("call " + call);
            } else {
                writer_.writeln("set " + prefix + "_result=" + call);
            }
            writer_.dedent();
            writer_.writeln("endfunction");
            writer_.writeln();
            emittedAny = true;
        }
    }

    if (!functionInterfaces_.empty()) {
        writer_.writeln("function vjassc__init_function_interfaces takes nothing returns nothing");
        writer_.indent();
        for (const auto& iface : functionInterfaces_) {
            std::string prefix = interfaceGlobalPrefix(iface);
            for (const auto& target : iface.targets) {
                writer_.writeln("set " + prefix + "_trigger[" + std::to_string(target.id) + "]=CreateTrigger()");
                writer_.writeln("call TriggerAddAction(" + prefix + "_trigger[" + std::to_string(target.id) + "], function " +
                                prefix + "__" + sanitizeName(target.finalName) + "__wrapper)");
            }
        }
        writer_.dedent();
        writer_.writeln("endfunction");
        writer_.writeln();
    }
    (void)emittedAny;
}

void Phase1Codegen::emitStructFunctions() {
    for (const auto& info : structs_) {
        emitStructSupportFunctions(info);
    }
}

void Phase1Codegen::emitStructSupportFunctions(const StructInfo& info) {
    const MethodInfo* customCreate = findMethod(info, "create");
    std::unordered_set<const MethodDecl*> emittedBeforeDestroy;
    if (!info.isArrayStruct) {
        writer_.writeln("function " + info.prefix + "__allocate takes nothing returns integer");
        writer_.indent();
        writer_.writeln("local integer this");
        writer_.writeln("if si__" + info.generatedName + "_F[0] == 0 then");
        writer_.indent();
        writer_.writeln("set si__" + info.generatedName + "_I=si__" + info.generatedName + "_I+1");
        writer_.writeln("set this=si__" + info.generatedName + "_I");
        writer_.dedent();
        writer_.writeln("else");
        writer_.indent();
        writer_.writeln("set this=si__" + info.generatedName + "_F[0]");
        writer_.writeln("set si__" + info.generatedName + "_F[0]=si__" + info.generatedName + "_F[this]");
        writer_.dedent();
        writer_.writeln("endif");
        writer_.writeln("set si__" + info.generatedName + "_V[this]=-1");
        writer_.writeln("return this");
        writer_.dedent();
        writer_.writeln("endfunction");
        writer_.writeln();

        if (!customCreate || !customCreate->isStatic) {
            writer_.writeln("function " + info.prefix + "_create takes nothing returns integer");
            writer_.indent();
            writer_.writeln("return " + info.prefix + "__allocate()");
            writer_.dedent();
            writer_.writeln("endfunction");
            writer_.writeln();
        }

        for (const auto& method : info.methods) {
            if (method.isOnDestroy) {
                emitStructMethod(info, method);
                writer_.writeln();
                emittedBeforeDestroy.insert(method.decl);
            }
        }

        writer_.writeln("function " + info.prefix + "_destroy takes integer this returns nothing");
        writer_.indent();
        for (const auto& method : info.methods) {
            if (method.isOnDestroy) {
                writer_.writeln("call " + method.generatedName + "(this)");
            }
        }
        writer_.writeln("set si__" + info.generatedName + "_V[this]=0");
        writer_.writeln("set si__" + info.generatedName + "_F[this]=si__" + info.generatedName + "_F[0]");
        writer_.writeln("set si__" + info.generatedName + "_F[0]=this");
        writer_.dedent();
        writer_.writeln("endfunction");
        writer_.writeln();
    }

    for (const auto& method : info.methods) {
        if (emittedBeforeDestroy.contains(method.decl)) {
            continue;
        }
        emitStructMethod(info, method);
        writer_.writeln();
        if (method.isOnInit) {
            structInitializers_.push_back(method.generatedName);
        }
    }
}

void Phase1Codegen::emitStructMethod(const StructInfo& info, const MethodInfo& method) {
    std::string params = rewriteParamList(method.decl->params, !method.isStatic, &info);
    std::string returns = rewriteTypeName(method.decl->returnType.name, &info);
    writer_.writeln("function " + method.generatedName + " takes " + params + " returns " + returns);
    writer_.indent();
    std::unordered_map<std::string, std::string> localTypes;
    seedMethodLocalTypes(info, method, localTypes);
    std::vector<std::string> rawLines;
    for (const auto& logical : method.decl->bodyLines) {
        rawLines.push_back(logical.text);
    }
    const StructInfo* prevLambdaStruct = lambdaContextStruct_;
    const Decl* prevLambdaContainer = lambdaContextContainer_;
    lambdaContextStruct_ = &info;
    lambdaContextContainer_ = info.container;
    rawLines = extractZincLambdas(rawLines, method.decl->loc);
    lambdaContextStruct_ = prevLambdaStruct;
    lambdaContextContainer_ = prevLambdaContainer;
    std::vector<std::string> lines = method.decl->mode == SyntaxMode::Zinc ? lowerZincBody(rawLines) : rawLines;
    std::vector<std::string> tempLocals;
    std::unordered_map<std::string, ArrayShape> localArrayShapes;
    LoweringContext ctx{&info, info.container, &localTypes, &localArrayShapes, &tempLocals, 0};
    std::vector<std::string> locals;
    std::vector<std::string> body;
    for (const auto& raw : lines) {
        std::string line = trim(rewriteForContainer(raw, info.container));
        if (line.empty() || line.rfind("//", 0) == 0) {
            continue;
        }
        std::vector<std::string> extraLines;
        if (startsWithWord(line, "local")) {
            locals.push_back(rewriteLocalDeclLine(line, &info, localTypes, ctx.localArrayShapes, extraLines));
            for (const auto& extra : extraLines) {
                for (const auto& lowered : lowerStatementLine(extra, ctx)) {
                    body.push_back(lowered);
                }
            }
        } else {
            for (const auto& lowered : lowerStatementLine(line, ctx)) {
                body.push_back(lowered);
            }
        }
    }
    for (const auto& local : locals) {
        writer_.writeln(local);
    }
    for (const auto& line : body) {
        writer_.writeln(line);
    }
    writer_.dedent();
    writer_.writeln("endfunction");
}

void Phase1Codegen::emitInitHelper(const LibraryGraphResult&, const Program&) {
    writer_.writeln("function vjassc__init_structs takes nothing returns nothing");
    writer_.indent();
    for (const auto& name : structInitializers_) {
        writer_.writeln("call " + name + "()");
    }
    writer_.dedent();
    writer_.writeln("endfunction");
    writer_.writeln();

    writer_.writeln("function vjassc__init_libraries takes nothing returns nothing");
    writer_.indent();
    for (const auto& [name, executeFunc] : initializers_) {
        if (executeFunc) {
            writer_.writeln("call ExecuteFunc(\"" + name + "\")");
        } else {
            writer_.writeln("call " + name + "()");
        }
    }
    writer_.dedent();
    writer_.writeln("endfunction");
    writer_.writeln();
}

void Phase1Codegen::collectInitializers(const Decl& decl, const Decl* container) {
    if (decl.kind == DeclKind::Module) {
        return;
    }
    if ((decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) && !decl.initializer.empty()) {
        initializers_.push_back({renameInContainer(decl.initializer, container), decl.kind == DeclKind::Library});
    }
    const Decl* nextContainer = (decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) ? &decl : container;
    for (const auto& child : decl.children) {
        collectInitializers(child, nextContainer);
    }
}

void Phase1Codegen::collectRootInitializers(const std::vector<Decl>& decls) {
    for (const auto& decl : decls) {
        if (decl.kind == DeclKind::Scope) {
            collectInitializers(decl, &decl);
        }
    }
}

void Phase1Codegen::collectStructs(const std::vector<Decl>& decls, const Decl* container) {
    for (const auto& decl : decls) {
        if (decl.kind == DeclKind::Struct) {
            collectStruct(decl, container);
            continue;
        }
        if (decl.kind == DeclKind::Module) {
            continue;
        }
        const Decl* nextContainer = (decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) ? &decl : container;
        collectStructs(decl.children, nextContainer);
    }
}

void Phase1Codegen::collectStruct(const Decl& decl, const Decl* container) {
    StructInfo info;
    info.decl = &decl;
    info.container = container;
    info.originalName = decl.name;
    info.generatedName = makeScopedStructName(decl, container);
    info.prefix = "s__" + info.generatedName;
    info.typeId = static_cast<int>(structs_.size()) + 1;
    info.isArrayStruct = decl.isArrayStruct;

    std::unordered_set<std::string> seenFields;
    std::unordered_set<std::string> seenMethods;
    for (const auto& field : decl.fields) {
        if (!seenFields.insert(field.name).second) {
            diagnostics_.error(field.loc, "duplicate field '" + field.name + "' in struct '" + decl.name + "'");
        }
        FieldInfo fieldInfo;
        fieldInfo.decl = &field;
        fieldInfo.name = field.name;
        fieldInfo.typeName = field.type.name;
        fieldInfo.generatedName = info.prefix + "_" + field.name;
        fieldInfo.isStatic = field.isStatic;
        fieldInfo.isArray = field.isArray;
        fieldInfo.isFixedArray = field.isFixedArray;
        fieldInfo.fixedArraySize = field.fixedArraySize;
        fieldInfo.arrayDimensions = field.arrayDimensions;
        if (fieldInfo.arrayDimensions.size() > 1 ||
            (!fieldInfo.isStatic && fieldInfo.isFixedArray && !fieldInfo.arrayDimensions.empty())) {
            globalArrayShapes_[fieldInfo.generatedName] = ArrayShape{fieldInfo.arrayDimensions, !fieldInfo.isStatic};
        }
        info.fieldIndex[fieldInfo.name] = info.fields.size();
        info.fields.push_back(std::move(fieldInfo));
    }
    for (const auto& method : decl.methods) {
        if (!seenMethods.insert(method.name).second) {
            diagnostics_.error(method.loc, "duplicate method '" + method.name + "' in struct '" + decl.name + "'");
        }
        if (seenFields.contains(method.name)) {
            diagnostics_.error(method.loc, "field and method share name '" + method.name + "' in struct '" + decl.name + "'");
        }
        if (method.isOnDestroy && (method.isStatic || !method.params.empty() || method.returnType.name != "nothing")) {
            diagnostics_.error(method.loc, "onDestroy must be an instance method returning nothing with no parameters");
        }
        if (method.isOnInit && (!method.isStatic || !method.params.empty() || method.returnType.name != "nothing")) {
            diagnostics_.error(method.loc, "onInit must be a static method returning nothing with no parameters");
        }
        MethodInfo methodInfo;
        methodInfo.decl = &method;
        methodInfo.name = method.name;
        methodInfo.isStatic = method.isStatic;
        methodInfo.isOnDestroy = method.isOnDestroy;
        methodInfo.isOnInit = method.isOnInit;
        if (method.isOnDestroy && method.name == "onDestroy") {
            methodInfo.generatedName = "sc__" + info.generatedName + "_onDestroy";
        } else {
            methodInfo.generatedName = info.prefix + "_" + method.name;
        }
        info.methodIndex[methodInfo.name] = info.methods.size();
        info.methods.push_back(std::move(methodInfo));
    }

    size_t index = structs_.size();
    structs_.push_back(std::move(info));
    structIndexByDecl_[&decl] = index;
    structIndexByName_[structs_[index].originalName] = index;
    structIndexByName_[structs_[index].generatedName] = index;
}

Phase1Codegen::FunctionSignature Phase1Codegen::signatureFromParams(const std::vector<ParamDecl>& params,
                                                                     const TypeRef& returnType,
                                                                     const StructInfo* currentStruct) const {
    FunctionSignature sig;
    for (const auto& param : params) {
        sig.paramTypes.push_back(param.type.name == "thistype" && currentStruct ? currentStruct->originalName : param.type.name);
    }
    sig.returnType = returnType.name == "thistype" && currentStruct ? currentStruct->originalName : (returnType.name.empty() ? "nothing" : returnType.name);
    return sig;
}

Phase1Codegen::FunctionSignature Phase1Codegen::parseFunctionSignatureFromHeader(const std::string& header, SyntaxMode mode) const {
    FunctionSignature sig;
    sig.returnType = "nothing";
    if (mode == SyntaxMode::Zinc) {
        size_t open = header.find('(');
        size_t close = open == std::string::npos ? std::string::npos : header.find(')', open);
        if (open != std::string::npos && close != std::string::npos) {
            for (auto part : splitCommaList(header.substr(open + 1, close - open - 1))) {
                std::istringstream in(trim(part));
                std::string type;
                std::string name;
                in >> type >> name;
                if (!type.empty() && !name.empty()) {
                    sig.paramTypes.push_back(type);
                }
            }
            size_t arrow = header.find("->", close);
            if (arrow != std::string::npos) {
                std::string ret = trim(std::string_view(header).substr(arrow + 2));
                size_t brace = ret.find('{');
                if (brace != std::string::npos) {
                    ret = trim(std::string_view(ret).substr(0, brace));
                }
                if (!ret.empty()) {
                    sig.returnType = ret;
                }
            }
        }
        return sig;
    }

    size_t takes = header.find(" takes ");
    size_t returns = header.find(" returns ");
    if (takes != std::string::npos && returns != std::string::npos && returns > takes) {
        std::string params = trim(std::string_view(header).substr(takes + 7, returns - (takes + 7)));
        if (!params.empty() && params != "nothing") {
            for (auto part : splitCommaList(params)) {
                std::istringstream in(trim(part));
                std::string type;
                std::string name;
                in >> type >> name;
                if (!type.empty() && !name.empty()) {
                    sig.paramTypes.push_back(type);
                }
            }
        }
        std::string ret = trim(std::string_view(header).substr(returns + 9));
        if (!ret.empty()) {
            sig.returnType = ret;
        }
    }
    return sig;
}

bool Phase1Codegen::sameSignature(const FunctionSignature& a, const FunctionSignature& b) const {
    if (rewriteTypeName(a.returnType, nullptr) != rewriteTypeName(b.returnType, nullptr) || a.paramTypes.size() != b.paramTypes.size()) {
        return false;
    }
    for (size_t i = 0; i < a.paramTypes.size(); ++i) {
        if (rewriteTypeName(a.paramTypes[i], nullptr) != rewriteTypeName(b.paramTypes[i], nullptr)) {
            return false;
        }
    }
    return true;
}

void Phase1Codegen::collectFunctionInterfaces(const std::vector<Decl>& decls, const Decl* container) {
    for (const auto& decl : decls) {
        if (decl.kind == DeclKind::FunctionInterface) {
            FunctionInterfaceInfo info;
            info.decl = &decl;
            info.container = container;
            info.sourceName = decl.name;
            info.finalName = renameInContainer(decl.name, container);
            info.signature = signatureFromParams(decl.interfaceParams, decl.interfaceReturnType, nullptr);
            size_t index = functionInterfaces_.size();
            functionInterfaces_.push_back(std::move(info));
            functionInterfaceIndexByName_[functionInterfaces_[index].sourceName] = index;
            functionInterfaceIndexByName_[functionInterfaces_[index].finalName] = index;
            continue;
        }
        const Decl* nextContainer = (decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) ? &decl : container;
        collectFunctionInterfaces(decl.children, nextContainer);
    }
}

void Phase1Codegen::collectFunctions(const std::vector<Decl>& decls, const Decl* container) {
    for (const auto& decl : decls) {
        if (decl.kind == DeclKind::Function) {
            collectFunction(decl, container);
            continue;
        }
        if (decl.kind == DeclKind::Struct) {
            const StructInfo* info = findStruct(makeScopedStructName(decl, container));
            if (!info) {
                info = findStruct(decl.name);
            }
            if (info) {
                for (const auto& method : info->methods) {
                    if (!method.isStatic) {
                        continue;
                    }
                    FunctionInfo fn;
                    fn.sourceName = info->originalName + "." + method.name;
                    fn.finalName = method.generatedName;
                    fn.signature = signatureFromParams(method.decl->params, method.decl->returnType, info);
                    fn.isStaticMethod = true;
                    size_t index = functions_.size();
                    functions_.push_back(std::move(fn));
                    functionIndexByName_[functions_[index].sourceName] = index;
                    functionIndexByName_[functions_[index].finalName] = index;
                }
            }
            continue;
        }
        if (decl.kind == DeclKind::Module || decl.kind == DeclKind::FunctionInterface) {
            continue;
        }
        const Decl* nextContainer = (decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) ? &decl : container;
        collectFunctions(decl.children, nextContainer);
    }
}

void Phase1Codegen::collectFunction(const Decl& decl, const Decl* container) {
    if (decl.lines.empty()) {
        return;
    }
    FunctionInfo fn;
    fn.sourceName = decl.name;
    fn.finalName = renameInContainer(decl.name, container);
    fn.signature = parseFunctionSignatureFromHeader(stripAccessPrefixFromLine(decl.lines.front()), decl.mode);
    size_t index = functions_.size();
    functions_.push_back(std::move(fn));
    functionIndexByName_[functions_[index].sourceName] = index;
    functionIndexByName_[functions_[index].finalName] = index;
}

std::string Phase1Codegen::rewriteForContainer(const std::string& line, const Decl* container) const {
    return symbols_.rewriteLine(line, container);
}

std::string Phase1Codegen::rewriteGlobalLine(const std::string& line, const Decl* container) {
    std::string out = rewriteForContainer(line, container);
    out = stripAccessPrefixFromLine(out);
    out = removeSemicolonsOutsideProtected(out);
    ArrayDeclRewrite multi = rewriteMultiDimDeclarationLine(out);
    if (multi.matched) {
        globalArrayShapes_[multi.name] = ArrayShape{multi.dimensions, false};
        out = multi.line;
    }
    out = replaceRegex(out,
                       R"(^(\s*(?:constant\s+)?[A-Za-z_][A-Za-z0-9_$]*)\s+([A-Za-z_][A-Za-z0-9_$]*)\s*\[\])",
                       "$1 array $2");
    out = replaceRegex(out,
                       R"(^(\s*(?:constant\s+)?[A-Za-z_][A-Za-z0-9_$]*)\s+([A-Za-z_][A-Za-z0-9_$]*)\s+\[\])",
                       "$1 array $2");
    std::string t = trim(out);
    bool constant = false;
    if (startsWithWord(t, "constant")) {
        constant = true;
        t = trim(std::string_view(t).substr(8));
    }
    std::istringstream in(t);
    std::string type;
    in >> type;
    if (!type.empty() && (findStruct(type) || findFunctionInterface(type))) {
        size_t typePos = out.find(type);
        if (typePos != std::string::npos) {
            out.replace(typePos, type.size(), "integer");
        }
    }
    (void)constant;
    return trim(rewriteArrayAccesses(out, nullptr));
}

std::string Phase1Codegen::rewriteFunctionHeader(const Decl& decl, const Decl* container) const {
    std::string header = rewriteForContainer(decl.lines.front(), container);
    header = stripAccessPrefixFromLine(header);
    size_t takes = header.find(" takes ");
    size_t returns = header.find(" returns ");
    if (takes != std::string::npos && returns != std::string::npos && returns > takes) {
        std::string prefix = header.substr(0, takes + 7);
        std::string params = header.substr(takes + 7, returns - (takes + 7));
        std::string ret = trim(std::string_view(header).substr(returns + 9));
        if (trim(params) != "nothing") {
            std::vector<std::string> rewritten;
            for (auto part : splitCommaList(params)) {
                std::istringstream in(trim(part));
                std::string type;
                std::string name;
                in >> type >> name;
                if (!type.empty() && !name.empty()) {
                    rewritten.push_back(rewriteTypeName(type, nullptr) + " " + name);
                }
            }
            params = joinParams(rewritten);
        }
        header = prefix + params + " returns " + rewriteTypeName(ret, nullptr);
    }
    return trim(header);
}

std::string Phase1Codegen::renameInContainer(const std::string& name, const Decl* container) const {
    if (!container) {
        return name;
    }
    auto symbols = symbols_.symbolsFor(container);
    if (!symbols) {
        return name;
    }
    auto it = symbols->replacements.find(name);
    return it == symbols->replacements.end() ? name : it->second;
}

std::string Phase1Codegen::makeScopedStructName(const Decl& decl, const Decl* container) const {
    if (!container || decl.access.empty()) {
        return decl.name;
    }
    if (decl.access == "private") {
        return container->name + "___" + decl.name;
    }
    return container->name + "_" + decl.name;
}

std::string Phase1Codegen::rewriteArrayAccesses(
    const std::string& line,
    const std::unordered_map<std::string, ArrayShape>* localArrayShapes) const {
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
        if (!isIdentStart(c)) {
            out.push_back(c);
            ++i;
            continue;
        }

        size_t nameStart = i++;
        while (i < line.size() && isIdentPart(line[i])) {
            ++i;
        }
        std::string name = line.substr(nameStart, i - nameStart);
        const ArrayShape* shape = nullptr;
        auto globalIt = globalArrayShapes_.find(name);
        if (globalIt != globalArrayShapes_.end()) {
            shape = &globalIt->second;
        }
        if (localArrayShapes) {
            auto localIt = localArrayShapes->find(name);
            if (localIt != localArrayShapes->end()) {
                shape = &localIt->second;
            }
        }
        if (!shape || shape->dimensions.empty()) {
            out += name;
            continue;
        }

        size_t afterName = i;
        std::vector<std::string> indexes;
        size_t cursor = afterName;
        while (cursor < line.size()) {
            size_t ws = cursor;
            while (ws < line.size() && std::isspace(static_cast<unsigned char>(line[ws]))) {
                ++ws;
            }
            if (ws >= line.size() || line[ws] != '[') {
                break;
            }
            size_t close = findMatchingBracketOutsideProtected(line, ws);
            if (close == std::string::npos) {
                break;
            }
            indexes.push_back(line.substr(ws + 1, close - ws - 1));
            cursor = close + 1;
        }
        size_t needed = shape->dimensions.size() + (shape->structInstanceField ? 1 : 0);
        if (indexes.size() < needed || needed == 0) {
            out += name;
            continue;
        }

        std::string flat;
        if (shape->structInstanceField) {
            int stride = arrayFlatSize(shape->dimensions);
            if (stride <= 0) {
                out += name;
                continue;
            }
            flat = "(" + trim(indexes[0]) + ") * " + std::to_string(stride);
            std::string rest = composeFlatIndex(indexes, shape->dimensions, 1);
            if (!rest.empty()) {
                flat = "(" + flat + " + " + rest + ")";
            }
        } else {
            flat = composeFlatIndex(indexes, shape->dimensions, 0);
        }
        out += name + "[" + flat + "]";
        for (size_t extra = needed; extra < indexes.size(); ++extra) {
            out += "[" + indexes[extra] + "]";
        }
        i = cursor;
    }
    return out;
}

const Phase1Codegen::StructInfo* Phase1Codegen::findStruct(std::string_view name) const {
    auto it = structIndexByName_.find(std::string(name));
    return it == structIndexByName_.end() ? nullptr : &structs_[it->second];
}

const Phase1Codegen::FieldInfo* Phase1Codegen::findField(const StructInfo& info, std::string_view name) const {
    auto it = info.fieldIndex.find(std::string(name));
    return it == info.fieldIndex.end() ? nullptr : &info.fields[it->second];
}

const Phase1Codegen::MethodInfo* Phase1Codegen::findMethod(const StructInfo& info, std::string_view name) const {
    auto it = info.methodIndex.find(std::string(name));
    return it == info.methodIndex.end() ? nullptr : &info.methods[it->second];
}

std::string Phase1Codegen::rewriteTypeName(const std::string& typeName, const StructInfo* currentStruct) const {
    if (typeName == "thistype") {
        return "integer";
    }
    if (currentStruct && typeName == currentStruct->originalName) {
        return "integer";
    }
    if (findStruct(typeName)) {
        return "integer";
    }
    if (findFunctionInterface(typeName)) {
        return "integer";
    }
    return typeName;
}

std::string Phase1Codegen::rewriteParamList(const std::vector<ParamDecl>& params, bool includeThis, const StructInfo* currentStruct) const {
    std::vector<std::string> out;
    if (includeThis) {
        out.push_back("integer this");
    }
    for (const auto& param : params) {
        out.push_back(rewriteTypeName(param.type.name, currentStruct) + " " + param.name);
    }
    return joinParams(out);
}

void Phase1Codegen::seedMethodLocalTypes(const StructInfo& info, const MethodInfo& method, std::unordered_map<std::string, std::string>& localTypes) const {
    if (!method.isStatic) {
        localTypes["this"] = info.originalName;
    }
    for (const auto& param : method.decl->params) {
        if (param.type.name == "thistype") {
            localTypes[param.name] = info.originalName;
        } else if (findStruct(param.type.name)) {
            localTypes[param.name] = param.type.name;
        } else if (findFunctionInterface(param.type.name)) {
            localTypes[param.name] = param.type.name;
        } else {
            localTypes[param.name] = param.type.name;
        }
    }
}

void Phase1Codegen::seedFunctionLocalTypes(const std::string& header, std::unordered_map<std::string, std::string>& localTypes) const {
    size_t takes = header.find(" takes ");
    size_t returns = header.find(" returns ");
    if (takes == std::string::npos || returns == std::string::npos || returns <= takes) {
        return;
    }
    std::string params = header.substr(takes + 7, returns - (takes + 7));
    params = trim(params);
    if (params == "nothing" || params.empty()) {
        return;
    }
    for (auto part : splitCommaList(params)) {
        std::istringstream in(trim(part));
        std::string type;
        std::string name;
        in >> type >> name;
        if (!type.empty() && !name.empty()) {
            if (findStruct(type) || findFunctionInterface(type)) {
                localTypes[name] = type;
            } else {
                localTypes[name] = type;
            }
        }
    }
}

std::string Phase1Codegen::rewriteLocalDeclLine(const std::string& line,
                                                const StructInfo* currentStruct,
                                                std::unordered_map<std::string, std::string>& localTypes,
                                                std::unordered_map<std::string, ArrayShape>* localArrayShapes,
                                                std::vector<std::string>& extraLines) const {
    std::string t = trim(line);
    bool hasLocal = startsWithWord(t, "local");
    if (hasLocal) {
        t = trim(std::string_view(t).substr(5));
    }
    bool constant = false;
    if (startsWithWord(t, "constant")) {
        constant = true;
        t = trim(std::string_view(t).substr(8));
    }
    size_t assign = t.find('=');
    std::string declPart = assign == std::string::npos ? t : trim(std::string_view(t).substr(0, assign));
    std::string init = assign == std::string::npos ? "" : trim(std::string_view(t).substr(assign + 1));
    std::istringstream in(declPart);
    std::string type;
    std::string name;
    in >> type >> name;
    bool array = false;
    if (type.empty() || name.empty()) {
        return line;
    }
    if (name == "array") {
        in >> name;
        array = true;
    }
    size_t bracket = name.find('[');
    std::vector<int> dims;
    if (bracket != std::string::npos) {
        dims = parseArrayDimensions(std::string_view(name).substr(bracket));
        name = name.substr(0, bracket);
        array = true;
    } else {
        std::string suffix;
        std::getline(in, suffix);
        dims = parseArrayDimensions(trim(suffix));
        if (!dims.empty()) {
            array = true;
        }
    }
    if (dims.size() > 1 && localArrayShapes) {
        (*localArrayShapes)[name] = ArrayShape{dims, false};
    }
    if (type == "thistype") {
        localTypes[name] = currentStruct ? currentStruct->originalName : type;
    } else if (findStruct(type)) {
        localTypes[name] = type;
    } else if (findFunctionInterface(type)) {
        localTypes[name] = type;
    } else {
        localTypes[name] = type;
    }
    std::string rewrittenType = rewriteTypeName(type, currentStruct);
    std::string out = "local " + std::string(constant ? "constant " : "") + rewrittenType + (array ? " array " : " ") + name;
    if (!init.empty()) {
        extraLines.push_back("set " + name + "=" + rewriteArrayAccesses(rewriteStructExpression(init, currentStruct, localTypes), localArrayShapes));
    }
    return out;
}

std::string Phase1Codegen::rewriteStructExpression(const std::string& line,
                                                   const StructInfo* currentStruct,
                                                   const std::unordered_map<std::string, std::string>& localTypes) const {
    auto rewriteNormal = [&](std::string text) {
        if (currentStruct) {
            if (text.find("thistype") != std::string::npos) {
                text = replaceRegex(text, R"(\bthistype\s*\.\s*typeid\b)", std::to_string(currentStruct->typeId));
                text = replaceRegex(text, R"(\bthistype\s*\.\s*allocate\s*\()", currentStruct->prefix + "__allocate(");
                text = replaceRegex(text, R"(\bthistype\s*\.\s*create\s*\()", currentStruct->prefix + "_create(");
            }
        }
        for (const auto& info : structs_) {
            if (text.find("si__" + info.originalName + "_") != std::string::npos) {
                text = replaceRegex(text, "\\bsi__" + regexEscape(info.originalName) + "_", "si__" + info.generatedName + "_");
            }
            if (text.find("sc__" + info.originalName + "_") != std::string::npos) {
                text = replaceRegex(text, "\\bsc__" + regexEscape(info.originalName) + "_", "sc__" + info.generatedName + "_");
            }
            if (text.find("s__" + info.originalName + "_") != std::string::npos) {
                text = replaceRegex(text, "\\bs__" + regexEscape(info.originalName) + "_", "s__" + info.generatedName + "_");
            }
            if (text.find(info.originalName) == std::string::npos) {
                continue;
            }
            std::string structName = regexEscape(info.originalName);
            text = replaceRegex(text,
                                "\\b" + structName + R"(\s*\.\s*typeid\b)",
                                std::to_string(info.typeId));
            for (const auto& method : info.methods) {
                if (method.isStatic || method.name == "create") {
                    text = replaceRegex(text,
                                        "\\bfunction\\s+" + structName + "\\s*\\.\\s*" + regexEscape(method.name) + "\\b",
                                        "function " + method.generatedName);
                    text = replaceRegex(text,
                                        "\\b" + structName + "\\s*\\.\\s*" + regexEscape(method.name) + "\\s*\\(",
                                        method.generatedName + "(");
                }
            }
            if (!info.isArrayStruct) {
                text = replaceRegex(text, "\\b" + structName + "\\s*\\.\\s*create\\s*\\(", info.prefix + "_create(");
                text = replaceRegex(text, "\\b" + structName + "\\s*\\.\\s*allocate\\s*\\(", info.prefix + "__allocate(");
            }
            for (const auto& field : info.fields) {
                if (field.isStatic) {
                    text = replaceRegex(text,
                                        "\\b" + structName + "\\s*\\.\\s*" + regexEscape(field.name) + "\\b",
                                        field.generatedName);
                    text = rewriteArrayAccesses(text, nullptr);
                }
            }
        }

        if (currentStruct) {
            for (const auto& field : currentStruct->fields) {
                if (text.find(field.name) == std::string::npos) {
                    if (text.find(field.generatedName) == std::string::npos) {
                        continue;
                    }
                }
                if (field.isStatic) {
                    text = replaceRegex(text,
                                        R"((^|[^A-Za-z0-9_$.]))" + regexEscape(field.name) + R"(\b)",
                                        "$1" + field.generatedName);
                    text = replaceRegex(text,
                                        R"(\bthistype\s*\.\s*)" + regexEscape(field.name) + R"(\b)",
                                        field.generatedName);
                    text = replaceRegex(text,
                                        R"(\bthistype\s*\.\s*)" + regexEscape(field.generatedName) + R"(\b)",
                                        field.generatedName);
                    text = rewriteArrayAccesses(text, nullptr);
                    if (const StructInfo* fieldStruct = findStruct(field.typeName)) {
                        std::string receiverPattern = "\\b" + regexEscape(field.generatedName) + R"((\s*\[[^\]]+\])?)";
                        std::string receiverExpr = field.generatedName + "$1";
                        for (const auto& nestedField : fieldStruct->fields) {
                            if (!nestedField.isStatic) {
                                text = replaceRegex(text,
                                                    receiverPattern + R"(\s*\.\s*)" + regexEscape(nestedField.name) + R"(\b)",
                                                    nestedField.generatedName + "[" + receiverExpr + "]");
                            }
                        }
                        for (const auto& nestedMethod : fieldStruct->methods) {
                            if (!nestedMethod.isStatic) {
                                text = replaceRegex(text,
                                                    receiverPattern + R"(\s*\.\s*)" + regexEscape(nestedMethod.name) + R"(\s*\()",
                                                    nestedMethod.generatedName + "(" + receiverExpr +
                                                        (nestedMethod.decl->params.empty() ? "" : ", "));
                            }
                        }
                    }
                    continue;
                }
                if (field.isFixedArray && field.fixedArraySize > 0 && field.arrayDimensions.size() <= 1) {
                    std::string indexed = field.generatedName + "[this*" + std::to_string(field.fixedArraySize) + "+$1]";
                    text = replaceRegex(text,
                                        R"(\bthistype\s*\[\s*([^\]]+)\s*\]\s*\.\s*)" + regexEscape(field.name) + R"(\s*\[\s*([^\]]+)\s*\])",
                                        field.generatedName + "[$1*" + std::to_string(field.fixedArraySize) + "+$2]");
                    text = replaceRegex(text,
                                        R"(\bthis\s*\.\s*)" + regexEscape(field.name) + R"(\s*\[\s*([^\]]+)\s*\])",
                                        indexed);
                    text = replaceRegex(text,
                                        R"((^|[^A-Za-z0-9_$)\]])\.\s*)" + regexEscape(field.name) + R"(\s*\[\s*([^\]]+)\s*\])",
                                        "$1" + indexed);
                }
                std::string access = field.generatedName + "[this]";
                if (!localTypes.contains(field.name)) {
                    text = replaceRegex(text,
                                        R"((^|[^A-Za-z0-9_$.]))" + regexEscape(field.name) + R"(\b(?!\s*\())",
                                        "$1" + access);
                }
                text = replaceRegex(text,
                                    R"(\bthistype\s*\[\s*([^\]]+)\s*\]\s*\.\s*)" + regexEscape(field.name) + R"(\b)",
                                    field.generatedName + "[$1]");
                text = replaceRegex(text, R"(\bthis\s*\.\s*)" + regexEscape(field.name) + R"(\b)", access);
                text = replaceRegex(text, R"((^|[^A-Za-z0-9_$)\]])\.\s*)" + regexEscape(field.name) + R"(\b)", "$1" + access);
            }
            for (const auto& method : currentStruct->methods) {
                if (method.isStatic) {
                    text = replaceRegex(text,
                                        R"(\bfunction\s+thistype\s*\.\s*)" + regexEscape(method.name) + R"(\b)",
                                        "function " + method.generatedName);
                    text = replaceRegex(text,
                                        R"(\bfunction\s+thistype\s*\.\s*)" + regexEscape(method.generatedName) + R"(\b)",
                                        "function " + method.generatedName);
                    text = replaceRegex(text,
                                        R"(\bthistype\s*\.\s*)" + regexEscape(method.name) + R"(\s*\()",
                                        method.generatedName + "(");
                    text = replaceRegex(text,
                                        R"(\bthistype\s*\.\s*)" + regexEscape(method.generatedName) + R"(\s*\()",
                                        method.generatedName + "(");
                    continue;
                }
                if (text.find(method.name) == std::string::npos) {
                    continue;
                }
                text = replaceRegex(text,
                                    R"(\bthistype\s*\[\s*([^\]]+)\s*\]\s*\.\s*)" + regexEscape(method.name) + R"(\s*\()",
                                    method.generatedName + "($1" + (method.decl->params.empty() ? "" : ", "));
                text = replaceRegex(text,
                                    R"((^|[^A-Za-z0-9_$)\]])\.\s*)" + regexEscape(method.name) + R"(\s*\()",
                                    "$1" + method.generatedName + "(this" + (method.decl->params.empty() ? "" : ", "));
                if (!localTypes.contains(method.name)) {
                    text = replaceRegex(text,
                                        R"((^|[^A-Za-z0-9_$.]))" + regexEscape(method.name) + R"(\s*\()",
                                        "$1" + method.generatedName + "(this" + (method.decl->params.empty() ? "" : ", "));
                }
            }
            if (!currentStruct->isArrayStruct && text.find("allocate") != std::string::npos) {
                text = replaceRegex(text, R"(\ballocate\s*\()", currentStruct->prefix + "__allocate(");
            }
        }

        for (const auto& [varName, typeName] : localTypes) {
            if (text.find(varName) == std::string::npos) {
                continue;
            }
            const StructInfo* varStruct = typeName == "thistype" ? currentStruct : findStruct(typeName);
            if (!varStruct) {
                continue;
            }
            std::string var = regexEscape(varName);
            std::string receiverPattern = "\\b" + var + R"((\s*\[[^\]]+\])?)";
            std::string receiverExpr = varName + "$1";
            for (const auto& field : varStruct->fields) {
                if (field.isStatic) {
                    continue;
                }
                if (field.isFixedArray && field.fixedArraySize > 0 && field.arrayDimensions.size() <= 1) {
                    text = replaceRegex(text,
                                        "\\b" + var + R"(\s*\.\s*)" + regexEscape(field.name) + R"(\s*\[\s*([^\]]+)\s*\])",
                                        field.generatedName + "[" + varName + "*" + std::to_string(field.fixedArraySize) + "+$1]");
                }
                text = replaceRegex(text,
                                    "\\b" + var + R"(\s*\.\s*)" + regexEscape(field.name) + R"(\b)",
                                    field.generatedName + "[" + varName + "]");
                text = replaceRegex(text,
                                    receiverPattern + R"(\s*\.\s*)" + regexEscape(field.name) + R"(\b)",
                                    field.generatedName + "[" + receiverExpr + "]");
            }
            for (const auto& method : varStruct->methods) {
                if (method.isStatic) {
                    continue;
                }
                text = replaceRegex(text,
                                    "\\b" + var + R"(\s*\.\s*)" + regexEscape(method.name) + R"(\s*\()",
                                    method.generatedName + "(" + varName + (method.decl->params.empty() ? "" : ", "));
                text = replaceRegex(text,
                                    receiverPattern + R"(\s*\.\s*)" + regexEscape(method.name) + R"(\s*\()",
                                    method.generatedName + "(" + receiverExpr + (method.decl->params.empty() ? "" : ", "));
            }
            if (!varStruct->isArrayStruct) {
                text = replaceRegex(text,
                                    "\\b" + var + R"(\s*\.\s*destroy\s*\(\s*\))",
                                    varStruct->prefix + "_destroy(" + varName + ")");
                text = replaceRegex(text,
                                    receiverPattern + R"(\s*\.\s*destroy\s*\(\s*\))",
                                    varStruct->prefix + "_destroy(" + receiverExpr + ")");
            }
        }
        return text;
    };
    return rewriteOutsideProtected(line, rewriteNormal);
}

const Phase1Codegen::FunctionInterfaceInfo* Phase1Codegen::findFunctionInterface(std::string_view name) const {
    auto it = functionInterfaceIndexByName_.find(std::string(name));
    return it == functionInterfaceIndexByName_.end() ? nullptr : &functionInterfaces_[it->second];
}

const Phase1Codegen::FunctionInfo* Phase1Codegen::findFunctionInfo(std::string_view name) const {
    auto it = functionIndexByName_.find(std::string(name));
    return it == functionIndexByName_.end() ? nullptr : &functions_[it->second];
}

std::string Phase1Codegen::interfaceGlobalPrefix(const FunctionInterfaceInfo& iface) const {
    return "vjfi__" + sanitizeName(iface.finalName);
}

std::string Phase1Codegen::resolveFunctionTargetName(const std::string& expression, const StructInfo* currentStruct) const {
    std::string expr = trim(expression);
    if (startsWithWord(expr, "function")) {
        expr = trim(std::string_view(expr).substr(8));
    }
    if (const FunctionInfo* fn = findFunctionInfo(expr)) {
        return fn->finalName;
    }
    size_t dot = expr.find('.');
    if (dot != std::string::npos) {
        std::string left = trim(std::string_view(expr).substr(0, dot));
        std::string right = trim(std::string_view(expr).substr(dot + 1));
        if (currentStruct && (left == "thistype" || left == currentStruct->originalName || left == currentStruct->generatedName)) {
            if (const MethodInfo* method = findMethod(*currentStruct, right); method && method->isStatic) {
                return method->generatedName;
            }
        }
        if (const StructInfo* info = findStruct(left)) {
            if (const MethodInfo* method = findMethod(*info, right); method && method->isStatic) {
                return method->generatedName;
            }
        }
    }
    return expr;
}

int Phase1Codegen::registerInterfaceTarget(const FunctionInterfaceInfo& ifaceRef, const std::string& targetName, SourceLocation loc) const {
    auto ifaceIt = functionInterfaceIndexByName_.find(ifaceRef.finalName);
    if (ifaceIt == functionInterfaceIndexByName_.end()) {
        ifaceIt = functionInterfaceIndexByName_.find(ifaceRef.sourceName);
    }
    if (ifaceIt == functionInterfaceIndexByName_.end()) {
        diagnostics_.error(loc, "unknown function interface '" + ifaceRef.sourceName + "'");
        return 0;
    }
    auto& iface = functionInterfaces_[ifaceIt->second];
    std::string finalTarget = resolveFunctionTargetName(targetName, nullptr);
    if (const FunctionInfo* fn = findFunctionInfo(finalTarget)) {
        finalTarget = fn->finalName;
        if (!sameSignature(fn->signature, iface.signature)) {
            diagnostics_.error(loc, "function target signature mismatch for interface '" + iface.sourceName + "'");
        }
    } else {
        diagnostics_.error(loc, "unknown function target '" + targetName + "'");
    }
    auto existing = iface.targetIndexByFinalName.find(finalTarget);
    if (existing != iface.targetIndexByFinalName.end()) {
        return iface.targets[existing->second].id;
    }
    int id = static_cast<int>(iface.targets.size() + 1);
    size_t index = iface.targets.size();
    iface.targets.push_back(InterfaceTarget{finalTarget, id});
    iface.targetIndexByFinalName[finalTarget] = index;
    return id;
}

const Phase1Codegen::FunctionInterfaceInfo* Phase1Codegen::resolveReceiverInterface(const std::string& receiver,
                                                                                    const LoweringContext& ctx) const {
    std::string expr = trim(receiver);
    if (ctx.localTypes) {
        auto it = ctx.localTypes->find(expr);
        if (it != ctx.localTypes->end()) {
            return findFunctionInterface(it->second);
        }
    }
    size_t dot = expr.find('.');
    if (dot != std::string::npos) {
        std::string left = trim(std::string_view(expr).substr(0, dot));
        std::string right = trim(std::string_view(expr).substr(dot + 1));
        if (ctx.currentStruct && (left == "this" || left.empty())) {
            if (const FieldInfo* field = findField(*ctx.currentStruct, right)) {
                return findFunctionInterface(field->typeName);
            }
        }
        if (ctx.localTypes) {
            auto varIt = ctx.localTypes->find(left);
            if (varIt != ctx.localTypes->end()) {
                const StructInfo* info = findStruct(varIt->second);
                if (info) {
                    if (const FieldInfo* field = findField(*info, right)) {
                        return findFunctionInterface(field->typeName);
                    }
                }
            }
        }
    } else if (ctx.currentStruct) {
        if (const FieldInfo* field = findField(*ctx.currentStruct, expr)) {
            return findFunctionInterface(field->typeName);
        }
        for (const auto& field : ctx.currentStruct->fields) {
            if (expr == field.generatedName || expr.rfind(field.generatedName + "[", 0) == 0) {
                return findFunctionInterface(field.typeName);
            }
        }
    }
    return nullptr;
}

std::string Phase1Codegen::rewriteReceiverExpression(const std::string& receiver, const LoweringContext& ctx) const {
    return rewriteStructExpression(receiver, ctx.currentStruct, ctx.localTypes ? *ctx.localTypes : std::unordered_map<std::string, std::string>{});
}

std::string Phase1Codegen::lowerFunctionValue(std::string expression,
                                              const std::string& expectedInterfaceType,
                                              LoweringContext& ctx,
                                              std::vector<std::string>& prelude) const {
    expression = trim(expression);
    if (expression.empty()) {
        return expression;
    }
    bool functionKeyword = false;
    if (startsWithWord(expression, "function")) {
        functionKeyword = true;
        expression = trim(std::string_view(expression).substr(8));
    }
    if (const FunctionInterfaceInfo* expected = findFunctionInterface(expectedInterfaceType)) {
        if (!expression.empty() && (std::isdigit(static_cast<unsigned char>(expression[0])) || expression[0] == '-')) {
            return expression;
        }
        std::string targetExpr = expression;
        size_t dot = expression.find('.');
        if (dot != std::string::npos) {
            std::string maybeIface = trim(std::string_view(expression).substr(0, dot));
            if (findFunctionInterface(maybeIface)) {
                targetExpr = trim(std::string_view(expression).substr(dot + 1));
            }
        }
        std::string finalTarget = resolveFunctionTargetName(targetExpr, ctx.currentStruct);
        if (findFunctionInfo(finalTarget)) {
            if (finalTarget.rfind("vjlambda__", 0) == 0) {
                ++lambdasFunctionInterfaceContext_;
            }
            int id = registerInterfaceTarget(*expected, finalTarget, SourceLocation{});
            return std::to_string(id);
        }
        if (functionKeyword) {
            diagnostics_.error(SourceLocation{}, "unknown function target '" + targetExpr + "'");
            return "0";
        }
        return rewriteCallArguments(rewriteFunctionNames(rewriteStructExpression(expression,
                                                                                ctx.currentStruct,
                                                                                ctx.localTypes ? *ctx.localTypes : std::unordered_map<std::string, std::string>{}),
                                                        ctx.currentStruct),
                                    ctx,
                                    prelude);
    }
    size_t dot = expression.find('.');
    if (dot != std::string::npos) {
        std::string maybeIface = trim(std::string_view(expression).substr(0, dot));
        if (const FunctionInterfaceInfo* iface = findFunctionInterface(maybeIface)) {
            std::string targetExpr = trim(std::string_view(expression).substr(dot + 1));
            int id = registerInterfaceTarget(*iface, resolveFunctionTargetName(targetExpr, ctx.currentStruct), SourceLocation{});
            return std::to_string(id);
        }
    }
    if (functionKeyword) {
        return "function " + resolveFunctionTargetName(expression, ctx.currentStruct);
    }
    (void)prelude;
    return rewriteCallArguments(rewriteFunctionNames(rewriteStructExpression(expression,
                                                                            ctx.currentStruct,
                                                                            ctx.localTypes ? *ctx.localTypes : std::unordered_map<std::string, std::string>{}),
                                                    ctx.currentStruct),
                                ctx,
                                prelude);
}

std::string Phase1Codegen::rewriteFunctionNames(std::string expression, const StructInfo* currentStruct) const {
    auto rewriteNormal = [&](std::string text) {
        size_t dot = 0;
        while ((dot = text.find(".name", dot)) != std::string::npos) {
            size_t left = dot;
            while (left > 0 && (std::isalnum(static_cast<unsigned char>(text[left - 1])) ||
                                text[left - 1] == '_' || text[left - 1] == '$' || text[left - 1] == '.')) {
                --left;
            }
            std::string receiver = text.substr(left, dot - left);
            std::string finalName = resolveFunctionTargetName(receiver, currentStruct);
            if (findFunctionInfo(finalName)) {
                text.replace(left, dot + 5 - left, "\"" + finalName + "\"");
                dot = left + finalName.size() + 2;
            } else {
                dot += 5;
            }
        }
        return text;
    };
    return rewriteOutsideProtected(expression, rewriteNormal);
}

std::string Phase1Codegen::rewriteCallArguments(std::string expression, LoweringContext& ctx, std::vector<std::string>& prelude) const {
    auto rewriteNormal = [&](std::string text) {
        for (size_t pos = 0; pos < text.size();) {
            if (!isIdentStart(text[pos])) {
                ++pos;
                continue;
            }
            size_t nameStart = pos++;
            while (pos < text.size() && isIdentPart(text[pos])) {
                ++pos;
            }
            std::string name = text.substr(nameStart, pos - nameStart);
            size_t p = pos;
            while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) {
                ++p;
            }
            if (p >= text.size() || text[p] != '(') {
                continue;
            }
            const FunctionInfo* fn = findFunctionInfo(name);
            if (!fn || fn->signature.paramTypes.empty()) {
                pos = p + 1;
                continue;
            }
            size_t close = findMatchingParen(text, p);
            if (close == std::string::npos) {
                break;
            }
            std::vector<std::string> args = splitCommaList(text.substr(p + 1, close - p - 1));
            if (args.size() != fn->signature.paramTypes.size()) {
                pos = close + 1;
                continue;
            }
            bool changed = false;
            for (size_t i = 0; i < args.size(); ++i) {
                if (findFunctionInterface(fn->signature.paramTypes[i])) {
                    args[i] = lowerFunctionValue(args[i], fn->signature.paramTypes[i], ctx, prelude);
                    changed = true;
                }
            }
            if (changed) {
                std::string replacement = name + "(" + joinArgs(args) + ")";
                text.replace(nameStart, close + 1 - nameStart, replacement);
                pos = nameStart + replacement.size();
            } else {
                pos = close + 1;
            }
        }
        return text;
    };
    return rewriteOutsideProtected(expression, rewriteNormal);
}

std::string Phase1Codegen::lowerExpression(std::string expression,
                                           const std::string& expectedInterfaceType,
                                           LoweringContext& ctx,
                                           std::vector<std::string>& prelude) const {
    expression = lowerFunctionValue(expression, expectedInterfaceType, ctx, prelude);
    while (true) {
        size_t receiverStart = 0;
        size_t dotPos = 0;
        size_t open = 0;
        size_t close = 0;
        if (!findMethodCallSpan(expression, "evaluate", receiverStart, dotPos, open, close)) {
            break;
        }
        std::string receiver = trim(expression.substr(receiverStart, dotPos - receiverStart));
        std::string argsText = expression.substr(open + 1, close - open - 1);
        if (const FunctionInterfaceInfo* iface = resolveReceiverInterface(receiver, ctx)) {
            if (iface->signature.returnType == "nothing") {
                diagnostics_.error(SourceLocation{}, "evaluate used on non-returning function interface '" + iface->sourceName + "'");
                break;
            }
            std::vector<std::string> args = splitCommaList(argsText);
            if (args.size() != iface->signature.paramTypes.size()) {
                diagnostics_.error(SourceLocation{}, "wrong argument count for evaluate on function interface '" + iface->sourceName + "'");
            }
            std::string prefix = interfaceGlobalPrefix(*iface);
            for (size_t i = 0; i < args.size() && i < iface->signature.paramTypes.size(); ++i) {
                std::vector<std::string> nested;
                std::string arg = lowerExpression(args[i], {}, ctx, nested);
                prelude.insert(prelude.end(), nested.begin(), nested.end());
                prelude.push_back("set " + prefix + "_arg" + std::to_string(i) + "=" + arg);
            }
            ++functionInterfaceCalls_;
            prelude.push_back("call TriggerExecute(" + prefix + "_trigger[" + rewriteReceiverExpression(receiver, ctx) + "])");
            std::string replacement = prefix + "_result";
            if (receiverStart == 0 && close + 1 == expression.size()) {
                expression = replacement;
            } else {
                size_t depth = static_cast<size_t>(++ctx.tempCounter);
                functionInterfaceMaxEvaluateDepth_ = std::max(functionInterfaceMaxEvaluateDepth_, depth);
                if (depth > functionInterfaceEvaluateTempLimit_) {
                    diagnostics_.error(SourceLocation{}, "nested function interface evaluate exceeds temp limit " +
                                                          std::to_string(functionInterfaceEvaluateTempLimit_));
                    depth = functionInterfaceEvaluateTempLimit_;
                }
                std::string temp = prefix + "_tmp" + std::to_string(depth);
                prelude.push_back("set " + temp + "=" + replacement);
                expression.replace(receiverStart, close + 1 - receiverStart, temp);
            }
            continue;
        }
        std::string finalName = resolveFunctionTargetName(receiver, ctx.currentStruct);
        if (const FunctionInfo* fn = findFunctionInfo(finalName)) {
            ++functionObjectCalls_;
            std::string replacement = fn->finalName + "(" + argsText + ")";
            expression.replace(receiverStart, close + 1 - receiverStart, replacement);
            continue;
        }
        break;
    }
    expression = rewriteFunctionNames(expression, ctx.currentStruct);
    expression = rewriteStructExpression(expression, ctx.currentStruct, ctx.localTypes ? *ctx.localTypes : std::unordered_map<std::string, std::string>{});
    expression = rewriteArrayAccesses(expression, ctx.localArrayShapes);
    expression = rewriteCallArguments(expression, ctx, prelude);
    return trim(expression);
}

std::vector<std::string> Phase1Codegen::lowerStatementLine(const std::string& rawLine, LoweringContext& ctx) const {
    std::string line = trim(rawLine);
    std::string originalTrim = line;
    std::string leading;
    size_t firstNonSpace = rawLine.find_first_not_of(" \t");
    if (firstNonSpace != std::string::npos) {
        leading = rawLine.substr(0, firstNonSpace);
    }
    std::vector<std::string> out;
    if (line.empty()) {
        return out;
    }
    if (startsWithWord(line, "call")) {
        std::string callExpr = trim(std::string_view(line).substr(4));
        size_t receiverStart = 0;
        size_t dotPos = 0;
        size_t open = 0;
        size_t close = 0;
        if (findMethodCallSpan(callExpr, "execute", receiverStart, dotPos, open, close) && receiverStart == 0 && close + 1 == callExpr.size()) {
            std::string receiver = trim(callExpr.substr(receiverStart, dotPos - receiverStart));
            std::string argsText = callExpr.substr(open + 1, close - open - 1);
            if (const FunctionInterfaceInfo* iface = resolveReceiverInterface(receiver, ctx)) {
                if (iface->signature.returnType != "nothing") {
                    diagnostics_.error(SourceLocation{}, "execute used on returning function interface '" + iface->sourceName + "'");
                }
                std::vector<std::string> args = splitCommaList(argsText);
                if (args.size() != iface->signature.paramTypes.size()) {
                    diagnostics_.error(SourceLocation{}, "wrong argument count for execute on function interface '" + iface->sourceName + "'");
                }
                std::string prefix = interfaceGlobalPrefix(*iface);
                for (size_t i = 0; i < args.size() && i < iface->signature.paramTypes.size(); ++i) {
                    std::vector<std::string> prelude;
                    std::string arg = lowerExpression(args[i], {}, ctx, prelude);
                    out.insert(out.end(), prelude.begin(), prelude.end());
                    out.push_back("set " + prefix + "_arg" + std::to_string(i) + "=" + arg);
                }
                ++functionInterfaceCalls_;
                out.push_back("call TriggerExecute(" + prefix + "_trigger[" + rewriteReceiverExpression(receiver, ctx) + "])");
                return out;
            }
            std::string finalName = resolveFunctionTargetName(receiver, ctx.currentStruct);
            if (const FunctionInfo* fn = findFunctionInfo(finalName)) {
                ++functionObjectCalls_;
                out.push_back("call " + fn->finalName + "(" + argsText + ")");
                return out;
            }
        }
        if (findMethodCallSpan(callExpr, "evaluate", receiverStart, dotPos, open, close) && receiverStart == 0 && close + 1 == callExpr.size()) {
            std::string receiver = trim(callExpr.substr(receiverStart, dotPos - receiverStart));
            std::string argsText = callExpr.substr(open + 1, close - open - 1);
            if (const FunctionInterfaceInfo* iface = resolveReceiverInterface(receiver, ctx)) {
                std::vector<std::string> args = splitCommaList(argsText);
                if (args.size() != iface->signature.paramTypes.size()) {
                    diagnostics_.error(SourceLocation{}, "wrong argument count for evaluate on function interface '" + iface->sourceName + "'");
                }
                std::string prefix = interfaceGlobalPrefix(*iface);
                for (size_t i = 0; i < args.size() && i < iface->signature.paramTypes.size(); ++i) {
                    std::vector<std::string> prelude;
                    std::string arg = lowerExpression(args[i], {}, ctx, prelude);
                    out.insert(out.end(), prelude.begin(), prelude.end());
                    out.push_back("set " + prefix + "_arg" + std::to_string(i) + "=" + arg);
                }
                ++functionInterfaceCalls_;
                out.push_back("call TriggerExecute(" + prefix + "_trigger[" + rewriteReceiverExpression(receiver, ctx) + "])");
                return out;
            }
            std::string finalName = resolveFunctionTargetName(receiver, ctx.currentStruct);
            if (const FunctionInfo* fn = findFunctionInfo(finalName)) {
                ++functionObjectCalls_;
                out.push_back("call " + fn->finalName + "(" + argsText + ")");
                return out;
            }
        }
    }

    if (startsWithWord(line, "set")) {
        std::string rest = trim(std::string_view(line).substr(3));
        if (auto eq = findTopLevelAssignment(rest)) {
            std::string lhs = trim(std::string_view(rest).substr(0, *eq));
            std::string rhs = trim(std::string_view(rest).substr(*eq + 1));
            std::string expectedType;
            if (ctx.localTypes) {
                auto it = ctx.localTypes->find(lhs);
                if (it != ctx.localTypes->end() && findFunctionInterface(it->second)) {
                    expectedType = it->second;
                }
            }
            std::vector<std::string> prelude;
            rhs = lowerExpression(rhs, expectedType, ctx, prelude);
            out.insert(out.end(), prelude.begin(), prelude.end());
            bool compact = *eq > 0 && *eq + 1 < rest.size() &&
                           !std::isspace(static_cast<unsigned char>(rest[*eq - 1])) &&
                           !std::isspace(static_cast<unsigned char>(rest[*eq + 1]));
            std::string op = compact ? "=" : " = ";
            std::string lhsOut = rewriteStructExpression(lhs, ctx.currentStruct, ctx.localTypes ? *ctx.localTypes : std::unordered_map<std::string, std::string>{});
            lhsOut = rewriteArrayAccesses(lhsOut, ctx.localArrayShapes);
            out.push_back("set " + lhsOut + op + rhs);
            return out;
        }
    }

    if (startsWithWord(line, "return")) {
        std::string expr = trim(std::string_view(line).substr(6));
        std::vector<std::string> prelude;
        expr = lowerExpression(expr, {}, ctx, prelude);
        out.insert(out.end(), prelude.begin(), prelude.end());
        out.push_back("return " + expr);
        return out;
    }

    std::vector<std::string> prelude;
    line = lowerExpression(line, {}, ctx, prelude);
    out.insert(out.end(), prelude.begin(), prelude.end());
    if (prelude.empty() && line == originalTrim && !leading.empty()) {
        out.push_back(rawLine);
    } else {
        out.push_back(line);
    }
    return out;
}

std::vector<std::string> Phase1Codegen::lowerZincBody(const std::vector<std::string>& lines) {
    std::vector<std::string> locals;
    std::vector<std::string> body;
    std::vector<std::string> normalized = splitZincStructuralLines(expandLeadingDotChains(lines));
    size_t index = 0;
    lowerZincBlock(normalized, index, locals, body);
    std::vector<std::string> out;
    out.insert(out.end(), locals.begin(), locals.end());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<std::string> Phase1Codegen::extractZincLambdas(const std::vector<std::string>& lines, SourceLocation loc) {
    std::vector<std::string> out;
    std::unordered_set<std::string> localNames;

    auto rememberLocal = [&](const std::string& raw) {
        for (const auto& name : declaredNamesInZincStatements(raw)) {
            localNames.insert(name);
        }
    };

    for (size_t index = 0; index < lines.size(); ++index) {
        std::string line = lines[index];
        while (true) {
            auto lambdaStart = findAnonymousFunctionStart(line);
            if (!lambdaStart) {
                out.push_back(line);
                rememberLocal(line);
                break;
            }
            size_t fn = lambdaStart->first;
            size_t open = lambdaStart->second;

            size_t close = findMatchingParen(line, open);
            if (close == std::string::npos) {
                out.push_back(line);
                break;
            }
            size_t brace = line.find('{', close);
            if (brace == std::string::npos) {
                out.push_back(line);
                break;
            }

            LambdaInfo lambda;
            lambda.loc = loc;
            lambda.currentStruct = lambdaContextStruct_;
            lambda.container = lambdaContextContainer_;
            lambda.loc.line += static_cast<uint32_t>(index);
            lambda.name = "vjlambda__" + std::to_string(nextLambdaId_++);
            lambda.returnType = TypeRef{"nothing", loc, false};
            std::string paramsText = line.substr(open + 1, close - open - 1);
            lambda.params = parseCodegenParamList(paramsText, loc);
            size_t arrow = line.find("->", close);
            if (arrow != std::string::npos && arrow < brace) {
                std::string ret = trim(std::string_view(line).substr(arrow + 2, brace - (arrow + 2)));
                if (!ret.empty()) {
                    lambda.returnType = TypeRef{ret, loc, false};
                }
            }

            std::string before = line.substr(0, fn);
            recordLambdaContext(before);
            std::string afterBrace = line.substr(brace + 1);
            int depth = 1;
            std::string suffix;
            auto consumeBodyText = [&](const std::string& text) {
                std::string current;
                bool foundClose = false;
                for (size_t i = 0; i < text.size(); ++i) {
                    char c = text[i];
                    if (c == '{') {
                        ++depth;
                        current.push_back(c);
                    } else if (c == '}') {
                        --depth;
                        if (depth == 0) {
                            if (!trim(current).empty()) {
                                lambda.bodyLines.push_back(trim(current));
                            }
                            suffix = text.substr(i + 1);
                            foundClose = true;
                            break;
                        }
                        current.push_back(c);
                    } else {
                        current.push_back(c);
                    }
                }
                if (!foundClose && !trim(current).empty()) {
                    lambda.bodyLines.push_back(trim(current));
                }
                return foundClose;
            };

            bool closed = consumeBodyText(afterBrace);
            while (!closed && index + 1 < lines.size()) {
                ++index;
                closed = consumeBodyText(lines[index]);
            }
            lambda.bodyLines = extractZincLambdas(lambda.bodyLines, lambda.loc);

            std::unordered_set<std::string> lambdaScopedNames;
            for (const auto& param : lambda.params) {
                lambdaScopedNames.insert(param.name);
            }
            for (const auto& bodyLine : lambda.bodyLines) {
                for (const auto& local : declaredNamesInZincStatements(bodyLine)) {
                    lambdaScopedNames.insert(local);
                }
            }
            bool captured = false;
            for (const auto& local : localNames) {
                if (lambdaScopedNames.contains(local)) {
                    continue;
                }
                for (const auto& bodyLine : lambda.bodyLines) {
                    if (containsIdentifierOutsideProtected(bodyLine, local)) {
                        diagnostics_.error(lambda.loc, "capturing lambda is not supported in phase 5: '" + local + "'");
                        captured = true;
                        break;
                    }
                }
            }
            if (captured) {
                ++lambdasCapturing_;
                ++lambdasRejected_;
            }

            FunctionInfo fnInfo;
            fnInfo.sourceName = lambda.name;
            fnInfo.finalName = lambda.name;
            fnInfo.signature = signatureFromParams(lambda.params, lambda.returnType, lambda.currentStruct);
            size_t fnIndex = functions_.size();
            functions_.push_back(fnInfo);
            functionIndexByName_[lambda.name] = fnIndex;

            line = before + "function " + lambda.name + suffix;
            lambdas_.push_back(std::move(lambda));
        }
    }
    return out;
}

void Phase1Codegen::recordLambdaContext(const std::string& beforeText) {
    std::string before = trim(beforeText);
    if (before.find("Condition(") != std::string::npos || before.find("Filter(") != std::string::npos) {
        ++lambdasBoolexprContext_;
        return;
    }
    if (before.find('.') != std::string::npos) {
        ++lambdasMethodCallbackContext_;
        return;
    }
    static const std::vector<std::string> nativeCallbackNames = {
        "TimerStart",
        "TriggerAddAction",
        "TriggerAddCondition",
        "ForForce",
        "ForGroup",
        "EnumDestructablesInRect",
        "GroupEnumUnitsInRange",
        "GroupEnumUnitsInRangeEx",
        "DzFrameSetUpdateCallbackByCode",
        "DzTriggerRegisterMouseWheelEventByCode",
        "DzTriggerRegisterWindowResizeEventByCode",
        "DzTriggerRegisterMouseMoveEventByCode",
        "DzFrameSetScriptByCode",
        "DzTriggerRegisterKeyEventByCode",
    };
    for (const auto& name : nativeCallbackNames) {
        if (before.find(name + "(") != std::string::npos) {
            ++lambdasNativeCallbackContext_;
            return;
        }
    }
    ++lambdasCodeContext_;
}

void Phase1Codegen::lowerZincBlock(const std::vector<std::string>& lines, size_t& index, std::vector<std::string>& locals, std::vector<std::string>& body) {
    while (index < lines.size()) {
        std::string line = trim(lines[index]);
        if (line.empty() || line.rfind("//", 0) == 0) {
            ++index;
            continue;
        }
        if (line == "}" || line.rfind("} else", 0) == 0) {
            return;
        }
        if (startsWithWord(line, "while")) {
            std::string cond = conditionFromHeader(line);
            body.push_back("loop");
            body.push_back("    exitwhen not " + cond);
            ++index;
            lowerZincBlock(lines, index, locals, body);
            if (index < lines.size() && trim(lines[index]) == "}") {
                ++index;
            }
            body.push_back("endloop");
            continue;
        }
        if (startsWithWord(line, "if")) {
            std::string cond = conditionFromHeader(line);
            body.push_back("if " + cond + " then");
            size_t close = findMatchingParen(line, line.find('('));
            std::string inlineBody = close == std::string::npos ? "" : trim(std::string_view(line).substr(close + 1));
            if (!inlineBody.empty() && inlineBody.front() != '{') {
                lowerZincSimpleStatement(inlineBody, locals, body);
                body.push_back("endif");
                ++index;
                continue;
            }
            ++index;
            lowerZincBlock(lines, index, locals, body);
            if (index + 1 < lines.size() && trim(lines[index]) == "}" && trim(lines[index + 1]).rfind("else", 0) == 0) {
                ++index;
            }
            while (index < lines.size() &&
                   (trim(lines[index]).rfind("} else if", 0) == 0 || trim(lines[index]).rfind("else if", 0) == 0)) {
                std::string elseIfLine = trim(lines[index]);
                size_t ifPos = elseIfLine.find("if");
                std::string elseIfCond = ifPos == std::string::npos ? "()" : conditionFromHeader(elseIfLine.substr(ifPos));
                body.push_back("elseif " + elseIfCond + " then");
                ++index;
                lowerZincBlock(lines, index, locals, body);
            }
            if (index < lines.size() &&
                (trim(lines[index]).rfind("} else", 0) == 0 || trim(lines[index]).rfind("else", 0) == 0)) {
                body.push_back("else");
                ++index;
                lowerZincBlock(lines, index, locals, body);
            }
            if (index < lines.size() && trim(lines[index]) == "}") {
                ++index;
            }
            body.push_back("endif");
            continue;
        }
        if (startsWithWord(line, "for")) {
            std::string content = parenContent(line);
            auto parts = splitSemicolons(content);
            if (parts.size() == 3) {
                lowerZincSimpleStatement(parts[0], locals, body);
                body.push_back("loop");
                body.push_back("    exitwhen not (" + parts[1] + ")");
                ++index;
                lowerZincBlock(lines, index, locals, body);
                lowerZincSimpleStatement(parts[2], locals, body);
                if (index < lines.size() && trim(lines[index]) == "}") {
                    ++index;
                }
                body.push_back("endloop");
                continue;
            }
            RangeForParts range = parseRangeForParts(content);
            if (range.ok) {
                body.push_back("set " + range.var + " = " + range.start);
                body.push_back("loop");
                body.push_back("    exitwhen not (" + range.var + " " + range.upperOp + " " + range.end + ")");
                ++index;
                lowerZincBlock(lines, index, locals, body);
                body.push_back("set " + range.var + " = " + range.var + " + 1");
                if (index < lines.size() && trim(lines[index]) == "}") {
                    ++index;
                }
                body.push_back("endloop");
                continue;
            }
        }
        if (line.find(';') != std::string::npos) {
            for (const auto& statement : splitSemicolons(line)) {
                if (!trim(statement).empty()) {
                    lowerZincSimpleStatement(statement, locals, body);
                }
            }
            ++index;
            continue;
        }
        lowerZincSimpleStatement(line, locals, body);
        ++index;
    }
}

void Phase1Codegen::lowerZincSimpleStatement(const std::string& rawStatement, std::vector<std::string>& locals, std::vector<std::string>& body) {
    std::string statement = removeSemicolon(rawStatement);
    if (statement.empty()) {
        return;
    }
    if (startsWithWord(statement, "else") || statement.find('{') != std::string::npos || statement.find('}') != std::string::npos) {
        return;
    }
    if (startsWithWord(statement, "return")) {
        body.push_back(statement);
        return;
    }
    if (isLocalDecl(statement)) {
        std::string work = statement;
        bool constant = false;
        if (startsWithWord(work, "constant")) {
            constant = true;
            work = trim(std::string_view(work).substr(8));
        }
        size_t assign = work.find('=');
        std::string declPart = assign == std::string::npos ? work : trim(std::string_view(work).substr(0, assign));
        std::string init = assign == std::string::npos ? "" : trim(std::string_view(work).substr(assign + 1));
        std::istringstream in(declPart);
        std::string type;
        std::string name;
        in >> type >> name;
        if (!type.empty() && !name.empty()) {
            bool array = false;
            if (name == "array") {
                in >> name;
                array = true;
            }
            size_t bracket = name.find('[');
            std::string suffix;
            if (bracket != std::string::npos) {
                suffix = name.substr(bracket);
                name = name.substr(0, bracket);
                array = true;
            }
            std::vector<int> dims = parseArrayDimensions(suffix);
            if (dims.size() > 1) {
                locals.push_back(std::string("local ") + (constant ? "constant " : "") + type + " " + name + suffix);
            } else {
                locals.push_back(std::string("local ") + (constant ? "constant " : "") + type + (array ? " array " : " ") + name);
            }
            if (!init.empty()) {
                body.push_back("set " + name + " = " + init);
            }
        }
        return;
    }
    auto emitAssign = [&](const std::string& op, const std::string& jassOp) -> bool {
        size_t pos = statement.find(op);
        if (pos == std::string::npos) {
            return false;
        }
        std::string lhs = trim(std::string_view(statement).substr(0, pos));
        std::string rhs = trim(std::string_view(statement).substr(pos + op.size()));
        if (op == "=") {
            body.push_back("set " + lhs + " = " + rhs);
        } else {
            body.push_back("set " + lhs + " = " + lhs + " " + jassOp + " " + rhs);
        }
        return true;
    };
    if (emitAssign("+=", "+") || emitAssign("-=", "-") || emitAssign("*=", "*") || emitAssign("/=", "/")) {
        return;
    }
    if (isAssignmentLike(statement) && emitAssign("=", "=")) {
        return;
    }
    if (!statement.empty()) {
        body.push_back("call " + statement);
    }
}

} // namespace vjassc
