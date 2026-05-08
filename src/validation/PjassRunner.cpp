#include "validation/PjassRunner.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
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
    if (containsAny(line, {"Undefined function", "undeclared function", "not declared function"})) {
        return "undefinedFunction";
    }
    if (containsAny(line, {"Undefined variable", "Undeclared variable", "not declared"})) {
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
    if (containsAny(line, {"Missing return", "Missing linebreak before return"}) ||
        containsAny(line, {"return", "Return"})) {
        return "returnMismatch";
    }
    if (containsAny(line, {"local", "Local"})) {
        return "localOrder";
    }
    if (containsAny(generatedLine, {"[9][16]", "[8][4]", "[14][6]"}) ||
        generatedLine.find("][") != std::string::npos) {
        return "invalidArraySyntax";
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

std::vector<PjassErrorGroup> groupPjassErrors(const std::string& text, const std::string& generatedOutput) {
    std::vector<std::string> generatedLines = splitLines(generatedOutput);
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
        auto& group = groups[kind];
        if (group.kind.empty()) {
            group.kind = kind;
        }
        ++group.count;
        if (group.firstLine == 0) {
            group.firstLine = generatedLine;
            group.firstMessage = line;
        }
        if (group.examples.size() < 20) {
            group.examples.push_back(PjassErrorExample{generatedLine, line, excerptForLine(generatedLines, generatedLine)});
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
    result.errorSummary = classifyPjassErrors(result.stdoutText + "\n" + result.stderrText);
    result.errorGroups = groupPjassErrors(result.stdoutText + "\n" + result.stderrText, readTextFile(options.scriptPath));
    (void)options.timeoutMs;
    return result;
}

} // namespace vjassc
