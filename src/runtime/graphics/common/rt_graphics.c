//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_graphics.c
// Purpose: Umbrella translation unit for the Canvas graphics runtime. All
//   implementations have been split into focused files:
//     - rt_canvas.c          — canvas lifecycle and window management
//     - rt_drawing.c         — basic drawing primitives, text, blit, alpha
//     - rt_drawing_advanced.c — advanced shapes, curves, colors, gradients
//   This file is intentionally empty; it exists for backwards compatibility
//   with any build configurations that reference it directly.
//
// Key invariants:
//   - VIPER_ENABLE_GRAPHICS guards all real implementations in the split files.
//   - Stub implementations for non-graphics builds are in rt_graphics_stubs.c.
//
// Ownership/Lifetime:
//   - See individual split files for ownership details.
//
// Links: rt_canvas.c, rt_drawing.c, rt_drawing_advanced.c,
//        rt_graphics_internal.h, rt_graphics.h (public API)
//
//===----------------------------------------------------------------------===//

// All implementations are in rt_canvas.c, rt_drawing.c, and rt_drawing_advanced.c.
// ISO C requires at least one declaration per translation unit.
typedef int rt_graphics_empty_tu_guard;
