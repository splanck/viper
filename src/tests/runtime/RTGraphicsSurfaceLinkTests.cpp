//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp
// Purpose: Link-smoke coverage for the graphics/runtime surface that must
//          remain exported in both full and graphics-disabled builds.
//
//===----------------------------------------------------------------------===//

#include "rt_audio.h"
#include "rt_canvas3d.h"
#include "rt_fbx_loader.h"
#include "rt_gltf.h"
#include "rt_graphics.h"
#include "rt_joints3d.h"
#include "rt_physics3d.h"
#include "rt_scene3d.h"
#include "rt_terrain3d.h"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

template <typename Fn>
std::uintptr_t fn_bits(Fn fn) {
    static_assert(sizeof(fn) <= sizeof(std::uintptr_t));
    std::uintptr_t bits = 0;
    std::memcpy(&bits, &fn, sizeof(fn));
    return bits;
}

} // namespace

int main() {
    volatile std::uintptr_t surface[] = {
        fn_bits(&rt_canvas_is_available),
        fn_bits(&rt_audio_is_available),
        fn_bits(&rt_mesh3d_clear),
        fn_bits(&rt_mesh3d_from_stl),
        fn_bits(&rt_camera3d_new_ortho),
        fn_bits(&rt_camera3d_is_ortho),
        fn_bits(&rt_light3d_new_spot),
        fn_bits(&rt_scene3d_save),
        fn_bits(&rt_fbx_get_morph_target),
        fn_bits(&rt_gltf_load),
        fn_bits(&rt_gltf_mesh_count),
        fn_bits(&rt_gltf_get_mesh),
        fn_bits(&rt_gltf_material_count),
        fn_bits(&rt_gltf_get_material),
        fn_bits(&rt_distance_joint3d_new),
        fn_bits(&rt_distance_joint3d_get_distance),
        fn_bits(&rt_distance_joint3d_set_distance),
        fn_bits(&rt_spring_joint3d_new),
        fn_bits(&rt_spring_joint3d_get_stiffness),
        fn_bits(&rt_spring_joint3d_set_stiffness),
        fn_bits(&rt_spring_joint3d_get_damping),
        fn_bits(&rt_spring_joint3d_set_damping),
        fn_bits(&rt_spring_joint3d_get_rest_length),
        fn_bits(&rt_world3d_add_joint),
        fn_bits(&rt_world3d_remove_joint),
        fn_bits(&rt_world3d_joint_count),
        fn_bits(&rt_world3d_get_collision_count),
        fn_bits(&rt_world3d_get_collision_body_a),
        fn_bits(&rt_world3d_get_collision_body_b),
        fn_bits(&rt_world3d_get_collision_normal),
        fn_bits(&rt_world3d_get_collision_depth),
        fn_bits(&rt_terrain3d_set_splat_map),
        fn_bits(&rt_terrain3d_set_layer_texture),
        fn_bits(&rt_terrain3d_set_layer_scale),
    };

    for (std::uintptr_t bits : surface) {
        assert(bits != 0);
    }

    return 0;
}
