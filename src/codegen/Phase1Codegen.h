#pragma once

#include "core/Diagnostics.h"
#include "codegen/CodeWriter.h"
#include "parser/Ast.h"
#include "sema/LibraryGraph.h"
#include "sema/SymbolTable.h"

#include <string>
#include <unordered_set>

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
    void emitGlobals(const Program& program, const LibraryGraphResult& graph);
    void emitDeclGlobals(const Decl& decl, const Decl* container);
    void emitTypesAndNatives(const Program& program, const LibraryGraphResult& graph);
    void emitTypeOrNative(const Decl& decl);
    void emitFunctions(const Program& program, const LibraryGraphResult& graph);
    void emitDeclFunctions(const Decl& decl, const Decl* container);
    void emitJassFunction(const Decl& decl, const Decl* container, bool injectInit);
    void emitZincFunction(const Decl& decl, const Decl* container);
    void emitInitHelper(const LibraryGraphResult& graph, const Program& program);
    void collectInitializers(const Decl& decl, const Decl* container);
    void collectRootInitializers(const std::vector<Decl>& decls);

    std::string rewriteForContainer(const std::string& line, const Decl* container) const;
    std::string rewriteFunctionHeader(const Decl& decl, const Decl* container) const;
    std::string renameInContainer(const std::string& name, const Decl* container) const;

    std::vector<std::string> lowerZincBody(const std::vector<std::string>& lines);
    void lowerZincBlock(const std::vector<std::string>& lines, size_t& index, std::vector<std::string>& locals, std::vector<std::string>& body);
    void lowerZincSimpleStatement(const std::string& statement, std::vector<std::string>& locals, std::vector<std::string>& body);

    Diagnostics& diagnostics_;
    CodegenOptions options_;
    SymbolTable symbols_;
    CodeWriter writer_;
    std::unordered_set<std::string> emittedNatives_;
    std::vector<std::pair<std::string, bool>> initializers_;
    const Decl* mainFunction_ = nullptr;
    const Decl* mainContainer_ = nullptr;
};

} // namespace vjassc
