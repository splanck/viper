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

#ifdef __cplusplus
}
#endif
