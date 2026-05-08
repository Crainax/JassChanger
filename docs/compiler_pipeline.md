# Compiler Pipeline

## 1. Read

`SourceManager` loads files into memory, normalizes CRLF/CR to LF, stores file ids, and provides line text for diagnostics.

## 2. Preprocess

`Preprocessor` turns source files into logical lines with syntax mode metadata:

- resolves `//! import` with duplicate-import protection
- removes `//! novjass` blocks
- handles release/debug `debug` lines
- expands `//! textmacro` / `//! runtextmacro`
- switches between JASS-like and Zinc mode with `//! zinc` / `//! endzinc`

The output can be written with `--emit-preprocessed`.

## 3. Lex

`Lexer` tokenizes logical lines while preserving `SourceLocation` and syntax mode. It supports the phase-1 JASS/vJASS and Zinc token set, including strings, rawcodes, comments, braces, assignment operators, arrows, and boolean operators.

The output can be written with `--emit-tokens`.

## 4. Parse

`Parser` builds a lightweight AST for:

- `globals`
- `native`
- `type`
- `function`
- `library` / `library_once`
- `scope`
- basic Zinc `library` / `function` / globals

Unsupported declarations are counted and represented as `Unsupported` nodes where they affect code generation.

The output can be written with `--emit-ast`.

## 5. Symbol And Library Analysis

`SymbolTable` builds deterministic public/private rename maps for functions and globals inside libraries/scopes.

`LibraryGraph` validates and topologically sorts library dependencies, respecting optional requirements and `library_once` duplicate handling.

## 6. Codegen

`Phase1Codegen` emits a clear phase-1 JASS layout:

1. one merged `globals` block with `LIBRARY_` constants
2. type declarations and deduplicated natives
3. functions in sorted library order
4. basic Zinc-lowered functions
5. `vjassc__init_libraries`
6. `main` with init helper injection when present

If unsupported phase-2+ language constructs are present, normal codegen fails instead of producing invalid success.
