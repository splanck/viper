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
//          src/frontends/zia/rt_zia_completion.cpp (part of
//          zia_editor_services). When zia_editor_services is linked the linker
//          prefers those strong symbols; test binaries that omit editor
//          services fall back to these stubs, which return protocol-shaped
//          "unavailable" payloads for interactive tooling.
//          Diagnostic checks are the exception: they return an empty diagnostic
//          stream so generated apps do not show false editor warnings.
//
// Key invariants:
//   - Stubs use __attribute__((weak)) on Clang/GCC (macOS, Linux); on MSVC
//     the define expands to nothing (MSVC builds always link zia_editor_services).
//   - rt_zia_complete/hover/symbols stubs return valid payloads in the same
//     wire formats as the real completion bridge, with an explicit unavailable
//     message.
//   - rt_zia_check/check_for_file return an empty diagnostic stream. Structured
//     toolchain diagnostics return empty Seq/Map-shaped payloads. A missing
//     analyzer is not a source diagnostic and must not paint editor warnings.
//   - rt_zia_completion_clear_cache stub is a no-op.
//   - If zia_editor_services is linked, none of these functions are called; the
//     overriding strong symbols in rt_zia_completion.cpp take precedence.
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
#include "rt_platform.h" // RT_WEAK (weak-symbol linkage, compiler-abstracted)
#include "rt_seq.h"

#include <string.h>

/// @brief Construct a runtime string from a static C-string payload.
/// @details Helper used by every weak stub in this file to wrap the shared
///          `kUnavailableMessage` (or any caller-provided literal) in a fresh `rt_string`
///          so the IDE's completion / hover / diagnostics tooling receives a well-formed
///          handle even when editor services are not linked into this binary. The caller owns the
///          returned reference.
static rt_string zia_completion_unavailable_string(const char *payload) {
    return rt_string_from_bytes(payload, strlen(payload));
}

static const char *const kUnavailableMessage =
    "Zia completion engine unavailable: link fe_zia to enable editor tooling";

/// @brief Weak stub: returns an unavailable completion item.
/// Overridden by rt_zia_completion.cpp when zia_editor_services is linked.
RT_WEAK rt_string rt_zia_complete(rt_string source, int64_t line, int64_t col) {
    (void)source;
    (void)line;
    (void)col;
    return zia_completion_unavailable_string(
        "Zia completion unavailable\t\t8\tlink fe_zia to enable editor completions\n");
}

/// @brief Weak stub: returns an unavailable completion item.
/// Overridden by rt_zia_completion.cpp when zia_editor_services is linked.
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

/// @brief Weak stub: async diagnostics unavailable; return null job.
RT_WEAK void *rt_zia_toolchain_begin_check_for_file(rt_string source, rt_string file_path) {
    (void)source;
    (void)file_path;
    return NULL;
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

static void zia_stub_map_set_bool(void *map, const char *key_text, int8_t value) {
    rt_string key = rt_string_from_bytes(key_text, strlen(key_text));
    rt_map_set_bool(map, key, value);
    rt_string_unref(key);
}

static void zia_stub_map_set_int(void *map, const char *key_text, int64_t value) {
    rt_string key = rt_string_from_bytes(key_text, strlen(key_text));
    rt_map_set_int(map, key, value);
    rt_string_unref(key);
}

static void zia_stub_map_set_str(void *map, const char *key_text, const char *value_text) {
    rt_string key = rt_string_from_bytes(key_text, strlen(key_text));
    rt_string value = rt_string_from_bytes(value_text, strlen(value_text));
    rt_map_set_str(map, key, value);
    rt_string_unref(value);
    rt_string_unref(key);
}

static void zia_stub_map_set_obj(void *map, const char *key_text, void *value) {
    rt_string key = rt_string_from_bytes(key_text, strlen(key_text));
    rt_map_set(map, key, value);
    rt_string_unref(key);
}

static void *zia_stub_completion_items_unavailable(void) {
    void *seq = rt_seq_new_owned();
    void *item = rt_map_new();
    zia_stub_map_set_str(item, "label", "Zia completion unavailable");
    zia_stub_map_set_str(item, "insertText", "");
    zia_stub_map_set_int(item, "kind", 8);
    zia_stub_map_set_str(item, "kindName", "value");
    zia_stub_map_set_str(item, "detail", "link fe_zia to enable editor completions");
    zia_stub_map_set_str(item, "documentation", "");
    zia_stub_map_set_str(item, "source", "unavailable");
    zia_stub_map_set_str(item, "commitCharacters", "");
    zia_stub_map_set_bool(item, "isSnippet", 0);
    zia_stub_map_set_int(item, "replacementStartLine", 1);
    zia_stub_map_set_int(item, "replacementStartColumn", 0);
    zia_stub_map_set_int(item, "replacementEndLine", 1);
    zia_stub_map_set_int(item, "replacementEndColumn", 0);
    rt_seq_push(seq, item);
    if (item && rt_obj_release_check0(item))
        rt_obj_free(item);
    return seq;
}

/// @brief Weak stub: returns a structured unavailable completion item.
RT_WEAK void *rt_zia_completion_items(rt_string source, int64_t line, int64_t col) {
    (void)source;
    (void)line;
    (void)col;
    return zia_stub_completion_items_unavailable();
}

/// @brief Weak stub: returns a structured unavailable completion item.
RT_WEAK void *rt_zia_completion_items_for_file(rt_string source,
                                               rt_string file_path,
                                               int64_t line,
                                               int64_t col) {
    (void)file_path;
    return rt_zia_completion_items(source, line, col);
}

/// @brief Weak stub: async completion unavailable; return null job.
RT_WEAK void *rt_zia_completion_begin_items_for_file(rt_string source,
                                                     rt_string file_path,
                                                     int64_t line,
                                                     int64_t col) {
    (void)source;
    (void)file_path;
    (void)line;
    (void)col;
    return NULL;
}

static void *zia_stub_signature_unavailable_map(void) {
    void *params = rt_seq_new_owned();
    void *result = rt_map_new();
    zia_stub_map_set_bool(result, "available", 0);
    zia_stub_map_set_str(result, "display", "");
    zia_stub_map_set_str(result, "name", "");
    zia_stub_map_set_str(result, "returnType", "");
    zia_stub_map_set_int(result, "activeParameter", 0);
    zia_stub_map_set_int(result, "activeSignature", 0);
    zia_stub_map_set_int(result, "overloadCount", 0);
    zia_stub_map_set_str(result, "documentation", "");
    zia_stub_map_set_str(result, "source", "unavailable");
    zia_stub_map_set_obj(result, "parameters", params);
    if (params && rt_obj_release_check0(params))
        rt_obj_free(params);
    return result;
}

/// @brief Weak stub: structured signature help unavailable.
RT_WEAK void *rt_zia_signature_info(rt_string source, int64_t line, int64_t col) {
    (void)source;
    (void)line;
    (void)col;
    return zia_stub_signature_unavailable_map();
}

/// @brief Weak stub: structured signature help unavailable.
RT_WEAK void *rt_zia_signature_info_for_file(rt_string source,
                                             rt_string file_path,
                                             int64_t line,
                                             int64_t col) {
    (void)file_path;
    return rt_zia_signature_info(source, line, col);
}

/// @brief Weak stub: async signature unavailable; return null job.
RT_WEAK void *rt_zia_completion_begin_signature_info_for_file(rt_string source,
                                                              rt_string file_path,
                                                              int64_t line,
                                                              int64_t col) {
    (void)source;
    (void)file_path;
    (void)line;
    (void)col;
    return NULL;
}

static void *zia_stub_not_found_map(const char *reason) {
    void *result = rt_map_new();
    zia_stub_map_set_bool(result, "found", 0);
    zia_stub_map_set_str(result, "reason", reason);
    return result;
}

static void *zia_stub_rename_failure_map(const char *reason) {
    void *edits = rt_seq_new_owned();
    void *result = rt_map_new();
    zia_stub_map_set_bool(result, "success", 0);
    zia_stub_map_set_str(result, "reason", reason);
    zia_stub_map_set_obj(result, "edits", edits);
    if (edits && rt_obj_release_check0(edits))
        rt_obj_free(edits);
    return result;
}

/// @brief Weak stub: project indexes require editor services.
RT_WEAK void *rt_zia_project_index_new(rt_string root) {
    (void)root;
    return NULL;
}

/// @brief Weak stub: no project index handle is valid without editor services.
RT_WEAK int8_t rt_zia_project_index_is_valid(void *handle) {
    (void)handle;
    return 0;
}

/// @brief Weak stub: cannot update a missing project index.
RT_WEAK int8_t rt_zia_project_index_update_file(void *handle,
                                                rt_string file_path,
                                                rt_string source) {
    (void)handle;
    (void)file_path;
    (void)source;
    return 0;
}

/// @brief Weak stub: cannot remove from a missing project index.
RT_WEAK int8_t rt_zia_project_index_remove_file(void *handle, rt_string file_path) {
    (void)handle;
    (void)file_path;
    return 0;
}

/// @brief Weak stub: no-op without editor services.
RT_WEAK void rt_zia_project_index_clear(void *handle) {
    (void)handle;
}

/// @brief Weak stub: no-op without editor services.
RT_WEAK void rt_zia_project_index_destroy(void *handle) {
    (void)handle;
}

/// @brief Weak stub: definition lookup unavailable.
RT_WEAK void *rt_zia_project_index_definition(
    void *handle, rt_string file_path, rt_string source, int64_t line, int64_t col) {
    (void)handle;
    (void)file_path;
    (void)source;
    (void)line;
    (void)col;
    return zia_stub_not_found_map("unavailable");
}

/// @brief Weak stub: reference lookup unavailable.
RT_WEAK void *rt_zia_project_index_references(
    void *handle, rt_string file_path, rt_string source, int64_t line, int64_t col) {
    (void)handle;
    (void)file_path;
    (void)source;
    (void)line;
    (void)col;
    return rt_seq_new_owned();
}

/// @brief Weak stub: rename unavailable.
RT_WEAK void *rt_zia_project_index_rename_edits(void *handle,
                                                rt_string file_path,
                                                rt_string source,
                                                int64_t line,
                                                int64_t col,
                                                rt_string new_name) {
    (void)handle;
    (void)file_path;
    (void)source;
    (void)line;
    (void)col;
    (void)new_name;
    return zia_stub_rename_failure_map("unavailable");
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

static void *zia_stub_hover_unavailable_map(void) {
    void *result = rt_map_new();
    zia_stub_map_set_bool(result, "available", 0);
    zia_stub_map_set_str(result, "display", "");
    zia_stub_map_set_str(result, "title", "");
    zia_stub_map_set_str(result, "kind", "");
    zia_stub_map_set_str(result, "name", "");
    zia_stub_map_set_str(result, "type", "");
    zia_stub_map_set_str(result, "documentation", "");
    zia_stub_map_set_str(result, "source", "unavailable");
    return result;
}

/// @brief Weak stub: structured hover unavailable.
RT_WEAK void *rt_zia_hover_info(rt_string source, int64_t line, int64_t col) {
    (void)source;
    (void)line;
    (void)col;
    return zia_stub_hover_unavailable_map();
}

/// @brief Weak stub: structured hover unavailable.
RT_WEAK void *rt_zia_hover_info_for_file(rt_string source,
                                         rt_string file_path,
                                         int64_t line,
                                         int64_t col) {
    (void)file_path;
    return rt_zia_hover_info(source, line, col);
}

/// @brief Weak stub: async hover unavailable; return null job.
RT_WEAK void *rt_zia_completion_begin_hover_info_for_file(rt_string source,
                                                          rt_string file_path,
                                                          int64_t line,
                                                          int64_t col) {
    (void)source;
    (void)file_path;
    (void)line;
    (void)col;
    return NULL;
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

/// @brief Weak stub: async symbols unavailable; return null job.
RT_WEAK void *rt_zia_completion_begin_symbols_for_file(rt_string source, rt_string file_path) {
    (void)source;
    (void)file_path;
    return NULL;
}

/// @brief Weak stub: async semantic tokens unavailable; return null job.
RT_WEAK void *rt_zia_completion_begin_tokens_for_file(rt_string source, rt_string file_path) {
    (void)source;
    (void)file_path;
    return NULL;
}

/// @brief Weak stub: null async jobs are treated as already done.
RT_WEAK int8_t rt_zia_semantic_job_is_done(void *handle) {
    (void)handle;
    return 1;
}

/// @brief Weak stub: null async jobs have no error.
RT_WEAK int8_t rt_zia_semantic_job_is_error(void *handle) {
    (void)handle;
    return 0;
}

/// @brief Weak stub: null async jobs have no error text.
RT_WEAK rt_string rt_zia_semantic_job_error(void *handle) {
    (void)handle;
    return rt_str_empty();
}

/// @brief Weak stub: null async jobs have no kind.
RT_WEAK int64_t rt_zia_semantic_job_kind(void *handle) {
    (void)handle;
    return 0;
}

/// @brief Weak stub: cancel is a no-op.
RT_WEAK void rt_zia_semantic_job_cancel(void *handle) {
    (void)handle;
}

/// @brief Weak stub: empty completion result.
RT_WEAK void *rt_zia_semantic_job_completion_items(void *handle) {
    (void)handle;
    return zia_stub_completion_items_unavailable();
}

/// @brief Weak stub: empty signature result.
RT_WEAK void *rt_zia_semantic_job_signature_info(void *handle) {
    (void)handle;
    return zia_stub_signature_unavailable_map();
}

/// @brief Weak stub: empty hover result.
RT_WEAK void *rt_zia_semantic_job_hover_info(void *handle) {
    (void)handle;
    return zia_stub_hover_unavailable_map();
}

/// @brief Weak stub: empty symbol result.
RT_WEAK rt_string rt_zia_semantic_job_symbols(void *handle) {
    (void)handle;
    return rt_str_empty();
}

/// @brief Weak stub: empty semantic-token result.
RT_WEAK rt_string rt_zia_semantic_job_tokens(void *handle) {
    (void)handle;
    return rt_str_empty();
}

/// @brief Weak stub: empty diagnostic result.
RT_WEAK void *rt_zia_semantic_job_diagnostics(void *handle) {
    (void)handle;
    return rt_seq_new_owned();
}

/// @brief Weak stub: no-op.
RT_WEAK void rt_zia_completion_clear_cache(void) {}
