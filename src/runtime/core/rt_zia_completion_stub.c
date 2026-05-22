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
//          protocol-shaped "unavailable" payloads for interactive tooling.
//          Diagnostic checks are the exception: they return an empty diagnostic
//          stream so generated apps do not show false editor warnings.
//
// Key invariants:
//   - Stubs use __attribute__((weak)) on Clang/GCC (macOS, Linux); on MSVC
//     the define expands to nothing (MSVC builds always link fe_zia).
//   - rt_zia_complete/hover/symbols stubs return valid payloads in the same
//     wire formats as the real completion bridge, with an explicit unavailable
//     message.
//   - rt_zia_check/check_for_file return an empty diagnostic stream. Structured
//     toolchain diagnostics return empty Seq/Map-shaped payloads. A missing
//     analyzer is not a source diagnostic and must not paint editor warnings.
//   - rt_zia_completion_clear_cache stub is a no-op.
//   - If fe_zia is linked, none of these functions are called; the overriding
//     strong symbols in rt_zia_completion.cpp take precedence.
//
// Ownership/Lifetime:
//   - The string-returning stubs return newly allocated protocol payloads; the
//     caller owns the reference and must call rt_string_unref when done.
//   - No heap allocation is performed by rt_zia_completion_clear_cache.
//
// Links: src/frontends/zia/rt_zia_completion.cpp (strong-symbol overrides),
//        src/runtime/core/rt_string.h (rt_str_empty, rt_string type)
//
//===----------------------------------------------------------------------===//

#include "rt_string.h"

#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"

#include <string.h>

#ifndef _MSC_VER
#define RT_WEAK __attribute__((weak))
#else
// MSVC: accept a duplicate-symbol link error rather than silently stub out;
// production Windows builds always link fe_zia so the issue never arises.
#define RT_WEAK
#endif

/// @brief Construct a runtime string from a static C-string payload.
/// @details Helper used by every weak stub in this file to wrap the shared
///          `kUnavailableMessage` (or any caller-provided literal) in a fresh `rt_string`
///          so the IDE's completion / hover / diagnostics tooling receives a well-formed
///          handle even when fe_zia isn't linked into this binary. The caller owns the
///          returned reference.
static rt_string zia_completion_unavailable_string(const char *payload) {
    return rt_string_from_bytes(payload, strlen(payload));
}

static const char *const kUnavailableMessage =
    "Zia completion engine unavailable: link fe_zia to enable editor tooling";

/// @brief Weak stub: returns an unavailable completion item.
/// Overridden by rt_zia_completion.cpp when fe_zia is linked.
RT_WEAK rt_string rt_zia_complete(rt_string source, int64_t line, int64_t col) {
    (void)source;
    (void)line;
    (void)col;
    return zia_completion_unavailable_string(
        "Zia completion unavailable\t\t8\tlink fe_zia to enable editor completions\n");
}

/// @brief Weak stub: returns an unavailable completion item.
/// Overridden by rt_zia_completion.cpp when fe_zia is linked.
RT_WEAK rt_string rt_zia_complete_for_file(rt_string source,
                                           rt_string file_path,
                                           int64_t line,
                                           int64_t col) {
    (void)source;
    (void)file_path;
    (void)line;
    (void)col;
    return rt_zia_complete(source, line, col);
}

/// @brief Weak stub: returns an unavailable signature-help payload.
RT_WEAK rt_string rt_zia_signature_help(rt_string source, int64_t line, int64_t col) {
    (void)source;
    (void)line;
    (void)col;
    return zia_completion_unavailable_string(kUnavailableMessage);
}

/// @brief Weak stub: returns an unavailable signature-help payload.
RT_WEAK rt_string rt_zia_signature_help_for_file(rt_string source,
                                                 rt_string file_path,
                                                 int64_t line,
                                                 int64_t col) {
    (void)source;
    (void)file_path;
    (void)line;
    (void)col;
    return rt_zia_signature_help(source, line, col);
}

/// @brief Weak stub: diagnostics unavailable means "no diagnostics".
RT_WEAK rt_string rt_zia_check(rt_string source) {
    (void)source;
    return rt_str_empty();
}

/// @brief Weak stub: diagnostics unavailable means "no diagnostics".
RT_WEAK rt_string rt_zia_check_for_file(rt_string source, rt_string file_path) {
    (void)source;
    (void)file_path;
    return rt_zia_check(source);
}

/// @brief Weak stub: structured diagnostics unavailable means "no diagnostics".
RT_WEAK void *rt_zia_toolchain_check(rt_string source) {
    (void)source;
    return rt_seq_new_owned();
}

/// @brief Weak stub: structured diagnostics unavailable means "no diagnostics".
RT_WEAK void *rt_zia_toolchain_check_for_file(rt_string source, rt_string file_path) {
    (void)source;
    (void)file_path;
    return rt_zia_toolchain_check(source);
}

/// @brief Weak stub: compile unavailable returns a structured failed result
///        without source diagnostics.
RT_WEAK void *rt_zia_toolchain_compile(rt_string source) {
    (void)source;
    void *diagnostics = rt_seq_new_owned();
    void *result = rt_map_new();

    rt_string success_key = rt_string_from_bytes("success", 7);
    rt_map_set_bool(result, success_key, 0);
    rt_string_unref(success_key);

    rt_string diagnostics_key = rt_string_from_bytes("diagnostics", 11);
    rt_map_set(result, diagnostics_key, diagnostics);
    rt_string_unref(diagnostics_key);
    if (diagnostics && rt_obj_release_check0(diagnostics))
        rt_obj_free(diagnostics);

    rt_string source_path_key = rt_string_from_bytes("sourcePath", 10);
    rt_string empty = rt_str_empty();
    rt_map_set_str(result, source_path_key, empty);
    rt_string_unref(empty);
    rt_string_unref(source_path_key);

    rt_string output_path_key = rt_string_from_bytes("outputPath", 10);
    empty = rt_str_empty();
    rt_map_set_str(result, output_path_key, empty);
    rt_string_unref(empty);
    rt_string_unref(output_path_key);

    rt_string il_key = rt_string_from_bytes("il", 2);
    empty = rt_str_empty();
    rt_map_set_str(result, il_key, empty);
    rt_string_unref(empty);
    rt_string_unref(il_key);

    return result;
}

/// @brief Weak stub: compile unavailable returns a structured failed result
///        without source diagnostics.
RT_WEAK void *rt_zia_toolchain_compile_for_file(rt_string source, rt_string file_path) {
    (void)file_path;
    return rt_zia_toolchain_compile(source);
}

/// @brief Weak stub: returns an unavailable hover payload.
RT_WEAK rt_string rt_zia_hover(rt_string source, int64_t line, int64_t col) {
    (void)source;
    (void)line;
    (void)col;
    return zia_completion_unavailable_string(kUnavailableMessage);
}

/// @brief Weak stub: returns an unavailable hover payload.
RT_WEAK rt_string rt_zia_hover_for_file(rt_string source,
                                        rt_string file_path,
                                        int64_t line,
                                        int64_t col) {
    (void)source;
    (void)file_path;
    (void)line;
    (void)col;
    return rt_zia_hover(source, line, col);
}

/// @brief Weak stub: returns an unavailable symbol payload.
RT_WEAK rt_string rt_zia_symbols(rt_string source) {
    (void)source;
    return zia_completion_unavailable_string(
        "Zia completion unavailable\tstatus\tlink fe_zia to enable document symbols\t1\n");
}

/// @brief Weak stub: returns an unavailable symbol payload.
RT_WEAK rt_string rt_zia_symbols_for_file(rt_string source, rt_string file_path) {
    (void)source;
    (void)file_path;
    return rt_zia_symbols(source);
}

/// @brief Weak stub: no-op.
RT_WEAK void rt_zia_completion_clear_cache(void) {}
