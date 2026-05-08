#include "cli/CliOptions.h"

#include <iostream>

namespace vjassc {
namespace {

bool needsValue(const std::string& arg) {
    return arg == "-o" || arg == "--emit-preprocessed" || arg == "--emit-tokens" ||
           arg == "--emit-ast" || arg == "--emit-stats" || arg == "--import-path";
}

} // namespace

CliParseResult parseCli(int argc, char** argv) {
    CliParseResult result;
    auto& opt = result.options;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto requireValue = [&](const std::string& name, std::filesystem::path& target) -> bool {
            if (i + 1 >= argc) {
                result.error = "missing value for " + name;
                return false;
            }
            target = argv[++i];
            return true;
        };

        if (arg == "--help" || arg == "-h") {
            opt.showHelp = true;
        } else if (arg == "--version") {
            opt.showVersion = true;
        } else if (arg == "-o") {
            if (!requireValue(arg, opt.outputPath)) {
                return result;
            }
        } else if (arg == "--debug") {
            opt.debugMode = true;
        } else if (arg == "--release") {
            opt.debugMode = false;
        } else if (arg == "--scan-only") {
            opt.scanOnly = true;
        } else if (arg == "--allow-unsupported") {
            opt.allowUnsupported = true;
        } else if (arg == "--emit-preprocessed") {
            if (!requireValue(arg, opt.emitPreprocessedPath)) {
                return result;
            }
        } else if (arg == "--emit-tokens") {
            if (!requireValue(arg, opt.emitTokensPath)) {
                return result;
            }
        } else if (arg == "--emit-ast") {
            if (!requireValue(arg, opt.emitAstPath)) {
                return result;
            }
        } else if (arg == "--emit-stats") {
            if (!requireValue(arg, opt.emitStatsPath)) {
                return result;
            }
        } else if (arg == "--import-path") {
            std::filesystem::path path;
            if (!requireValue(arg, path)) {
                return result;
            }
            opt.importPaths.push_back(path);
        } else if (!arg.empty() && arg[0] == '-') {
            result.error = "unknown option: " + arg;
            return result;
        } else if (!opt.inputPath.empty()) {
            result.error = "multiple input files provided: " + opt.inputPath.string() + " and " + arg;
            return result;
        } else {
            opt.inputPath = arg;
        }

        if (needsValue(arg) && i < argc && std::string(argv[i]).empty()) {
            result.error = "empty value for " + arg;
            return result;
        }
    }

    if (!opt.showHelp && !opt.showVersion && opt.inputPath.empty()) {
        result.error = "missing input file";
        return result;
    }

    if (!opt.scanOnly && !opt.showHelp && !opt.showVersion && opt.outputPath.empty()) {
        const bool emitsOnly = !opt.emitPreprocessedPath.empty() || !opt.emitTokensPath.empty() ||
                               !opt.emitAstPath.empty() || !opt.emitStatsPath.empty();
        if (!emitsOnly) {
            result.error = "missing output path; use -o <output.j> or --scan-only";
            return result;
        }
    }

    result.ok = true;
    return result;
}

void printHelp(std::ostream& out) {
    out << "vjassc phase2 - vJass/Zinc to JASS compiler prototype\n"
        << "\n"
        << "Usage:\n"
        << "  vjassc <input.j> -o <output.j> [--debug|--release]\n"
        << "  vjassc <input.j> --scan-only [--allow-unsupported]\n"
        << "\n"
        << "Options:\n"
        << "  -o <path>                    Write generated JASS\n"
        << "  --debug                      Keep debug lines and strip the debug prefix\n"
        << "  --release                    Drop debug lines (default)\n"
        << "  --scan-only                  Read, preprocess, lex, parse, and emit diagnostics/stats only\n"
        << "  --emit-preprocessed <path>   Write preprocessed logical source\n"
        << "  --emit-tokens <path>         Write token stream\n"
        << "  --emit-ast <path>            Write AST dump\n"
        << "  --emit-stats <path>          Write JSON statistics\n"
        << "  --import-path <dir>          Add import search directory; may be repeated\n"
        << "  --allow-unsupported          Allow unsupported declarations during scan-only\n"
        << "  --version                    Print version\n"
        << "  --help                       Print this help\n";
}

void printVersion(std::ostream& out) {
    out << "vjassc phase2 0.2.0\n";
}

} // namespace vjassc
