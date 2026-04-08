//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_scene3d_internal.h
// Purpose: Internal shared structs for Scene3D / SceneNode3D implementation.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_scene3d.h"
#include <stdint.h>

#ifdef VIPER_ENABLE_GRAPHICS

typedef struct rt_scene_node3d {
    void *vptr;

    double position[3];
    double rotation[4];
    double scale_xyz[3];

    double world_matrix[16];
    int8_t world_dirty;

    struct rt_scene_node3d *parent;
    struct rt_scene_node3d **children;
    int32_t child_count;
    int32_t child_capacity;

    void *mesh;
    void *material;
    void *bound_body;
    void *bound_animator;
    int32_t sync_mode;

    int8_t visible;
    rt_string name;

    float aabb_min[3];
    float aabb_max[3];
    float bsphere_radius;

    struct {
        double distance;
        void *mesh;
    } *lod_levels;

    int32_t lod_count;
    int32_t lod_capacity;
} rt_scene_node3d;

typedef struct {
    void *vptr;
    rt_scene_node3d *root;
    int32_t node_count;
    int32_t last_culled_count;
} rt_scene3d;

#endif /* VIPER_ENABLE_GRAPHICS */
