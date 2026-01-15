# Viper C/C++ Hotspot Report

**Generated:** 2025-12-22

## Scope

- Reviewed C/C++ sources and headers under `src/` and `include/`.
- Hotspots computed via `cloc --by-file`, excluding test sources.
- Generated tables called out explicitly.

## Hotspot Inventory (Top C/C++ Files by SLOC, non-tests)

| File | SLOC | Notes |
|------|------|-------|
| `src/frontends/zia/Lowerer_Expr.cpp` | 2,306 |  |
| `src/il/runtime/RuntimeSignatures.cpp` | 1,972 |  |
| `src/runtime/rt_network_http.c` | 1,938 |  |
| `src/runtime/rt_graphics.c` | 1,588 |  |
| `src/runtime/rt_network.c` | 1,426 |  |
| `src/vm/ops/generated/OpSchema.hpp` | 1,210 |  |
| `src/tools/rtgen/rtgen.cpp` | 1,151 |  |
| `src/frontends/pascal/BuiltinRegistry.cpp` | 1,113 |  |
| `src/frontends/pascal/Lowerer_Stmt.cpp` | 1,104 |  |
| `src/runtime/rt_input_pad.c` | 1,100 |  |
| `src/runtime/rt_input.c` | 1,080 |  |
| `src/il/transform/SCCP.cpp` | 1,075 |  |
| `src/frontends/pascal/SemanticAnalyzer_Expr.cpp` | 1,067 |  |
| `src/frontends/zia/Parser_Expr.cpp` | 1,048 |  |
| `src/runtime/rt_compress.c` | 1,037 |  |
| `src/runtime/rt_archive.c` | 1,021 |  |
| `src/il/verify/generated/SpecTables.cpp` | 974 |  |
| `src/frontends/zia/Lexer.cpp` | 969 |  |
| `src/il/verify/EhChecks.cpp` | 962 |  |
| `src/runtime/rt_regex.c` | 958 |  |

## Large Generated Tables (Non-C/C++ Extensions)

| File | SLOC | Notes |
|------|------|-------|
| `src/il/runtime/generated/RuntimeSignatures.inc` | 8,511 | Generated |
| `src/il/runtime/generated/RuntimeClasses.inc` | 1,270 | Generated |
| `src/il/runtime/generated/RuntimeNameMap.inc` | 1,024 | Generated |
| `src/il/verify/generated/SpecTables.cpp` | 974 | Generated C++ |
| `src/vm/ops/generated/OpSchema.hpp` | 1,210 | Generated C++ |

## Cleanup / Refactor Actions (This Pass)

- **Runtime string/object retention**: ensured object helpers treat runtime string handles as retainable values, preventing heap header corruption when strings flow through object collections.
- **TreeMap retention**: switched TreeMap value retention to `rt_obj_retain_maybe` to align with other collections and support string handles.

## Areas to Monitor (No Refactor Planned)

- Large front-end lowering and parser files (BASIC/Pascal/Zia) remain dense; consider splitting by expression family when new features land.
- Networking and input runtime modules remain sizable; keep protocol parsers isolated from transport layers.
- Generated tables (`RuntimeSignatures.inc`, `OpSchema.hpp`, `SpecTables.cpp`) should remain machine-generated; avoid manual edits.

## Validation

- Build/test status tracked in `docs/devdocs/codex_work.md`.
