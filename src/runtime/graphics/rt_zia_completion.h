//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_zia_completion.h
// Purpose: Zia language completion engine — runtime bridge declarations.
//
// The implementations live in src/frontends/zia/rt_zia_completion.cpp (part of
// fe_zia). Symbols are resolved at final link time when both fe_zia and
// viper_runtime are linked into the same binary.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>
#include "rt_string.h"

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
