# CODEMAP: Support & Common

Shared support and infrastructure used across the toolchain.

Directories: `src/support/`, `src/common/`, `src/parse/`, `src/pass/`.

Last updated: 2026-01-15

## Overview

- **Total source files**: 27 (.hpp/.cpp)
  - support/: 20 files
  - common/: 5 files
  - parse/: 1 file
  - pass/: 1 file

## Source Management (`src/support/`)

| File                  | Purpose                                         |
|-----------------------|-------------------------------------------------|
| `source_manager.cpp`  | Source file registration implementation         |
| `source_manager.hpp`  | Source file registration and path normalization |
| `source_location.cpp` | Source location implementation                  |
| `source_location.hpp` | Source location value type (file, line, column) |

## Diagnostics (`src/support/`)

| File                 | Purpose                                             |
|----------------------|-----------------------------------------------------|
| `diagnostics.cpp`    | Diagnostic engine implementation                    |
| `diagnostics.hpp`    | Diagnostic engine: collect, count, print messages   |
| `diag_expected.cpp`  | Expected/diagnostic wrapper implementation          |
| `diag_expected.hpp`  | Expected/diagnostic wrapper for result-style errors |
| `diag_capture.cpp`   | Diagnostic buffer capture implementation            |
| `diag_capture.hpp`   | Diagnostic buffer capture for tests                 |

## Memory (`src/support/`)

| File        | Purpose                           |
|-------------|-----------------------------------|
| `arena.cpp` | Bump-pointer arena implementation |
| `arena.hpp` | Bump-pointer arena allocator      |

## String Interning (`src/support/`)

| File                  | Purpose                               |
|-----------------------|---------------------------------------|
| `string_interner.cpp` | String deduplication implementation   |
| `string_interner.hpp` | String deduplication and symbol table |
| `symbol.cpp`          | Interned symbol implementation        |
| `symbol.hpp`          | Interned symbol identifier type       |

## Configuration (`src/support/`)

| File              | Purpose                                 |
|-------------------|-----------------------------------------|
| `options.hpp`     | Global compile-time options and toggles |
| `result.hpp`      | Minimal Result<T> helper                |
| `small_vector.hpp`| Small-buffer-optimized vector type      |

## Common Utilities (`src/common/`)

| File                 | Purpose                                                |
|----------------------|--------------------------------------------------------|
| `Mangle.cpp`         | Name mangling implementation                           |
| `Mangle.hpp`         | Name mangling helpers used by frontends/codegen        |
| `IntegerHelpers.hpp` | Integer helpers (width/signedness, overflow policies)  |
| `RunProcess.cpp`     | Test helper to spawn subprocesses implementation       |
| `RunProcess.hpp`     | Test helper to spawn subprocesses with env/dir control |

## Parsing Helpers (`src/parse/`)

| File         | Purpose                                                             |
|--------------|---------------------------------------------------------------------|
| `Cursor.cpp` | Source cursor utilities; C header at `include/viper/parse/Cursor.h` |

## Pass Framework (`src/pass/`)

| File              | Purpose                                                                         |
|-------------------|---------------------------------------------------------------------------------|
| `PassManager.cpp` | Generic pass manager facade; public API at `include/viper/pass/PassManager.hpp` |
