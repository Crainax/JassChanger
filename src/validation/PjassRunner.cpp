#include "validation/PjassRunner.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace vjassc {
namespace {

std::string quotePath(const std::filesystem::path& path) {
    std::string text = path.string();
    std::string out = "\"";
    for (char c : text) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out.push_back(c);
        }
    }
    out += "\"";
    return out;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path firstExisting(const std::filesystem::path& cwd,
                                    const std::filesystem::path& explicitPath,
                                    const std::vector<std::filesystem::path>& candidates) {
    if (!explicitPath.empty()) {
        return explicitPath;
    }
    for (const auto& candidate : candidates) {
        std::filesystem::path full = cwd / candidate;
        if (std::filesystem::exists(full)) {
            return full;
        }
    }
    return {};
}

bool containsAny(const std::string& text, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (text.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string classifyPjassLine(const std::string& line, const std::string& generatedLine = {}) {
    if (containsAny(generatedLine, {"][", "[9][16]", "[8][4]", "[14][6]"})) {
        return "invalidArraySyntax";
    }
    if (containsAny(line, {"must not take any arguments when used as code",
                           "Cannot convert codereturnsboolean to integer",
                           "Cannot convert code to boolexpr"})) {
        return "callbackCodeSignatureMismatch";
    }
    if (std::regex_search(generatedLine, std::regex(R"((\)|\])\s*\.\s*[A-Za-z_$])")) ||
        std::regex_search(generatedLine, std::regex(R"(^\s*call\s+\.\s*[A-Za-z_$])"))) {
        return "methodChainReceiverResidue";
    }
    if (std::regex_search(generatedLine, std::regex(R"(\[[^\]]+\]\s*\.\s*[A-Za-z_$])"))) {
        return "indexedStructMemberResidue";
    }
    if (std::regex_search(generatedLine, std::regex(R"(^\s*if\s*\(.+\)\s*(return|call|set)\b)"))) {
        return "inlineZincControlResidue";
    }
    if (std::regex_search(generatedLine, std::regex(R"(^\s*local\s+[A-Za-z_$][A-Za-z0-9_$]*(?:\s+array)?\s+[^,\r\n]+,)"))) {
        return "commaSeparatedLocal";
    }
    if (containsAny(line, {"Cannot convert integer to boolean", "Cannot convert boolean to integer"})) {
        return "booleanIntegerMismatch";
    }
    if (containsAny(line, {"Cannot convert integer to code", "Cannot convert code to integer",
                           "Cannot convert integer to boolexpr", "Cannot convert boolexpr to integer"})) {
        return "codeInterfaceConversion";
    }
    if (containsAny(line, {"Index missing for array variable"})) {
        return "arrayIndexMissing";
    }
    if (containsAny(line, {"Undefined function", "Undeclared function", "undeclared function", "not declared function"})) {
        if (line.find("vjlambda__") != std::string::npos) {
            return "forwardLambdaReference";
        }
        return "undefinedFunction";
    }
    if (containsAny(line, {"Undefined variable", "Undeclared variable", "undeclared variable", "not declared"})) {
        if (line.find("vjassc__") != std::string::npos) {
            return "unresolvedGeneratedHelper";
        }
        if (line.find("HASH_TIMER") != std::string::npos || line.find("HASH_ABILITY") != std::string::npos) {
            return "unresolvedPublicGlobal";
        }
        if (line.find("uiHT") != std::string::npos || line.find("KEY_TOTAL") != std::string::npos) {
            return "unresolvedKnownSourceSymbol";
        }
        if (line.find("yd_") != std::string::npos || line.find("bj_") != std::string::npos) {
            return "unresolvedEnvironmentSymbol";
        }
        return "undefinedVariable";
    }
    if (containsAny(line, {"Expected endfunction", "endfunction"})) {
        return "expectedEndfunction";
    }
    if (containsAny(line, {"Expected globals", "globals"})) {
        return "expectedGlobals";
    }
    if (containsAny(line, {"Undefined type", "Invalid type", "type mismatch"})) {
        return "unknownType";
    }
    if (containsAny(line, {"multiply defined", "Duplicate", "already defined"})) {
        return "duplicateDeclaration";
    }
    if (containsAny(line, {"Missing return", "Missing linebreak before return"})) {
        return "returnMissingValue";
    }
    if (containsAny(line, {"return", "Return"})) {
        return "returnMismatch";
    }
    if (containsAny(line, {" is uninitialized", "uninitialized"})) {
        return "uninitializedVariable";
    }
    if (containsAny(line, {"local", "Local"})) {
        return "localOrder";
    }
    if (containsAny(line, {"native", "Native", "global ordering", "declaration order"})) {
        return "nativeTypeGlobalOrdering";
    }
    if (containsAny(line, {"Comparing two variables of different primitive types"})) {
        return "invalidComparison";
    }
    if (containsAny(line, {"syntax error", "Unrecognized character", "Expected"})) {
        return "syntaxError";
    }
    return "other";
}

size_t parseGeneratedLineNumber(const std::string& line) {
    size_t firstColon = line.find(':');
    if (firstColon == std::string::npos) {
        return 0;
    }
    size_t secondColon = line.find(':', firstColon + 1);
    if (secondColon == std::string::npos || secondColon <= firstColon + 1) {
        return 0;
    }
    std::string number = line.substr(firstColon + 1, secondColon - firstColon - 1);
    try {
        return static_cast<size_t>(std::stoull(number));
    } catch (...) {
        return 0;
    }
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> excerptForLine(const std::vector<std::string>& lines, size_t oneBasedLine) {
    std::vector<std::string> out;
    if (oneBasedLine == 0 || lines.empty()) {
        return out;
    }
    size_t start = oneBasedLine > 3 ? oneBasedLine - 3 : 1;
    size_t end = std::min(lines.size(), oneBasedLine + 3);
    for (size_t lineNo = start; lineNo <= end; ++lineNo) {
        out.push_back(std::to_string(lineNo) + ": " + lines[lineNo - 1]);
    }
    return out;
}

std::string functionNameFromHeader(const std::string& line) {
    std::istringstream in(line);
    std::string word;
    std::string name;
    in >> word >> name;
    return name;
}

std::vector<std::string> functionAtLines(const std::vector<std::string>& lines) {
    std::vector<std::string> out(lines.size() + 1);
    std::string current;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string t = lines[i];
        size_t first = t.find_first_not_of(" \t");
        if (first != std::string::npos) {
            t = t.substr(first);
        }
        if (t.rfind("function ", 0) == 0) {
            current = functionNameFromHeader(t);
        }
        out[i + 1] = current;
        if (t.rfind("endfunction", 0) == 0) {
            current.clear();
        }
    }
    return out;
}

std::unordered_map<std::string, size_t> functionDefinitionLines(const std::vector<std::string>& lines) {
    std::unordered_map<std::string, size_t> defs;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string t = lines[i];
        size_t first = t.find_first_not_of(" \t");
        if (first != std::string::npos) {
            t = t.substr(first);
        }
        if (t.rfind("function ", 0) == 0) {
            std::string name = functionNameFromHeader(t);
            if (!name.empty()) {
                defs[name] = i + 1;
            }
        }
    }
    return defs;
}

std::string extractPjassSymbol(const std::string& line) {
    static const std::vector<std::regex> patterns = {
        std::regex(R"((?:Undefined|Undeclared|undeclared) function ([A-Za-z_$][A-Za-z0-9_$]*))"),
        std::regex(R"((?:Undefined|Undeclared|undeclared) variable ([A-Za-z_$][A-Za-z0-9_$]*))"),
    };
    for (const auto& pattern : patterns) {
        std::smatch match;
        if (std::regex_search(line, match, pattern)) {
            return match[1].str();
        }
    }
    return {};
}

} // namespace

PjassResolvedPaths resolvePjassPaths(const std::filesystem::path& cwd,
                                     const std::filesystem::path& explicitPjass,
                                     const std::filesystem::path& explicitCommon,
                                     const std::filesystem::path& explicitBlizzard) {
    PjassResolvedPaths result;
    result.pjassPath = firstExisting(cwd, explicitPjass, {
        "jasshelper/pjass.exe",
        "jasshelper/pjass/pjass.exe",
        "tools/pjass.exe",
        "pjass.exe",
    });
    result.commonPath = firstExisting(cwd, explicitCommon, {
        "jasshelper/common.j",
        "jasshelper/war3/common.j",
        "common.j",
    });
    result.blizzardPath = firstExisting(cwd, explicitBlizzard, {
        "jasshelper/blizzard.j",
        "jasshelper/war3/blizzard.j",
        "blizzard.j",
    });

    if (result.pjassPath.empty() || result.commonPath.empty() || result.blizzardPath.empty() ||
        !std::filesystem::exists(result.pjassPath) || !std::filesystem::exists(result.commonPath) ||
        !std::filesystem::exists(result.blizzardPath)) {
        result.error = "PJASS validation requested, but pjass/common.j/blizzard.j was not found. "
                       "Pass --pjass, --common, and --blizzard explicitly.";
        return result;
    }
    result.ok = true;
    return result;
}

std::unordered_map<std::string, size_t> classifyPjassErrors(const std::string& text) {
    std::unordered_map<std::string, size_t> counts;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.find("Parse successful") != std::string::npos) {
            continue;
        }
        ++counts[classifyPjassLine(line)];
    }
    return counts;
}

std::vector<PjassErrorGroup> groupPjassErrors(const std::string& text,
                                              const std::string& generatedOutput,
                                              size_t exampleLimit) {
    std::vector<std::string> generatedLines = splitLines(generatedOutput);
    std::vector<std::string> currentFunctions = functionAtLines(generatedLines);
    std::unordered_map<std::string, size_t> defLines = functionDefinitionLines(generatedLines);
    std::map<std::string, PjassErrorGroup> groups;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.find("Parse successful") != std::string::npos) {
            continue;
        }
        size_t generatedLine = parseGeneratedLineNumber(line);
        std::string generatedText;
        if (generatedLine > 0 && generatedLine <= generatedLines.size()) {
            generatedText = generatedLines[generatedLine - 1];
        }
        std::string kind = classifyPjassLine(line, generatedText);
        std::string symbol = extractPjassSymbol(line);
        if (!symbol.empty() && containsAny(line, {"Undefined function", "Undeclared function", "undeclared function"})) {
            auto def = defLines.find(symbol);
            if (def != defLines.end() && generatedLine != 0 && def->second > generatedLine) {
                kind = symbol.rfind("vjlambda__", 0) == 0 ? "forwardLambdaReference" : "forwardFunctionReference";
            }
        }
        auto& group = groups[kind];
        if (group.kind.empty()) {
            group.kind = kind;
        }
        ++group.count;
        if (group.firstLine == 0) {
            group.firstLine = generatedLine;
            group.firstMessage = line;
        }
        if (group.examples.size() < exampleLimit) {
            std::string functionName;
            if (generatedLine > 0 && generatedLine < currentFunctions.size()) {
                functionName = currentFunctions[generatedLine];
            }
            group.examples.push_back(PjassErrorExample{
                generatedLine,
                functionName,
                generatedText,
                line,
                excerptForLine(generatedLines, generatedLine),
            });
        }
    }

    std::vector<PjassErrorGroup> out;
    out.reserve(groups.size());
    for (auto& [_, group] : groups) {
        out.push_back(std::move(group));
    }
    std::sort(out.begin(), out.end(), [](const PjassErrorGroup& a, const PjassErrorGroup& b) {
        if (a.count != b.count) {
            return a.count > b.count;
        }
        return a.kind < b.kind;
    });
    return out;
}

PjassResult runPjass(const PjassOptions& options) {
    PjassResult result;
    result.requested = true;
    result.stdoutPath = options.stdoutPath;
    result.stderrPath = options.stderrPath;
    std::error_code ec;
    if (!options.stdoutPath.parent_path().empty()) {
        std::filesystem::create_directories(options.stdoutPath.parent_path(), ec);
    }
    if (!options.stderrPath.parent_path().empty()) {
        std::filesystem::create_directories(options.stderrPath.parent_path(), ec);
    }

    result.commandLine = quotePath(options.pjassPath) + " " + quotePath(options.commonPath) + " " +
                         quotePath(options.blizzardPath) + " " + quotePath(options.scriptPath);
    std::string command = "call " + result.commandLine + " > " + quotePath(options.stdoutPath) + " 2> " + quotePath(options.stderrPath);
    auto start = std::chrono::steady_clock::now();
    int rc = std::system(command.c_str());
    auto end = std::chrono::steady_clock::now();
    result.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    result.exitCode = rc;
    result.ran = true;
    result.stdoutText = readTextFile(options.stdoutPath);
    result.stderrText = readTextFile(options.stderrPath);
    result.ok = rc == 0;
    result.errorGroups = groupPjassErrors(result.stdoutText + "\n" + result.stderrText,
                                          readTextFile(options.scriptPath),
                                          options.exampleLimit);
    for (const auto& group : result.errorGroups) {
        result.errorSummary[group.kind] = group.count;
    }
    (void)options.timeoutMs;
    return result;
}

} // namespace vjassc
