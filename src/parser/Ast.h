#pragma once

#include "core/SourceManager.h"
#include "preprocess/TextMacro.h"

#include <string>
#include <vector>

namespace vjassc {

enum class DeclKind {
    GlobalBlock,
    Native,
    TypeDecl,
    Function,
    Library,
    Scope,
    Unsupported,
};

struct Requirement {
    std::string name;
    bool optional = false;
};

struct Decl {
    DeclKind kind = DeclKind::Unsupported;
    SyntaxMode mode = SyntaxMode::JassLike;
    SourceLocation loc;
    std::string name;
    std::string access;
    std::string initializer;
    std::string unsupportedFeature;
    bool libraryOnce = false;
    std::vector<Requirement> requirements;
    std::vector<std::string> lines;
    std::vector<Decl> children;
};

struct ParserStats {
    size_t libraries = 0;
    size_t libraryOnce = 0;
    size_t scopes = 0;
    size_t globalsBlocks = 0;
    size_t natives = 0;
    size_t types = 0;
    size_t functions = 0;
    size_t structsUnsupported = 0;
    size_t methodsUnsupported = 0;
    size_t modulesUnsupported = 0;
    size_t staticIfUnsupported = 0;
    size_t functionInterfacesUnsupported = 0;
};

struct Program {
    std::vector<Decl> decls;
    ParserStats stats;

    bool hasUnsupported() const;
};

const char* declKindName(DeclKind kind);

} // namespace vjassc
