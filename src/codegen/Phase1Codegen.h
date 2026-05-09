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
    bool warnMode = false;
};

struct CodegenPerformanceCounters {
    size_t linesVisited = 0;
    size_t regexCalls = 0;
    size_t memberAccessScans = 0;
    size_t structLookupCalls = 0;
    size_t functionLookupCalls = 0;
    size_t cachedRewriteHits = 0;
    size_t cachedRewriteMisses = 0;
};

struct CodegenResult {
    bool ok = false;
    std::string output;
    size_t lambdasLowered = 0;
    size_t lambdasCodeContext = 0;
    size_t lambdasBoolexprContext = 0;
    size_t lambdasFunctionInterfaceContext = 0;
    size_t lambdasNativeCallbackContext = 0;
    size_t lambdasMethodCallbackContext = 0;
    size_t lambdasUnknownContext = 0;
    size_t lambdasCapturing = 0;
    size_t lambdasRejected = 0;
    size_t lambdasGeneratedFunctions = 0;
    size_t functionInterfaceTargets = 0;
    size_t functionInterfaceCalls = 0;
    size_t functionObjectCalls = 0;
    size_t functionInterfaceMaxEvaluateDepth = 0;
    size_t functionInterfaceEvaluateTempLimit = 8;
    std::unordered_map<std::string, long long> passTimings;
    CodegenPerformanceCounters performanceCounters;
};

class Phase1Codegen {
public:
    Phase1Codegen(Diagnostics& diagnostics, CodegenOptions options);
    CodegenResult generate(const Program& program);

private:
    struct ArrayShape {
        std::vector<int> dimensions;
        bool structInstanceField = false;
    };

    struct FieldInfo {
        const FieldDecl* decl = nullptr;
        std::string name;
        std::string typeName;
        std::string generatedName;
        bool isStatic = false;
        bool isArray = false;
        bool isFixedArray = false;
        int fixedArraySize = 0;
        std::vector<int> arrayDimensions;
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
        const Decl* libraryContainer = nullptr;
        std::string originalName;
        std::string generatedName;
        std::string prefix;
        int typeId = 0;
        bool isArrayStruct = false;
        std::vector<FieldInfo> fields;
        std::vector<MethodInfo> methods;
        std::unordered_map<std::string, size_t> fieldIndex;
        std::unordered_map<std::string, size_t> methodIndex;
    };

    struct StructInitializerInfo {
        std::string name;
        const Decl* libraryContainer = nullptr;
    };

    struct FunctionSignature {
        std::vector<std::string> paramTypes;
        std::string returnType = "nothing";
    };

    struct FunctionInfo {
        std::string sourceName;
        std::string finalName;
        FunctionSignature signature;
        bool isStaticMethod = false;
    };

    struct InterfaceTarget {
        std::string finalName;
        int id = 0;
        bool needsAction = false;
    };

    struct FunctionInterfaceInfo {
        const Decl* decl = nullptr;
        const Decl* container = nullptr;
        std::string sourceName;
        std::string finalName;
        FunctionSignature signature;
        std::vector<InterfaceTarget> targets;
        std::unordered_map<std::string, size_t> targetIndexByFinalName;
        bool allTargetsNeedAction = false;
        bool syntheticFunctionObject = false;
    };

    struct LambdaInfo {
        std::string name;
        SourceLocation loc;
        const StructInfo* currentStruct = nullptr;
        const Decl* container = nullptr;
        std::vector<ParamDecl> params;
        TypeRef returnType;
        std::vector<std::string> bodyLines;
    };

    struct LoweringContext {
        const StructInfo* currentStruct = nullptr;
        const Decl* container = nullptr;
        std::unordered_map<std::string, std::string>* localTypes = nullptr;
        std::unordered_map<std::string, ArrayShape>* localArrayShapes = nullptr;
        std::vector<std::string>* tempLocals = nullptr;
        int tempCounter = 0;
    };

    void emitGlobals(const Program& program, const LibraryGraphResult& graph);
    void emitDeclGlobals(const Decl& decl, const Decl* container);
    void emitStructGlobals();
    void emitStructGlobalBlock(const StructInfo& info);
    void emitFunctionInterfaceGlobals();
    void emitTypesAndNatives(const Program& program, const LibraryGraphResult& graph);
    void emitTypeOrNative(const Decl& decl);
    void emitFunctions(const Program& program, const LibraryGraphResult& graph);
    void emitDeclFunctions(const Decl& decl, const Decl* container);
    void emitJassFunction(const Decl& decl, const Decl* container, bool injectInit);
    void emitZincFunction(const Decl& decl, const Decl* container);
    void emitGeneratedLambdas();
    void emitFunctionInterfaceRuntime();
    void emitStructFunctions();
    void emitStructSupportFunctions(const StructInfo& info);
    void emitStructMethod(const StructInfo& info, const MethodInfo& method);
    void emitInitHelper(const LibraryGraphResult& graph, const Program& program);
    void collectInitializers(const Decl& decl, const Decl* container);
    void collectRootInitializers(const std::vector<Decl>& decls);
    void collectStructs(const std::vector<Decl>& decls, const Decl* container, const Decl* libraryContainer);
    void collectStruct(const Decl& decl, const Decl* container, const Decl* libraryContainer);
    void collectFunctionInterfaces(const std::vector<Decl>& decls, const Decl* container);
    void collectFunctionObjectEvaluateInterfaces(const std::vector<Decl>& decls,
                                                 const Decl* container,
                                                 const StructInfo* currentStruct);
    void collectFunctionObjectEvaluateInterfaceFromLine(const std::string& line, const StructInfo* currentStruct);
    void collectFunctions(const std::vector<Decl>& decls, const Decl* container);
    void collectFunction(const Decl& decl, const Decl* container);
    void rememberFunctionInterfaceParamTypes(const std::string& sourceName,
                                             const std::string& finalName,
                                             const std::vector<std::string>& paramTypes,
                                             const Decl* declContainer);

    std::string rewriteForContainer(const std::string& line, const Decl* container) const;
    std::string rewriteGlobalLine(const std::string& line, const Decl* container);
    std::string rewriteFunctionHeader(const Decl& decl, const Decl* container) const;
    std::string renameInContainer(const std::string& name, const Decl* container) const;
    std::string makeScopedStructName(const Decl& decl, const Decl* container) const;
    std::string fixedArrayStorageName(const StructInfo& info, const FieldInfo& field) const;
    int structFixedArrayStride(const StructInfo& info) const;
    int structInstanceLimit(const StructInfo& info) const;
    std::string rewriteTypeName(const std::string& typeName, const StructInfo* currentStruct) const;
    std::string rewriteParamList(const std::vector<ParamDecl>& params, bool includeThis, const StructInfo* currentStruct) const;
    std::string rewriteStructExpression(const std::string& line,
                                        const StructInfo* currentStruct,
                                        const std::unordered_map<std::string, std::string>& localTypes) const;
    std::string rewriteReceiverChains(const std::string& line, const StructInfo* currentStruct) const;
    std::vector<std::string> rewriteLocalDeclLine(const std::string& line,
                                                  const StructInfo* currentStruct,
                                                  std::unordered_map<std::string, std::string>& localTypes,
                                                  std::unordered_map<std::string, ArrayShape>* localArrayShapes,
                                                  std::vector<std::string>& extraLines) const;
    std::string rewriteArrayAccesses(const std::string& line,
                                     const std::unordered_map<std::string, ArrayShape>* localArrayShapes) const;
    std::vector<std::string> lowerStatementLine(const std::string& line, LoweringContext& ctx) const;
    std::string lowerExpression(std::string expression,
                                const std::string& expectedInterfaceType,
                                LoweringContext& ctx,
                                std::vector<std::string>& prelude) const;
    std::string inferUniqueFunctionInterfaceTypeForFunctionValue(const std::string& expression,
                                                                 const LoweringContext& ctx) const;
    std::string lowerFunctionValue(std::string expression,
                                   const std::string& expectedInterfaceType,
                                   LoweringContext& ctx,
                                   std::vector<std::string>& prelude) const;
    std::string rewriteCallArguments(std::string expression, LoweringContext& ctx, std::vector<std::string>& prelude) const;
    std::string rewriteFunctionNames(std::string expression, const StructInfo* currentStruct) const;
    const FunctionInterfaceInfo* resolveReceiverInterface(const std::string& receiver, const LoweringContext& ctx) const;
    std::string rewriteReceiverExpression(const std::string& receiver, const LoweringContext& ctx) const;
    int registerInterfaceTarget(const FunctionInterfaceInfo& iface, const std::string& targetName, SourceLocation loc) const;
    void markInterfaceTargetNeedsAction(const FunctionInterfaceInfo& iface,
                                        const std::string& rewrittenReceiverExpression,
                                        SourceLocation loc) const;
    const FunctionInterfaceInfo* findFunctionInterface(std::string_view name) const;
    FunctionInterfaceInfo& ensureFunctionObjectInterface(const FunctionSignature& signature) const;
    const FunctionInterfaceInfo* findFunctionObjectInterface(const FunctionSignature& signature) const;
    std::string functionSignatureKey(const FunctionSignature& signature) const;
    std::string resolveFunctionInterfaceTypeName(std::string_view name, const Decl* container) const;
    const FunctionInfo* findFunctionInfo(std::string_view name) const;
    std::string resolveFunctionTargetName(const std::string& expression, const StructInfo* currentStruct) const;
    bool sameSignature(const FunctionSignature& a, const FunctionSignature& b) const;
    FunctionSignature signatureFromParams(const std::vector<ParamDecl>& params, const TypeRef& returnType, const StructInfo* currentStruct) const;
    FunctionSignature parseFunctionSignatureFromHeader(const std::string& header, SyntaxMode mode) const;
    std::string interfaceGlobalPrefix(const FunctionInterfaceInfo& iface) const;
    void seedFunctionLocalTypes(const std::string& header, std::unordered_map<std::string, std::string>& localTypes) const;
    void seedMethodLocalTypes(const StructInfo& info, const MethodInfo& method, std::unordered_map<std::string, std::string>& localTypes) const;
    const StructInfo* findStruct(std::string_view name) const;
    const FieldInfo* findField(const StructInfo& info, std::string_view name) const;
    const MethodInfo* findMethod(const StructInfo& info, std::string_view name) const;
    bool structUsesDeallocate(const StructInfo& info) const;

    std::vector<std::string> lowerZincBody(const std::vector<std::string>& lines);
    std::vector<std::string> extractZincLambdas(const std::vector<std::string>& lines, SourceLocation loc);
    void recordLambdaContext(const std::string& beforeText);
    CodegenResult makeResult(bool ok);
    void lowerZincBlock(const std::vector<std::string>& lines, size_t& index, std::vector<std::string>& locals, std::vector<std::string>& body);
    void lowerZincSimpleStatement(const std::string& statement, std::vector<std::string>& locals, std::vector<std::string>& body);

    Diagnostics& diagnostics_;
    CodegenOptions options_;
    SymbolTable symbols_;
    CodeWriter writer_;
    std::unordered_set<std::string> emittedNatives_;
    std::vector<std::pair<std::string, bool>> initializers_;
    std::vector<StructInitializerInfo> structInitializers_;
    std::vector<StructInfo> structs_;
    std::unordered_map<const Decl*, size_t> structIndexByDecl_;
    std::unordered_map<std::string, size_t> structIndexByName_;
    std::unordered_map<std::string, ArrayShape> globalArrayShapes_;
    std::unordered_map<std::string, std::string> globalStructTypes_;
    mutable std::vector<FunctionInterfaceInfo> functionInterfaces_;
    mutable std::unordered_map<std::string, size_t> functionInterfaceIndexByName_;
    mutable std::unordered_map<std::string, size_t> functionObjectInterfaceIndexBySignature_;
    std::vector<FunctionInfo> functions_;
    std::unordered_map<std::string, size_t> functionIndexByName_;
    std::unordered_map<std::string, std::vector<std::string>> functionInterfaceParamTypesByFunction_;
    std::vector<size_t> arrayStructIndexes_;
    std::vector<LambdaInfo> lambdas_;
    std::unordered_map<const Decl*, std::vector<std::string>> processedZincFunctionBodies_;
    std::unordered_map<const MethodDecl*, std::vector<std::string>> processedZincMethodBodies_;
    size_t nextLambdaId_ = 1;
    size_t lambdasCodeContext_ = 0;
    size_t lambdasBoolexprContext_ = 0;
    mutable size_t lambdasFunctionInterfaceContext_ = 0;
    size_t lambdasNativeCallbackContext_ = 0;
    size_t lambdasMethodCallbackContext_ = 0;
    size_t lambdasUnknownContext_ = 0;
    size_t lambdasCapturing_ = 0;
    size_t lambdasRejected_ = 0;
    const StructInfo* lambdaContextStruct_ = nullptr;
    const Decl* lambdaContextContainer_ = nullptr;
    mutable size_t functionInterfaceCalls_ = 0;
    mutable size_t functionObjectCalls_ = 0;
    mutable size_t functionInterfaceMaxEvaluateDepth_ = 0;
    static constexpr size_t functionInterfaceEvaluateTempLimit_ = 8;
    std::unordered_map<std::string, long long> passTimings_;
    mutable CodegenPerformanceCounters performanceCounters_;
    mutable std::unordered_map<std::string, const StructInfo*> structLookupCache_;
    mutable std::unordered_map<std::string, const FunctionInfo*> functionLookupCache_;
    mutable std::unordered_map<std::string, bool> arrayStructReceiverCache_;
    mutable std::unordered_map<std::string, bool> structDeallocateCache_;
    const Decl* mainFunction_ = nullptr;
    const Decl* mainContainer_ = nullptr;
};

} // namespace vjassc
