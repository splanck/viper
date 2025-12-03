# CODEMAP: Support

Shared support libraries (`src/support/`) used across the toolchain.

## Source Management

| File | Purpose |
|------|---------|
| `source_manager.hpp/cpp` | Source file registration and path normalization |
| `source_location.hpp/cpp` | Source location value type (file, line, column) |

## Diagnostics

| File | Purpose |
|------|---------|
| `diagnostics.hpp/cpp` | Diagnostic engine: collect, count, print messages |
| `diag_expected.hpp/cpp` | Expected/diagnostic wrapper for result-style errors |
| `diag_capture.hpp/cpp` | Diagnostic buffer capture for tests |

## Memory

| File | Purpose |
|------|---------|
| `arena.hpp/cpp` | Bump-pointer arena allocator |

## String Interning

| File | Purpose |
|------|---------|
| `string_interner.hpp/cpp` | String deduplication and symbol table |
| `symbol.hpp/cpp` | Interned symbol identifier type |

## Configuration

| File | Purpose |
|------|---------|
| `options.hpp` | Global compile-time options and toggles |
| `result.hpp` | Minimal Result<T> helper |
