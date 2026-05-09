# Phase 17 Status

Phase 17 focused on the second performance pass for `Phase1Codegen`, War3Lib
mode routing, and keeping fast mode honest about invalid source/output shapes.
The run improved lookup and scan counters heavily and kept PJASS green, but it
did not fully reach every aggressive timing target.

## Implemented

- Added `CodegenOptions::fastMode` and stopped charging fast mode with
  `finalOutputValidationPrep`.
- Replaced the `InitTrig_*` missing-call regex pass with direct line scanning.
- Avoided broad function argument lookup for callees that cannot require
  function-interface/function-value lowering.
- Reduced struct/function lookup hot paths by using existing name indexes and
  small caches instead of repeated lookup calls.
- Narrowed current-struct field/method rewrite entry with line features and
  cheap `this` / `thistype` / `.` guards.
- Counted array rewrite attempts only when a known array receiver is actually
  encountered.
- Added a function-ordering prefilter so dependency token scans skip lines
  without calls or `function` references.
- Added fast-mode statement-shape validation and parser errors for bare numeric
  Zinc members, so a stray `123` is no longer silently accepted.
- Updated War3Lib vjassc tasks so `WAR3_VJASSC_MODE=fast` reaches compare mode
  instead of being forced back to `full-validation`.

## Standalone Results

Commands were run against `samples/input.j` with release `build/vjassc.exe`.
The baseline files are the Phase 17 pre-edit captures.

| Mode | Baseline total | Phase 17 total | Baseline codegen | Phase 17 codegen |
| --- | ---: | ---: | ---: | ---: |
| fast | 18410 | 17486 | 17786 | 16626 |
| validate | 20697 | 18292 | 17612 | 15569 |
| full-validation | 21536 | 17552 | 18062 | 14432 |

Correctness:

| Check | Result |
| --- | --- |
| `ctest --test-dir build --output-on-failure` | pass |
| validate syntax-lite | pass, 0 issues |
| validate init validation | pass, 0 issues |
| validate PJASS | pass, groupedCount 0 |
| full-validation syntax-lite | pass, 0 issues |
| full-validation init validation | pass, 0 issues |
| full-validation PJASS | pass, groupedCount 0 |

## Counters Delta

| Metric | Baseline | Phase 17 |
| --- | ---: | ---: |
| arrayAccessRewriteAttempts | 108677 | 25738 |
| arrayAccessRewriteChanged | 7352 | 7352 |
| functionLookupCalls | 125223 | 29933 |
| structLookupCalls | 90306 | 38325 |
| functionOrderTokenScans | 208696 | 110035 |
| lineFeatureScans | 144792 | 143901 |

## Top Pass Timings

Latest representative artifacts:

- `build/input.phase17.final.fast.hot4.stats.json`
- `build/input.phase17.final.validate3.stats.json`
- `build/input.phase17.final.full-validation3.stats.json`

| Mode | sourceMethods | finalPrep | sanitize | functionOrdering |
| --- | ---: | ---: | ---: | ---: |
| fast | 10020 | 0 | 1646 | 559 |
| validate | 8956 | 2254 | 1662 | 592 |
| full-validation | 8093 | 2208 | 1678 | 530 |

## War3Lib / Xlimon ALPHA

The temporary `123` inserted in `edit/hero/data/HeroCondition.j` was removed
after confirming both jasshelper and vjassc catch it. With the corrected source,
ALPHA builds pass again.

Validate command:

```bat
set WAR3_VJASSC_EXE=D:/Project/JassChanger/build/vjassc.exe
set WAR3_VJASSC_MODE=validate
lua Lua/tasks/TaskCompileAlphaWithVjassc.lua D:/War3 D:/War3/Maps/Xlimon D:/WE/KKWE_Plugin
```

Validate result: pass. The task selected `Output/5_vjassc.j` and wrote
`Output/output.j`.

| Metric | Value |
| --- | ---: |
| vjassc elapsedMs | 18972 |
| vjassc internal total | 18884 |
| vjassc internal codegen | 15872 |
| syntaxLite | 1960 |
| PJASS | 304 |
| jassCompilerMs | 19374 |
| totalCompileMs | 30087 |

Fast compare command:

```bat
set WAR3_VJASSC_MODE=fast
lua Lua/tasks/TaskCompileCompareAlpha.lua D:/War3 D:/War3/Maps/Xlimon D:/WE/KKWE_Plugin
```

Fast compare result: pass. The compare task recorded real fast-mode vjassc
timing and selected JassHelper output for the final map output.

| Metric | Value |
| --- | ---: |
| jasshelper elapsedMs | 11935 |
| vjassc fast elapsedMs | 15777 |
| vjassc fast internal total | 15679 |
| vjassc fast codegen | 14883 |
| jassCompilerMs | 28100 |
| totalCompileMs | 39028 |

## Answers

- standalone fast is not yet faster than JassHelper in the War3Lib compare
  sense. It improved versus the Phase 17 baseline, but still misses the
  `<= 17000 ms` standalone target by about 486 ms in the captured run.
- War3Lib ALPHA fast is not faster than JassHelper: `15777 ms` vs `11935 ms`.
- validate mode is usable as a daily candidate: ALPHA validate compiled and the
  standalone validate/full-validation paths are PJASS green.
- Remaining hotspots are source method lowering, final sanitize/order passes,
  and line-feature scanning. `emitStructSupport.sourceMethods` is still around
  8-10 seconds, so Phase 18 should move from gating regex paths to a real
  token/member lowering plan.

## Next Phase

- Build a method-level lowering plan with field/method/local shadow sets and
  use it to skip source-method body rewriting at function scope, not just line
  scope.
- Replace the current-struct field/method regex blocks with a token-level member
  rewriter.
- Keep fast mode's new statement-shape guard; it is cheap enough and prevents
  invalid source like a bare `123` from being treated as a valid compile.
