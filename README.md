# vjassc

`vjassc` is a C++20 phase-8 compiler prototype for lowering a supported subset of vJASS/Zinc to plain Warcraft III JASS.

Phase 1 built the compiler foundation: file loading, preprocessing, lexing, top-level parsing, library sorting, minimal public/private rewriting, basic Zinc function lowering, diagnostics, stats, and golden fixture tests.

Phase 2 added structured vJASS/Zinc `struct` and `method` parsing plus basic lowering for fields, methods, static fields, static methods, `thistype`, default allocate/create/destroy, `onDestroy`, and static `onInit`.

Phase 3 added pre-parse `static if` pruning, `LIBRARY_X` / `DEBUG_MODE` / constant boolean evaluation, and AST-level module expansion for vJASS `implement` plus Zinc `module` / `optional module` use.

Phase 4 added `function interface` parsing/lowering, function-object `.execute/.evaluate`, function-object `.name`, static-method function references, callback trigger wrappers, and non-capturing Zinc anonymous function lowering for the supported fixture subset.

Phase 5 unlocks full code generation for the real lambda-heavy `samples/input.j`, lowers nested/same-call anonymous functions, emits lambda/function-interface metrics, and adds a lightweight generated-output syntax check. It is still not a complete JassHelper replacement: interface dispatch, delegate, operator overloads, stub/super, capturing closures, byte-for-byte output matching, pjass validation, and runtime validation remain future work.

Phase 6 adds PJASS validation hooks, stronger output syntax-lite checks, initialization integrity validation, and structural comparison against the legacy JassHelper output. The current real-sample output passes syntax-lite and writes validation reports, but PJASS still fails on remaining lowering gaps; runtime validation and behavior matching remain future work.

Phase 7 reduces the first PJASS blocker set by filtering block comments before parse/lowering, lowering fixed and multi-dimensional arrays to JASS arrays, normalizing generated struct names, removing duplicate generated declarations, preserving public/private rewrites inside lambdas, and writing grouped PJASS examples into validation reports. The real sample now passes syntax-lite with zero duplicate function/global/native names, but PJASS still fails on remaining ordering and advanced struct/member lowering gaps.

Phase 8 legalizes more generated JASS by splitting comma-separated locals, normalizing inline Zinc one-line control forms, lowering indexed struct member access, moving lambda/function blocks before non-cyclic uses, and expanding syntax-lite/PJASS triage. The real sample still does not pass PJASS, but syntax-lite is green with comma-local, inline-Zinc, and indexed-struct residues at zero.

## Repository Layout

- `src/`: compiler implementation
- `tests/fixtures/`: golden cases for the supported phase-1/2/3/4/5/6/7/8 subset
- `samples/input.j`: large real input used for scan-only validation
- `samples/output_jasshelper.j`: legacy JassHelper output reference for later phases
- `jasshelper/`: old compiler package, kept for behavior comparisons when needed
- `docs/phase1_status.md`: phase-1 implementation status and scan baseline
- `docs/phase2_status.md`: phase-2 implementation status and scan baseline
- `docs/phase3_status.md`: phase-3 implementation status and scan baseline
- `docs/phase4_status.md`: phase-4 implementation status and scan baseline
- `docs/phase5_status.md`: phase-5 full-codegen status and real input baseline
- `docs/phase6_status.md`: phase-6 validation, PJASS, comparison, and performance baseline
- `docs/phase7_status.md`: phase-7 PJASS blocker cleanup status and remaining blockers
- `docs/phase8_status.md`: phase-8 declaration-order, local/control/member lowering, and PJASS triage status

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
vjassc <input.j> -o <output.j> --emit-validation-report build/validation.json --check-output-syntax-lite
vjassc <input.j> -o <output.j> --validate-pjass --pjass jasshelper/pjass.exe --common jasshelper/common.j --blizzard jasshelper/blizzard.j
vjassc --help
vjassc --version
```

Example:

```bash
build/vjassc samples/minimal_phase1.j -o build/minimal_phase1.out.j
build/vjassc samples/input.j --scan-only --allow-unsupported --emit-stats build/input.phase5.scan.stats.json
build/vjassc samples/input.j -o build/input.phase5.out.j --emit-stats build/input.phase5.codegen.stats.json --check-output-syntax-lite
build/vjassc samples/input.j -o build/input.phase6.out.j --emit-stats build/input.phase6.codegen.stats.json --emit-validation-report build/input.phase6.validation.json --compare-jasshelper samples/output_jasshelper.j --check-output-syntax-lite
build/vjassc samples/input.j -o build/input.phase7.out.j --emit-stats build/input.phase7.codegen.stats.json --emit-validation-report build/input.phase7.validation.json --compare-jasshelper samples/output_jasshelper.j --check-output-syntax-lite
build/vjassc samples/input.j -o build/input.phase7.pjass.out.j --emit-stats build/input.phase7.pjass.stats.json --emit-validation-report build/input.phase7.pjass.validation.json --compare-jasshelper samples/output_jasshelper.j --check-output-syntax-lite --validate-pjass --pjass jasshelper/pjass.exe --common jasshelper/common.j --blizzard jasshelper/blizzard.j
build/vjassc samples/input.j -o build/input.phase8.out.j --emit-stats build/input.phase8.codegen.stats.json --emit-validation-report build/input.phase8.validation.json --compare-jasshelper samples/output_jasshelper.j --check-output-syntax-lite
build/vjassc samples/input.j -o build/input.phase8.pjass.out.j --emit-stats build/input.phase8.pjass.stats.json --emit-validation-report build/input.phase8.pjass.validation.json --compare-jasshelper samples/output_jasshelper.j --check-output-syntax-lite --validate-pjass --pjass jasshelper/pjass.exe --common jasshelper/common.j --blizzard jasshelper/blizzard.j
build/vjassc tests/fixtures/phase4_function_interface_execute.in.j -o build/phase4_function_interface_execute.out.j
```

## Phase Boundary

Phase 8 can generate a complete plain-JASS candidate for the real `samples/input.j`, run syntax-lite validation, write a grouped validation report, run PJASS when paths are provided, validate main/init helper wiring, and compare coarse output structure with `samples/output_jasshelper.j`.

The current output is not yet a JassHelper replacement. PJASS execution is wired up but still fails on remaining source-lowering gaps such as method chains on returned struct instances, callback/code signature adaptation, unresolved environment/source symbols, return mismatches, and a few true cyclic forward references. `--allow-unsupported` is only for scan-only validation and statistics; it does not make partial code generation safe.
