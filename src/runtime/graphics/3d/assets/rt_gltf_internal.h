//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_gltf_internal.h
// Purpose: Shared glTF JSON accessors used by rt_gltf.c and rt_gltf_trs.c.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_gltf_json.h"

void *jarr(void *obj, const char *key);
int64_t jarr_len(void *arr);
double jvalue_num(void *value, double def);

// TRS helpers (defined in rt_gltf_trs.c) used by rt_gltf.c and its .inc fragments.
void gltf_matrix_to_trs(const double *m, double *pos, double *quat, double *scale);
void gltf_matrix_column_major_to_row_major(const double *src, double *dst);
int gltf_node_local_matrix(void *nodes_arr, int32_t node_idx, double *out);
