#include "codegen/Phase1Codegen.h"

#include "codegen/CodeWriter.h"
#include "util/PathUtil.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace vjassc {
namespace {

std::string removeSemicolon(std::string text) {
    text = trim(text);
    if (!text.empty() && text.back() == ';') {
        text.pop_back();
    }
    return trim(text);
}

std::string parseNativeName(const std::string& line) {
    std::istringstream in(trim(line));
    std::string word;
    std::string name;
    in >> word >> name;
    return name;
}

bool isTypeWord(const std::string& word) {
    static const std::unordered_set<std::string> types = {
        "integer", "real", "boolean", "string", "code", "handle", "unit", "player", "timer",
        "trigger", "effect", "group", "force", "rect", "location", "item", "destructable",
        "widget", "image", "sound", "region", "hashtable", "boolexpr", "dialog", "button",
        "thistype"
    };
    return types.contains(word);
}

bool isLocalDecl(const std::string& line) {
    std::string t = trim(line);
    if (startsWithWord(t, "local")) {
        return true;
    }
    if (startsWithWord(t, "call") || startsWithWord(t, "set") || startsWithWord(t, "return") ||
        startsWithWord(t, "if") || startsWithWord(t, "while") || startsWithWord(t, "for") ||
        startsWithWord(t, "loop") || startsWithWord(t, "exitwhen") || startsWithWord(t, "else")) {
        return false;
    }
    std::istringstream in(t);
    std::string word;
    std::string name;
    in >> word >> name;
    if (word == "constant") {
        in >> word >> name;
    }
    if (word.empty() || name.empty()) {
        return false;
    }
    if (!std::all_of(word.begin(), word.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
        })) {
        return false;
    }
    if (!std::isalpha(static_cast<unsigned char>(word[0])) && word[0] != '_' && word[0] != '$') {
        return false;
    }
    return (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_' || name[0] == '$') ||
           isTypeWord(word);
}

std::string parenContent(const std::string& text) {
    size_t open = text.find('(');
    size_t close = text.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) {
        return {};
    }
    return trim(std::string_view(text).substr(open + 1, close - open - 1));
}

std::string conditionFromHeader(const std::string& text) {
    return "(" + parenContent(text) + ")";
}

bool isAssignmentLike(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '=') {
            continue;
        }
        char prev = i > 0 ? text[i - 1] : '\0';
        char next = i + 1 < text.size() ? text[i + 1] : '\0';
        if (prev != '=' && prev != '!' && prev != '<' && prev != '>' && next != '=') {
            return true;
        }
    }
    return text.find("+=") != std::string::npos || text.find("-=") != std::string::npos ||
           text.find("*=") != std::string::npos || text.find("/=") != std::string::npos;
}

std::vector<std::string> splitSemicolons(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    int parens = 0;
    bool inString = false;
    for (char c : text) {
        if (c == '"') {
            inString = !inString;
        } else if (!inString && c == '(') {
            ++parens;
        } else if (!inString && c == ')' && parens > 0) {
            --parens;
        }
        if (!inString && parens == 0 && c == ';') {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(trim(cur));
    return out;
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool isIdentPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

std::string regexEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (std::string_view(R"(\.^$|()[]{}*+?)").find(c) != std::string_view::npos) {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::string rewriteOutsideProtected(const std::string& line, const std::function<std::string(std::string)>& rewrite) {
    std::string out;
    std::string normal;
    bool inString = false;
    bool inRaw = false;
    bool escaped = false;
    auto flush = [&]() {
        if (!normal.empty()) {
            out += rewrite(normal);
            normal.clear();
        }
    };
    for (size_t i = 0; i < line.size();) {
        if (!inString && !inRaw && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
            flush();
            out += line.substr(i);
            break;
        }
        char c = line[i];
        if (inString) {
            out.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            ++i;
            continue;
        }
        if (inRaw) {
            out.push_back(c);
            if (c == '\'') {
                inRaw = false;
            }
            ++i;
            continue;
        }
        if (c == '"') {
            flush();
            inString = true;
            out.push_back(c);
            ++i;
            continue;
        }
        if (c == '\'') {
            flush();
            inRaw = true;
            out.push_back(c);
            ++i;
            continue;
        }
        normal.push_back(c);
        ++i;
    }
    flush();
    return out;
}

std::string replaceRegex(std::string text, const std::string& pattern, const std::string& replacement) {
    return std::regex_replace(text, std::regex(pattern), replacement);
}

std::vector<std::string> splitCommaList(const std::string& text) {
    return splitCommaListRespectingQuotes(text);
}

std::string joinParams(const std::vector<std::string>& params) {
    std::string out;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += params[i];
    }
    return out.empty() ? "nothing" : out;
}

std::string compactSetSpacing(std::string line) {
    line = std::regex_replace(line, std::regex(R"(\s*=\s*)"), "=");
    line = std::regex_replace(line, std::regex(R"(\s*\+\s*)"), "+");
    return line;
}

} // namespace

Phase1Codegen::Phase1Codegen(Diagnostics& diagnostics, CodegenOptions options)
    : diagnostics_(diagnostics), options_(options) {}

CodegenResult Phase1Codegen::generate(const Program& program) {
    if (program.hasUnsupported() && !options_.scanOnly) {
        diagnostics_.error(SourceLocation{}, "unsupported declarations prevent code generation");
        return CodegenResult{false, {}};
    }

    symbols_.build(program);
    structs_.clear();
    structIndexByDecl_.clear();
    structIndexByName_.clear();
    structInitializers_.clear();
    mainFunction_ = nullptr;
    mainContainer_ = nullptr;
    collectStructs(program.decls, nullptr);
    LibraryGraph graph(diagnostics_);
    LibraryGraphResult graphResult = graph.sort(program);
    if (diagnostics_.hasErrors()) {
        return CodegenResult{false, {}};
    }

    writer_.writeln("// Generated by vjassc phase3");
    writer_.writeln();
    emitGlobals(program, graphResult);
    writer_.writeln();
    emitTypesAndNatives(program, graphResult);
    writer_.writeln();
    emitFunctions(program, graphResult);

    return CodegenResult{true, writer_.str()};
}

void Phase1Codegen::emitGlobals(const Program& program, const LibraryGraphResult& graph) {
    writer_.writeln("globals");
    writer_.indent();
    for (const Decl* library : graph.sortedLibraries) {
        writer_.writeln("constant boolean LIBRARY_" + library->name + "=true");
    }
    emitStructGlobals();
    for (const auto& decl : program.decls) {
        emitDeclGlobals(decl, nullptr);
    }
    writer_.dedent();
    writer_.writeln("endglobals");
}

void Phase1Codegen::emitDeclGlobals(const Decl& decl, const Decl* container) {
    if (decl.kind == DeclKind::Struct || decl.kind == DeclKind::Module) {
        return;
    }
    if (decl.kind == DeclKind::GlobalBlock) {
        for (auto line : decl.lines) {
            std::string out = rewriteForContainer(line, container);
            out = stripAccessPrefixFromLine(out);
            out = removeSemicolon(out);
            if (!trim(out).empty()) {
                writer_.writeln(out);
            }
        }
        return;
    }
    const Decl* nextContainer = (decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) ? &decl : container;
    for (const auto& child : decl.children) {
        emitDeclGlobals(child, nextContainer);
    }
}

void Phase1Codegen::emitStructGlobals() {
    for (const auto& info : structs_) {
        emitStructGlobalBlock(info);
    }
}

void Phase1Codegen::emitStructGlobalBlock(const StructInfo& info) {
    if (!info.isArrayStruct) {
        writer_.writeln("integer array si__" + info.generatedName + "_F");
        writer_.writeln("integer si__" + info.generatedName + "_I=0");
        writer_.writeln("integer array si__" + info.generatedName + "_V");
    }
    for (const auto& field : info.fields) {
        std::string typeName = rewriteTypeName(field.typeName, &info);
        if (field.isFixedArray && field.fixedArraySize > 0) {
            writer_.writeln("constant integer s___" + info.generatedName + "_" + field.name + "_size=" + std::to_string(field.fixedArraySize));
        }
        if (field.isStatic) {
            if (field.isArray) {
                writer_.writeln(typeName + " array " + field.generatedName);
            } else if (!field.decl->initializer.empty()) {
                writer_.writeln(typeName + " " + field.generatedName + "=" + rewriteStructExpression(field.decl->initializer, &info, {}));
            } else {
                writer_.writeln(typeName + " " + field.generatedName);
            }
        } else {
            writer_.writeln(typeName + " array " + field.generatedName);
        }
    }
}

void Phase1Codegen::emitTypesAndNatives(const Program& program, const LibraryGraphResult& graph) {
    for (const auto& decl : program.decls) {
        if (decl.kind != DeclKind::Library) {
            emitTypeOrNative(decl);
        }
    }
    for (const Decl* library : graph.sortedLibraries) {
        emitTypeOrNative(*library);
    }
}

void Phase1Codegen::emitTypeOrNative(const Decl& decl) {
    if (decl.kind == DeclKind::Module) {
        return;
    }
    if (decl.kind == DeclKind::TypeDecl) {
        writer_.writeln(trim(decl.lines.front()));
    } else if (decl.kind == DeclKind::Native) {
        std::string name = parseNativeName(decl.lines.front());
        if (emittedNatives_.insert(name).second) {
            writer_.writeln(trim(decl.lines.front()));
        } else {
            diagnostics_.warning(decl.loc, "duplicate native '" + name + "' ignored");
        }
    }
    for (const auto& child : decl.children) {
        emitTypeOrNative(child);
    }
}

void Phase1Codegen::emitFunctions(const Program& program, const LibraryGraphResult& graph) {
    initializers_.clear();
    collectRootInitializers(program.decls);
    emitStructFunctions();
    for (const Decl* library : graph.sortedLibraries) {
        collectInitializers(*library, library);
        emitDeclFunctions(*library, library);
    }
    for (const auto& decl : program.decls) {
        if (decl.kind != DeclKind::Library) {
            emitDeclFunctions(decl, nullptr);
        }
    }
    emitInitHelper(graph, program);
    if (mainFunction_) {
        emitJassFunction(*mainFunction_, mainContainer_, true);
    }
}

void Phase1Codegen::emitDeclFunctions(const Decl& decl, const Decl* container) {
    if (decl.kind == DeclKind::Struct || decl.kind == DeclKind::Module) {
        return;
    }
    if (decl.kind == DeclKind::Function) {
        if (decl.name == "main") {
            mainFunction_ = &decl;
            mainContainer_ = container;
            return;
        }
        if (decl.mode == SyntaxMode::Zinc) {
            emitZincFunction(decl, container);
        } else {
            emitJassFunction(decl, container, false);
        }
        writer_.writeln();
        return;
    }
    const Decl* nextContainer = (decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) ? &decl : container;
    for (const auto& child : decl.children) {
        emitDeclFunctions(child, nextContainer);
    }
}

void Phase1Codegen::emitJassFunction(const Decl& decl, const Decl* container, bool injectInit) {
    if (decl.lines.empty()) {
        return;
    }
    std::string header = rewriteFunctionHeader(decl, container);
    writer_.writeln(header);
    writer_.indent();
    std::unordered_map<std::string, std::string> localTypes;
    seedFunctionLocalTypes(stripAccessPrefixFromLine(decl.lines.front()), localTypes);
    bool injected = false;
    for (size_t i = 1; i < decl.lines.size(); ++i) {
        std::string t = trim(decl.lines[i]);
        if (startsWithWord(t, "endfunction")) {
            break;
        }
        std::string line = trim(rewriteForContainer(decl.lines[i], container));
        std::vector<std::string> extraLines;
        if (startsWithWord(line, "local")) {
            writer_.writeln(rewriteLocalDeclLine(line, nullptr, localTypes, extraLines));
            for (const auto& extra : extraLines) {
                writer_.writeln(extra);
            }
        } else {
            writer_.writeln(trim(rewriteStructExpression(line, nullptr, localTypes)));
        }
        if (injectInit && !injected && t.find("InitBlizzard") != std::string::npos) {
            writer_.writeln("call vjassc__init_structs()");
            writer_.writeln("call vjassc__init_libraries()");
            injected = true;
        }
    }
    if (injectInit && !injected) {
        writer_.writeln("call vjassc__init_structs()");
        writer_.writeln("call vjassc__init_libraries()");
    }
    writer_.dedent();
    writer_.writeln("endfunction");
}

void Phase1Codegen::emitZincFunction(const Decl& decl, const Decl* container) {
    if (decl.lines.empty()) {
        return;
    }
    std::string header = trim(decl.lines.front());
    std::string accessHeader = header;
    (void)stripAccessPrefixFromLine(accessHeader);
    size_t functionPos = header.find("function");
    size_t open = header.find('(', functionPos);
    size_t close = header.find(')', open);
    std::string functionName = decl.name;
    std::string finalName = renameInContainer(functionName, container);
    std::string params = (open != std::string::npos && close != std::string::npos) ? header.substr(open + 1, close - open - 1) : "";
    std::string returns = "nothing";
    size_t arrow = header.find("->", close == std::string::npos ? 0 : close);
    if (arrow != std::string::npos) {
        std::string after = trim(std::string_view(header).substr(arrow + 2));
        size_t brace = after.find('{');
        if (brace != std::string::npos) {
            after = trim(std::string_view(after).substr(0, brace));
        }
        if (!after.empty()) {
            returns = after;
        }
    }
    params = trim(params);
    if (params.empty()) {
        params = "nothing";
    } else {
        std::vector<std::string> rewrittenParams;
        for (auto part : splitCommaList(params)) {
            std::istringstream in(trim(part));
            std::string type;
            std::string name;
            in >> type >> name;
            if (!type.empty() && !name.empty()) {
                rewrittenParams.push_back(rewriteTypeName(type, nullptr) + " " + name);
            }
        }
        params = joinParams(rewrittenParams);
    }
    writer_.writeln("function " + finalName + " takes " + params + " returns " + rewriteTypeName(returns, nullptr));
    writer_.indent();
    std::unordered_map<std::string, std::string> localTypes;
    std::vector<std::string> bodyLines(decl.lines.begin() + 1, decl.lines.end());
    for (const auto& line : lowerZincBody(bodyLines)) {
        std::string out = rewriteForContainer(line, container);
        std::vector<std::string> extraLines;
        if (startsWithWord(trim(out), "local")) {
            writer_.writeln(rewriteLocalDeclLine(out, nullptr, localTypes, extraLines));
            for (const auto& extra : extraLines) {
                writer_.writeln(extra);
            }
        } else {
            writer_.writeln(rewriteStructExpression(out, nullptr, localTypes));
        }
    }
    writer_.dedent();
    writer_.writeln("endfunction");
}

void Phase1Codegen::emitStructFunctions() {
    for (const auto& info : structs_) {
        emitStructSupportFunctions(info);
    }
}

void Phase1Codegen::emitStructSupportFunctions(const StructInfo& info) {
    const MethodInfo* customCreate = findMethod(info, "create");
    if (!info.isArrayStruct) {
        writer_.writeln("function " + info.prefix + "__allocate takes nothing returns integer");
        writer_.indent();
        writer_.writeln("local integer this");
        writer_.writeln("if si__" + info.generatedName + "_F[0] == 0 then");
        writer_.indent();
        writer_.writeln("set si__" + info.generatedName + "_I=si__" + info.generatedName + "_I+1");
        writer_.writeln("set this=si__" + info.generatedName + "_I");
        writer_.dedent();
        writer_.writeln("else");
        writer_.indent();
        writer_.writeln("set this=si__" + info.generatedName + "_F[0]");
        writer_.writeln("set si__" + info.generatedName + "_F[0]=si__" + info.generatedName + "_F[this]");
        writer_.dedent();
        writer_.writeln("endif");
        writer_.writeln("set si__" + info.generatedName + "_V[this]=-1");
        writer_.writeln("return this");
        writer_.dedent();
        writer_.writeln("endfunction");
        writer_.writeln();

        if (!customCreate || !customCreate->isStatic) {
            writer_.writeln("function " + info.prefix + "_create takes nothing returns integer");
            writer_.indent();
            writer_.writeln("return " + info.prefix + "__allocate()");
            writer_.dedent();
            writer_.writeln("endfunction");
            writer_.writeln();
        }

        writer_.writeln("function " + info.prefix + "_destroy takes integer this returns nothing");
        writer_.indent();
        for (const auto& method : info.methods) {
            if (method.isOnDestroy) {
                writer_.writeln("call " + method.generatedName + "(this)");
            }
        }
        writer_.writeln("set si__" + info.generatedName + "_V[this]=0");
        writer_.writeln("set si__" + info.generatedName + "_F[this]=si__" + info.generatedName + "_F[0]");
        writer_.writeln("set si__" + info.generatedName + "_F[0]=this");
        writer_.dedent();
        writer_.writeln("endfunction");
        writer_.writeln();
    }

    for (const auto& method : info.methods) {
        emitStructMethod(info, method);
        writer_.writeln();
        if (method.isOnInit) {
            structInitializers_.push_back(method.generatedName);
        }
    }
}

void Phase1Codegen::emitStructMethod(const StructInfo& info, const MethodInfo& method) {
    std::string params = rewriteParamList(method.decl->params, !method.isStatic, &info);
    std::string returns = rewriteTypeName(method.decl->returnType.name, &info);
    writer_.writeln("function " + method.generatedName + " takes " + params + " returns " + returns);
    writer_.indent();
    std::unordered_map<std::string, std::string> localTypes;
    seedMethodLocalTypes(info, method, localTypes);
    std::vector<std::string> rawLines;
    for (const auto& logical : method.decl->bodyLines) {
        rawLines.push_back(logical.text);
    }
    std::vector<std::string> lines = method.decl->mode == SyntaxMode::Zinc ? lowerZincBody(rawLines) : rawLines;
    for (const auto& raw : lines) {
        std::string line = trim(rewriteForContainer(raw, info.container));
        if (line.empty() || line.rfind("//", 0) == 0) {
            continue;
        }
        std::vector<std::string> extraLines;
        if (startsWithWord(line, "local")) {
            writer_.writeln(rewriteLocalDeclLine(line, &info, localTypes, extraLines));
            for (const auto& extra : extraLines) {
                writer_.writeln(extra);
            }
        } else {
            writer_.writeln(rewriteStructExpression(line, &info, localTypes));
        }
    }
    writer_.dedent();
    writer_.writeln("endfunction");
}

void Phase1Codegen::emitInitHelper(const LibraryGraphResult&, const Program&) {
    writer_.writeln("function vjassc__init_structs takes nothing returns nothing");
    writer_.indent();
    for (const auto& name : structInitializers_) {
        writer_.writeln("call " + name + "()");
    }
    writer_.dedent();
    writer_.writeln("endfunction");
    writer_.writeln();

    writer_.writeln("function vjassc__init_libraries takes nothing returns nothing");
    writer_.indent();
    for (const auto& [name, executeFunc] : initializers_) {
        if (executeFunc) {
            writer_.writeln("call ExecuteFunc(\"" + name + "\")");
        } else {
            writer_.writeln("call " + name + "()");
        }
    }
    writer_.dedent();
    writer_.writeln("endfunction");
    writer_.writeln();
}

void Phase1Codegen::collectInitializers(const Decl& decl, const Decl* container) {
    if (decl.kind == DeclKind::Module) {
        return;
    }
    if ((decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) && !decl.initializer.empty()) {
        initializers_.push_back({renameInContainer(decl.initializer, container), decl.kind == DeclKind::Library});
    }
    const Decl* nextContainer = (decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) ? &decl : container;
    for (const auto& child : decl.children) {
        collectInitializers(child, nextContainer);
    }
}

void Phase1Codegen::collectRootInitializers(const std::vector<Decl>& decls) {
    for (const auto& decl : decls) {
        if (decl.kind == DeclKind::Scope) {
            collectInitializers(decl, &decl);
        }
    }
}

void Phase1Codegen::collectStructs(const std::vector<Decl>& decls, const Decl* container) {
    for (const auto& decl : decls) {
        if (decl.kind == DeclKind::Struct) {
            collectStruct(decl, container);
            continue;
        }
        if (decl.kind == DeclKind::Module) {
            continue;
        }
        const Decl* nextContainer = (decl.kind == DeclKind::Library || decl.kind == DeclKind::Scope) ? &decl : container;
        collectStructs(decl.children, nextContainer);
    }
}

void Phase1Codegen::collectStruct(const Decl& decl, const Decl* container) {
    StructInfo info;
    info.decl = &decl;
    info.container = container;
    info.originalName = decl.name;
    info.generatedName = makeScopedStructName(decl, container);
    info.prefix = "s__" + info.generatedName;
    info.isArrayStruct = decl.isArrayStruct;

    std::unordered_set<std::string> seenFields;
    std::unordered_set<std::string> seenMethods;
    for (const auto& field : decl.fields) {
        if (!seenFields.insert(field.name).second) {
            diagnostics_.error(field.loc, "duplicate field '" + field.name + "' in struct '" + decl.name + "'");
        }
        FieldInfo fieldInfo;
        fieldInfo.decl = &field;
        fieldInfo.name = field.name;
        fieldInfo.typeName = field.type.name;
        fieldInfo.generatedName = info.prefix + "_" + field.name;
        fieldInfo.isStatic = field.isStatic;
        fieldInfo.isArray = field.isArray;
        fieldInfo.isFixedArray = field.isFixedArray;
        fieldInfo.fixedArraySize = field.fixedArraySize;
        info.fieldIndex[fieldInfo.name] = info.fields.size();
        info.fields.push_back(std::move(fieldInfo));
    }
    for (const auto& method : decl.methods) {
        if (!seenMethods.insert(method.name).second) {
            diagnostics_.error(method.loc, "duplicate method '" + method.name + "' in struct '" + decl.name + "'");
        }
        if (seenFields.contains(method.name)) {
            diagnostics_.error(method.loc, "field and method share name '" + method.name + "' in struct '" + decl.name + "'");
        }
        if (method.isOnDestroy && (method.isStatic || !method.params.empty() || method.returnType.name != "nothing")) {
            diagnostics_.error(method.loc, "onDestroy must be an instance method returning nothing with no parameters");
        }
        if (method.isOnInit && (!method.isStatic || !method.params.empty() || method.returnType.name != "nothing")) {
            diagnostics_.error(method.loc, "onInit must be a static method returning nothing with no parameters");
        }
        MethodInfo methodInfo;
        methodInfo.decl = &method;
        methodInfo.name = method.name;
        methodInfo.isStatic = method.isStatic;
        methodInfo.isOnDestroy = method.isOnDestroy;
        methodInfo.isOnInit = method.isOnInit;
        if (method.isOnDestroy && method.name == "onDestroy") {
            methodInfo.generatedName = "sc__" + info.generatedName + "_onDestroy";
        } else {
            methodInfo.generatedName = info.prefix + "_" + method.name;
        }
        info.methodIndex[methodInfo.name] = info.methods.size();
        info.methods.push_back(std::move(methodInfo));
    }

    size_t index = structs_.size();
    structs_.push_back(std::move(info));
    structIndexByDecl_[&decl] = index;
    structIndexByName_[structs_[index].originalName] = index;
    structIndexByName_[structs_[index].generatedName] = index;
}

std::string Phase1Codegen::rewriteForContainer(const std::string& line, const Decl* container) const {
    return symbols_.rewriteLine(line, container);
}

std::string Phase1Codegen::rewriteFunctionHeader(const Decl& decl, const Decl* container) const {
    std::string header = rewriteForContainer(decl.lines.front(), container);
    header = stripAccessPrefixFromLine(header);
    size_t takes = header.find(" takes ");
    size_t returns = header.find(" returns ");
    if (takes != std::string::npos && returns != std::string::npos && returns > takes) {
        std::string prefix = header.substr(0, takes + 7);
        std::string params = header.substr(takes + 7, returns - (takes + 7));
        std::string ret = trim(std::string_view(header).substr(returns + 9));
        if (trim(params) != "nothing") {
            std::vector<std::string> rewritten;
            for (auto part : splitCommaList(params)) {
                std::istringstream in(trim(part));
                std::string type;
                std::string name;
                in >> type >> name;
                if (!type.empty() && !name.empty()) {
                    rewritten.push_back(rewriteTypeName(type, nullptr) + " " + name);
                }
            }
            params = joinParams(rewritten);
        }
        header = prefix + params + " returns " + rewriteTypeName(ret, nullptr);
    }
    return trim(header);
}

std::string Phase1Codegen::renameInContainer(const std::string& name, const Decl* container) const {
    if (!container) {
        return name;
    }
    auto symbols = symbols_.symbolsFor(container);
    if (!symbols) {
        return name;
    }
    auto it = symbols->replacements.find(name);
    return it == symbols->replacements.end() ? name : it->second;
}

std::string Phase1Codegen::makeScopedStructName(const Decl& decl, const Decl* container) const {
    if (!container || decl.access.empty()) {
        return decl.name;
    }
    if (decl.access == "private") {
        return container->name + "___" + decl.name;
    }
    return container->name + "_" + decl.name;
}

const Phase1Codegen::StructInfo* Phase1Codegen::findStruct(std::string_view name) const {
    auto it = structIndexByName_.find(std::string(name));
    return it == structIndexByName_.end() ? nullptr : &structs_[it->second];
}

const Phase1Codegen::FieldInfo* Phase1Codegen::findField(const StructInfo& info, std::string_view name) const {
    auto it = info.fieldIndex.find(std::string(name));
    return it == info.fieldIndex.end() ? nullptr : &info.fields[it->second];
}

const Phase1Codegen::MethodInfo* Phase1Codegen::findMethod(const StructInfo& info, std::string_view name) const {
    auto it = info.methodIndex.find(std::string(name));
    return it == info.methodIndex.end() ? nullptr : &info.methods[it->second];
}

std::string Phase1Codegen::rewriteTypeName(const std::string& typeName, const StructInfo* currentStruct) const {
    if (typeName == "thistype") {
        return "integer";
    }
    if (currentStruct && typeName == currentStruct->originalName) {
        return "integer";
    }
    if (findStruct(typeName)) {
        return "integer";
    }
    return typeName;
}

std::string Phase1Codegen::rewriteParamList(const std::vector<ParamDecl>& params, bool includeThis, const StructInfo* currentStruct) const {
    std::vector<std::string> out;
    if (includeThis) {
        out.push_back("integer this");
    }
    for (const auto& param : params) {
        out.push_back(rewriteTypeName(param.type.name, currentStruct) + " " + param.name);
    }
    return joinParams(out);
}

void Phase1Codegen::seedMethodLocalTypes(const StructInfo& info, const MethodInfo& method, std::unordered_map<std::string, std::string>& localTypes) const {
    if (!method.isStatic) {
        localTypes["this"] = info.originalName;
    }
    for (const auto& param : method.decl->params) {
        if (param.type.name == "thistype") {
            localTypes[param.name] = info.originalName;
        } else if (findStruct(param.type.name)) {
            localTypes[param.name] = param.type.name;
        }
    }
}

void Phase1Codegen::seedFunctionLocalTypes(const std::string& header, std::unordered_map<std::string, std::string>& localTypes) const {
    size_t takes = header.find(" takes ");
    size_t returns = header.find(" returns ");
    if (takes == std::string::npos || returns == std::string::npos || returns <= takes) {
        return;
    }
    std::string params = header.substr(takes + 7, returns - (takes + 7));
    params = trim(params);
    if (params == "nothing" || params.empty()) {
        return;
    }
    for (auto part : splitCommaList(params)) {
        std::istringstream in(trim(part));
        std::string type;
        std::string name;
        in >> type >> name;
        if (!type.empty() && !name.empty() && findStruct(type)) {
            localTypes[name] = type;
        }
    }
}

std::string Phase1Codegen::rewriteLocalDeclLine(const std::string& line,
                                                const StructInfo* currentStruct,
                                                std::unordered_map<std::string, std::string>& localTypes,
                                                std::vector<std::string>& extraLines) const {
    std::string t = trim(line);
    bool hasLocal = startsWithWord(t, "local");
    if (hasLocal) {
        t = trim(std::string_view(t).substr(5));
    }
    bool constant = false;
    if (startsWithWord(t, "constant")) {
        constant = true;
        t = trim(std::string_view(t).substr(8));
    }
    size_t assign = t.find('=');
    std::string declPart = assign == std::string::npos ? t : trim(std::string_view(t).substr(0, assign));
    std::string init = assign == std::string::npos ? "" : trim(std::string_view(t).substr(assign + 1));
    std::istringstream in(declPart);
    std::string type;
    std::string name;
    in >> type >> name;
    if (type.empty() || name.empty()) {
        return line;
    }
    if (name == "array") {
        in >> name;
    }
    size_t bracket = name.find('[');
    if (bracket != std::string::npos) {
        name = name.substr(0, bracket);
    }
    if (type == "thistype") {
        localTypes[name] = currentStruct ? currentStruct->originalName : type;
    } else if (findStruct(type)) {
        localTypes[name] = type;
    }
    std::string rewrittenType = rewriteTypeName(type, currentStruct);
    std::string out = "local " + std::string(constant ? "constant " : "") + rewrittenType + " " + name;
    if (!init.empty()) {
        extraLines.push_back("set " + name + "=" + rewriteStructExpression(init, currentStruct, localTypes));
    }
    return out;
}

std::string Phase1Codegen::rewriteStructExpression(const std::string& line,
                                                   const StructInfo* currentStruct,
                                                   const std::unordered_map<std::string, std::string>& localTypes) const {
    auto rewriteNormal = [&](std::string text) {
        if (currentStruct) {
            text = replaceRegex(text, R"(\bthistype\s*\.\s*allocate\s*\()", currentStruct->prefix + "__allocate(");
            text = replaceRegex(text, R"(\bthistype\s*\.\s*create\s*\()", currentStruct->prefix + "_create(");
        }
        for (const auto& info : structs_) {
            std::string structName = regexEscape(info.originalName);
            for (const auto& method : info.methods) {
                if (method.isStatic || method.name == "create") {
                    text = replaceRegex(text,
                                        "\\bfunction\\s+" + structName + "\\s*\\.\\s*" + regexEscape(method.name) + "\\b",
                                        "function " + method.generatedName);
                    text = replaceRegex(text,
                                        "\\b" + structName + "\\s*\\.\\s*" + regexEscape(method.name) + "\\s*\\(",
                                        method.generatedName + "(");
                }
            }
            if (!info.isArrayStruct) {
                text = replaceRegex(text, "\\b" + structName + "\\s*\\.\\s*create\\s*\\(", info.prefix + "_create(");
                text = replaceRegex(text, "\\b" + structName + "\\s*\\.\\s*allocate\\s*\\(", info.prefix + "__allocate(");
            }
            for (const auto& field : info.fields) {
                if (field.isStatic) {
                    text = replaceRegex(text,
                                        "\\b" + structName + "\\s*\\.\\s*" + regexEscape(field.name) + "\\b",
                                        field.generatedName);
                }
            }
        }

        if (currentStruct) {
            for (const auto& field : currentStruct->fields) {
                if (field.isStatic) {
                    text = replaceRegex(text, "\\b" + regexEscape(field.name) + "\\b", field.generatedName);
                    text = replaceRegex(text,
                                        R"(\bthistype\s*\.\s*)" + regexEscape(field.name) + R"(\b)",
                                        field.generatedName);
                    continue;
                }
                if (field.isFixedArray && field.fixedArraySize > 0) {
                    std::string indexed = field.generatedName + "[this*" + std::to_string(field.fixedArraySize) + "+$1]";
                    text = replaceRegex(text,
                                        R"(\bthis\s*\.\s*)" + regexEscape(field.name) + R"(\s*\[\s*([^\]]+)\s*\])",
                                        indexed);
                    text = replaceRegex(text,
                                        R"((^|[^A-Za-z0-9_$])\.\s*)" + regexEscape(field.name) + R"(\s*\[\s*([^\]]+)\s*\])",
                                        "$1" + indexed);
                }
                std::string access = field.generatedName + "[this]";
                text = replaceRegex(text, R"(\bthis\s*\.\s*)" + regexEscape(field.name) + R"(\b)", access);
                text = replaceRegex(text, R"((^|[^A-Za-z0-9_$])\.\s*)" + regexEscape(field.name) + R"(\b)", "$1" + access);
            }
            for (const auto& method : currentStruct->methods) {
                if (method.isStatic) {
                    continue;
                }
                text = replaceRegex(text,
                                    R"((^|[^A-Za-z0-9_$])\.\s*)" + regexEscape(method.name) + R"(\s*\()",
                                    "$1" + method.generatedName + "(this" + (method.decl->params.empty() ? "" : ", "));
            }
            if (!currentStruct->isArrayStruct) {
                text = replaceRegex(text, R"(\ballocate\s*\()", currentStruct->prefix + "__allocate(");
            }
        }

        for (const auto& [varName, typeName] : localTypes) {
            const StructInfo* varStruct = typeName == "thistype" ? currentStruct : findStruct(typeName);
            if (!varStruct) {
                continue;
            }
            std::string var = regexEscape(varName);
            for (const auto& field : varStruct->fields) {
                if (field.isStatic) {
                    continue;
                }
                if (field.isFixedArray && field.fixedArraySize > 0) {
                    text = replaceRegex(text,
                                        "\\b" + var + R"(\s*\.\s*)" + regexEscape(field.name) + R"(\s*\[\s*([^\]]+)\s*\])",
                                        field.generatedName + "[" + varName + "*" + std::to_string(field.fixedArraySize) + "+$1]");
                }
                text = replaceRegex(text,
                                    "\\b" + var + R"(\s*\.\s*)" + regexEscape(field.name) + R"(\b)",
                                    field.generatedName + "[" + varName + "]");
            }
            for (const auto& method : varStruct->methods) {
                if (method.isStatic) {
                    continue;
                }
                text = replaceRegex(text,
                                    "\\b" + var + R"(\s*\.\s*)" + regexEscape(method.name) + R"(\s*\()",
                                    method.generatedName + "(" + varName + (method.decl->params.empty() ? "" : ", "));
            }
            if (!varStruct->isArrayStruct) {
                text = replaceRegex(text,
                                    "\\b" + var + R"(\s*\.\s*destroy\s*\(\s*\))",
                                    varStruct->prefix + "_destroy(" + varName + ")");
            }
        }
        return text;
    };
    return rewriteOutsideProtected(line, rewriteNormal);
}

std::vector<std::string> Phase1Codegen::lowerZincBody(const std::vector<std::string>& lines) {
    std::vector<std::string> locals;
    std::vector<std::string> body;
    size_t index = 0;
    lowerZincBlock(lines, index, locals, body);
    std::vector<std::string> out;
    out.insert(out.end(), locals.begin(), locals.end());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

void Phase1Codegen::lowerZincBlock(const std::vector<std::string>& lines, size_t& index, std::vector<std::string>& locals, std::vector<std::string>& body) {
    while (index < lines.size()) {
        std::string line = trim(lines[index]);
        if (line.empty() || line.rfind("//", 0) == 0) {
            ++index;
            continue;
        }
        if (line == "}" || line.rfind("} else", 0) == 0) {
            return;
        }
        if (startsWithWord(line, "while")) {
            std::string cond = conditionFromHeader(line);
            body.push_back("loop");
            body.push_back("    exitwhen not " + cond);
            ++index;
            lowerZincBlock(lines, index, locals, body);
            if (index < lines.size() && trim(lines[index]) == "}") {
                ++index;
            }
            body.push_back("endloop");
            continue;
        }
        if (startsWithWord(line, "if")) {
            std::string cond = conditionFromHeader(line);
            body.push_back("if " + cond + " then");
            ++index;
            lowerZincBlock(lines, index, locals, body);
            if (index < lines.size() && trim(lines[index]).rfind("} else", 0) == 0) {
                body.push_back("else");
                ++index;
                lowerZincBlock(lines, index, locals, body);
            }
            if (index < lines.size() && trim(lines[index]) == "}") {
                ++index;
            }
            body.push_back("endif");
            continue;
        }
        if (startsWithWord(line, "for")) {
            std::string content = parenContent(line);
            auto parts = splitSemicolons(content);
            if (parts.size() == 3) {
                lowerZincSimpleStatement(parts[0], locals, body);
                body.push_back("loop");
                body.push_back("    exitwhen not (" + parts[1] + ")");
                ++index;
                lowerZincBlock(lines, index, locals, body);
                lowerZincSimpleStatement(parts[2], locals, body);
                if (index < lines.size() && trim(lines[index]) == "}") {
                    ++index;
                }
                body.push_back("endloop");
                continue;
            }
        }
        lowerZincSimpleStatement(line, locals, body);
        ++index;
    }
}

void Phase1Codegen::lowerZincSimpleStatement(const std::string& rawStatement, std::vector<std::string>& locals, std::vector<std::string>& body) {
    std::string statement = removeSemicolon(rawStatement);
    if (statement.empty()) {
        return;
    }
    if (startsWithWord(statement, "return")) {
        body.push_back(statement);
        return;
    }
    if (isLocalDecl(statement)) {
        std::string work = statement;
        bool constant = false;
        if (startsWithWord(work, "constant")) {
            constant = true;
            work = trim(std::string_view(work).substr(8));
        }
        size_t assign = work.find('=');
        std::string declPart = assign == std::string::npos ? work : trim(std::string_view(work).substr(0, assign));
        std::string init = assign == std::string::npos ? "" : trim(std::string_view(work).substr(assign + 1));
        std::istringstream in(declPart);
        std::string type;
        std::string name;
        in >> type >> name;
        if (!type.empty() && !name.empty()) {
            locals.push_back(std::string("local ") + (constant ? "constant " : "") + type + " " + name);
            if (!init.empty()) {
                body.push_back("set " + name + " = " + init);
            }
        }
        return;
    }
    auto emitAssign = [&](const std::string& op, const std::string& jassOp) -> bool {
        size_t pos = statement.find(op);
        if (pos == std::string::npos) {
            return false;
        }
        std::string lhs = trim(std::string_view(statement).substr(0, pos));
        std::string rhs = trim(std::string_view(statement).substr(pos + op.size()));
        if (op == "=") {
            body.push_back("set " + lhs + " = " + rhs);
        } else {
            body.push_back("set " + lhs + " = " + lhs + " " + jassOp + " " + rhs);
        }
        return true;
    };
    if (emitAssign("+=", "+") || emitAssign("-=", "-") || emitAssign("*=", "*") || emitAssign("/=", "/")) {
        return;
    }
    if (isAssignmentLike(statement) && emitAssign("=", "=")) {
        return;
    }
    if (!statement.empty()) {
        body.push_back("call " + statement);
    }
}

} // namespace vjassc
