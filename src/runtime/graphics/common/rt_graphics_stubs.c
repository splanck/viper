//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/common/rt_graphics_stubs.c
// Purpose: Compatibility anchor for the graphics-disabled runtime stubs.
//
// Key invariants:
//   - This file intentionally contains no public runtime entry points.
//   - Disabled graphics APIs are implemented in the focused split files next
//     to this anchor and wired through src/runtime/CMakeLists.txt.
//
// Ownership/Lifetime:
//   - No resources are allocated here.
//
// Links: src/runtime/graphics/common/rt_graphics_stubs_internal.h,
//        src/runtime/graphics/common/rt_canvas_stubs.c,
//        src/runtime/graphics/common/rt_canvas3d_stubs.c
//
//===----------------------------------------------------------------------===//

#include "rt_graphics_stubs_internal.h"
