//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_zia_completion.h
// Purpose: Runtime bridge declarations for the Zia language completion engine, provided by fe_zia
// at link time via weak/strong symbol resolution.
//
// Key invariants:
//   - Strong implementations live in src/frontends/zia/rt_zia_completion.cpp (fe_zia).
//   - Weak stub implementations in viper_runtime allow linking without fe_zia.
//   - Symbols are resolved at final link time when both fe_zia and viper_runtime are linked.
//   - Completion API takes source text, cursor line (1-based), and column (0-based).
//
// Ownership/Lifetime:
//   - Returned completion results are heap-allocated strings; caller must release them.
//   - Source text string is borrowed for the duration of the call only.
//
// Links: src/frontends/zia/rt_zia_completion.cpp (strong implementation),
// src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Run Zia code completion at the given source position.
    /// @param source Zia source text (full file contents).
    /// @param line   1-based line number of the cursor.
    /// @param col    0-based column number of the cursor.
    /// @return Tab-delimited completion items: label\tinsertText\tkindInt\tdetail\n
    ///         Returns an empty string when no completions are available.
    rt_string rt_zia_complete(rt_string source, int64_t line, int64_t col);

    /// @brief Flush the cached parse result, forcing a fresh parse on the next call.
    void rt_zia_completion_clear_cache(void);

#ifdef __cplusplus
}
#endif
