//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file rt_graphics_stubs_internal.h
/// @brief Shared declarations and trap helpers for graphics-disabled runtime
/// stub translation units.
///
/// @details This internal header centralizes the unavailable-graphics trap
/// path used by every split stub source file. Keeping the helper and public API
/// includes in one place preserves the original monolithic include environment
/// while avoiding duplicated failure-policy code.
///
// File: src/runtime/graphics/common/rt_graphics_stubs_internal.h
// Purpose: Shared declarations and trap helpers for graphics-disabled runtime
//   stub translation units.
//
// Key invariants:
//   - Included only by files in src/runtime/graphics/common that implement
//     graphics-disabled API stubs.
//   - The shared trap helper always reports Err_InvalidOperation with the
//     runtime trap kind used by the original monolithic stub file.
//   - Public API headers stay included here so split stub files expose the
//     same declarations and compile under the same include environment.
//
// Ownership/Lifetime:
//   - No resources are owned by this header; helper calls only raise traps.
//
// Links: src/runtime/graphics/common/rt_canvas_stubs.c,
//        src/runtime/graphics/common/rt_canvas3d_stubs.c,
//        src/runtime/graphics/common/rt_3d_asset_stubs.c,
//        src/runtime/graphics/common/rt_3d_scene_stubs.c,
//        src/runtime/graphics/common/rt_3d_physics_stubs.c,
//        src/runtime/graphics/common/rt_3d_world_stubs.c,
//        src/runtime/graphics/common/rt_graphics_media_stubs.c,
//        src/runtime/graphics/common/rt_graphics_helper_stubs.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_blendtree3d.h"
#include "rt_canvas3d.h"
#include "rt_collider3d.h"
#include "rt_decal3d.h"
#include "rt_error.h"
#include "rt_fbx_loader.h"
#include "rt_gltf.h"
#include "rt_graphics.h"
#include "rt_graphics_internal.h"
#include "rt_instbatch3d.h"
#include "rt_joints3d.h"
#include "rt_model3d.h"
#include "rt_morphtarget3d.h"
#include "rt_navmesh3d.h"
#include "rt_particles3d.h"
#include "rt_path3d.h"
#include "rt_physics3d.h"
#include "rt_postfx3d.h"
#include "rt_raycast3d.h"
#include "rt_scene3d.h"
#include "rt_skeleton3d.h"
#include "rt_sound3d.h"
#include "rt_sprite3d.h"
#include "rt_terrain3d.h"
#include "rt_texatlas3d.h"
#include "rt_textureasset3d.h"
#include "rt_transform3d.h"
#include "rt_water3d.h"

#include <stddef.h>
#include <stdint.h>

/// @brief Raise the canonical "graphics support not compiled in" trap.
///
/// @details Shared sink used by every split graphics stub translation unit so
/// the same trap kind, diagnostic code, and synthetic source location are
/// reported regardless of whether the disabled API was Canvas2D, Canvas3D,
/// physics, navigation, media, or an asset loader. Builds without
/// `VIPER_ENABLE_GRAPHICS` compile these stub units instead of the real
/// backend-backed implementations, so reaching this helper means user code
/// attempted to use a graphics API that was intentionally excluded.
///
/// @param msg Diagnostic string naming the unavailable runtime entry point.
///            Callers should use the existing convention
///            `"<Class>.<Method>: graphics support not compiled in"`.
///
/// @note Normal execution does not return from this function because the trap
/// handler unwinds control flow. The small fallback returns in the stub bodies
/// remain only to satisfy the C type checker after the trap call.
static inline void rt_graphics_unavailable_(const char *msg) {
    rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, 0, msg);
}

/// @brief Raise the graphics-unavailable trap from a void-returning stub.
///
/// @param msg Diagnostic string naming the unavailable runtime entry point.
#define RT_GRAPHICS_TRAP_VOID(msg)                                                                 \
    do {                                                                                           \
        rt_graphics_unavailable_(msg);                                                             \
    } while (0)

/// @brief Raise the graphics-unavailable trap from a value-returning stub.
///
/// @param msg Diagnostic string naming the unavailable runtime entry point.
/// @param fallback Expression returned only to satisfy control-flow analysis
///                 after the trap helper is called.
#define RT_GRAPHICS_TRAP_RET(msg, fallback)                                                        \
    do {                                                                                           \
        rt_graphics_unavailable_(msg);                                                             \
        return (fallback);                                                                         \
    } while (0)
