//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/io/rt_ide_primitives.h
// Purpose: Workspace, asset, manifest, and transactional edit helpers used by
//          Zanna Studio and editor-style tooling.
// Key invariants:
//   - Workspace edit targets are validated before any disk mutation is attempted.
//   - Workspace/file-index helpers never depend on compiler-layer services.
// Ownership/Lifetime:
//   - Runtime strings passed to these functions are borrowed from the caller.
//   - Map and sequence results are runtime-owned objects returned to callers.
// Links: src/runtime/io/rt_ide_primitives.cpp, src/runtime/io/rt_watcher.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_workspace_file_index_enumerate(rt_string root,
                                        rt_string extensions_csv,
                                        rt_string excludes_csv,
                                        int8_t include_dirs);

/// @brief Return one bounded page of workspace file-index entries.
/// @details Applies the same validation, extension filtering, ignore rules, and
///          entry cap as @ref rt_workspace_file_index_enumerate, but emits at
///          most @p limit entries starting at logical match offset @p offset.
///          The result map contains `entries`, `offset`, `limit`, `emitted`,
///          `nextOffset`, `done`, `truncated`, and `diagnostics`.
/// @param root Directory to enumerate.
/// @param extensions_csv Comma-separated extension filter, such as ".zia,.png".
/// @param excludes_csv Comma-separated additional ignore patterns.
/// @param include_dirs Non-zero to count matching directories as entries.
/// @param offset Zero-based logical match offset to start returning.
/// @param limit Maximum entries to emit; values outside 1..4096 are clamped.
/// @return Page result map owned by the caller.
void *rt_workspace_file_index_page(rt_string root,
                                   rt_string extensions_csv,
                                   rt_string excludes_csv,
                                   int8_t include_dirs,
                                   int64_t offset,
                                   int64_t limit);

/// @brief Return status metadata for a workspace file-index traversal.
/// @details Applies the same root validation, extension filtering, ignore rules,
///          and entry cap as @ref rt_workspace_file_index_enumerate without
///          allocating one map per file. The result map contains `valid`, `root`,
///          `entryCount`, `maxEntries`, `truncated`, and `diagnostics`.
/// @param root Directory to enumerate.
/// @param extensions_csv Comma-separated extension filter, such as ".zia,.png".
/// @param excludes_csv Comma-separated additional ignore patterns.
/// @param include_dirs Non-zero to count matching directories as entries.
/// @return Status map owned by the caller.
void *rt_workspace_file_index_status(rt_string root,
                                     rt_string extensions_csv,
                                     rt_string excludes_csv,
                                     int8_t include_dirs);

int8_t rt_workspace_file_index_should_ignore(rt_string root,
                                             rt_string relative_path,
                                             rt_string patterns);
void *rt_workspace_watcher_poll_batch(void *watcher, int64_t max_events);

void *rt_asset_resolver_resolve(rt_string scene_path,
                                rt_string project_root,
                                rt_string asset_roots_csv,
                                rt_string asset_path);

void *rt_project_manifest_parse_text(rt_string text);
void *rt_project_manifest_parse_file(rt_string path);

void *rt_workspace_edit_validate(void *edits);
void *rt_workspace_edit_apply(void *edits);

/// @brief Validate a workspace edit batch against an explicit root directory.
/// @details This is the rooted variant of @ref rt_workspace_edit_validate. Each
///          edit file is canonicalized relative to @p root and rejected if it
///          would escape that tree. The returned map has the same shape as the
///          unrooted validator: `success`, `editCount`, and `diagnostics`.
/// @param edits Seq of edit maps with file/range/newText fields.
/// @param root Workspace root that bounds every edit target.
/// @return Validation result map owned by the caller.
void *rt_workspace_edit_validate_in_root(void *edits, rt_string root);

/// @brief Apply a workspace edit batch constrained to an explicit root directory.
/// @details Validates with @ref rt_workspace_edit_validate_in_root, writes each
///          updated file through a same-directory temporary file, then renames
///          the completed temp into place. Existing files are restored from
///          backups if any later write or rename fails.
/// @param edits Seq of edit maps with file/range/newText fields.
/// @param root Workspace root that bounds every edit target.
/// @return Result map owned by the caller.
void *rt_workspace_edit_apply_in_root(void *edits, rt_string root);

/// @brief Validate a workspace edit batch against explicit workspace roots.
/// @details Every edit target is canonicalized and must be contained by at
///          least one directory in @p roots. Absolute paths are required when
///          more than one root is supplied so relative targets are never
///          resolved ambiguously. The full batch is validated together, which
///          preserves overlap and version checks across root boundaries.
/// @param edits Seq of edit maps with file/range/newText fields.
/// @param roots Seq of workspace-root strings that bound every edit target.
/// @return Validation result map owned by the caller.
void *rt_workspace_edit_validate_in_roots(void *edits, void *roots);

/// @brief Apply one transactional edit batch constrained to workspace roots.
/// @details This is the multi-root counterpart to
///          @ref rt_workspace_edit_apply_in_root. Validation, staging, commit,
///          and rollback cover the complete batch, including edits spanning
///          unrelated workspace folders.
/// @param edits Seq of edit maps with file/range/newText fields.
/// @param roots Seq of workspace-root strings that bound every edit target.
/// @return Result map owned by the caller.
void *rt_workspace_edit_apply_in_roots(void *edits, void *roots);

#ifdef __cplusplus
}
#endif
