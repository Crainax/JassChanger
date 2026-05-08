#include "validation/OutputAnalysis.h"

#include "util/PathUtil.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace vjassc {
namespace {

bool isWordPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
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

std::string stripProtected(const std::string& line) {
    std::string out;
    out.reserve(line.size());
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

std::string stripLineCommentPreservingLiterals(const std::string& line) {
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (!inString && !inRaw && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
            return line.substr(0, i);
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
        } else if (inRaw) {
            if (c == '\'') {
                inRaw = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '\'') {
            inRaw = true;
        }
    }
    return line;
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

bool hasChainedIndexing(const std::string& text) {
    bool sawClose = false;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        if (sawClose) {
            if (c == '[') {
                return true;
            }
            sawClose = false;
        }
        if (c == ']') {
            sawClose = true;
        }
    }
    return false;
}

std::string functionNameFromHeader(const std::string& line) {
    std::istringstream in(line);
    std::string word;
    std::string name;
    in >> word >> name;
    return name;
}

std::string returnTypeFromHeader(const std::string& line) {
    size_t pos = line.find(" returns ");
    if (pos == std::string::npos) {
        return {};
    }
    return trim(std::string_view(line).substr(pos + 9));
}

std::string declarationNameInGlobals(const std::string& line) {
    std::istringstream in(line);
    std::string word;
    std::string type;
    std::string name;
    in >> word;
    if (word == "constant") {
        in >> type;
    } else {
        type = word;
    }
    in >> name;
    if (name == "array") {
        in >> name;
    }
    if (name.empty()) {
        return {};
    }
    size_t bracket = name.find('[');
    if (bracket != std::string::npos) {
        name = name.substr(0, bracket);
    }
    size_t eq = name.find('=');
    if (eq != std::string::npos) {
        name = name.substr(0, eq);
    }
    return name;
}

void addIssue(OutputSyntaxReport& report, std::string check, size_t line, std::string message, const std::string& snippet) {
    report.ok = false;
    report.issues.push_back(ValidationIssue{std::move(check), line, std::move(message), trim(snippet)});
}

void addResidue(OutputSyntaxReport& report, const std::string& label) {
    if (std::find(report.residualSourceForms.begin(), report.residualSourceForms.end(), label) ==
        report.residualSourceForms.end()) {
        report.residualSourceForms.push_back(label);
    }
    ++report.metrics.sourceFormResidues;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool containsCallInMain(std::string_view output, const std::string& callName) {
    std::istringstream in{std::string(output)};
    std::string line;
    bool inMain = false;
    while (std::getline(in, line)) {
        std::string t = trim(stripProtected(line));
        if (startsWithWord(t, "function main")) {
            inMain = true;
            continue;
        }
        if (inMain && startsWithWord(t, "endfunction")) {
            return false;
        }
        if (inMain && t.find("call " + callName + "(") != std::string::npos) {
            return true;
        }
    }
    return false;
}

size_t linePositionInMain(std::string_view output, const std::string& callName) {
    std::istringstream in{std::string(output)};
    std::string line;
    bool inMain = false;
    size_t pos = 0;
    size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::string t = trim(stripProtected(line));
        if (startsWithWord(t, "function main")) {
            inMain = true;
            continue;
        }
        if (inMain && startsWithWord(t, "endfunction")) {
            return 0;
        }
        if (inMain) {
            ++pos;
            if (t.find("call " + callName + "(") != std::string::npos) {
                (void)lineNo;
                return pos;
            }
        }
    }
    return 0;
}

} // namespace

OutputMetrics collectOutputMetrics(std::string_view output) {
    OutputMetrics metrics;
    metrics.bytes = output.size();
    std::istringstream in{std::string(output)};
    std::string line;
    bool inGlobals = false;
    std::unordered_set<std::string> functions;
    std::unordered_set<std::string> globals;
    std::unordered_set<std::string> natives;
    while (std::getline(in, line)) {
        ++metrics.lines;
        std::string t = trim(stripProtected(line));
        if (t.empty()) {
            continue;
        }
        if (t == "globals") {
            ++metrics.globalsBlocks;
            inGlobals = true;
            continue;
        }
        if (t == "endglobals") {
            ++metrics.endglobals;
            inGlobals = false;
            continue;
        }
        if (startsWithWord(t, "native")) {
            ++metrics.natives;
            std::string name = functionNameFromHeader(t);
            if (!name.empty() && !natives.insert(name).second) {
                ++metrics.duplicateNativeNames;
            }
            continue;
        }
        if (startsWithWord(t, "type")) {
            ++metrics.types;
            continue;
        }
        if (startsWithWord(t, "function")) {
            ++metrics.functions;
            std::string name = functionNameFromHeader(t);
            if (name == "main") {
                metrics.hasMain = true;
            } else if (name == "config") {
                metrics.hasConfig = true;
            }
            if (name.rfind("vjlambda__", 0) == 0) {
                ++metrics.generatedLambdaFunctions;
            }
            if (name.rfind("s__", 0) == 0 || name.rfind("sc__", 0) == 0 || name.rfind("si__", 0) == 0) {
                ++metrics.structSupportFunctions;
            }
            if (name.rfind("vjfi__", 0) == 0 && name.find("__wrapper") != std::string::npos) {
                ++metrics.functionInterfaceWrappers;
            }
            if (name.rfind("vjassc__init_", 0) == 0) {
                metrics.initHelperNames.push_back(name);
            }
            if (!name.empty() && !functions.insert(name).second) {
                ++metrics.duplicateFunctionNames;
            }
            continue;
        }
        if (inGlobals) {
            ++metrics.globalsDeclarations;
            std::string name = declarationNameInGlobals(t);
            if (!name.empty() && !globals.insert(name).second) {
                ++metrics.duplicateGlobalNames;
            }
        }
    }
    return metrics;
}

OutputSyntaxReport analyzeOutputSyntaxLite(std::string_view output) {
    OutputSyntaxReport report;
    report.ran = true;
    report.metrics = collectOutputMetrics(output);
    std::istringstream in{std::string(output)};
    std::string line;
    bool inFunction = false;
    bool sawExecutableInFunction = false;
    std::string currentReturnType;
    size_t functionDepth = 0;
    size_t lineNo = 0;
    const std::vector<std::string> forbiddenWords = {
        "library", "endlibrary", "scope", "endscope", "struct", "endstruct",
        "method", "endmethod", "module", "endmodule", "implement", "delegate",
        "stub", "super", "operator", "interface"
    };
    while (std::getline(in, line)) {
        ++lineNo;
        std::string rawTrim = trim(line);
        std::string code = stripProtected(line);
        std::string t = trim(code);
        if (t.empty()) {
            continue;
        }
        if (rawTrim.rfind("//!", 0) == 0) {
            addResidue(report, "//! directive");
            addIssue(report, "noBangDirective", lineNo, "residual //! directive", line);
        }
        for (const auto& word : forbiddenWords) {
            if (containsWord(t, word)) {
                addResidue(report, word);
                addIssue(report, "noSourceForms", lineNo, "residual source form '" + word + "'", line);
                break;
            }
        }
        if (t.find("static if") != std::string::npos) {
            addResidue(report, "static if");
            addIssue(report, "noSourceForms", lineNo, "residual static if", line);
        }
        if (t.find("function interface") != std::string::npos) {
            addResidue(report, "function interface");
            addIssue(report, "noSourceForms", lineNo, "residual function interface", line);
        }
        if (containsAnonymousFunctionSyntax(t)) {
            addResidue(report, "anonymous function");
            addIssue(report, "noAnonymousFunction", lineNo, "residual anonymous function syntax", line);
        }
        if (t.find('{') != std::string::npos || t.find('}') != std::string::npos || t.find(';') != std::string::npos ||
            t.find("->") != std::string::npos || t.find("=>") != std::string::npos) {
            addResidue(report, "zinc syntax");
            addIssue(report, "noZincSyntax", lineNo, "residual Zinc syntax token", line);
        }
        if (t.find("thistype") != std::string::npos) {
            addResidue(report, "thistype");
            addIssue(report, "noThistype", lineNo, "residual thistype", line);
        }
        if (t.find(".execute(") != std::string::npos || t.find(".evaluate(") != std::string::npos) {
            addResidue(report, "function object member");
            addIssue(report, "noFunctionObjectMembers", lineNo, "residual function-object member access", line);
        }
        if (t.find("||") != std::string::npos || t.find("&&") != std::string::npos) {
            addIssue(report, "noZincBooleanOperators", lineNo, "residual Zinc boolean operator", line);
        }
        if (t.find("/*") != std::string::npos || t.find("*/") != std::string::npos) {
            addIssue(report, "noBlockCommentLeak", lineNo, "residual block comment marker", line);
        }
        if (hasChainedIndexing(t)) {
            addIssue(report, "noMultidimensionalArray", lineNo, "residual chained array indexing", line);
        }
        if (startsWithWord(t, "function")) {
            if (inFunction) {
                addIssue(report, "noNestedFunction", lineNo, "function declaration nested in function", line);
            }
            inFunction = true;
            ++functionDepth;
            sawExecutableInFunction = false;
            currentReturnType = returnTypeFromHeader(t);
            if (t.find(" takes ") == std::string::npos || currentReturnType.empty()) {
                addIssue(report, "validFunctionHeader", lineNo, "invalid function header", line);
            }
            continue;
        }
        if (startsWithWord(t, "endfunction")) {
            if (!inFunction || functionDepth == 0) {
                addIssue(report, "balancedFunctions", lineNo, "endfunction without function", line);
            } else {
                --functionDepth;
            }
            inFunction = functionDepth > 0;
            sawExecutableInFunction = false;
            currentReturnType.clear();
            continue;
        }
        if (inFunction) {
            if (startsWithWord(t, "local")) {
                if (sawExecutableInFunction) {
                    addIssue(report, "localOrder", lineNo, "local declaration after executable statement", line);
                }
            } else {
                sawExecutableInFunction = true;
            }
            if (startsWithWord(t, "return")) {
                std::string returnLine = trim(stripLineCommentPreservingLiterals(line));
                std::string expr = trim(std::string_view(returnLine).substr(6));
                if (currentReturnType == "nothing" && !expr.empty()) {
                    addIssue(report, "returnType", lineNo, "return value in returns nothing function", line);
                } else if (!currentReturnType.empty() && currentReturnType != "nothing" && expr.empty()) {
                    addIssue(report, "returnType", lineNo, "empty return in non-nothing function", line);
                }
            }
        }
        if (startsWithWord(t, "set") && t.find("= call ") != std::string::npos) {
            addIssue(report, "setCallSyntax", lineNo, "set uses call syntax in assignment", line);
        }
        if (t.find("= call ") != std::string::npos) {
            addIssue(report, "callInAssignment", lineNo, "call used in assignment value", line);
        }
    }
    if (report.metrics.globalsBlocks != 1) {
        addIssue(report, "singleGlobalsBlock", 0, "expected exactly one globals block", "");
    }
    if (report.metrics.endglobals != report.metrics.globalsBlocks || report.metrics.endglobals == 0) {
        addIssue(report, "balancedGlobals", 0, "missing or unbalanced endglobals", "");
    }
    if (functionDepth != 0) {
        addIssue(report, "balancedFunctions", 0, "missing endfunction", "");
    }
    if (report.metrics.duplicateFunctionNames != 0) {
        addIssue(report, "duplicateFunctionNames", 0, "duplicate function declarations remain", "");
    }
    if (report.metrics.duplicateGlobalNames != 0) {
        addIssue(report, "duplicateGlobalNames", 0, "duplicate global declarations remain", "");
    }
    return report;
}

InitValidationReport analyzeInitIntegrity(std::string_view output) {
    InitValidationReport report;
    OutputMetrics metrics = collectOutputMetrics(output);
    report.hasMain = metrics.hasMain;
    report.hasConfig = metrics.hasConfig;
    for (const auto& helper : metrics.initHelperNames) {
        if (helper == "vjassc__init_structs") {
            report.hasStructInit = true;
        } else if (helper == "vjassc__init_function_interfaces") {
            report.hasFunctionInterfaceInit = true;
        } else if (helper == "vjassc__init_libraries") {
            report.hasLibraryInit = true;
        }
    }
    report.mainCallsStructInit = containsCallInMain(output, "vjassc__init_structs");
    report.mainCallsFunctionInterfaceInit = !report.hasFunctionInterfaceInit ||
                                            containsCallInMain(output, "vjassc__init_function_interfaces");
    report.mainCallsLibraryInit = containsCallInMain(output, "vjassc__init_libraries");
    size_t structPos = linePositionInMain(output, "vjassc__init_structs");
    size_t ifacePos = linePositionInMain(output, "vjassc__init_function_interfaces");
    size_t libPos = linePositionInMain(output, "vjassc__init_libraries");
    report.structBeforeFunctionInterface = structPos != 0 && (ifacePos == 0 || structPos < ifacePos);
    report.functionInterfaceBeforeLibrary = libPos != 0 && (ifacePos == 0 || ifacePos < libPos);

    std::istringstream in{std::string(output)};
    std::string line;
    bool inLibraryInit = false;
    bool inStructInit = false;
    while (std::getline(in, line)) {
        std::string t = trim(stripProtected(line));
        if (startsWithWord(t, "function vjassc__init_libraries")) {
            inLibraryInit = true;
            continue;
        }
        if (startsWithWord(t, "function vjassc__init_structs")) {
            inStructInit = true;
            continue;
        }
        if (startsWithWord(t, "endfunction")) {
            inLibraryInit = false;
            inStructInit = false;
            continue;
        }
        if (inLibraryInit && (startsWithWord(t, "call") || t.find("ExecuteFunc(") != std::string::npos)) {
            ++report.libraryInitializerCount;
        }
        if (inStructInit && startsWithWord(t, "call")) {
            ++report.structInitializerCount;
        }
    }
    if (!report.hasMain) {
        report.issues.push_back("missing main");
    }
    if (!report.hasConfig) {
        report.issues.push_back("missing config");
    }
    if (!report.hasStructInit || !report.mainCallsStructInit) {
        report.issues.push_back("struct init helper missing or not called by main");
    }
    if (report.hasFunctionInterfaceInit && !report.mainCallsFunctionInterfaceInit) {
        report.issues.push_back("function interface init helper not called by main");
    }
    if (!report.hasLibraryInit || !report.mainCallsLibraryInit) {
        report.issues.push_back("library init helper missing or not called by main");
    }
    if (!report.structBeforeFunctionInterface || !report.functionInterfaceBeforeLibrary) {
        report.issues.push_back("main init helper order is not struct -> function interface -> library");
    }
    return report;
}

ComparisonReport compareWithReference(std::string_view generated, const std::filesystem::path& referencePath) {
    ComparisonReport report;
    report.referencePath = referencePath;
    report.generated = collectOutputMetrics(generated);
    if (referencePath.empty() || !std::filesystem::exists(referencePath)) {
        report.referenceFound = false;
        report.notes.push_back("JassHelper reference output was not found.");
        return report;
    }
    report.referenceFound = true;
    std::string reference = readTextFile(referencePath);
    report.reference = collectOutputMetrics(reference);
    if (report.generated.functions < report.reference.functions) {
        report.notes.push_back("Generated output has fewer functions than the JassHelper reference; verify wrapper generation and init helpers.");
    }
    if (report.generated.globalsBlocks != 1 || report.reference.globalsBlocks != 1) {
        report.notes.push_back("Generated/reference globals block counts differ from the expected single-block layout.");
    }
    if (!report.generated.hasMain || !report.generated.hasConfig) {
        report.notes.push_back("Generated output is missing main or config.");
    }
    return report;
}

std::string previewText(std::string_view text, size_t maxBytes) {
    if (text.size() <= maxBytes) {
        return std::string(text);
    }
    return std::string(text.substr(0, maxBytes)) + "\n... truncated ...";
}

} // namespace vjassc
