#include "validation/PjassRunner.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
        std::string key = "other";
        if (containsAny(line, {"Undefined function", "undeclared function", "not declared function"})) {
            key = "undefinedFunction";
        } else if (containsAny(line, {"Undefined variable", "Undeclared variable", "not declared"})) {
            key = "undefinedVariable";
        } else if (containsAny(line, {"Expected endfunction", "endfunction"})) {
            key = "expectedEndfunction";
        } else if (containsAny(line, {"Expected globals", "globals"})) {
            key = "expectedGlobals";
        } else if (containsAny(line, {"Undefined type", "Invalid type", "type mismatch"})) {
            key = "invalidType";
        } else if (containsAny(line, {"multiply defined", "Duplicate", "already defined"})) {
            key = "duplicateDeclaration";
        } else if (containsAny(line, {"return", "Return"})) {
            key = "returnMismatch";
        } else if (containsAny(line, {"local", "Local"})) {
            key = "localOrder";
        } else if (containsAny(line, {"native", "Native", "global ordering", "declaration order"})) {
            key = "nativeTypeGlobalOrdering";
        } else if (containsAny(line, {"syntax error", "Unrecognized character", "Expected"})) {
            key = "syntaxError";
        }
        ++counts[key];
    }
    return counts;
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
    (void)options.timeoutMs;
    return result;
}

} // namespace vjassc
