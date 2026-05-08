#pragma once

#include "core/Diagnostics.h"
#include "codegen/CodeWriter.h"
#include "parser/Ast.h"
#include "sema/LibraryGraph.h"
#include "sema/SymbolTable.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vjassc {

struct CodegenOptions {
    bool scanOnly = false;
    bool allowUnsupported = false;
};

struct CodegenResult {
    bool ok = false;
    std::string output;
};

class Phase1Codegen {
public:
    Phase1Codegen(Diagnostics& diagnostics, CodegenOptions options);
    CodegenResult generate(const Program& program);

private:
    struct FieldInfo {
        const FieldDecl* decl = nullptr;
        std::string name;
        std::string typeName;
        std::string generatedName;
        bool isStatic = false;
        bool isArray = false;
        bool isFixedArray = false;
        int fixedArraySize = 0;
    };

    struct MethodInfo {
        const MethodDecl* decl = nullptr;
        std::string name;
        std::string generatedName;
        bool isStatic = false;
        bool isOnDestroy = false;
        bool isOnInit = false;
    };

    struct StructInfo {
        const Decl* decl = nullptr;
        const Decl* container = nullptr;
        std::string originalName;
        std::string generatedName;
        std::string prefix;
        bool isArrayStruct = false;
        std::vector<FieldInfo> fields;
        std::vector<MethodInfo> methods;
        std::unordered_map<std::string, size_t> fieldIndex;
        std::unordered_map<std::string, size_t> methodIndex;
    };

    void emitGlobals(const Program& program, const LibraryGraphResult& graph);
    void emitDeclGlobals(const Decl& decl, const Decl* container);
    void emitStructGlobals();
    void emitStructGlobalBlock(const StructInfo& info);
    void emitTypesAndNatives(const Program& program, const LibraryGraphResult& graph);
    void emitTypeOrNative(const Decl& decl);
    void emitFunctions(const Program& program, const LibraryGraphResult& graph);
    void emitDeclFunctions(const Decl& decl, const Decl* container);
    void emitJassFunction(const Decl& decl, const Decl* container, bool injectInit);
    void emitZincFunction(const Decl& decl, const Decl* container);
    void emitStructFunctions();
    void emitStructSupportFunctions(const StructInfo& info);
    void emitStructMethod(const StructInfo& info, const MethodInfo& method);
    void emitInitHelper(const LibraryGraphResult& graph, const Program& program);
    void collectInitializers(const Decl& decl, const Decl* container);
    void collectRootInitializers(const std::vector<Decl>& decls);
    void collectStructs(const std::vector<Decl>& decls, const Decl* container);
    void collectStruct(const Decl& decl, const Decl* container);

    std::string rewriteForContainer(const std::string& line, const Decl* container) const;
    std::string rewriteFunctionHeader(const Decl& decl, const Decl* container) const;
    std::string renameInContainer(const std::string& name, const Decl* container) const;
    std::string makeScopedStructName(const Decl& decl, const Decl* container) const;
    std::string rewriteTypeName(const std::string& typeName, const StructInfo* currentStruct) const;
    std::string rewriteParamList(const std::vector<ParamDecl>& params, bool includeThis, const StructInfo* currentStruct) const;
    std::string rewriteStructExpression(const std::string& line,
                                        const StructInfo* currentStruct,
                                        const std::unordered_map<std::string, std::string>& localTypes) const;
    std::string rewriteLocalDeclLine(const std::string& line,
                                     const StructInfo* currentStruct,
                                     std::unordered_map<std::string, std::string>& localTypes,
                                     std::vector<std::string>& extraLines) const;
    void seedFunctionLocalTypes(const std::string& header, std::unordered_map<std::string, std::string>& localTypes) const;
    void seedMethodLocalTypes(const StructInfo& info, const MethodInfo& method, std::unordered_map<std::string, std::string>& localTypes) const;
    const StructInfo* findStruct(std::string_view name) const;
    const FieldInfo* findField(const StructInfo& info, std::string_view name) const;
    const MethodInfo* findMethod(const StructInfo& info, std::string_view name) const;

    std::vector<std::string> lowerZincBody(const std::vector<std::string>& lines);
    void lowerZincBlock(const std::vector<std::string>& lines, size_t& index, std::vector<std::string>& locals, std::vector<std::string>& body);
    void lowerZincSimpleStatement(const std::string& statement, std::vector<std::string>& locals, std::vector<std::string>& body);

    Diagnostics& diagnostics_;
    CodegenOptions options_;
    SymbolTable symbols_;
    CodeWriter writer_;
    std::unordered_set<std::string> emittedNatives_;
    std::vector<std::pair<std::string, bool>> initializers_;
    std::vector<std::string> structInitializers_;
    std::vector<StructInfo> structs_;
    std::unordered_map<const Decl*, size_t> structIndexByDecl_;
    std::unordered_map<std::string, size_t> structIndexByName_;
    const Decl* mainFunction_ = nullptr;
    const Decl* mainContainer_ = nullptr;
};

} // namespace vjassc
