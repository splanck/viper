//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
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
extern "C" {
#endif

/// @brief Run Zia code completion at the given source position.
/// @param source Zia source text (full file contents).
/// @param line   1-based line number of the cursor.
/// @param col    0-based column number of the cursor.
/// @return Tab-delimited completion items: label\tinsertText\tkindInt\tdetail\n
///         Returns an empty string when no completions are available.
rt_string rt_zia_complete(rt_string source, int64_t line, int64_t col);

/// @brief Run semantic analysis and return serialized diagnostics for editor tooling.
/// @param source Zia source text (full file contents).
/// @return One diagnostic per line encoded as severity\tline\tcol\tcode\tmessage.
rt_string rt_zia_check(rt_string source);

/// @brief Return hover information for the identifier at the given source location.
/// @param source Zia source text (full file contents).
/// @param line   1-based line number of the cursor.
/// @param col    0-based column number of the cursor.
/// @return Human-readable hover text, or an empty string when nothing is found.
rt_string rt_zia_hover(rt_string source, int64_t line, int64_t col);

/// @brief Return serialized document symbols for the supplied source.
/// @param source Zia source text (full file contents).
/// @return Tab-delimited symbol rows, or an empty string when no symbols are found.
rt_string rt_zia_symbols(rt_string source);

/// @brief Flush the cached parse result, forcing a fresh parse on the next call.
void rt_zia_completion_clear_cache(void);

#ifdef __cplusplus
}
#endif
