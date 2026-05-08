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
#include "util/PathUtil.h"
#include "validation/OutputAnalysis.h"
#include "validation/PjassRunner.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <unordered_map>
#include <vector>

using namespace vjassc;

namespace {

struct Timings {
    long long read = 0;
    long long preprocess = 0;
    long long staticIf = 0;
    long long lex = 0;
    long long parse = 0;
    long long moduleExpand = 0;
    long long codegen = 0;
    long long syntaxLite = 0;
    long long pjass = 0;
    long long comparison = 0;
    long long total = 0;
    std::unordered_map<std::string, long long> codegenPasses;
    CodegenPerformanceCounters performanceCounters;
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

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool isWordPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

std::string stripStringRawcodeAndComment(const std::string& line) {
    std::string out;
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (!inString && !inRaw && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
            break;
        }
        char c = line[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            out.push_back(' ');
            continue;
        }
        if (inRaw) {
            if (c == '\'') {
                inRaw = false;
            }
            out.push_back(' ');
            continue;
        }
        if (c == '"') {
            inString = true;
            out.push_back(' ');
            continue;
        }
        if (c == '\'') {
            inRaw = true;
            out.push_back(' ');
            continue;
        }
        out.push_back(c);
    }
    return out;
}

bool containsWord(const std::string& text, const std::string& word) {
    size_t pos = 0;
    while ((pos = text.find(word, pos)) != std::string::npos) {
        char before = pos > 0 ? text[pos - 1] : '\0';
        char after = pos + word.size() < text.size() ? text[pos + word.size()] : '\0';
        if (!isWordPart(before) && !isWordPart(after)) {
            return true;
        }
        pos += word.size();
    }
    return false;
}

bool containsAnonymousFunctionSyntax(const std::string& text) {
    size_t pos = 0;
    while ((pos = text.find("function", pos)) != std::string::npos) {
        char before = pos > 0 ? text[pos - 1] : '\0';
        char after = pos + 8 < text.size() ? text[pos + 8] : '\0';
        if (!isWordPart(before) && !isWordPart(after)) {
            size_t p = pos + 8;
            while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) {
                ++p;
            }
            if (p < text.size() && text[p] == '(') {
                return true;
            }
        }
        pos += 8;
    }
    return false;
}

std::vector<std::string> checkOutputSyntaxLite(const std::string& output) {
    std::vector<std::string> errors;
    std::istringstream in(output);
    std::string line;
    bool sawGlobals = false;
    bool sawEndglobals = false;
    bool sawMain = false;
    bool inFunction = false;
    bool sawExecutableInFunction = false;
    size_t lineNo = 0;
    const std::vector<std::string> forbiddenWords = {
        "library", "endlibrary", "scope", "endscope", "struct", "endstruct",
        "method", "endmethod", "module", "endmodule", "implement"
    };
    while (std::getline(in, line)) {
        ++lineNo;
        std::string rawTrim = trim(line);
        if (rawTrim.find("//! zinc") != std::string::npos || rawTrim.find("//! endzinc") != std::string::npos) {
            errors.push_back("line " + std::to_string(lineNo) + ": residual zinc marker");
        }
        std::string code = stripStringRawcodeAndComment(line);
        std::string t = trim(code);
        if (t.empty()) {
            continue;
        }
        if (t == "globals") {
            sawGlobals = true;
        } else if (t == "endglobals") {
            sawEndglobals = true;
        }
        if (startsWithWord(t, "function main")) {
            sawMain = true;
        }
        for (const auto& word : forbiddenWords) {
            if (containsWord(t, word)) {
                errors.push_back("line " + std::to_string(lineNo) + ": residual '" + word + "'");
                break;
            }
        }
        if (t.find("static if") != std::string::npos) {
            errors.push_back("line " + std::to_string(lineNo) + ": residual static if");
        }
        if (t.find("function interface") != std::string::npos) {
            errors.push_back("line " + std::to_string(lineNo) + ": residual function interface");
        }
        if (containsAnonymousFunctionSyntax(t)) {
            errors.push_back("line " + std::to_string(lineNo) + ": residual anonymous function syntax");
        }
        if (t.find("->") != std::string::npos) {
            errors.push_back("line " + std::to_string(lineNo) + ": residual Zinc return arrow");
        }
        if (startsWithWord(t, "function")) {
            inFunction = true;
            sawExecutableInFunction = false;
            if (startsWithWord(t, "function takes") || t.find(" takes ") == std::string::npos) {
                errors.push_back("line " + std::to_string(lineNo) + ": invalid function header");
            }
            continue;
        }
        if (startsWithWord(t, "endfunction")) {
            inFunction = false;
            sawExecutableInFunction = false;
            continue;
        }
        if (inFunction) {
            if (startsWithWord(t, "local")) {
                if (sawExecutableInFunction) {
                    errors.push_back("line " + std::to_string(lineNo) + ": local declaration after executable statement");
                }
            } else {
                sawExecutableInFunction = true;
            }
        }
    }
    if (!sawGlobals || !sawEndglobals) {
        errors.push_back("missing globals/endglobals block");
    }
    if (!sawMain) {
        errors.push_back("missing function main");
    }
    return errors;
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
    if (decl.kind == DeclKind::FunctionInterface) {
        out << pad << "  - interface: " << decl.name
            << ", returns: " << decl.interfaceReturnType.name
            << ", params: " << decl.interfaceParams.size() << '\n';
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

void writeCodegenPassesJson(std::ostream& out, const std::unordered_map<std::string, long long>& timings, int indent);

void writePerformanceCountersJson(std::ostream& out, const CodegenPerformanceCounters& counters, int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    out << "{\n"
        << pad << "  \"linesVisited\": " << counters.linesVisited << ",\n"
        << pad << "  \"regexCalls\": " << counters.regexCalls << ",\n"
        << pad << "  \"memberAccessScans\": " << counters.memberAccessScans << ",\n"
        << pad << "  \"structLookupCalls\": " << counters.structLookupCalls << ",\n"
        << pad << "  \"functionLookupCalls\": " << counters.functionLookupCalls << ",\n"
        << pad << "  \"cachedRewriteHits\": " << counters.cachedRewriteHits << ",\n"
        << pad << "  \"cachedRewriteMisses\": " << counters.cachedRewriteMisses << "\n"
        << pad << "}";
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
        << "  \"functionInterfaces\": " << parser.functionInterfaces << ",\n"
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
        << "  \"functionInterfaceTargets\": " << parser.functionInterfaceTargets << ",\n"
        << "  \"functionInterfaceCalls\": " << parser.functionInterfaceCalls << ",\n"
        << "  \"functionObjectCalls\": " << parser.functionObjectCalls << ",\n"
        << "  \"lambdas\": " << parser.lambdas << ",\n"
        << "  \"lambdasLowered\": " << parser.lambdasLowered << ",\n"
        << "  \"lambdasCodeContext\": " << parser.lambdasCodeContext << ",\n"
        << "  \"lambdasBoolexprContext\": " << parser.lambdasBoolexprContext << ",\n"
        << "  \"lambdasFunctionInterfaceContext\": " << parser.lambdasFunctionInterfaceContext << ",\n"
        << "  \"lambdasNativeCallbackContext\": " << parser.lambdasNativeCallbackContext << ",\n"
        << "  \"lambdasMethodCallbackContext\": " << parser.lambdasMethodCallbackContext << ",\n"
        << "  \"lambdasUnknownContext\": " << parser.lambdasUnknownContext << ",\n"
        << "  \"lambdasCapturing\": " << parser.lambdasCapturing << ",\n"
        << "  \"lambdasRejected\": " << parser.lambdasRejected << ",\n"
        << "  \"lambdasGeneratedFunctions\": " << parser.lambdasGeneratedFunctions << ",\n"
        << "  \"lambdasCapturingUnsupported\": " << parser.lambdasCapturingUnsupported << ",\n"
        << "  \"functionInterfaceMaxEvaluateDepth\": " << parser.functionInterfaceMaxEvaluateDepth << ",\n"
        << "  \"functionInterfaceEvaluateTempLimit\": " << parser.functionInterfaceEvaluateTempLimit << ",\n"
        << "  \"prototypeWrappers\": " << parser.prototypeWrappers << ",\n"
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
        << "    \"codegen\": " << timings.codegen << ",\n"
        << "    \"syntaxLite\": " << timings.syntaxLite << ",\n"
        << "    \"pjass\": " << timings.pjass << ",\n"
        << "    \"comparison\": " << timings.comparison << ",\n"
        << "    \"total\": " << timings.total << "\n"
        << "  },\n"
        << "  \"codegenPasses\": ";
    writeCodegenPassesJson(out, timings.codegenPasses, 2);
    out << ",\n"
        << "  \"performanceCounters\": ";
    writePerformanceCountersJson(out, timings.performanceCounters, 2);
    out << "\n"
        << "}\n";
    return out.str();
}

void writeMetricsJson(std::ostream& out, const OutputMetrics& metrics, int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    std::string pad2(static_cast<size_t>(indent + 2), ' ');
    out << "{\n"
        << pad2 << "\"bytes\": " << metrics.bytes << ",\n"
        << pad2 << "\"lines\": " << metrics.lines << ",\n"
        << pad2 << "\"globalsBlocks\": " << metrics.globalsBlocks << ",\n"
        << pad2 << "\"globalDeclarations\": " << metrics.globalsDeclarations << ",\n"
        << pad2 << "\"natives\": " << metrics.natives << ",\n"
        << pad2 << "\"types\": " << metrics.types << ",\n"
        << pad2 << "\"functions\": " << metrics.functions << ",\n"
        << pad2 << "\"generatedLambdaFunctions\": " << metrics.generatedLambdaFunctions << ",\n"
        << pad2 << "\"structSupportFunctions\": " << metrics.structSupportFunctions << ",\n"
        << pad2 << "\"functionInterfaceWrappers\": " << metrics.functionInterfaceWrappers << ",\n"
        << pad2 << "\"sourceFormResidues\": " << metrics.sourceFormResidues << ",\n"
        << pad2 << "\"duplicateFunctionNames\": " << metrics.duplicateFunctionNames << ",\n"
        << pad2 << "\"duplicateGlobalNames\": " << metrics.duplicateGlobalNames << ",\n"
        << pad2 << "\"duplicateNativeNames\": " << metrics.duplicateNativeNames << ",\n"
        << pad2 << "\"hasMain\": " << (metrics.hasMain ? "true" : "false") << ",\n"
        << pad2 << "\"hasConfig\": " << (metrics.hasConfig ? "true" : "false") << "\n"
        << pad << "}";
}

void writeStringArrayJson(std::ostream& out, const std::vector<std::string>& values, int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    out << "[";
    if (!values.empty()) {
        out << "\n";
        for (size_t i = 0; i < values.size(); ++i) {
            out << pad << "  ";
            writeJsonString(out, values[i]);
            out << (i + 1 == values.size() ? "\n" : ",\n");
        }
        out << pad;
    }
    out << "]";
}

void writeIssueArrayJson(std::ostream& out, const std::vector<ValidationIssue>& issues, int indent, size_t limit) {
    std::string pad(static_cast<size_t>(indent), ' ');
    out << "[";
    if (!issues.empty()) {
        out << "\n";
        size_t count = std::min(limit, issues.size());
        for (size_t i = 0; i < count; ++i) {
            const auto& issue = issues[i];
            out << pad << "  {\n"
                << pad << "    \"check\": ";
            writeJsonString(out, issue.check);
            out << ",\n" << pad << "    \"line\": " << issue.line << ",\n"
                << pad << "    \"message\": ";
            writeJsonString(out, issue.message);
            out << ",\n" << pad << "    \"snippet\": ";
            writeJsonString(out, issue.snippet);
            out << "\n" << pad << "  }" << (i + 1 == count ? "\n" : ",\n");
        }
        out << pad;
    }
    out << "]";
}

void writePjassSummaryJson(std::ostream& out, const std::unordered_map<std::string, size_t>& summary, int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    out << "{";
    if (!summary.empty()) {
        out << "\n";
        size_t i = 0;
        for (const auto& [key, value] : summary) {
            out << pad << "  ";
            writeJsonString(out, key);
            out << ": " << value << (++i == summary.size() ? "\n" : ",\n");
        }
        out << pad;
    }
    out << "}";
}

void writeCodegenPassesJson(std::ostream& out, const std::unordered_map<std::string, long long>& timings, int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    std::vector<std::string> keys;
    keys.reserve(timings.size());
    for (const auto& [key, _] : timings) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    out << "{";
    if (!keys.empty()) {
        out << "\n";
        for (size_t i = 0; i < keys.size(); ++i) {
            out << pad << "  ";
            writeJsonString(out, keys[i]);
            out << ": " << timings.at(keys[i]) << (i + 1 == keys.size() ? "\n" : ",\n");
        }
        out << pad;
    }
    out << "}";
}

void writePjassGroupsJson(std::ostream& out, const std::vector<PjassErrorGroup>& groups, int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    out << "[";
    if (!groups.empty()) {
        out << "\n";
        for (size_t i = 0; i < groups.size(); ++i) {
            const auto& group = groups[i];
            out << pad << "  {\n"
                << pad << "    \"kind\": ";
            writeJsonString(out, group.kind);
            out << ",\n" << pad << "    \"count\": " << group.count << ",\n"
                << pad << "    \"firstLine\": " << group.firstLine << ",\n"
                << pad << "    \"firstMessage\": ";
            writeJsonString(out, group.firstMessage);
            out << ",\n" << pad << "    \"examples\": [";
            if (!group.examples.empty()) {
                out << "\n";
                for (size_t j = 0; j < group.examples.size(); ++j) {
                    const auto& example = group.examples[j];
                    out << pad << "      {\n"
                        << pad << "        \"generatedLine\": " << example.generatedLine << ",\n"
                        << pad << "        \"function\": ";
                    writeJsonString(out, example.functionName);
                    out << ",\n"
                        << pad << "        \"text\": ";
                    writeJsonString(out, example.generatedText);
                    out << ",\n"
                        << pad << "        \"message\": ";
                    writeJsonString(out, example.message);
                    out << ",\n" << pad << "        \"excerpt\": ";
                    writeStringArrayJson(out, example.excerpt, indent + 8);
                    out << "\n" << pad << "      }" << (j + 1 == group.examples.size() ? "\n" : ",\n");
                }
                out << pad << "    ";
            }
            out << "]\n" << pad << "  }" << (i + 1 == groups.size() ? "\n" : ",\n");
        }
        out << pad;
    }
    out << "]";
}

size_t pjassGroupedCount(const std::vector<PjassErrorGroup>& groups) {
    size_t total = 0;
    for (const auto& group : groups) {
        total += group.count;
    }
    return total;
}

std::string extractPjassSymbolForReport(const std::string& message) {
    static const std::vector<std::regex> patterns = {
        std::regex(R"((?:Undefined|Undeclared|undeclared) variable ([A-Za-z_$][A-Za-z0-9_$]*))"),
        std::regex(R"((?:Undefined|Undeclared|undeclared) function ([A-Za-z_$][A-Za-z0-9_$]*))"),
    };
    for (const auto& pattern : patterns) {
        std::smatch match;
        if (std::regex_search(message, match, pattern)) {
            return match[1].str();
        }
    }
    return {};
}

std::string provenanceBucketForSymbol(const std::string& symbol, const std::string& kind) {
    if (symbol.rfind("yd_", 0) == 0 || symbol == "YDHT") {
        return "EnvironmentProvidedByYDWE";
    }
    if (symbol.rfind("bj_", 0) == 0) {
        return "CommonBlizzardProvided";
    }
    if (symbol.rfind("vjassc__", 0) == 0 || symbol.rfind("vjlambda__", 0) == 0 ||
        symbol.rfind("s__", 0) == 0 || symbol.rfind("sc__", 0) == 0 || symbol.rfind("si__", 0) == 0) {
        return "GeneratedHelperMiss";
    }
    if (symbol.rfind("HASH_", 0) == 0 || symbol.rfind("KEY_", 0) == 0 || kind == "unresolvedPublicGlobal") {
        return "PrivatePublicRewriteMiss";
    }
    return "TypoOrUnsupported";
}

struct SymbolProvenanceEntry {
    size_t count = 0;
    std::string bucket;
    std::string action;
    std::vector<PjassErrorExample> examples;
};

std::map<std::string, SymbolProvenanceEntry> collectPjassSymbolProvenance(const PjassResult& pjass, bool environmentOnly) {
    std::map<std::string, SymbolProvenanceEntry> result;
    for (const auto& group : pjass.errorGroups) {
        if (group.kind != "undefinedVariable" && group.kind != "unresolvedEnvironmentSymbol" &&
            group.kind != "undefinedFunction" && group.kind != "unresolvedPublicGlobal") {
            continue;
        }
        for (const auto& example : group.examples) {
            std::string symbol = extractPjassSymbolForReport(example.message);
            if (symbol.empty()) {
                continue;
            }
            std::string bucket = provenanceBucketForSymbol(symbol, group.kind);
            bool isEnvironment = bucket == "EnvironmentProvidedByYDWE" || bucket == "CommonBlizzardProvided";
            if (environmentOnly != isEnvironment) {
                continue;
            }
            auto& entry = result[symbol];
            entry.bucket = bucket;
            entry.action = isEnvironment ? "requires environment policy" : "compiler/source investigation required";
            if (entry.examples.size() < 10) {
                entry.examples.push_back(example);
            }
        }
    }

    std::string merged = pjass.stdoutText + "\n" + pjass.stderrText;
    std::istringstream in(merged);
    std::string line;
    while (std::getline(in, line)) {
        std::string symbol = extractPjassSymbolForReport(line);
        if (symbol.empty()) {
            continue;
        }
        std::string bucket = provenanceBucketForSymbol(symbol, {});
        bool isEnvironment = bucket == "EnvironmentProvidedByYDWE" || bucket == "CommonBlizzardProvided";
        if (environmentOnly != isEnvironment) {
            continue;
        }
        auto& entry = result[symbol];
        ++entry.count;
        if (entry.bucket.empty()) {
            entry.bucket = bucket;
        }
        if (entry.action.empty()) {
            entry.action = isEnvironment ? "requires environment policy" : "compiler/source investigation required";
        }
    }
    return result;
}

void writePjassProvenanceEntriesJson(std::ostream& out,
                                     const std::map<std::string, SymbolProvenanceEntry>& entries,
                                     int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    out << "{";
    if (!entries.empty()) {
        out << "\n";
        size_t i = 0;
        for (const auto& [symbol, entry] : entries) {
            out << pad << "  ";
            writeJsonString(out, symbol);
            out << ": {\n"
                << pad << "    \"count\": " << entry.count << ",\n"
                << pad << "    \"bucket\": ";
            writeJsonString(out, entry.bucket);
            out << ",\n" << pad << "    \"action\": ";
            writeJsonString(out, entry.action);
            out << ",\n" << pad << "    \"examples\": [";
            if (!entry.examples.empty()) {
                out << "\n";
                for (size_t j = 0; j < entry.examples.size(); ++j) {
                    const auto& example = entry.examples[j];
                    out << pad << "      {\n"
                        << pad << "        \"generatedLine\": " << example.generatedLine << ",\n"
                        << pad << "        \"function\": ";
                    writeJsonString(out, example.functionName);
                    out << ",\n" << pad << "        \"message\": ";
                    writeJsonString(out, example.message);
                    out << "\n" << pad << "      }" << (j + 1 == entry.examples.size() ? "\n" : ",\n");
                }
                out << pad << "    ";
            }
            out << "]\n" << pad << "  }" << (++i == entries.size() ? "\n" : ",\n");
        }
        out << pad;
    }
    out << "}";
}

void writePjassProvenanceJson(std::ostream& out, const PjassResult& pjass, int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    out << "{\n"
        << pad << "  \"undefinedVariables\": ";
    writePjassProvenanceEntriesJson(out, collectPjassSymbolProvenance(pjass, false), indent + 2);
    out << ",\n" << pad << "  \"environmentSymbols\": ";
    writePjassProvenanceEntriesJson(out, collectPjassSymbolProvenance(pjass, true), indent + 2);
    out << "\n" << pad << "}";
}

size_t countPjassGroup(const PjassResult& pjass, const std::string& kind) {
    for (const auto& group : pjass.errorGroups) {
        if (group.kind == kind) {
            return group.count;
        }
    }
    return 0;
}

void writeCallbackAdaptersJson(std::ostream& out, const PjassResult& pjass, int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    size_t rejected = countPjassGroup(pjass, "callbackCodeSignatureMismatch");
    out << "{\n"
        << pad << "  \"generated\": 0,\n"
        << pad << "  \"rejected\": " << rejected << ",\n"
        << pad << "  \"unknownContext\": " << rejected << "\n"
        << pad << "}";
}

void writeForwardCyclesJson(std::ostream& out, const OutputSyntaxReport& syntax, int indent) {
    writeIssueArrayJson(out, syntax.trueFunctionCycles, indent, 100);
}

bool isCompilerGeneratedFunctionName(std::string_view name) {
    return name.rfind("vjassc__", 0) == 0 || name.rfind("vjlambda__", 0) == 0 ||
           name.find("__wrapper") != std::string_view::npos;
}

std::string classifyPhase12TriageBucket(const PjassErrorGroup& group, const PjassErrorExample& example) {
    if (group.kind == "returnMissingValue") {
        return isCompilerGeneratedFunctionName(example.functionName) ? "returnMissingValue.generatedWrapper"
                                                                     : "returnMissingValue.sourceFunction";
    }
    if (group.kind == "callbackCodeSignatureMismatch") {
        if (example.generatedText.find("TriggerAddCondition") != std::string::npos ||
            example.generatedText.find("Condition(") != std::string::npos ||
            example.generatedText.find("Filter(") != std::string::npos) {
            return "callbackCodeSignatureMismatch.unknownContext";
        }
        return "callbackCodeSignatureMismatch.unknownContext";
    }
    if (group.kind == "other" && example.message.find("Call expected instead of set") != std::string::npos) {
        return "statementShape.callExpectedInsteadOfSet";
    }
    if (group.kind == "undefinedVariable" || group.kind == "unresolvedKnownSourceSymbol") {
        std::string symbol = extractPjassSymbolForReport(example.message);
        std::string bucket = provenanceBucketForSymbol(symbol, group.kind);
        if (bucket == "EnvironmentProvidedByYDWE" || bucket == "CommonBlizzardProvided") {
            return "undefinedVariable.environment";
        }
        if (bucket == "PrivatePublicRewriteMiss" || bucket == "GeneratedHelperMiss" ||
            group.kind == "unresolvedKnownSourceSymbol") {
            return "undefinedVariable.rewriteMiss";
        }
        return "undefinedVariable.sourceMissing";
    }
    if (group.kind == "forwardFunctionReference" || group.kind == "forwardLambdaReference") {
        return example.generatedText.rfind("    call ", 0) == 0 ? "forwardFunctionReference.safeExecuteFunc"
                                                                : "forwardFunctionReference.needsDispatcher";
    }
    if (group.kind == "undefinedFunction") {
        return "undefinedFunction.trueMissing";
    }
    return group.kind;
}

std::string probableOriginForPhase12Bucket(const std::string& bucket) {
    if (bucket == "returnMissingValue.generatedWrapper") {
        return "compiler-generated function requires a safe default return";
    }
    if (bucket == "returnMissingValue.sourceFunction") {
        return "source function or source lowering path needs provenance review";
    }
    if (bucket == "callbackCodeSignatureMismatch.unknownContext") {
        return "raw code callback has no proven argument source";
    }
    if (bucket == "statementShape.callExpectedInsteadOfSet") {
        return "Zinc statement lowering emitted set for a discarded call";
    }
    if (bucket == "undefinedVariable.rewriteMiss") {
        return "known source symbol or generated-name rewrite was missed";
    }
    if (bucket == "undefinedVariable.environment") {
        return "symbol belongs to validation environment";
    }
    if (bucket == "undefinedVariable.sourceMissing") {
        return "source symbol provenance is still unresolved";
    }
    if (bucket == "forwardFunctionReference.safeExecuteFunc") {
        return "callee is declared after caller and needs signature-aware bridging";
    }
    if (bucket == "forwardFunctionReference.needsDispatcher") {
        return "callee is declared after caller but cannot use a trivial bridge";
    }
    if (bucket == "undefinedFunction.trueMissing") {
        return "function is external or missing from generated output";
    }
    return "unclassified PJASS issue";
}

std::string suggestedFixForPhase12Bucket(const std::string& bucket) {
    if (bucket == "returnMissingValue.generatedWrapper") {
        return "append generated default return";
    }
    if (bucket == "returnMissingValue.sourceFunction") {
        return "classify source policy or repair lowering that dropped source returns";
    }
    if (bucket == "callbackCodeSignatureMismatch.unknownContext") {
        return "do not adapt until callback argument provenance is known";
    }
    if (bucket == "statementShape.callExpectedInsteadOfSet") {
        return "fix statement lowering";
    }
    if (bucket == "undefinedVariable.rewriteMiss") {
        return "fix symbol rewrite";
    }
    if (bucket == "undefinedVariable.environment") {
        return "document environment policy";
    }
    if (bucket == "forwardFunctionReference.safeExecuteFunc") {
        return "verify signature before ExecuteFunc bridge";
    }
    if (bucket == "forwardFunctionReference.needsDispatcher") {
        return "preserve as dispatcher/source limitation until safe";
    }
    if (bucket == "undefinedFunction.trueMissing") {
        return "classify external map initializer or missing source declaration";
    }
    return "inspect manually";
}

void writePhase12TriageJson(std::ostream& out, const PjassResult& pjass, int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    const std::vector<std::string> requiredBuckets = {
        "returnMissingValue.generatedWrapper",
        "returnMissingValue.sourceFunction",
        "callbackCodeSignatureMismatch.knownContext",
        "callbackCodeSignatureMismatch.unknownContext",
        "statementShape.callExpectedInsteadOfSet",
        "undefinedVariable.sourceMissing",
        "undefinedVariable.rewriteMiss",
        "undefinedVariable.environment",
        "undefinedFunction.trueMissing",
        "undefinedFunction.forwardCycle",
        "forwardFunctionReference.safeExecuteFunc",
        "forwardFunctionReference.needsDispatcher",
    };
    std::map<std::string, std::vector<PjassErrorExample>> examplesByBucket;
    for (const auto& bucket : requiredBuckets) {
        examplesByBucket[bucket];
    }
    for (const auto& group : pjass.errorGroups) {
        for (const auto& example : group.examples) {
            std::string bucket = classifyPhase12TriageBucket(group, example);
            auto& examples = examplesByBucket[bucket];
            if (examples.size() < 20) {
                examples.push_back(example);
            }
        }
    }

    out << "{\n" << pad << "  \"topGroups\": [";
    if (!pjass.errorGroups.empty()) {
        out << "\n";
        for (size_t i = 0; i < pjass.errorGroups.size(); ++i) {
            const auto& group = pjass.errorGroups[i];
            out << pad << "    {\"kind\": ";
            writeJsonString(out, group.kind);
            out << ", \"count\": " << group.count << "}" << (i + 1 == pjass.errorGroups.size() ? "\n" : ",\n");
        }
        out << pad << "  ";
    }
    out << "],\n" << pad << "  \"examplesByGroup\": {";
    if (!examplesByBucket.empty()) {
        out << "\n";
        size_t bucketIndex = 0;
        for (const auto& [bucket, examples] : examplesByBucket) {
            out << pad << "    ";
            writeJsonString(out, bucket);
            out << ": [";
            if (!examples.empty()) {
                out << "\n";
                for (size_t i = 0; i < examples.size(); ++i) {
                    const auto& example = examples[i];
                    bool generated = isCompilerGeneratedFunctionName(example.functionName);
                    out << pad << "      {\n"
                        << pad << "        \"sourceFunction\": ";
                    writeJsonString(out, generated ? "" : example.functionName);
                    out << ",\n" << pad << "        \"generatedFunction\": ";
                    writeJsonString(out, example.functionName);
                    out << ",\n" << pad << "        \"generatedLine\": " << example.generatedLine << ",\n"
                        << pad << "        \"generatedText\": ";
                    writeJsonString(out, example.generatedText);
                    out << ",\n" << pad << "        \"probableOrigin\": ";
                    writeJsonString(out, probableOriginForPhase12Bucket(bucket));
                    out << ",\n" << pad << "        \"isCompilerGenerated\": " << (generated ? "true" : "false") << ",\n"
                        << pad << "        \"suggestedFixClass\": ";
                    writeJsonString(out, suggestedFixForPhase12Bucket(bucket));
                    out << ",\n" << pad << "        \"message\": ";
                    writeJsonString(out, example.message);
                    out << "\n" << pad << "      }" << (i + 1 == examples.size() ? "\n" : ",\n");
                }
                out << pad << "    ";
            }
            out << "]" << (++bucketIndex == examplesByBucket.size() ? "\n" : ",\n");
        }
        out << pad << "  ";
    }
    out << "}\n" << pad << "}";
}

void writePerformanceJson(std::ostream& out, const Timings& timings, int indent) {
    std::string pad(static_cast<size_t>(indent), ' ');
    std::vector<std::pair<std::string, long long>> hotspots(timings.codegenPasses.begin(), timings.codegenPasses.end());
    std::sort(hotspots.begin(), hotspots.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    out << "{\n"
        << pad << "  \"passes\": ";
    writeCodegenPassesJson(out, timings.codegenPasses, indent + 2);
    out << ",\n" << pad << "  \"counters\": ";
    writePerformanceCountersJson(out, timings.performanceCounters, indent + 2);
    out << ",\n" << pad << "  \"hotspots\": [";
    if (!hotspots.empty()) {
        out << "\n";
        size_t count = std::min<size_t>(10, hotspots.size());
        for (size_t i = 0; i < count; ++i) {
            out << pad << "    {\"name\": ";
            writeJsonString(out, hotspots[i].first);
            out << ", \"ms\": " << hotspots[i].second << "}" << (i + 1 == count ? "\n" : ",\n");
        }
        out << pad << "  ";
    }
    out << "]\n" << pad << "}";
}

std::string emitValidationReportJson(const CliOptions& options,
                                     const OutputSyntaxReport& syntax,
                                     const InitValidationReport& init,
                                     const PjassResult& pjass,
                                     const ComparisonReport& comparison,
                                     const Timings& timings) {
    std::ostringstream out;
    out << "{\n"
        << "  \"phase\": 13,\n"
        << "  \"input\": ";
    writeJsonString(out, pathToGenericString(options.inputPath));
    out << ",\n  \"output\": ";
    writeJsonString(out, pathToGenericString(options.outputPath));
    out << ",\n  \"syntaxLite\": {\n"
        << "    \"ran\": " << (syntax.ran ? "true" : "false") << ",\n"
        << "    \"ok\": " << (syntax.ok ? "true" : "false") << ",\n"
        << "    \"residualSourceForms\": ";
    writeStringArrayJson(out, syntax.residualSourceForms, 4);
    out << ",\n    \"issueCount\": " << syntax.issues.size() << ",\n"
        << "    \"issuesPreview\": ";
    writeIssueArrayJson(out, syntax.issues, 4, 20);
    out << ",\n    \"forwardFunctionReferences\": ";
    writeIssueArrayJson(out, syntax.forwardFunctionReferences, 4, 20);
    out << ",\n    \"forwardLambdaReferences\": ";
    writeIssueArrayJson(out, syntax.forwardLambdaReferences, 4, 20);
    out << ",\n    \"commaLocalResidues\": ";
    writeIssueArrayJson(out, syntax.commaLocalResidues, 4, 20);
    out << ",\n    \"indexedStructMemberResidues\": ";
    writeIssueArrayJson(out, syntax.indexedStructMemberResidues, 4, 20);
    out << ",\n    \"inlineZincControlResidues\": ";
    writeIssueArrayJson(out, syntax.inlineZincControlResidues, 4, 20);
    out << ",\n    \"methodChainCallResultResidues\": ";
    writeIssueArrayJson(out, syntax.methodChainCallResultResidues, 4, 20);
    out << ",\n    \"callbackCodeSignatureResidues\": ";
    writeIssueArrayJson(out, syntax.callbackCodeSignatureResidues, 4, 20);
    out << ",\n    \"unresolvedKnownSourceSymbols\": ";
    writeIssueArrayJson(out, syntax.unresolvedKnownSourceSymbols, 4, 20);
    out << ",\n    \"returnMismatchLikelyResidues\": ";
    writeIssueArrayJson(out, syntax.returnMismatchLikelyResidues, 4, 20);
    out << ",\n    \"trueFunctionCycles\": ";
    writeIssueArrayJson(out, syntax.trueFunctionCycles, 4, 20);
    out << ",\n    \"metrics\": ";
    writeMetricsJson(out, syntax.metrics, 4);
    out << "\n  },\n  \"init\": {\n"
        << "    \"hasMain\": " << (init.hasMain ? "true" : "false") << ",\n"
        << "    \"hasConfig\": " << (init.hasConfig ? "true" : "false") << ",\n"
        << "    \"hasStructInit\": " << (init.hasStructInit ? "true" : "false") << ",\n"
        << "    \"hasFunctionInterfaceInit\": " << (init.hasFunctionInterfaceInit ? "true" : "false") << ",\n"
        << "    \"hasLibraryInit\": " << (init.hasLibraryInit ? "true" : "false") << ",\n"
        << "    \"mainCallsStructInit\": " << (init.mainCallsStructInit ? "true" : "false") << ",\n"
        << "    \"mainCallsFunctionInterfaceInit\": " << (init.mainCallsFunctionInterfaceInit ? "true" : "false") << ",\n"
        << "    \"mainCallsLibraryInit\": " << (init.mainCallsLibraryInit ? "true" : "false") << ",\n"
        << "    \"structBeforeFunctionInterface\": " << (init.structBeforeFunctionInterface ? "true" : "false") << ",\n"
        << "    \"functionInterfaceBeforeLibrary\": " << (init.functionInterfaceBeforeLibrary ? "true" : "false") << ",\n"
        << "    \"libraryInitializerCount\": " << init.libraryInitializerCount << ",\n"
        << "    \"structInitializerCount\": " << init.structInitializerCount << ",\n"
        << "    \"issues\": ";
    writeStringArrayJson(out, init.issues, 4);
    out << "\n  },\n  \"pjass\": {\n"
        << "    \"requested\": " << (pjass.requested ? "true" : "false") << ",\n"
        << "    \"ran\": " << (pjass.ran ? "true" : "false") << ",\n"
        << "    \"ok\": " << (pjass.ok ? "true" : "false") << ",\n"
        << "    \"groupedCount\": " << pjassGroupedCount(pjass.errorGroups) << ",\n"
        << "    \"exitCode\": " << pjass.exitCode << ",\n"
        << "    \"elapsedMs\": " << pjass.elapsedMs << ",\n"
        << "    \"commandLine\": ";
    writeJsonString(out, pjass.commandLine);
    out << ",\n    \"stdoutPath\": ";
    writeJsonString(out, pathToGenericString(pjass.stdoutPath));
    out << ",\n    \"stderrPath\": ";
    writeJsonString(out, pathToGenericString(pjass.stderrPath));
    out << ",\n    \"validationEnvStubPath\": ";
    writeJsonString(out, pathToGenericString(pjass.validationEnvStubPath));
    out << ",\n    \"allowedExternalFunctions\": ";
    writeStringArrayJson(out, pjass.allowedExternalFunctions, 4);
    out << ",\n    \"stdoutPreview\": ";
    writeJsonString(out, previewText(pjass.stdoutText));
    out << ",\n    \"stderrPreview\": ";
    writeJsonString(out, previewText(pjass.stderrText));
    out << ",\n    \"error\": ";
    writeJsonString(out, pjass.error);
    out << ",\n    \"errorSummary\": ";
    writePjassSummaryJson(out, pjass.errorSummary, 4);
    out << ",\n    \"groups\": ";
    writePjassGroupsJson(out, pjass.errorGroups, 4);
    out << "\n  },\n  \"provenance\": ";
    writePjassProvenanceJson(out, pjass, 2);
    out << ",\n  \"callbackAdapters\": ";
    writeCallbackAdaptersJson(out, pjass, 2);
    out << ",\n  \"forwardCycles\": ";
    writeForwardCyclesJson(out, syntax, 2);
    out << ",\n  \"phase12Triage\": ";
    writePhase12TriageJson(out, pjass, 2);
    out << ",\n  \"performance\": ";
    writePerformanceJson(out, timings, 2);
    out << ",\n  \"comparison\": {\n"
        << "    \"jasshelperReference\": ";
    writeJsonString(out, pathToGenericString(comparison.referencePath));
    out << ",\n    \"referenceFound\": " << (comparison.referenceFound ? "true" : "false") << ",\n"
        << "    \"generated\": ";
    writeMetricsJson(out, comparison.generated, 4);
    out << ",\n    \"reference\": ";
    writeMetricsJson(out, comparison.reference, 4);
    long long functionDelta = static_cast<long long>(comparison.generated.functions) -
                              static_cast<long long>(comparison.reference.functions);
    long long lineDelta = static_cast<long long>(comparison.generated.lines) -
                          static_cast<long long>(comparison.reference.lines);
    out << ",\n    \"delta\": {\n"
        << "      \"functions\": " << functionDelta << ",\n"
        << "      \"lines\": " << lineDelta << "\n"
        << "    },\n"
        << "    \"notes\": ";
    writeStringArrayJson(out, comparison.notes, 4);
    out << "\n  },\n  \"timingMs\": {\n"
        << "    \"read\": " << timings.read << ",\n"
        << "    \"preprocess\": " << timings.preprocess << ",\n"
        << "    \"staticIf\": " << timings.staticIf << ",\n"
        << "    \"lex\": " << timings.lex << ",\n"
        << "    \"parse\": " << timings.parse << ",\n"
        << "    \"moduleExpand\": " << timings.moduleExpand << ",\n"
        << "    \"codegen\": " << timings.codegen << ",\n"
        << "    \"syntaxLite\": " << timings.syntaxLite << ",\n"
        << "    \"pjass\": " << timings.pjass << ",\n"
        << "    \"comparison\": " << timings.comparison << ",\n"
        << "    \"total\": " << timings.total << "\n"
        << "  },\n"
        << "  \"codegenPasses\": ";
    writeCodegenPassesJson(out, timings.codegenPasses, 2);
    out << ",\n  \"performanceCounters\": ";
    writePerformanceCountersJson(out, timings.performanceCounters, 2);
    out << "\n"
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

    if (!options.analyzePjassLogPath.empty()) {
        auto totalStart = std::chrono::steady_clock::now();
        PjassResult pjassResult;
        pjassResult.requested = true;
        pjassResult.ran = true;
        pjassResult.stdoutPath = options.analyzePjassLogPath;
        pjassResult.stdoutText = readTextFile(options.analyzePjassLogPath);
        pjassResult.errorGroups = groupPjassErrors(pjassResult.stdoutText, {}, options.emitPjassExamples);
        for (const auto& group : pjassResult.errorGroups) {
            pjassResult.errorSummary[group.kind] = group.count;
        }
        pjassResult.ok = pjassResult.errorGroups.empty();
        pjassResult.exitCode = pjassResult.ok ? 0 : 1;
        Timings timings;
        timings.total = elapsedMs(totalStart, std::chrono::steady_clock::now());
        CliOptions reportOptions = options;
        reportOptions.inputPath = options.analyzePjassLogPath;
        OutputSyntaxReport syntaxReport;
        InitValidationReport initReport;
        ComparisonReport comparisonReport;
        if (!options.emitValidationReportPath.empty()) {
            if (!writeTextFile(options.emitValidationReportPath,
                               emitValidationReportJson(reportOptions, syntaxReport, initReport, pjassResult, comparisonReport, timings))) {
                return 1;
            }
        }
        std::cout << "PJASS grouped errors: " << pjassGroupedCount(pjassResult.errorGroups) << "\n";
        for (const auto& group : pjassResult.errorGroups) {
            std::cout << group.kind << ": " << group.count << "\n";
        }
        return 0;
    }

    if (!options.validateExistingOutputPath.empty()) {
        auto totalStart = std::chrono::steady_clock::now();
        bool ok = true;
        std::string existingOutput = readTextFile(options.validateExistingOutputPath);
        CliOptions reportOptions = options;
        reportOptions.inputPath = options.validateExistingOutputPath;
        reportOptions.outputPath = options.validateExistingOutputPath;

        Timings timings;
        OutputSyntaxReport syntaxReport;
        InitValidationReport initReport;
        ComparisonReport comparisonReport;
        PjassResult pjassResult;

        auto syntaxStart = std::chrono::steady_clock::now();
        syntaxReport = analyzeOutputSyntaxLite(existingOutput);
        initReport = analyzeInitIntegrity(existingOutput);
        auto syntaxEnd = std::chrono::steady_clock::now();
        timings.syntaxLite = elapsedMs(syntaxStart, syntaxEnd);
        if (options.checkOutputSyntaxLite && !syntaxReport.ok) {
            ok = false;
        }

        if (!options.compareJasshelperPath.empty() || !options.emitValidationReportPath.empty()) {
            auto comparisonStart = std::chrono::steady_clock::now();
            comparisonReport = compareWithReference(existingOutput, options.compareJasshelperPath.empty()
                                                                        ? std::filesystem::path("samples/output_jasshelper.j")
                                                                        : options.compareJasshelperPath);
            auto comparisonEnd = std::chrono::steady_clock::now();
            timings.comparison = elapsedMs(comparisonStart, comparisonEnd);
        }

        if (options.validatePjass) {
            pjassResult.requested = true;
            PjassResolvedPaths paths = resolvePjassPaths(std::filesystem::current_path(),
                                                         options.pjassPath,
                                                         options.commonPath,
                                                         options.blizzardPath);
            if (!paths.ok) {
                pjassResult.error = paths.error;
                std::cerr << paths.error << '\n';
                ok = false;
            } else {
                std::filesystem::path reportBase = options.emitValidationReportPath.empty()
                    ? options.validateExistingOutputPath
                    : options.emitValidationReportPath;
                std::filesystem::path stdoutPath = reportBase;
                stdoutPath.replace_extension(".pjass.stdout.txt");
                std::filesystem::path stderrPath = reportBase;
                stderrPath.replace_extension(".pjass.stderr.txt");
                PjassOptions pjassOptions;
                pjassOptions.pjassPath = paths.pjassPath;
                pjassOptions.commonPath = paths.commonPath;
                pjassOptions.blizzardPath = paths.blizzardPath;
                pjassOptions.scriptPath = options.validateExistingOutputPath;
                pjassOptions.stdoutPath = stdoutPath;
                pjassOptions.stderrPath = stderrPath;
                pjassOptions.allowedExternalFunctions = options.pjassAllowedExternalFunctions;
                pjassOptions.timeoutMs = options.pjassTimeoutMs;
                pjassOptions.exampleLimit = options.emitPjassExamples;
                auto pjassStart = std::chrono::steady_clock::now();
                pjassResult = runPjass(pjassOptions);
                auto pjassEnd = std::chrono::steady_clock::now();
                timings.pjass = elapsedMs(pjassStart, pjassEnd);
                if (!pjassResult.ok) {
                    ok = false;
                }
            }
        }

        timings.total = elapsedMs(totalStart, std::chrono::steady_clock::now());
        if (!options.emitValidationReportPath.empty()) {
            ok = writeTextFile(options.emitValidationReportPath,
                               emitValidationReportJson(reportOptions, syntaxReport, initReport, pjassResult, comparisonReport, timings)) && ok;
        }
        return ok ? 0 : 5;
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
    OutputSyntaxReport syntaxReport;
    InitValidationReport initReport;
    ComparisonReport comparisonReport;
    PjassResult pjassResult;
    if (!options.scanOnly && !options.outputPath.empty()) {
        auto codegenStart = std::chrono::steady_clock::now();
        Phase1Codegen generator(diagnostics, CodegenOptions{options.scanOnly, options.allowUnsupported});
        codegen = generator.generate(expandedProgram);
        auto codegenEnd = std::chrono::steady_clock::now();
        timings.codegen = elapsedMs(codegenStart, codegenEnd);
        timings.codegenPasses = codegen.passTimings;
        timings.performanceCounters = codegen.performanceCounters;
        expandedProgram.stats.lambdasLowered = codegen.lambdasLowered;
        if (codegen.lambdasGeneratedFunctions > expandedProgram.stats.lambdas) {
            expandedProgram.stats.lambdas = codegen.lambdasGeneratedFunctions;
        }
        expandedProgram.stats.lambdasCodeContext = codegen.lambdasCodeContext;
        expandedProgram.stats.lambdasBoolexprContext = codegen.lambdasBoolexprContext;
        expandedProgram.stats.lambdasFunctionInterfaceContext = codegen.lambdasFunctionInterfaceContext;
        expandedProgram.stats.lambdasNativeCallbackContext = codegen.lambdasNativeCallbackContext;
        expandedProgram.stats.lambdasMethodCallbackContext = codegen.lambdasMethodCallbackContext;
        expandedProgram.stats.lambdasUnknownContext = codegen.lambdasUnknownContext;
        expandedProgram.stats.lambdasCapturing = codegen.lambdasCapturing;
        expandedProgram.stats.lambdasRejected = codegen.lambdasRejected;
        expandedProgram.stats.lambdasGeneratedFunctions = codegen.lambdasGeneratedFunctions;
        expandedProgram.stats.lambdasCapturingUnsupported = codegen.lambdasRejected;
        expandedProgram.stats.functionInterfaceTargets = codegen.functionInterfaceTargets;
        expandedProgram.stats.functionInterfaceCalls = codegen.functionInterfaceCalls;
        expandedProgram.stats.functionObjectCalls = codegen.functionObjectCalls;
        expandedProgram.stats.functionInterfaceMaxEvaluateDepth = codegen.functionInterfaceMaxEvaluateDepth;
        expandedProgram.stats.functionInterfaceEvaluateTempLimit = codegen.functionInterfaceEvaluateTempLimit;
        ok = codegen.ok && ok;
        if (codegen.ok) {
            ok = writeTextFile(options.outputPath, codegen.output) && ok;
            if (options.checkOutputSyntaxLite || !options.emitValidationReportPath.empty()) {
                auto syntaxStart = std::chrono::steady_clock::now();
                syntaxReport = analyzeOutputSyntaxLite(codegen.output);
                initReport = analyzeInitIntegrity(codegen.output);
                auto syntaxEnd = std::chrono::steady_clock::now();
                timings.syntaxLite = elapsedMs(syntaxStart, syntaxEnd);
                if (options.checkOutputSyntaxLite && !syntaxReport.ok) {
                    size_t shown = 0;
                    for (const auto& issue : syntaxReport.issues) {
                        if (shown++ >= 20) {
                            break;
                        }
                        diagnostics.error(SourceLocation{}, "output syntax lite check failed: " +
                                                              issue.check + ": " + issue.message +
                                                              (issue.line == 0 ? "" : " at line " + std::to_string(issue.line)));
                    }
                    ok = false;
                }
            }
            if (!options.compareJasshelperPath.empty() || !options.emitValidationReportPath.empty()) {
                auto comparisonStart = std::chrono::steady_clock::now();
                std::filesystem::path reference = options.compareJasshelperPath.empty()
                    ? std::filesystem::path("samples/output_jasshelper.j")
                    : options.compareJasshelperPath;
                comparisonReport = compareWithReference(codegen.output, reference);
                auto comparisonEnd = std::chrono::steady_clock::now();
                timings.comparison = elapsedMs(comparisonStart, comparisonEnd);
            }
            if (options.validatePjass) {
                pjassResult.requested = true;
                PjassResolvedPaths paths = resolvePjassPaths(std::filesystem::current_path(),
                                                             options.pjassPath,
                                                             options.commonPath,
                                                             options.blizzardPath);
                if (!paths.ok) {
                    pjassResult.error = paths.error;
                    diagnostics.error(SourceLocation{}, paths.error);
                    ok = false;
                } else {
                    std::filesystem::path reportBase = options.emitValidationReportPath.empty()
                        ? options.outputPath
                        : options.emitValidationReportPath;
                    std::filesystem::path stdoutPath = reportBase;
                    stdoutPath.replace_extension(".pjass.stdout.txt");
                    std::filesystem::path stderrPath = reportBase;
                    stderrPath.replace_extension(".pjass.stderr.txt");
                    PjassOptions pjassOptions;
                    pjassOptions.pjassPath = paths.pjassPath;
                    pjassOptions.commonPath = paths.commonPath;
                    pjassOptions.blizzardPath = paths.blizzardPath;
                    pjassOptions.scriptPath = options.outputPath;
                    pjassOptions.stdoutPath = stdoutPath;
                    pjassOptions.stderrPath = stderrPath;
                    pjassOptions.allowedExternalFunctions = options.pjassAllowedExternalFunctions;
                    pjassOptions.timeoutMs = options.pjassTimeoutMs;
                    pjassOptions.exampleLimit = options.emitPjassExamples;
                    auto pjassStart = std::chrono::steady_clock::now();
                    pjassResult = runPjass(pjassOptions);
                    auto pjassEnd = std::chrono::steady_clock::now();
                    timings.pjass = elapsedMs(pjassStart, pjassEnd);
                    if (!pjassResult.ok) {
                        diagnostics.error(SourceLocation{}, "PJASS validation failed; see validation report and pjass logs");
                        ok = false;
                    }
                }
            }
        }
    }

    timings.total = elapsedMs(totalStart, std::chrono::steady_clock::now());
    if (!options.emitStatsPath.empty()) {
        ok = writeTextFile(options.emitStatsPath, emitStatsJson(sources, preprocessed.stats, expandedProgram.stats, diagnostics, timings)) && ok;
    }
    if (!options.emitValidationReportPath.empty()) {
        if (!syntaxReport.ran && !codegen.output.empty()) {
            syntaxReport = analyzeOutputSyntaxLite(codegen.output);
            initReport = analyzeInitIntegrity(codegen.output);
        }
        if (comparisonReport.generated.bytes == 0 && !codegen.output.empty()) {
            comparisonReport = compareWithReference(codegen.output, options.compareJasshelperPath.empty()
                                                                      ? std::filesystem::path("samples/output_jasshelper.j")
                                                                      : options.compareJasshelperPath);
        }
        ok = writeTextFile(options.emitValidationReportPath,
                           emitValidationReportJson(options, syntaxReport, initReport, pjassResult, comparisonReport, timings)) && ok;
    }

    if (!diagnostics.all().empty()) {
        diagnostics.print(sources, std::cerr);
    }

    if (!ok) {
        return (expandedProgram.hasUnsupported() || diagnostics.unsupportedCount() > 0) ? 6 : 5;
    }
    if (diagnostics.hasErrors()) {
        return 4;
    }
    if (expandedProgram.hasUnsupported() && !options.allowUnsupported && !options.scanOnly) {
        return 6;
    }
    return 0;
}
