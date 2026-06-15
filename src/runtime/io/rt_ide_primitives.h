//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/io/rt_ide_primitives.h
// Purpose: Workspace, asset, manifest, and transactional edit helpers used by
//          ViperIDE and editor-style tooling.
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

#ifdef __cplusplus
}
#endif
