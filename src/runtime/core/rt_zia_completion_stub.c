//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_zia_completion_stub.c
// Purpose: Provides weak-symbol stub implementations for the Zia IntelliSense
//          completion bridge. The real implementations live in
//          src/frontends/zia/rt_zia_completion.cpp (part of fe_zia). When
//          fe_zia is linked the linker prefers those strong symbols; test
//          binaries that omit fe_zia fall back to these stubs, which return
//          empty results rather than causing a link error.
//
// Key invariants:
//   - Stubs use __attribute__((weak)) on Clang/GCC (macOS, Linux); on MSVC
//     the define expands to nothing (MSVC builds always link fe_zia).
//   - rt_zia_complete/check/hover/symbols stubs return rt_str_empty() — a
//     valid, empty rt_string.
//   - rt_zia_completion_clear_cache stub is a no-op.
//   - If fe_zia is linked, none of these functions are called; the overriding
//     strong symbols in rt_zia_completion.cpp take precedence.
//
// Ownership/Lifetime:
//   - The string-returning stubs return a newly allocated empty string; the
//     caller owns the reference and must call rt_string_unref when done.
//   - No heap allocation is performed by rt_zia_completion_clear_cache.
//
// Links: src/frontends/zia/rt_zia_completion.cpp (strong-symbol overrides),
//        src/runtime/core/rt_string.h (rt_str_empty, rt_string type)
//
//===----------------------------------------------------------------------===//

#include "rt_string.h"

#ifndef _MSC_VER
#define RT_WEAK __attribute__((weak))
#else
// MSVC: accept a duplicate-symbol link error rather than silently stub out;
// production Windows builds always link fe_zia so the issue never arises.
#define RT_WEAK
#endif

/// @brief Weak stub: returns an empty string.
/// Overridden by rt_zia_completion.cpp when fe_zia is linked.
RT_WEAK rt_string rt_zia_complete(rt_string source, int64_t line, int64_t col) {
    (void)source;
    (void)line;
    (void)col;
    return rt_str_empty();
}

/// @brief Weak stub: returns an empty string.
/// Overridden by rt_zia_completion.cpp when fe_zia is linked.
RT_WEAK rt_string rt_zia_complete_for_file(rt_string source,
                                           rt_string file_path,
                                           int64_t line,
                                           int64_t col) {
    (void)source;
    (void)file_path;
    (void)line;
    (void)col;
    return rt_str_empty();
}

/// @brief Weak stub: returns an empty diagnostic payload.
RT_WEAK rt_string rt_zia_check(rt_string source) {
    (void)source;
    return rt_str_empty();
}

/// @brief Weak stub: returns an empty diagnostic payload.
RT_WEAK rt_string rt_zia_check_for_file(rt_string source, rt_string file_path) {
    (void)source;
    (void)file_path;
    return rt_str_empty();
}

/// @brief Weak stub: returns an empty hover payload.
RT_WEAK rt_string rt_zia_hover(rt_string source, int64_t line, int64_t col) {
    (void)source;
    (void)line;
    (void)col;
    return rt_str_empty();
}

/// @brief Weak stub: returns an empty hover payload.
RT_WEAK rt_string rt_zia_hover_for_file(rt_string source,
                                        rt_string file_path,
                                        int64_t line,
                                        int64_t col) {
    (void)source;
    (void)file_path;
    (void)line;
    (void)col;
    return rt_str_empty();
}

/// @brief Weak stub: returns an empty symbol payload.
RT_WEAK rt_string rt_zia_symbols(rt_string source) {
    (void)source;
    return rt_str_empty();
}

/// @brief Weak stub: returns an empty symbol payload.
RT_WEAK rt_string rt_zia_symbols_for_file(rt_string source, rt_string file_path) {
    (void)source;
    (void)file_path;
    return rt_str_empty();
}

/// @brief Weak stub: no-op.
RT_WEAK void rt_zia_completion_clear_cache(void) {}
