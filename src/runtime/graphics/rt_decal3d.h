//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_decal3d.h
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
extern "C"
{
#endif

    void   *rt_decal3d_new(void *position, void *normal, double size, void *texture);
    void    rt_decal3d_set_lifetime(void *decal, double seconds);
    void    rt_decal3d_update(void *decal, double dt);
    int8_t  rt_decal3d_is_expired(void *decal);
    void    rt_canvas3d_draw_decal(void *canvas, void *decal);

#ifdef __cplusplus
}
#endif
