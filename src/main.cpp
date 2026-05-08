#include "cli/CliOptions.h"
#include "codegen/Phase1Codegen.h"
#include "condition/StaticIfPruner.h"
#include "core/Diagnostics.h"
#include "core/SourceManager.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "preprocess/Preprocessor.h"
#include "sema/ModuleExpander.h"
#include "util/JsonWriter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace vjassc;

namespace {

struct Timings {
    long long read = 0;
    long long preprocess = 0;
    long long staticIf = 0;
    long long lex = 0;
    long long parse = 0;
    long long moduleExpand = 0;
    long long total = 0;
};

long long elapsedMs(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
    if (path.empty()) {
        return true;
    }
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "failed to write " << path.string() << '\n';
        return false;
    }
    out << text;
    return true;
}

std::string emitPreprocessed(const std::vector<LogicalLine>& lines) {
    std::string out;
    for (const auto& line : lines) {
        out += line.text;
        out += '\n';
    }
    return out;
}

std::string emitTokens(const SourceManager& sources, const std::vector<Token>& tokens) {
    std::ostringstream out;
    for (const auto& token : tokens) {
        if (token.kind == TokenKind::EndOfFile) {
            continue;
        }
        out << sources.describe(token.loc)
            << "  mode=" << modeName(token.mode)
            << "  kind=" << tokenKindName(token.kind)
            << "  text=";
        writeJsonString(out, token.text);
        out << '\n';
    }
    return out.str();
}

void emitAstDecl(std::ostream& out, const Decl& decl, int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    out << pad << "- kind: " << declKindName(decl.kind);
    if (!decl.name.empty()) {
        out << ", name: " << decl.name;
    }
    if (!decl.access.empty()) {
        out << ", access: " << decl.access;
    }
    if (!decl.initializer.empty()) {
        out << ", initializer: " << decl.initializer;
    }
    if (!decl.unsupportedFeature.empty()) {
        out << ", unsupported: " << decl.unsupportedFeature;
    }
    if (decl.isArrayStruct) {
        out << ", arrayStruct: true";
    }
    out << ", mode: " << modeName(decl.mode) << '\n';
    for (const auto& field : decl.fields) {
        out << pad << "  - field: " << field.name << ", type: " << field.type.name;
        if (field.isStatic) {
            out << ", static";
        }
        if (field.isArray) {
            out << ", array";
        }
        if (field.isFixedArray) {
            out << ", fixedSize: " << field.fixedArraySize;
        }
        if (!field.initializer.empty()) {
            out << ", initializer: " << field.initializer;
        }
        out << '\n';
    }
    for (const auto& method : decl.methods) {
        out << pad << "  - method: " << method.name;
        if (method.isStatic) {
            out << ", static";
        }
        out << ", returns: " << method.returnType.name << ", params: " << method.params.size() << '\n';
    }
    for (const auto& use : decl.moduleUses) {
        out << pad << "  - moduleUse: " << use.name;
        if (use.optional) {
            out << ", optional";
        }
        out << '\n';
    }
    for (const auto& child : decl.children) {
        emitAstDecl(out, child, indent + 2);
    }
}

std::string emitAst(const Program& program) {
    std::ostringstream out;
    out << "Program\n";
    for (const auto& decl : program.decls) {
        emitAstDecl(out, decl, 2);
    }
    return out.str();
}

std::string emitStatsJson(const SourceManager& sources,
                          const PreprocessStats& pp,
                          const ParserStats& parser,
                          const Diagnostics& diagnostics,
                          const Timings& timings) {
    std::ostringstream out;
    out << "{\n"
        << "  \"files\": " << sources.fileCount() << ",\n"
        << "  \"bytes\": " << sources.totalBytes() << ",\n"
        << "  \"lines\": " << sources.totalLines() << ",\n"
        << "  \"zincBlocks\": " << pp.zincBlocks << ",\n"
        << "  \"libraries\": " << parser.libraries << ",\n"
        << "  \"libraryOnce\": " << parser.libraryOnce << ",\n"
        << "  \"scopes\": " << parser.scopes << ",\n"
        << "  \"globalsBlocks\": " << parser.globalsBlocks << ",\n"
        << "  \"natives\": " << parser.natives << ",\n"
        << "  \"types\": " << parser.types << ",\n"
        << "  \"functions\": " << parser.functions << ",\n"
        << "  \"modules\": " << parser.modules << ",\n"
        << "  \"moduleUses\": " << parser.moduleUses << ",\n"
        << "  \"staticIfs\": " << parser.staticIfs << ",\n"
        << "  \"staticIfResolvedTrue\": " << parser.staticIfResolvedTrue << ",\n"
        << "  \"staticIfResolvedFalse\": " << parser.staticIfResolvedFalse << ",\n"
        << "  \"staticIfPrunedLines\": " << parser.staticIfPrunedLines << ",\n"
        << "  \"moduleExpansions\": " << parser.moduleExpansions << ",\n"
        << "  \"structsUnsupported\": " << parser.structsUnsupported << ",\n"
        << "  \"methodsUnsupported\": " << parser.methodsUnsupported << ",\n"
        << "  \"modulesUnsupported\": " << parser.modulesUnsupported << ",\n"
        << "  \"staticIfUnsupported\": " << parser.staticIfUnsupported << ",\n"
        << "  \"functionInterfacesUnsupported\": " << parser.functionInterfacesUnsupported << ",\n"
        << "  \"textmacros\": " << pp.textmacros << ",\n"
        << "  \"runtextmacros\": " << pp.runtextmacros << ",\n"
        << "  \"diagnostics\": {\n"
        << "    \"errors\": " << diagnostics.errorCount() << ",\n"
        << "    \"warnings\": " << diagnostics.warningCount() << ",\n"
        << "    \"unsupported\": " << diagnostics.unsupportedCount() << "\n"
        << "  },\n"
        << "  \"timingMs\": {\n"
        << "    \"read\": " << timings.read << ",\n"
        << "    \"preprocess\": " << timings.preprocess << ",\n"
        << "    \"staticIf\": " << timings.staticIf << ",\n"
        << "    \"lex\": " << timings.lex << ",\n"
        << "    \"parse\": " << timings.parse << ",\n"
        << "    \"moduleExpand\": " << timings.moduleExpand << ",\n"
        << "    \"total\": " << timings.total << "\n"
        << "  }\n"
        << "}\n";
    return out.str();
}

} // namespace

int main(int argc, char** argv) {
    auto parsed = parseCli(argc, argv);
    if (!parsed.ok) {
        std::cerr << parsed.error << '\n';
        printHelp(std::cerr);
        return 1;
    }
    const CliOptions& options = parsed.options;
    if (options.showHelp) {
        printHelp(std::cout);
        return 0;
    }
    if (options.showVersion) {
        printVersion(std::cout);
        return 0;
    }

    auto totalStart = std::chrono::steady_clock::now();
    SourceManager sources;
    Diagnostics diagnostics;

    Preprocessor preprocessor(sources, diagnostics, PreprocessOptions{options.debugMode, options.importPaths});
    auto ppStart = std::chrono::steady_clock::now();
    PreprocessResult preprocessed = preprocessor.run(options.inputPath);
    auto ppEnd = std::chrono::steady_clock::now();

    StaticIfSymbolCollector staticIfCollector;
    StaticIfPruner staticIfPruner;
    auto staticIfStart = std::chrono::steady_clock::now();
    StaticIfSymbols staticIfSymbols = staticIfCollector.collect(preprocessed.lines, options.debugMode, diagnostics);
    StaticIfPruneResult pruned = staticIfPruner.prune(preprocessed.lines, staticIfSymbols, diagnostics);
    auto staticIfEnd = std::chrono::steady_clock::now();

    Lexer lexer(diagnostics);
    auto lexStart = std::chrono::steady_clock::now();
    LexerResult lexed = lexer.lex(pruned.lines);
    auto lexEnd = std::chrono::steady_clock::now();

    Parser parser(diagnostics);
    auto parseStart = std::chrono::steady_clock::now();
    Program program = parser.parse(pruned.lines);
    auto parseEnd = std::chrono::steady_clock::now();

    program.stats.staticIfs = pruned.stats.staticIfs;
    program.stats.staticIfResolvedTrue = pruned.stats.resolvedTrue;
    program.stats.staticIfResolvedFalse = pruned.stats.resolvedFalse;
    program.stats.staticIfPrunedLines = pruned.stats.prunedLines;

    ModuleExpansionStats moduleStats;
    ModuleExpander moduleExpander;
    auto moduleStart = std::chrono::steady_clock::now();
    Program expandedProgram = moduleExpander.expand(program, diagnostics, moduleStats);
    auto moduleEnd = std::chrono::steady_clock::now();
    expandedProgram.stats.staticIfs = program.stats.staticIfs;
    expandedProgram.stats.staticIfResolvedTrue = program.stats.staticIfResolvedTrue;
    expandedProgram.stats.staticIfResolvedFalse = program.stats.staticIfResolvedFalse;
    expandedProgram.stats.staticIfPrunedLines = program.stats.staticIfPrunedLines;

    Timings timings;
    timings.read = sources.readElapsedMs();
    timings.preprocess = elapsedMs(ppStart, ppEnd);
    timings.staticIf = elapsedMs(staticIfStart, staticIfEnd);
    timings.lex = elapsedMs(lexStart, lexEnd);
    timings.parse = elapsedMs(parseStart, parseEnd);
    timings.moduleExpand = elapsedMs(moduleStart, moduleEnd);

    bool ok = true;
    if (!options.emitPreprocessedPath.empty()) {
        ok = writeTextFile(options.emitPreprocessedPath, emitPreprocessed(pruned.lines)) && ok;
    }
    if (!options.emitTokensPath.empty()) {
        ok = writeTextFile(options.emitTokensPath, emitTokens(sources, lexed.tokens)) && ok;
    }
    if (!options.emitAstPath.empty()) {
        ok = writeTextFile(options.emitAstPath, emitAst(program)) && ok;
    }
    if (!options.emitExpandedAstPath.empty()) {
        ok = writeTextFile(options.emitExpandedAstPath, emitAst(expandedProgram)) && ok;
    }

    CodegenResult codegen;
    if (!options.scanOnly && !options.outputPath.empty()) {
        Phase1Codegen generator(diagnostics, CodegenOptions{options.scanOnly, options.allowUnsupported});
        codegen = generator.generate(expandedProgram);
        ok = codegen.ok && ok;
        if (codegen.ok) {
            ok = writeTextFile(options.outputPath, codegen.output) && ok;
        }
    }

    timings.total = elapsedMs(totalStart, std::chrono::steady_clock::now());
    if (!options.emitStatsPath.empty()) {
        ok = writeTextFile(options.emitStatsPath, emitStatsJson(sources, preprocessed.stats, expandedProgram.stats, diagnostics, timings)) && ok;
    }

    if (!diagnostics.all().empty()) {
        diagnostics.print(sources, std::cerr);
    }

    if (!ok) {
        return expandedProgram.hasUnsupported() ? 6 : 5;
    }
    if (diagnostics.hasErrors()) {
        return 4;
    }
    if (expandedProgram.hasUnsupported() && !options.allowUnsupported && !options.scanOnly) {
        return 6;
    }
    return 0;
}
