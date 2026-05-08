cmake_minimum_required(VERSION 3.20)
project(vjassc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(vjassc
    src/main.cpp
    src/cli/CliOptions.cpp
    src/core/SourceManager.cpp
    src/core/Diagnostics.cpp
    src/core/StringInterner.cpp
    src/preprocess/Preprocessor.cpp
    src/preprocess/TextMacro.cpp
    src/condition/BoolExpr.cpp
    src/condition/StaticIfPruner.cpp
    src/lexer/Lexer.cpp
    src/parser/Parser.cpp
    src/sema/SymbolTable.cpp
    src/sema/LibraryGraph.cpp
    src/sema/ModuleTable.cpp
    src/sema/ModuleExpander.cpp
    src/codegen/CodeWriter.cpp
    src/codegen/Phase1Codegen.cpp
    src/util/PathUtil.cpp
    src/util/JsonWriter.cpp
)

target_include_directories(vjassc PRIVATE src)

if (MSVC)
    target_compile_options(vjassc PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(vjassc PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_subdirectory(tests)
