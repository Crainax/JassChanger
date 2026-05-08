#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace vjassc {

struct PjassOptions {
    std::filesystem::path pjassPath;
    std::filesystem::path commonPath;
    std::filesystem::path blizzardPath;
    std::filesystem::path scriptPath;
    std::filesystem::path stdoutPath;
    std::filesystem::path stderrPath;
    long long timeoutMs = 30000;
};

struct PjassResult {
    bool requested = false;
    bool ran = false;
    bool ok = false;
    int exitCode = -1;
    std::string commandLine;
    std::filesystem::path stdoutPath;
    std::filesystem::path stderrPath;
    std::string stdoutText;
    std::string stderrText;
    std::string error;
    long long elapsedMs = 0;
    std::unordered_map<std::string, size_t> errorSummary;
};

struct PjassResolvedPaths {
    bool ok = false;
    std::string error;
    std::filesystem::path pjassPath;
    std::filesystem::path commonPath;
    std::filesystem::path blizzardPath;
};

PjassResolvedPaths resolvePjassPaths(const std::filesystem::path& cwd,
                                     const std::filesystem::path& explicitPjass,
                                     const std::filesystem::path& explicitCommon,
                                     const std::filesystem::path& explicitBlizzard);
PjassResult runPjass(const PjassOptions& options);
std::unordered_map<std::string, size_t> classifyPjassErrors(const std::string& text);

} // namespace vjassc
