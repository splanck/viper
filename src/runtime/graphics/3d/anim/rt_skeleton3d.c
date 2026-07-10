//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_skeleton3d.c
// Purpose: Skeleton3D (bone hierarchy + bind pose), Animation3D (keyframe
//   clips), and AnimPlayer3D (playback, sampling, crossfade, palette output).
//
// Key invariants:
//   - Authored bones may arrive in non-topological order; global-pose builders
//     resolve parent chains recursively and break cycles as roots.
//   - Palette computation: local → global (multiply up hierarchy) → * inverse_bind.
//   - Keyframe sampling: binary search for bracket, SLERP rotation, lerp pos/scale.
//   - Crossfade: blend per-bone local transforms between two animations.
//   - GPU vs CPU skinning gated per-backend by bone-count limits.
//
// Ownership/Lifetime:
//   - Skeleton3D / Animation3D / AnimPlayer3D are GC-managed.
//   - Animation keyframe arrays are owned heap allocations freed in the finalizer.
//   - Bone-name strings are retained on assignment.
//
// Links: rt_skeleton3d.h, vgfx3d_skinning.h, plans/3d/14-skeletal-animation.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_skeleton3d.h"
#include "rt_animcontroller3d.h"
#include "rt_blendtree3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_g3d_ref_slots.h"
#include "rt_graphics3d_ids.h"
#include "rt_heap.h"
#include "rt_instbatch3d.h"
#include "rt_mat4.h"
#include "rt_object.h"
#include "rt_option.h"
#include "rt_quat.h"
#include "rt_seq.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_skinning.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SKELETON3D_FLOAT_ABS_MAX 3.40282346638528859812e38
#define SKELETON3D_ANIM_ABS_MAX 1.0e12f

/// @brief Heuristic — should we hand bone matrices to the GPU instead of skinning on the CPU?
///
/// GPU backends can skin directly while the active palette fits the backend's
/// shader-visible upload limit. The software backend always returns 0 and uses
/// CPU skinning.
/// The Software backend always returns 0 (CPU-skin path).
static int vgfx3d_backend_prefers_gpu_skinning(const char *backend_name, int32_t bone_count) {
    if (!backend_name || bone_count <= 0)
        return 0;
    if (strcmp(backend_name, "metal") == 0)
        return bone_count <= VGFX3D_MAX_BONES;
    if (strcmp(backend_name, "opengl") == 0)
        return bone_count <= VGFX3D_MAX_BONES;
    if (strcmp(backend_name, "d3d11") == 0)
        return bone_count <= VGFX3D_MAX_BONES;
    return 0;
}

// clang-format off
#include "rt_skeleton3d_matrix.inc"
#include "rt_skeleton3d_skeleton.inc"
#include "rt_skeleton3d_animation.inc"
#include "rt_skeleton3d_player.inc"
#include "rt_skeleton3d_skinning.inc"
#include "rt_skeleton3d_blend.inc"
// clang-format on
#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
