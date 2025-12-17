# CODEMAP: Support & Common

Shared support and infrastructure used across the toolchain.

Directories: `src/support/`, `src/common/`, `src/parse/`, `src/pass/`.

## Source Management

| File                      | Purpose                                         |
|---------------------------|-------------------------------------------------|
| `source_manager.hpp/cpp`  | Source file registration and path normalization |
| `source_location.hpp/cpp` | Source location value type (file, line, column) |

## Diagnostics

| File                    | Purpose                                             |
|-------------------------|-----------------------------------------------------|
| `diagnostics.hpp/cpp`   | Diagnostic engine: collect, count, print messages   |
| `diag_expected.hpp/cpp` | Expected/diagnostic wrapper for result-style errors |
| `diag_capture.hpp/cpp`  | Diagnostic buffer capture for tests                 |

## Memory

| File            | Purpose                      |
|-----------------|------------------------------|
| `arena.hpp/cpp` | Bump-pointer arena allocator |

## String Interning

| File                      | Purpose                               |
|---------------------------|---------------------------------------|
| `string_interner.hpp/cpp` | String deduplication and symbol table |
| `symbol.hpp/cpp`          | Interned symbol identifier type       |

## Configuration

| File          | Purpose                                 |
|---------------|-----------------------------------------|
| `options.hpp` | Global compile-time options and toggles |
| `result.hpp`  | Minimal Result<T> helper                |

## Common Utilities (`src/common/`)

| File                 | Purpose                                                |
|----------------------|--------------------------------------------------------|
| `Mangle.hpp/cpp`     | Name mangling helpers used by frontends/codegen        |
| `IntegerHelpers.hpp` | Integer helpers (width/signedness, overflow policies)  |
| `RunProcess.hpp/cpp` | Test helper to spawn subprocesses with env/dir control |

## Parsing Helpers (`src/parse/`)

| File         | Purpose                                                             |
|--------------|---------------------------------------------------------------------|
| `Cursor.cpp` | Source cursor utilities; C header at `include/viper/parse/Cursor.h` |

## Pass Framework (`src/pass/`)

| File              | Purpose                                                                         |
|-------------------|---------------------------------------------------------------------------------|
| `PassManager.cpp` | Generic pass manager façade; public API at `include/viper/pass/PassManager.hpp` |
