#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace vjassc {

struct CliOptions {
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    std::filesystem::path emitPreprocessedPath;
    std::filesystem::path emitTokensPath;
    std::filesystem::path emitAstPath;
    std::filesystem::path emitExpandedAstPath;
    std::filesystem::path emitStatsPath;
    std::vector<std::filesystem::path> importPaths;
    bool debugMode = false;
    bool scanOnly = false;
    bool allowUnsupported = false;
    bool checkOutputSyntaxLite = false;
    bool showHelp = false;
    bool showVersion = false;
};

struct CliParseResult {
    CliOptions options;
    bool ok = false;
    std::string error;
};

CliParseResult parseCli(int argc, char** argv);
void printHelp(std::ostream& out);
void printVersion(std::ostream& out);

} // namespace vjassc
