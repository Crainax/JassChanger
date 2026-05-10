#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace vjassc {

enum class CompileMode {
    Legacy,
    Fast,
    Validate,
    FullValidation,
};

struct CliOptions {
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    std::filesystem::path emitPreprocessedPath;
    std::filesystem::path emitTokensPath;
    std::filesystem::path emitAstPath;
    std::filesystem::path emitExpandedAstPath;
    std::filesystem::path emitStatsPath;
    std::filesystem::path emitValidationReportPath;
    std::filesystem::path emitPerformanceReportPath;
    std::filesystem::path emitIncrementalReportPath;
    std::filesystem::path emitIncrementalStatePath;
    std::filesystem::path compareIncrementalStatePath;
    std::filesystem::path analyzePjassLogPath;
    std::filesystem::path validateExistingOutputPath;
    std::filesystem::path pjassPath;
    std::filesystem::path commonPath;
    std::filesystem::path blizzardPath;
    std::filesystem::path compareJasshelperPath;
    std::vector<std::filesystem::path> importPaths;
    std::vector<std::string> pjassAllowedExternalFunctions;
    long long pjassTimeoutMs = 30000;
    size_t emitPjassExamples = 20;
    bool debugMode = false;
    bool scanOnly = false;
    bool allowUnsupported = false;
    bool warnMode = false;
    bool checkOutputSyntaxLite = false;
    bool validatePjass = false;
    bool showHelp = false;
    bool showVersion = false;
    CompileMode mode = CompileMode::Legacy;
};

struct CliParseResult {
    CliOptions options;
    bool ok = false;
    std::string error;
};

CliParseResult parseCli(int argc, char** argv);
const char* compileModeName(CompileMode mode);
void printHelp(std::ostream& out);
void printVersion(std::ostream& out);

} // namespace vjassc
