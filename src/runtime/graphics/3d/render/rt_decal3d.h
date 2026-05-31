//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_decal3d.h
// Purpose: 3D decals — surface-oriented quads with texture, lifetime, fade.
//   Used for bullet holes, blood splatters, footprints, tire marks.
//
// Key invariants:
//   - Position + normal define the decal plane.
//   - Rendered as a textured quad offset slightly from surface.
//   - Lifetime auto-decrements; alpha fades linearly over last 20%.
//
// Links: rt_canvas3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a projected decal at @p position facing @p normal, of world
///        @p size, textured with @p texture.
void *rt_decal3d_new(void *position, void *normal, double size, void *texture);
/// @brief Set the decal's lifetime in seconds (it expires after this long).
void rt_decal3d_set_lifetime(void *decal, double seconds);
/// @brief Advance the decal's age by @p dt seconds.
void rt_decal3d_update(void *decal, double dt);
/// @brief Whether the decal has exceeded its lifetime and should be removed.
int8_t rt_decal3d_is_expired(void *decal);
/// @brief Project and draw @p decal onto the 3D canvas.
void rt_canvas3d_draw_decal(void *canvas, void *decal);
/// @brief Internal: shift decal world position by `-delta` and rebuild cached geometry.
void rt_decal3d_rebase_origin(void *decal, double dx, double dy, double dz);
/// @brief Internal: copy the decal world position into @p out (zeroed on invalid handle).
void rt_decal3d_get_position(void *decal, double out[3]);

#ifdef __cplusplus
}
#endif
