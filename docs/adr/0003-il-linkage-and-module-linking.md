# ADR-0003: IL Linkage and Module Linking

## Status

Accepted

## Context

Viper has two frontend languages (Zia and BASIC) that compile to the same IL and
share the same runtime, but there is no mechanism for cross-language interop.
Each frontend produces a self-contained IL module, the VM loads exactly one
module, and there is no IL linker. To support mixed-language projects, the IL
spec needs linkage annotations and a module-linking algorithm.

### Current Limitations

- `il::core::Function` has no linkage or visibility metadata
- Both frontends emit a function named `main`, causing collisions if merged
- `il::core::Module` is self-contained with no import/export mechanism
- The VM and codegen backends process exactly one module
- Boolean representation differs: Zia uses `i1` (0/1), BASIC uses `i64` (-1/0)

## Decision

### 1. Add `Linkage` enum to IL core types

A new `Linkage` enum with three values:
- `Internal` (default) -- visible only within the defining module
- `Export` -- defined here, visible to other modules
- `Import` -- declared here, defined in another module (no body)

Added as a field to `Function` and `Global`, defaulting to `Internal` for full
backwards compatibility.

### 2. IL text syntax

```
func export @name(...) -> type { ... }   // Export linkage
func import @name(...) -> type           // Import linkage (no body)
func @name(...) -> type { ... }          // Internal (default, backwards compat)
```

The keywords `export` and `import` appear between `func` and the function name.
Omitting the keyword implies `Internal`.

### 3. Module linker

A new `il::link` subsystem merges multiple IL modules into one:
- Exactly one module provides `main` (the entry module)
- Export/Import pairs are resolved by name and signature match
- Internal functions from different modules get disambiguating prefixes
- Externs are deduplicated; signature mismatches are link errors
- Init functions from all modules are called before user entry code
- Boolean type mismatches (i1 vs i64) are bridged via auto-generated thunks

### 4. Verifier changes

Import-linkage functions are valid call targets (like externs but with
IL-level type information). They must have no body (empty blocks vector).

## Consequences

- Existing single-language projects are unaffected (all defaults to Internal)
- Existing `.il` files parse correctly (no linkage keyword = Internal)
- The VM and codegen backends require no changes (they see a single merged module)
- Frontends gain `expose`/`foreign` (Zia) and `EXPORT`/`DECLARE FOREIGN` (BASIC)
- Mixed-language projects become possible via the project manifest
