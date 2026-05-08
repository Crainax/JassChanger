# vjassc

`vjassc` is a C++20 phase-3 compiler prototype for lowering a supported subset of vJASS/Zinc to plain Warcraft III JASS.

Phase 1 built the compiler foundation: file loading, preprocessing, lexing, top-level parsing, library sorting, minimal public/private rewriting, basic Zinc function lowering, diagnostics, stats, and golden fixture tests.

Phase 2 added structured vJASS/Zinc `struct` and `method` parsing plus basic lowering for fields, methods, static fields, static methods, `thistype`, default allocate/create/destroy, `onDestroy`, and static `onInit`.

Phase 3 adds pre-parse `static if` pruning, `LIBRARY_X` / `DEBUG_MODE` / constant boolean evaluation, and AST-level module expansion for vJASS `implement` plus Zinc `module` / `optional module` use. It is still not a complete JassHelper replacement: `function interface`, lambda lowering, interface dispatch, delegate, operator overloads, stub/super, and other advanced extensions remain future work.

## Repository Layout

- `src/`: compiler implementation
- `tests/fixtures/`: golden cases for the supported phase-1/2/3 subset
- `samples/input.j`: large real input used for scan-only validation
- `samples/output_jasshelper.j`: legacy JassHelper output reference for later phases
- `jasshelper/`: old compiler package, kept for behavior comparisons when needed
- `docs/phase1_status.md`: phase-1 implementation status and scan baseline
- `docs/phase2_status.md`: phase-2 implementation status and scan baseline
- `docs/phase3_status.md`: phase-3 implementation status and scan baseline

## Build

Windows with Visual Studio Build Tools, CMake, and Ninja:

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Linux or WSL with a C++20 compiler:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## CLI

```bash
vjassc <input.j> -o <output.j>
vjassc <input.j> -o <output.j> --debug
vjassc <input.j> --scan-only --allow-unsupported
vjassc <input.j> --emit-preprocessed build/preprocessed.j
vjassc <input.j> --emit-tokens build/tokens.txt
vjassc <input.j> --emit-ast build/ast.txt
vjassc <input.j> --emit-expanded-ast build/expanded.ast.txt
vjassc <input.j> --emit-stats build/stats.json
vjassc --help
vjassc --version
```

Example:

```bash
build/vjassc samples/minimal_phase1.j -o build/minimal_phase1.out.j
build/vjassc samples/input.j --scan-only --allow-unsupported --emit-stats build/input.phase3.stats.json
build/vjassc tests/fixtures/phase3_module_zinc_cross_library.in.j -o build/phase3_module_zinc_cross_library.out.j
```

## Phase Boundary

Phase 3 supports small vJASS/Zinc static-if and module cases in its declared subset. It intentionally refuses full code generation when remaining unsupported declarations such as `function interface`, lambda constructs, interface/delegate/operator/stub/super, or other future-phase syntax are present. `--allow-unsupported` is only for scan-only validation and statistics; it does not make partial code generation safe.
