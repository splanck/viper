//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d.c
// Purpose: Viper.Graphics3D.Canvas3D — 3D rendering surface that dispatches
//   through the vgfx3d_backend_t vtable. Backend selection is automatic
//   and platform-specific, with software fallback always available.
//
// Key invariants:
//   - Begin/End must bracket DrawMesh calls (no nesting)
//   - All rendering dispatches through backend->submit_draw
//   - Canvas3D owns the backend context (created in New, freed in finalizer)
//
// Ownership/Lifetime:
//   - Canvas3D is GC-managed; finalizer destroys backend ctx + window
//
// Links: vgfx3d_backend.h, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_action.h"
#include "rt_canvas3d_internal.h"
#include "rt_graphics_internal.h"
#include "rt_heap.h"
#include "rt_input.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "rt_time.h"
#include "rt_trap.h"
#include "rt_vec3.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_backend_utils.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CANVAS3D_MAX_INSTANCES 1048576
#define CANVAS3D_MAX_FALLBACK_INSTANCES 65536
#define CANVAS3D_FLOAT_ABS_MAX 3.40282346638528859812e38
#define CANVAS3D_SYNTHETIC_KEY_QUEUE_MAX 64
#define CANVAS3D_SYNTHETIC_DT_DEFAULT_US 16667LL
#define CANVAS3D_SYNTHETIC_DT_MAX_US 10000000LL
#define CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX 1000000.0

static void *g_canvas3d_synthetic_owner = NULL;
static void canvas3d_release_synthetic_input(rt_canvas3d *c);
static void canvas3d_release_synthetic_state(rt_canvas3d *c);

/// @brief Sanitize an input-source mode (0 live, 1 synthetic, 2 live+synthetic); out of
///   range falls back to 0 (live).
static int32_t canvas3d_input_source_from_mode(int64_t mode) {
    return (mode >= 0 && mode <= 2) ? (int32_t)mode : 0;
}

/// @brief Sanitize a clock-source mode: 1 = fixed synthetic delta, anything else = live.
static int32_t canvas3d_clock_source_from_mode(int64_t mode) {
    return mode == 1 ? 1 : 0;
}

/// @brief Convert a synthetic frame delta (seconds) to microseconds, clamping negatives
///   to 0 and capping at CANVAS3D_SYNTHETIC_DT_MAX_US.
static int64_t canvas3d_synthetic_seconds_to_us(double dt) {
    if (!isfinite(dt) || dt < 0.0)
        return 0;
    if (dt > (double)CANVAS3D_SYNTHETIC_DT_MAX_US / 1000000.0)
        return CANVAS3D_SYNTHETIC_DT_MAX_US;
    return (int64_t)(dt * 1000000.0 + 0.5);
}

/// @brief Round a synthetic mouse delta to an integer (half-away-from-zero), clamped to
///   ±CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX; non-finite → 0.
static int64_t canvas3d_round_synthetic_mouse_delta(double value) {
    if (!isfinite(value))
        return 0;
    if (value > CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX)
        value = CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX;
    else if (value < -CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX)
        value = -CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX;
    return value >= 0.0 ? (int64_t)(value + 0.5) : (int64_t)(value - 0.5);
}

/// @brief Accumulate a queued synthetic mouse delta onto the running total, saturating
///   at ±CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX and treating non-finite inputs safely.
static double canvas3d_accumulate_synthetic_mouse_delta(double current, double delta) {
    double next;
    if (!isfinite(delta))
        return current;
    if (!isfinite(current))
        current = 0.0;
    next = current + delta;
    if (!isfinite(next))
        return delta > 0.0 ? CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX : -CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX;
    if (next > CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX)
        return CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX;
    if (next < -CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX)
        return -CANVAS3D_SYNTHETIC_MOUSE_ABS_MAX;
    return next;
}

/// @brief Drive the canvas's delta-time fields from the latched synthetic timestep
///   (used when the clock source is synthetic for deterministic frames).
static void canvas3d_apply_synthetic_clock(rt_canvas3d *c) {
    if (!c)
        return;
    if (c->synthetic_dt_us < 0)
        c->synthetic_dt_us = 0;
    c->delta_time_us = c->synthetic_dt_us;
    c->delta_time_ms = c->synthetic_dt_us > 0 ? c->synthetic_dt_us / 1000 : 0;
}

static void canvas3d_update_live_clock(rt_canvas3d *c) {
    if (!c)
        return;
    int64_t now_us = rt_clock_ticks_us();
    if (c->last_flip_us > 0) {
        int64_t delta_us = now_us - c->last_flip_us;
        c->delta_time_us = delta_us > 0 ? delta_us : 0;
        c->delta_time_ms = delta_us > 0 ? delta_us / 1000 : 0;
    } else {
        c->delta_time_us = 0;
        c->delta_time_ms = 0;
    }
    c->last_flip_us = now_us;
}

static void canvas3d_release_global_owner_ref(rt_canvas3d *c) {
    if (c && rt_heap_is_payload(c) && rt_obj_release_check0(c))
        rt_obj_free(c);
}

static void canvas3d_set_synthetic_owner(rt_canvas3d *c) {
    rt_canvas3d *previous;
    if (c && rt_heap_is_payload(c))
        rt_obj_retain_maybe(c);
    previous = (rt_canvas3d *)__atomic_exchange_n(
        &g_canvas3d_synthetic_owner, c, __ATOMIC_ACQ_REL);
    if (previous == c) {
        canvas3d_release_global_owner_ref(c);
        return;
    }
    if (previous) {
        canvas3d_release_synthetic_state(previous);
        canvas3d_release_global_owner_ref(previous);
    }
}

/// @brief Replay queued synthetic input into the live input runtime for one frame:
///   apply pending key transitions, the accumulated mouse delta, button state changes,
///   and wheel scroll, then clear the queues.
static void canvas3d_apply_synthetic_input(rt_canvas3d *c) {
    if (!c)
        return;
    canvas3d_set_synthetic_owner(c);

    for (int32_t i = 0; i < c->synthetic_key_count; i++) {
        int64_t key = c->synthetic_key_keys[i];
        if (key <= 0 || key >= VIPER_KEY_MAX)
            continue;
        if (c->synthetic_key_downs[i]) {
            rt_keyboard_on_key_down(key);
            c->synthetic_key_state[key] = 1;
        } else {
            rt_keyboard_on_key_up(key);
            c->synthetic_key_state[key] = 0;
        }
    }
    c->synthetic_key_count = 0;

    int64_t dx = canvas3d_round_synthetic_mouse_delta(c->synthetic_mouse_dx);
    int64_t dy = canvas3d_round_synthetic_mouse_delta(c->synthetic_mouse_dy);
    if (dx != 0 || dy != 0)
        rt_mouse_force_delta(dx, dy);
    c->synthetic_mouse_dx = 0.0;
    c->synthetic_mouse_dy = 0.0;

    if (c->synthetic_mouse_has_buttons) {
        for (int i = 0; i < VIPER_MOUSE_BUTTON_MAX; i++) {
            uint8_t down = (uint8_t)((c->synthetic_mouse_buttons >> i) & 1LL);
            if (down && !c->synthetic_mouse_button_state[i]) {
                rt_mouse_button_down(i);
                c->synthetic_mouse_button_state[i] = 1;
            } else if (!down && c->synthetic_mouse_button_state[i]) {
                rt_mouse_button_up(i);
                c->synthetic_mouse_button_state[i] = 0;
            }
        }
    }
    c->synthetic_mouse_has_buttons = 0;
    c->synthetic_mouse_buttons = 0;

    if (isfinite(c->synthetic_mouse_wheel_y) && c->synthetic_mouse_wheel_y != 0.0)
        rt_mouse_update_wheel(0.0, c->synthetic_mouse_wheel_y);
    c->synthetic_mouse_wheel_y = 0.0;
}

/// @brief Release every key/button the synthetic source is currently holding (called
///   when synthetic input is cleared/disabled) so no key stays stuck down.
static void canvas3d_release_synthetic_state(rt_canvas3d *c) {
    if (!c)
        return;
    for (int i = 1; i < VIPER_KEY_MAX; i++) {
        if (c->synthetic_key_state[i]) {
            rt_keyboard_on_key_up(i);
            c->synthetic_key_state[i] = 0;
        }
    }
    for (int i = 0; i < VIPER_MOUSE_BUTTON_MAX; i++) {
        if (c->synthetic_mouse_button_state[i]) {
            rt_mouse_button_up(i);
            c->synthetic_mouse_button_state[i] = 0;
        }
    }
    c->synthetic_key_count = 0;
    c->synthetic_mouse_dx = 0.0;
    c->synthetic_mouse_dy = 0.0;
    c->synthetic_mouse_wheel_y = 0.0;
    c->synthetic_mouse_buttons = 0;
    c->synthetic_mouse_has_buttons = 0;
}

static void canvas3d_release_synthetic_input(rt_canvas3d *c) {
    void *expected;
    if (!c)
        return;
    canvas3d_release_synthetic_state(c);
    expected = c;
    if (__atomic_compare_exchange_n(&g_canvas3d_synthetic_owner,
                                    &expected,
                                    NULL,
                                    0,
                                    __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE))
        canvas3d_release_global_owner_ref(c);
}

/// @brief True when the active backend can apply GPU post-FX during present.
///
/// Requires the backend to expose `present_postfx` AND the canvas
/// not be in RTT mode (RTT outputs are read back directly without
/// post-processing).
static int canvas3d_backend_uses_gpu_postfx(const rt_canvas3d *c) {
    return c && c->backend && c->backend->present_postfx && c->render_target == NULL;
}

/// @brief True when GPU post-FX can be split from final presentation.
///
/// Split-capable backends composite the post-FX scene first, then Canvas3D
/// replays final-overlay commands over that composited target before calling
/// the normal present hook.
static int canvas3d_backend_splits_gpu_postfx_present(const rt_canvas3d *c) {
    return c && c->backend && c->backend->apply_postfx && c->backend->present &&
           c->render_target == NULL;
}

/// @brief True when the canvas's RTT is owned by a hardware backend.
///
/// Software backends fall back to a CPU-side copy for RTT; hardware
/// backends (D3D11/OpenGL/Metal) keep the texture on the GPU and
/// only read it back when the user requests `Pixels()`.
static int canvas3d_backend_owns_gpu_rtt(const rt_canvas3d *c) {
    return c && c->render_target && c->backend && c->backend != &vgfx3d_software_backend;
}

/// @brief Clamp a double to `[0, 1]` and narrow to float; non-finite → 0.
/// @details Canvas-facing RGB/alpha inputs come in as `double` from the IL, but the
///   backends consume `float`. Sanitising and narrowing at this boundary keeps NaN out
///   of shader uniforms and caps values at the physical [0, 1] range.
static float canvas3d_clamp01_f64(double value) {
    if (!isfinite(value))
        return 0.0f;
    if (value < 0.0)
        return 0.0f;
    if (value > 1.0)
        return 1.0f;
    return (float)value;
}

/// @brief Clamp a float to [0,1] (NaN/inf → 0) and convert to a 0-255 byte.
static uint8_t canvas3d_clamp01_to_u8(float value) {
    if (!isfinite(value))
        value = 0.0f;
    if (value < 0.0f)
        value = 0.0f;
    if (value > 1.0f)
        value = 1.0f;
    return (uint8_t)(value * 255.0f + 0.5f);
}

/// @brief Check whether @p value is finite and within ±FLT_MAX.
/// @details Pre-flight test for safe `double → float` narrowing.
static int canvas3d_double_fits_float(double value) {
    return isfinite(value) && value >= -CANVAS3D_FLOAT_ABS_MAX && value <= CANVAS3D_FLOAT_ABS_MAX;
}

/// @brief Narrow a 16-entry double Mat4 to float, returning 0 if any entry is non-finite.
/// @details Used by every matrix-keyed Canvas3D draw entry so a malformed Mat4
///          surfaces as a no-op instead of feeding garbage to the GPU backend.
static int canvas3d_mat4_d2f_checked(const double *src, float *dst) {
    if (!src || !dst)
        return 0;
    for (int i = 0; i < 16; i++) {
        if (!canvas3d_double_fits_float(src[i]))
            return 0;
        dst[i] = (float)src[i];
    }
    return 1;
}

/// @brief Verify every entry across an array of @p count Mat4s is finite.
/// @details Used to validate instanced draw matrix arrays before submission.
static int canvas3d_matrices_f32_are_finite(const float *matrices, int32_t count) {
    if (!matrices || count <= 0)
        return 0;
    for (int32_t i = 0; i < count; i++) {
        const float *m = &matrices[(size_t)i * 16u];
        for (int j = 0; j < 16; j++) {
            if (!isfinite(m[j]))
                return 0;
        }
    }
    return 1;
}

/// @brief Validate @p obj is a live `Viper.Math.Mat4` heap object, NULL otherwise.
static mat4_impl *canvas3d_mat4_checked(void *obj) {
    if (!obj || !rt_heap_is_payload(obj) || rt_obj_class_id(obj) != RT_MAT4_CLASS_ID)
        return NULL;
    return (mat4_impl *)obj;
}

/// @brief Narrow a non-negative double to float; NaN/inf → `fallback`, negatives → 0.
/// @details Used for scalar knobs without an upper bound (exposure, intensities, strengths)
///   where the canvas wants to preserve the author's value faithfully but still refuse
///   non-finite input.
static float canvas3d_sanitize_nonnegative_f64(double value, float fallback) {
    if (!isfinite(value))
        return fallback;
    if (value < 0.0)
        return 0.0f;
    if (value > CANVAS3D_FLOAT_ABS_MAX)
        return FLT_MAX;
    return (float)value;
}

/// @brief Narrow a double to float, returning `fallback` when out of float range or non-finite.
static float canvas3d_sanitize_f64_to_float(double value, float fallback) {
    return canvas3d_double_fits_float(value) ? (float)value : fallback;
}

/// @brief Clamp a finite double to [lo, hi] and narrow to float; non-finite -> fallback.
static float canvas3d_clamp_f64_to_float(double value, double lo, double hi, float fallback) {
    if (!canvas3d_double_fits_float(value))
        return fallback;
    if (value < lo)
        value = lo;
    if (value > hi)
        value = hi;
    return (float)value;
}

/// @brief Validate an RGBA8 readback target: positive dimensions, a non-negative stride
///   at least 4·w bytes, and no 32-bit overflow in the row size.
static int canvas3d_rgba8_stride_valid(int32_t w, int32_t h, int32_t stride) {
    int64_t required;
    if (w <= 0 || h <= 0 || stride < 0)
        return 0;
    required = (int64_t)w * 4;
    if (required > INT32_MAX)
        return 0;
    return (int64_t)stride >= required;
}

/// @brief Clear the per-draw splat-map staging slot on the canvas.
/// @details Called on every early-return path of `rt_canvas3d_draw_mesh_matrix_keyed`
///          so a failed splat-configured draw cannot leak its splat-map and four
///          layer pointers into the next successful draw.
static void canvas3d_clear_pending_splat(rt_canvas3d *c) {
    if (!c)
        return;
    c->pending_has_splat = 0;
    c->pending_splat_map = NULL;
    for (int i = 0; i < 4; i++) {
        c->pending_splat_layers[i] = NULL;
        c->pending_splat_layer_scales[i] = 0.0f;
    }
}

#include "rt_canvas3d_frame_postfx.inc"

/// @brief Estimate a physical backing size for a requested logical size.
static int32_t canvas3d_scale_logical_size(rt_canvas3d *c, int32_t logical) {
    float scale;

    if (!c || logical <= 0)
        return logical;
    scale = c->gfx_win ? vgfx_window_get_scale(c->gfx_win) : 1.0f;
    if (!isfinite(scale) || scale < 1.0f)
        scale = 1.0f;
    if ((double)logical > (double)INT32_MAX / (double)scale)
        return logical;
    return (int32_t)((double)logical * (double)scale + 0.5);
}

/// @brief Convert a physical framebuffer dimension back to the public logical space.
static int32_t canvas3d_unscale_physical_size(rt_canvas3d *c, int32_t physical) {
    float scale;

    if (physical <= 0)
        return physical;
    scale = (c && c->gfx_win) ? vgfx_window_get_scale(c->gfx_win) : 1.0f;
    if (!isfinite(scale) || scale < 1.0f)
        scale = 1.0f;
    return (int32_t)((double)physical / (double)scale + 0.5);
}

/// @brief Apply a window-size change to the canvas + active backend.
///
/// `logical_*` is the public coordinate/capture size, while `physical_*`
/// is the framebuffer/backing-pixel size used by native backends. Keeping
/// both prevents Retina/HiDPI resize events from leaking 2x dimensions into
/// Game3D and Canvas3D public APIs.
static void rt_canvas3d_apply_resize(
    rt_canvas3d *c, int32_t logical_w, int32_t logical_h, int32_t physical_w, int32_t physical_h) {
    int size_changed;
    int framebuffer_changed;

    if (!c || logical_w <= 0 || logical_h <= 0)
        return;
    if (physical_w <= 0)
        physical_w = canvas3d_scale_logical_size(c, logical_w);
    if (physical_h <= 0)
        physical_h = canvas3d_scale_logical_size(c, logical_h);
    if (physical_w <= 0)
        physical_w = logical_w;
    if (physical_h <= 0)
        physical_h = logical_h;

    size_changed = (c->width != logical_w || c->height != logical_h);
    framebuffer_changed =
        (c->framebuffer_width != physical_w || c->framebuffer_height != physical_h);
    if (!size_changed && !framebuffer_changed)
        return;
    c->width = logical_w;
    c->height = logical_h;
    c->framebuffer_width = physical_w;
    c->framebuffer_height = physical_h;
    if (framebuffer_changed && c->backend && c->backend->resize)
        c->backend->resize(c->backend_ctx, physical_w, physical_h);
}

static void canvas3d_record_event_type(rt_canvas3d *c, int64_t type) {
    if (!c || type == VGFX_EVENT_NONE)
        return;
    c->last_event_type = type;
    if (c->event_type_count >= RT_CANVAS3D_EVENT_QUEUE_CAPACITY) {
        c->event_type_head = (c->event_type_head + 1) % RT_CANVAS3D_EVENT_QUEUE_CAPACITY;
        c->event_type_count--;
    }
    int32_t tail = (c->event_type_head + c->event_type_count) % RT_CANVAS3D_EVENT_QUEUE_CAPACITY;
    c->event_type_queue[tail] = type;
    c->event_type_count++;
}

/// @brief Public resize entry point mirroring window resize events.
void rt_canvas3d_resize(void *obj, int64_t w, int64_t h) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c || w <= 0 || h <= 0 || w > INT32_MAX || h > INT32_MAX)
        return;
    if (c->gfx_win)
        vgfx_set_window_size(c->gfx_win, (int32_t)w, (int32_t)h);
    rt_canvas3d_apply_resize(c,
                             (int32_t)w,
                             (int32_t)h,
                             canvas3d_scale_logical_size(c, (int32_t)w),
                             canvas3d_scale_logical_size(c, (int32_t)h));
}

/// @brief Resolve the dimensions of the canvas's active render output.
///
/// When a render target is bound, 2D overlays and coordinate
/// conversions should operate in render-target pixel space rather than
/// the window's framebuffer size.
static void canvas3d_active_output_size(const rt_canvas3d *c, int32_t *out_w, int32_t *out_h) {
    if (out_w)
        *out_w = 0;
    if (out_h)
        *out_h = 0;
    if (!c)
        return;
    if (c->render_target) {
        if (out_w)
            *out_w = c->render_target->width;
        if (out_h)
            *out_h = c->render_target->height;
        return;
    }
    if (out_w)
        *out_w = c->width;
    if (out_h)
        *out_h = c->height;
}

/// @brief Window-system resize callback — `userdata` is the canvas pointer.
///
/// Hooked into the underlying `vgfx_window_t`'s resize event so the
/// canvas state stays in sync without requiring per-frame polling.
static void rt_canvas3d_on_resize(void *userdata, int32_t w, int32_t h) {
    rt_canvas3d *c = (rt_canvas3d *)userdata;
    int32_t logical_w = 0;
    int32_t logical_h = 0;

    if (!c)
        return;
    if (!c->gfx_win || !vgfx_get_size(c->gfx_win, &logical_w, &logical_h))
        logical_w = canvas3d_unscale_physical_size(c, w);
    if (!c->gfx_win || logical_h <= 0)
        logical_h = canvas3d_unscale_physical_size(c, h);
    rt_canvas3d_apply_resize(c, logical_w, logical_h, w, h);
}

/// @brief Drop a GC-managed reference held in a `**slot` and zero the slot.
///
/// Idempotent — safe to call on already-NULL slots. Used in the
/// canvas finalizer to release every owned sub-object cleanly.
static void canvas3d_release_owned_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Replace the GC-managed reference in `*slot` with `value`, retain/release as needed.
/// @details Implements the canonical retain-then-release ownership swap so
///          the new value's refcount goes up *before* the old value's
///          goes down — important when `value == *slot`'s child or the
///          two objects share lifetime, since releasing first could free
///          the new value through a transitive reference. The early
///          return on `*slot == value` skips the round trip when the
///          assignment is a no-op.
static void canvas3d_assign_owned_ref(void **slot, void *value) {
    if (!slot || *slot == value)
        return;
    rt_obj_retain_maybe(value);
    canvas3d_release_owned_ref(slot);
    *slot = value;
}

/// @brief Tell keyboard + mouse subsystems to forget this window.
///
/// Called when the canvas is destroyed. Without this, focus
/// queries would still return the dead window pointer until the
/// next focus event arrived.
static void rt_canvas3d_detach_input(vgfx_window_t gfx_win) {
    if (!gfx_win)
        return;
    rt_keyboard_clear_canvas_if_matches(gfx_win);
    rt_mouse_clear_canvas_if_matches(gfx_win);
}

/*==========================================================================
 * Deferred draw command (for transparency sorting)
 *=========================================================================*/

typedef enum {
    DEFERRED_DRAW_MESH = 0,
    DEFERRED_DRAW_INSTANCED = 1,
} deferred_draw_kind_t;

typedef enum {
    DEFERRED_PASS_MAIN = 0,
    DEFERRED_PASS_SCREEN_OVERLAY = 1,
} deferred_pass_t;

typedef struct {
    deferred_draw_kind_t kind;
    deferred_pass_t pass_kind;
    vgfx3d_draw_cmd_t cmd;
    const float *instance_matrices; /* row-major float[instance_count * 16] */
    int32_t instance_count;
    vgfx3d_light_params_t lights[VGFX3D_MAX_LIGHTS];
    int32_t light_count;
    float ambient[3];
    int8_t wireframe;
    int8_t backface_cull;
    int8_t has_local_bounds;
    int8_t visible;
    int8_t requires_blend;
    float local_bounds_min[3];
    float local_bounds_max[3];
    float sort_key; /* bounds-aware view-depth key for deferred draw sorting */
} deferred_draw_t;

typedef struct {
    uintptr_t key;
    float current_model[16];
    float prev_model[16];
    int64_t last_frame_seen;
    int8_t has_current;
    int8_t has_prev;
} canvas_motion_history_t;

static uint32_t canvas3d_hash_u64(uintptr_t value);
static int32_t canvas3d_next_power_of_two_i32(int32_t value);

/// @brief Grow the deferred-draw command buffer to hold `needed` entries.
///
/// Geometric growth (cap doubles, starting at 32). Used by the
/// transparency-sort path to buffer commands until end-of-frame.
/// Returns 0 on allocation failure; draw callers trap rather than bypassing
/// the queue because immediate submission breaks transparency ordering.
static int ensure_deferred_capacity(void **buf, int32_t *capacity, int32_t needed) {
    if (!buf || !capacity || needed <= 0)
        return 0;
    if (*capacity >= needed)
        return 1;

    int32_t new_cap = *capacity > 0 ? *capacity : 32;
    while (new_cap < needed) {
        if (new_cap > INT32_MAX / 2)
            new_cap = needed;
        else
            new_cap *= 2;
    }
    if ((size_t)new_cap > SIZE_MAX / sizeof(deferred_draw_t))
        return 0;

    deferred_draw_t *new_buf =
        (deferred_draw_t *)realloc(*buf, (size_t)new_cap * sizeof(deferred_draw_t));
    if (!new_buf)
        return 0;
    *buf = new_buf;
    *capacity = new_cap;
    return 1;
}

static void canvas3d_submit_deferred(rt_canvas3d *c, const deferred_draw_t *dd);

/// @brief Grow the canvas's text-rendering vertex + index scratch buffers.
///
/// Two-pair geometric growth — vertices and indices have separate
/// caps because the ratio depends on the glyph layout. Starting
/// capacities: 256 vertices, 384 indices.
static int ensure_text_capacity(rt_canvas3d *c, int32_t vertex_count, int32_t index_count) {
    if (!c || vertex_count < 0 || index_count < 0)
        return 0;
    if ((size_t)vertex_count > SIZE_MAX / sizeof(vgfx3d_vertex_t) ||
        (size_t)index_count > SIZE_MAX / sizeof(uint32_t))
        return 0;

    if (vertex_count > c->text_vertex_capacity) {
        int32_t new_cap = c->text_vertex_capacity > 0 ? c->text_vertex_capacity : 256;
        while (new_cap < vertex_count) {
            if (new_cap > INT32_MAX / 2)
                new_cap = vertex_count;
            else
                new_cap *= 2;
        }
        vgfx3d_vertex_t *new_verts =
            (vgfx3d_vertex_t *)realloc(c->text_vertices, (size_t)new_cap * sizeof(vgfx3d_vertex_t));
        if (!new_verts)
            return 0;
        c->text_vertices = new_verts;
        c->text_vertex_capacity = new_cap;
    }

    if (index_count > c->text_index_capacity) {
        int32_t new_cap = c->text_index_capacity > 0 ? c->text_index_capacity : 384;
        while (new_cap < index_count) {
            if (new_cap > INT32_MAX / 2)
                new_cap = index_count;
            else
                new_cap *= 2;
        }
        uint32_t *new_indices =
            (uint32_t *)realloc(c->text_indices, (size_t)new_cap * sizeof(uint32_t));
        if (!new_indices)
            return 0;
        c->text_indices = new_indices;
        c->text_index_capacity = new_cap;
    }

    return 1;
}

/// @brief `qsort` comparator: back-to-front (largest sort_key first).
///
/// Used to sort transparent draws so they composite correctly (back
/// objects drawn first so front objects blend over them).
static int cmp_back_to_front(const void *a, const void *b) {
    float ka = ((const deferred_draw_t *)a)->sort_key;
    float kb = ((const deferred_draw_t *)b)->sort_key;
    if (ka > kb)
        return -1;
    if (ka < kb)
        return 1;
    return 0;
}

/// @brief `qsort` comparator: front-to-back (smallest sort_key first).
///
/// Used for opaque draws — front-to-back order maximizes early-Z
/// rejection, dropping pixel-shader work for occluded fragments.
static int cmp_front_to_back(const void *a, const void *b) {
    float ka = ((const deferred_draw_t *)a)->sort_key;
    float kb = ((const deferred_draw_t *)b)->sort_key;
    if (ka < kb)
        return -1;
    if (ka > kb)
        return 1;
    return 0;
}

/*==========================================================================
 * Helpers
 *=========================================================================*/

/// @brief Convert a 16-element double-precision matrix to single precision.
///
/// Zia stores matrices as `Mat4` with double components; the GPU
/// uniform path takes float — straightforward narrowing conversion.
/// @brief Whether a draw command needs alpha blending (transparency).
///
/// Two cases trigger blending:
///   1. Legacy / Phong workflow with scalar alpha < 1.0 or diffuse alpha < 1.0.
///   2. PBR workflow with explicit `BLEND` alpha mode.
/// Used to route the command into the deferred transparency-sorted
/// pass instead of the immediate opaque pass.
static int8_t canvas3d_cmd_requires_blend(const vgfx3d_draw_cmd_t *cmd) {
    if (!cmd)
        return 0;
    return (int8_t)(vgfx3d_draw_cmd_uses_transparent_blend(cmd) ? 1 : 0);
}

/// @brief Resolve the effective backface-cull flag for a material draw.
///
/// AND of canvas-level backface cull AND material isn't `double_sided`.
/// Materials marked double-sided (e.g., leaves, fabric) override the
/// canvas setting to disable culling per-draw.
static int8_t canvas3d_material_backface_cull(const rt_canvas3d *c, const rt_material3d *mat) {
    if (!c)
        return 0;
    return (int8_t)(c->backface_cull && !(mat && mat->double_sided));
}

/// @brief Return 1 only when every vertex in the mesh has a finite non-zero tangent.
/// @details A partially-authored tangent stream is not safe for normal maps: accepting a
///   single non-zero tangent would leave the rest of the mesh with a broken basis. In
///   that case draw submission generates tangents on a queued geometry snapshot.
static int canvas3d_mesh_has_complete_tangents(const rt_mesh3d *mesh) {
    if (!mesh || !mesh->vertices)
        return 0;
    if (mesh->vertex_count == 0)
        return 0;
    for (uint32_t i = 0; i < mesh->vertex_count; i++) {
        const float *t = mesh->vertices[i].tangent;
        float len2 = t[0] * t[0] + t[1] * t[1] + t[2] * t[2];
        if (!isfinite(len2) || len2 <= 1e-8f || !isfinite(t[3]))
            return 0;
    }
    return 1;
}

/// @brief Return 1 when a normal-mapped draw needs generated tangents.
/// @details Draw submission must not mutate the caller's mesh. If authored tangents
///   are already present, mark the source mesh cache state. If tangents must be
///   generated, callers snapshot the geometry and generate them on that copy.
static int canvas3d_prepare_normal_map_tangent_state(rt_mesh3d *mesh, const rt_material3d *mat) {
    if (!mesh || !mat || !mat->normal_map)
        return 0;
    if (mesh->tangents_ready && mesh->tangent_revision == mesh->geometry_revision)
        return 0;
    if (canvas3d_mesh_has_complete_tangents(mesh)) {
        mesh->tangents_ready = 1;
        mesh->tangent_revision = mesh->geometry_revision;
        return 0;
    }
    return 1;
}

/// @brief Generate tangents for a snapshot copy without touching the source mesh.
static int canvas3d_generate_snapshot_tangents(const rt_mesh3d *source,
                                               vgfx3d_vertex_t *vertices,
                                               uint32_t *indices) {
    rt_mesh3d temp;
    if (!source || !vertices || !indices || source->vertex_count == 0 || source->index_count == 0)
        return 0;
    memset(&temp, 0, sizeof(temp));
    temp.vertices = vertices;
    temp.vertex_count = source->vertex_count;
    temp.vertex_capacity = source->vertex_count;
    temp.indices = indices;
    temp.index_count = source->index_count;
    temp.index_capacity = source->index_count;
    temp.geometry_revision = source->geometry_revision ? source->geometry_revision : 1u;
    temp.bounds_dirty = source->bounds_dirty;
    memcpy(temp.aabb_min, source->aabb_min, sizeof(temp.aabb_min));
    memcpy(temp.aabb_max, source->aabb_max, sizeof(temp.aabb_max));
    temp.bsphere_radius = source->bsphere_radius;
    rt_mesh3d_calc_tangents_impl(&temp);
    return temp.tangents_ready ? 1 : 0;
}

/// @brief Translate every material field into the corresponding draw-command field.
///
/// Per-vertex draw commands are float-typed (matches GPU expectations),
/// materials are double-typed (matches Zia's number type). This helper
/// performs the narrowing conversion plus the material→command name
/// remapping (e.g., `mat->shininess` → `cmd->shininess`).
static void canvas3d_fill_material_cmd(const rt_material3d *mat, vgfx3d_draw_cmd_t *cmd) {
    if (!mat || !cmd)
        return;

    cmd->diffuse_color[0] = canvas3d_clamp01_f64(mat->diffuse[0]);
    cmd->diffuse_color[1] = canvas3d_clamp01_f64(mat->diffuse[1]);
    cmd->diffuse_color[2] = canvas3d_clamp01_f64(mat->diffuse[2]);
    cmd->diffuse_color[3] = canvas3d_clamp01_f64(mat->diffuse[3]);
    cmd->specular[0] = canvas3d_clamp01_f64(mat->specular[0]);
    cmd->specular[1] = canvas3d_clamp01_f64(mat->specular[1]);
    cmd->specular[2] = canvas3d_clamp01_f64(mat->specular[2]);
    cmd->shininess = canvas3d_sanitize_nonnegative_f64(mat->shininess, 32.0f);
    cmd->alpha = canvas3d_clamp01_f64(mat->alpha);
    cmd->unlit = (int8_t)(mat->unlit || mat->shading_model == 3);
    cmd->texture = mat->texture;
    cmd->normal_map = mat->normal_map;
    cmd->specular_map = mat->specular_map;
    cmd->emissive_map = mat->emissive_map;
    cmd->metallic_roughness_map = mat->metallic_roughness_map;
    cmd->ao_map = mat->ao_map;
    cmd->emissive_color[0] = canvas3d_clamp01_f64(mat->emissive[0]);
    cmd->emissive_color[1] = canvas3d_clamp01_f64(mat->emissive[1]);
    cmd->emissive_color[2] = canvas3d_clamp01_f64(mat->emissive[2]);
    cmd->metallic = canvas3d_clamp01_f64(mat->metallic);
    cmd->roughness = canvas3d_clamp01_f64(mat->roughness);
    cmd->ao = canvas3d_clamp01_f64(mat->ao);
    cmd->emissive_intensity = canvas3d_sanitize_nonnegative_f64(mat->emissive_intensity, 1.0f);
    cmd->normal_scale = canvas3d_clamp_f64_to_float(mat->normal_scale, -1000.0, 1000.0, 1.0f);
    cmd->additive_blend = mat->additive_blend ? 1 : 0;
    cmd->workflow = (mat->workflow == 1) ? 1 : 0;
    cmd->alpha_mode = (mat->alpha_mode >= 0 && mat->alpha_mode <= 2) ? mat->alpha_mode : 0;
    cmd->alpha_cutoff = canvas3d_clamp01_f64(mat->alpha_cutoff);
    cmd->double_sided = mat->double_sided ? 1 : 0;
    cmd->texture_wrap_s = mat->texture_wrap_s;
    cmd->texture_wrap_t = mat->texture_wrap_t;
    cmd->texture_filter = mat->texture_filter;
    memcpy(cmd->texture_slot_wrap_s, mat->texture_slot_wrap_s, sizeof(cmd->texture_slot_wrap_s));
    memcpy(cmd->texture_slot_wrap_t, mat->texture_slot_wrap_t, sizeof(cmd->texture_slot_wrap_t));
    memcpy(cmd->texture_slot_filter, mat->texture_slot_filter, sizeof(cmd->texture_slot_filter));
    memcpy(cmd->texture_slot_uv_set, mat->texture_slot_uv_set, sizeof(cmd->texture_slot_uv_set));
    for (int slot = 0; slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; slot++) {
        for (int i = 0; i < 6; i++)
            cmd->texture_slot_uv_transform[slot][i] =
                canvas3d_sanitize_f64_to_float(mat->texture_slot_uv_transform[slot][i], 0.0f);
    }
    cmd->env_map = mat->env_map;
    cmd->reflectivity = canvas3d_clamp01_f64(mat->reflectivity);
    cmd->shading_model =
        (mat->shading_model >= 0 && mat->shading_model <= 5 && mat->shading_model != 3)
            ? mat->shading_model
            : 0;
    for (int pi = 0; pi < 8; pi++)
        cmd->custom_params[pi] = canvas3d_sanitize_f64_to_float(mat->custom_params[pi], 0.0f);
}

/// @brief Grow the motion-history table to hold `needed` entries.
///
/// Motion history is keyed by mesh-identity pointer and stores the
/// previous-frame model matrix for motion-blur / TAA. Geometric
/// growth starting at 32 entries.
static int ensure_motion_history_capacity(rt_canvas3d *c, int32_t needed) {
    if (!c || needed <= 0)
        return 0;
    if (c->motion_history_capacity >= needed)
        return 1;

    int32_t new_cap = c->motion_history_capacity > 0 ? c->motion_history_capacity : 32;
    while (new_cap < needed) {
        if (new_cap > INT32_MAX / 2)
            new_cap = needed;
        else
            new_cap *= 2;
    }
    if ((size_t)new_cap > SIZE_MAX / sizeof(canvas_motion_history_t))
        return 0;

    canvas_motion_history_t *new_hist = (canvas_motion_history_t *)realloc(
        c->motion_history, (size_t)new_cap * sizeof(canvas_motion_history_t));
    if (!new_hist)
        return 0;
    c->motion_history = new_hist;
    c->motion_history_capacity = new_cap;
    return 1;
}

static void canvas3d_motion_hash_reset(rt_canvas3d *c) {
    if (!c || !c->motion_history_hash || c->motion_history_hash_capacity <= 0)
        return;
    memset(c->motion_history_hash,
           0,
           (size_t)c->motion_history_hash_capacity * sizeof(*c->motion_history_hash));
}

static int canvas3d_ensure_motion_hash_capacity(rt_canvas3d *c, int32_t count_hint) {
    if (!c)
        return 0;
    int32_t needed = canvas3d_next_power_of_two_i32(count_hint > 0 ? count_hint * 2 : 32);
    if (needed < 32)
        needed = 32;
    if (c->motion_history_hash_capacity >= needed)
        return 1;
    if ((size_t)needed > SIZE_MAX / sizeof(*c->motion_history_hash))
        return 0;
    int32_t *grown = (int32_t *)realloc(c->motion_history_hash, (size_t)needed * sizeof(*grown));
    if (!grown)
        return 0;
    c->motion_history_hash = grown;
    c->motion_history_hash_capacity = needed;
    canvas3d_motion_hash_reset(c);
    return 1;
}

static int canvas3d_motion_hash_insert_existing(rt_canvas3d *c, int32_t index) {
    canvas_motion_history_t *hist = (canvas_motion_history_t *)c->motion_history;
    if (!c || !c->motion_history_hash || !hist || index < 0 || index >= c->motion_history_count)
        return 0;
    int32_t mask = c->motion_history_hash_capacity - 1;
    int32_t slot = (int32_t)(canvas3d_hash_u64(hist[index].key) & (uint32_t)mask);
    for (int32_t probe = 0; probe < c->motion_history_hash_capacity; ++probe) {
        if (c->motion_history_hash[slot] == 0) {
            c->motion_history_hash[slot] = index + 1;
            return 1;
        }
        slot = (slot + 1) & mask;
    }
    return 0;
}

static int canvas3d_rebuild_motion_hash(rt_canvas3d *c) {
    if (!c)
        return 0;
    if (c->motion_history_count <= 0) {
        canvas3d_motion_hash_reset(c);
        return 1;
    }
    if (!canvas3d_ensure_motion_hash_capacity(c, c->motion_history_count + 1))
        return 0;
    canvas3d_motion_hash_reset(c);
    for (int32_t i = 0; i < c->motion_history_count; ++i) {
        if (!canvas3d_motion_hash_insert_existing(c, i))
            return 0;
    }
    return 1;
}

static int32_t canvas3d_motion_hash_find_index(rt_canvas3d *c, uintptr_t key) {
    if (!c || key == 0 || c->motion_history_count <= 0)
        return -1;
    if (!c->motion_history_hash ||
        c->motion_history_hash_capacity < canvas3d_next_power_of_two_i32(c->motion_history_count * 2)) {
        if (!canvas3d_rebuild_motion_hash(c))
            return -1;
    }
    canvas_motion_history_t *hist = (canvas_motion_history_t *)c->motion_history;
    int32_t mask = c->motion_history_hash_capacity - 1;
    int32_t slot = (int32_t)(canvas3d_hash_u64(key) & (uint32_t)mask);
    for (int32_t probe = 0; probe < c->motion_history_hash_capacity; ++probe) {
        int32_t encoded = c->motion_history_hash[slot];
        if (encoded == 0)
            return -1;
        int32_t index = encoded - 1;
        if (index >= 0 && index < c->motion_history_count && hist[index].key == key)
            return index;
        slot = (slot + 1) & mask;
    }
    return -1;
}

/// @brief Drop motion-history entries that haven't been touched in over a frame.
///
/// In-place compaction. Anything not seen in the current or previous
/// frame is considered stale (the mesh has stopped being drawn or
/// has been destroyed). Bounded eviction prevents the table from
/// growing without bound.
static void canvas3d_prune_motion_history(rt_canvas3d *c) {
    if (!c || c->motion_history_count <= 0)
        return;

    canvas_motion_history_t *hist = (canvas_motion_history_t *)c->motion_history;
    int32_t dst = 0;
    for (int32_t i = 0; i < c->motion_history_count; i++) {
        if (c->frame_serial - hist[i].last_frame_seen > 1)
            continue;
        if (dst != i)
            hist[dst] = hist[i];
        dst++;
    }
    c->motion_history_count = dst;
    canvas3d_rebuild_motion_hash(c);
}

/// @brief Look up (and update) the previous-frame model matrix for a mesh.
///
/// Three cases:
///   1. Existing entry, first lookup this frame → roll current→previous,
///      update current, return previous.
///   2. Existing entry, repeat lookup this frame → just return the
///      previous (don't roll twice).
///   3. New entry → register, return "no previous yet".
/// Returns through `out_has_prev` whether the previous frame was
/// available — first-frame draws fall back to current=previous.
static void canvas3d_resolve_previous_model(rt_canvas3d *c,
                                            uintptr_t motion_key,
                                            const float *current_model,
                                            float *out_prev_model,
                                            int8_t *out_has_prev) {
    if (out_has_prev)
        *out_has_prev = 0;
    if (out_prev_model)
        memset(out_prev_model, 0, sizeof(float) * 16);
    if (!c || motion_key == 0 || !current_model || !out_prev_model || !out_has_prev)
        return;

    canvas_motion_history_t *hist = (canvas_motion_history_t *)c->motion_history;
    int32_t found_index = canvas3d_motion_hash_find_index(c, motion_key);
    if (found_index >= 0) {
        canvas_motion_history_t *entry = &hist[found_index];
        if (entry->last_frame_seen != c->frame_serial) {
            if (entry->has_current) {
                memcpy(entry->prev_model, entry->current_model, sizeof(entry->prev_model));
                entry->has_prev = 1;
            }
            memcpy(entry->current_model, current_model, sizeof(entry->current_model));
            entry->has_current = 1;
            entry->last_frame_seen = c->frame_serial;
        }

        if (entry->has_prev) {
            memcpy(out_prev_model, entry->prev_model, sizeof(entry->prev_model));
            *out_has_prev = 1;
        }
        return;
    }

    if (!ensure_motion_history_capacity(c, c->motion_history_count + 1))
        return;
    if (!canvas3d_ensure_motion_hash_capacity(c, c->motion_history_count + 1))
        return;

    hist = (canvas_motion_history_t *)c->motion_history;
    int32_t new_index = c->motion_history_count++;
    canvas_motion_history_t *entry = &hist[new_index];
    memset(entry, 0, sizeof(*entry));
    entry->key = motion_key;
    memcpy(entry->current_model, current_model, sizeof(entry->current_model));
    entry->has_current = 1;
    entry->last_frame_seen = c->frame_serial;
    canvas3d_motion_hash_insert_existing(c, new_index);
}

/// @brief Mix one pointer/value into a running motion-history hash key (boost-style
///   hash_combine with the golden-ratio constant) so per-object motion vectors stay stable.
static uintptr_t canvas3d_mix_motion_key(uintptr_t key, uintptr_t value) {
    key ^= value + (uintptr_t)0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    return key;
}

static uint32_t canvas3d_hash_u64(uintptr_t value) {
    uint64_t x = (uint64_t)value;
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    x *= UINT64_C(0xc4ceb9fe1a85ec53);
    x ^= x >> 33;
    return (uint32_t)x;
}

static int32_t canvas3d_next_power_of_two_i32(int32_t value) {
    int32_t cap = 1;
    if (value <= 1)
        return 1;
    while (cap < value) {
        if (cap > INT32_MAX / 2)
            return value;
        cap <<= 1;
    }
    return cap;
}

/// @brief Derive a stable object draw key for transform-handle draw calls.
static uintptr_t canvas3d_mesh_transform_motion_key(const void *mesh_obj,
                                                    const void *material_obj,
                                                    const void *transform_obj) {
    uintptr_t key = (uintptr_t)transform_obj;
    key = canvas3d_mix_motion_key(key, (uintptr_t)mesh_obj);
    key = canvas3d_mix_motion_key(key, (uintptr_t)material_obj);
    return key ? key : (uintptr_t)1u;
}

/// @brief Derive a stable per-instance key for the motion-blur history table.
/// @details Includes the caller's batch buffer identity so two batches with
///          the same mesh/material/count do not alias one another's previous
///          transforms. Keeping the same matrix buffer across frames preserves
///          continuous history; a reallocated buffer safely starts fresh.
static uintptr_t canvas3d_instance_motion_key(const void *mesh_obj,
                                              const void *material_obj,
                                              const void *batch_obj,
                                              int32_t instance_count,
                                              int32_t index) {
    uintptr_t key = (uintptr_t)mesh_obj;
    uintptr_t instance_key = (uintptr_t)((uint32_t)index + 1u);
    key = canvas3d_mix_motion_key(key, (uintptr_t)material_obj);
    key = canvas3d_mix_motion_key(key, (uintptr_t)batch_obj);
    key = canvas3d_mix_motion_key(key, (uintptr_t)((uint32_t)instance_count + 1u));
    key ^= instance_key * (uintptr_t)0xc2b2ae35u;
    return key ? key : instance_key;
}

/// @brief Compact the canvas's slotted light array into a dense param array.
///
/// Canvas lights live in a fixed array with NULL-able slots (so removal
/// doesn't shift indices). This packs the non-NULL slots into a dense
/// array the backend draw path consumes, returning the count.
static void canvas3d_copy_light_params(const rt_light3d *l, vgfx3d_light_params_t *out) {
    if (!l || !out)
        return;
    memset(out, 0, sizeof(*out));
    out->type = l->type;
    out->shadow_index = -1;
    out->direction[0] = canvas3d_sanitize_f64_to_float(l->direction[0], 0.0f);
    out->direction[1] = canvas3d_sanitize_f64_to_float(l->direction[1], -1.0f);
    out->direction[2] = canvas3d_sanitize_f64_to_float(l->direction[2], 0.0f);
    out->position[0] = canvas3d_sanitize_f64_to_float(l->position[0], 0.0f);
    out->position[1] = canvas3d_sanitize_f64_to_float(l->position[1], 0.0f);
    out->position[2] = canvas3d_sanitize_f64_to_float(l->position[2], 0.0f);
    out->color[0] = canvas3d_clamp01_f64(l->color[0]);
    out->color[1] = canvas3d_clamp01_f64(l->color[1]);
    out->color[2] = canvas3d_clamp01_f64(l->color[2]);
    out->intensity = canvas3d_sanitize_nonnegative_f64(l->intensity, 1.0f);
    out->attenuation = canvas3d_sanitize_nonnegative_f64(l->attenuation, 1.0f);
    out->inner_cos = canvas3d_clamp_f64_to_float(l->inner_cos, -1.0, 1.0, 1.0f);
    out->outer_cos = canvas3d_clamp_f64_to_float(l->outer_cos, -1.0, 1.0, 0.0f);
}

/// @brief Score a directional light by luminance-weighted intensity.
/// @details Used to rank shadow-caster candidates: only type 0 (directional)
///   lights score above zero, and their score uses the Rec. 709 luminance
///   coefficients so a dim yellow light doesn't outrank a bright blue one
///   just because it has higher green. Non-directional lights and nulls
///   return -1.0 to drop out of any ranking comparison.
static float canvas3d_shadow_light_param_score(const vgfx3d_light_params_t *l) {
    float luminance;

    if (!l || l->type != 0)
        return -1.0f;
    luminance = (float)(0.2126 * l->color[0] + 0.7152 * l->color[1] + 0.0722 * l->color[2]);
    return (float)l->intensity * luminance;
}

/// @brief Flatten the canvas's sparse light array into a dense backend buffer.
/// @details The canvas stores lights in fixed slots (`lights[0..VGFX3D_MAX_LIGHTS]`)
///   so that dropped-and-readded lights keep stable slot identities, but the
///   GPU backends expect a packed array — this routine bridges the two. Stops
///   when either every slot has been visited or `max` entries have been
///   written, whichever comes first.
/// @return The number of lights actually copied into `out`.
static int32_t build_light_params(const rt_canvas3d *c, vgfx3d_light_params_t *out, int32_t max) {
    int32_t count = 0;
    if (!c || !out || max <= 0)
        return 0;
    for (int i = 0; i < VGFX3D_MAX_LIGHTS && count < max; i++) {
        const rt_light3d *l = c->lights[i];
        if (!l || !l->enabled)
            continue;
        canvas3d_copy_light_params(l, &out[count]);
        count++;
    }
    for (int i = 0; i < c->scene_light_count && i < VGFX3D_MAX_LIGHTS && count < max; i++) {
        const rt_light3d *l = c->scene_lights[i];
        if (!l || !l->enabled)
            continue;
        canvas3d_copy_light_params(l, &out[count]);
        count++;
    }
    return count;
}

/// @brief Tolerance-based scalar equality for light parameters.
/// @details Uses a *relative* epsilon scaled by the larger magnitude of the two inputs
///          (floored at 1.0 so near-zero values still compare sensibly). This is what
///          keeps shadow-light slot selection stable across frames: if the same light's
///          direction or color wobbles by a few ULPs from floating-point noise, the
///          comparison still says "same light" and the slot assignment doesn't flicker.
static int canvas3d_light_param_close(float a, float b) {
    float scale = fmaxf(1.0f, fmaxf(fabsf(a), fabsf(b)));
    return fabsf(a - b) <= 1e-5f * scale;
}

/// @brief Test whether two directional-light param structs refer to the "same" light
///        for the purpose of shadow-slot de-duplication.
/// @details Compares direction[3], color[3], and intensity using `canvas3d_light_param_close`
///          (tolerance-based, see above). Non-directional lights always return "not
///          match" even when both are non-directional — shadow mapping in this runtime
///          is directional-only, so matching a spot-vs-spot or point-vs-point light
///          into the shadow slot table would be meaningless.
static int canvas3d_shadow_light_params_match(const vgfx3d_light_params_t *a,
                                              const vgfx3d_light_params_t *b) {
    if (!a || !b || a->type != b->type)
        return 0;
    if (a->type != 0)
        return 0;
    for (int i = 0; i < 3; i++) {
        if (!canvas3d_light_param_close(a->direction[i], b->direction[i]) ||
            !canvas3d_light_param_close(a->color[i], b->color[i]))
            return 0;
    }
    return canvas3d_light_param_close(a->intensity, b->intensity);
}

/// @brief Pick the strongest directional shadow-casting lights that are actually
///        referenced by opaque draw commands this frame.
/// @details Walks the deferred draw queue and, for every opaque main-pass command,
///          visits the light params the command has accumulated. Each directional
///          light is scored (see `canvas3d_shadow_light_param_score`) and inserted
///          into a top-K table sorted by score. Duplicate lights (same direction /
///          color / intensity within tolerance) are collapsed so one physical light
///          can only claim one shadow slot even when many draws reference it.
///
///          Behaviour difference from the old bind-order-based selection: picks
///          propagate strictly from the draw list, so a bound but unused light can't
///          steal a shadow slot from a bound-and-actually-drawn light. Slot assignment
///          is deterministic per-frame and stable across frames under normal
///          floating-point noise.
///
///          `shadow_index` on unused slots is set to -1 so downstream code can skip
///          them, and the output array is zero-initialized so stale residue from a
///          prior frame can't leak into the shader constants.
/// @param cmds Deferred draw queue.
/// @param draw_count Number of valid commands in `cmds`.
/// @param out_lights Output buffer sized `max_shadow_lights`.
/// @param max_shadow_lights Caller cap; clamped to `VGFX3D_MAX_SHADOW_LIGHTS`.
/// @return Number of shadow-light slots populated.
static int32_t canvas3d_select_shadow_directional_lights_from_draws(
    const deferred_draw_t *cmds,
    int32_t draw_count,
    vgfx3d_light_params_t *out_lights,
    int32_t max_shadow_lights) {
    float best_scores[VGFX3D_MAX_SHADOW_LIGHTS];
    int32_t count = 0;

    if (!cmds || draw_count <= 0 || !out_lights || max_shadow_lights <= 0)
        return 0;
    if (max_shadow_lights > VGFX3D_MAX_SHADOW_LIGHTS)
        max_shadow_lights = VGFX3D_MAX_SHADOW_LIGHTS;

    for (int32_t i = 0; i < max_shadow_lights; i++) {
        best_scores[i] = -1.0f;
        memset(&out_lights[i], 0, sizeof(out_lights[i]));
        out_lights[i].shadow_index = -1;
    }

    for (int32_t ci = 0; ci < draw_count; ci++) {
        if (cmds[ci].pass_kind != DEFERRED_PASS_MAIN || cmds[ci].requires_blend)
            continue;
        for (int32_t li = 0; li < cmds[ci].light_count; li++) {
            const vgfx3d_light_params_t *light = &cmds[ci].lights[li];
            float score = canvas3d_shadow_light_param_score(light);
            int32_t insert_at;
            int duplicate = 0;

            if (score <= 0.0f)
                continue;
            for (int32_t existing = 0; existing < count; existing++) {
                if (canvas3d_shadow_light_params_match(light, &out_lights[existing])) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate)
                continue;
            if (count >= max_shadow_lights && score <= best_scores[max_shadow_lights - 1])
                continue;

            insert_at = count;
            if (insert_at >= max_shadow_lights)
                insert_at = max_shadow_lights - 1;
            while (insert_at > 0 && score > best_scores[insert_at - 1]) {
                if (insert_at < max_shadow_lights) {
                    best_scores[insert_at] = best_scores[insert_at - 1];
                    out_lights[insert_at] = out_lights[insert_at - 1];
                }
                insert_at--;
            }
            if (insert_at < max_shadow_lights) {
                best_scores[insert_at] = score;
                out_lights[insert_at] = *light;
                out_lights[insert_at].shadow_index = -1;
                if (count < max_shadow_lights)
                    count++;
            }
        }
    }
    return count;
}

/// @brief Stamp shadow-map slot numbers onto a draw's copied light snapshot.
/// @details Shadow casters are selected from queued draw light snapshots, not
///   the canvas's live light slots. That lets transient Scene3D node lights keep
///   shadow support inside user-managed Begin/End frames.
static void canvas3d_apply_shadow_light_params(vgfx3d_light_params_t *lights,
                                               int32_t light_count,
                                               const vgfx3d_light_params_t *shadow_lights,
                                               int32_t shadow_count) {
    if (!lights || light_count <= 0)
        return;
    for (int32_t i = 0; i < light_count; i++)
        lights[i].shadow_index = -1;
    for (int32_t slot = 0; slot < shadow_count; slot++) {
        if (!shadow_lights)
            break;
        for (int32_t i = 0; i < light_count; i++) {
            if (canvas3d_shadow_light_params_match(&lights[i], &shadow_lights[slot]))
                lights[i].shadow_index = slot;
        }
    }
}

/// @brief Track a malloc'd buffer for end-of-frame cleanup.
///
/// Used when the deferred path needs to allocate a transient instance-
/// matrix buffer that outlives the calling Zia frame. Geometric
/// growth (cap doubles, starting at 8). Ownership transfers only on
/// success; callers keep ownership and must free the buffer on failure.
static int canvas3d_track_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!c || !buffer)
        return 0;
    for (int32_t i = 0; i < c->temp_buf_count; ++i) {
        if (c->temp_buffers[i] == buffer)
            return 1;
    }
    if (c->temp_buf_count >= c->temp_buf_capacity) {
        if (c->temp_buf_capacity > INT32_MAX / 2)
            return 0;
        int32_t new_cap = c->temp_buf_capacity == 0 ? 8 : c->temp_buf_capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(void *))
            return 0;
        void **nb = (void **)realloc(c->temp_buffers, (size_t)new_cap * sizeof(void *));
        if (!nb)
            return 0;
        c->temp_buffers = nb;
        c->temp_buf_capacity = new_cap;
    }
    c->temp_buffers[c->temp_buf_count++] = buffer;
    return 1;
}

/// @brief Remove a tracked temp buffer without freeing it.
static int canvas3d_untrack_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!c || !buffer)
        return 0;
    for (int32_t i = 0; i < c->temp_buf_count; ++i) {
        if (c->temp_buffers[i] == buffer) {
            for (int32_t j = i; j < c->temp_buf_count - 1; ++j)
                c->temp_buffers[j] = c->temp_buffers[j + 1];
            c->temp_buffers[--c->temp_buf_count] = NULL;
            return 1;
        }
    }
    return 0;
}

/// @brief Untrack and free a temp buffer when a later allocation path fails.
static void canvas3d_release_tracked_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!buffer)
        return;
    if (canvas3d_untrack_temp_buffer(c, buffer))
        free(buffer);
}

/// @brief Track a malloc'd buffer used by deferred final-overlay commands.
///
/// Final overlays are recorded before frame finalization and replayed after
/// post-FX. Their geometry must survive normal End() cleanup, so they use a
/// separate temp-buffer list cleared after Flip() or ClearOverlay().
static int canvas3d_track_final_overlay_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!c || !buffer)
        return 0;
    if (c->final_overlay_temp_buf_count >= c->final_overlay_temp_buf_capacity) {
        if (c->final_overlay_temp_buf_capacity > INT32_MAX / 2)
            return 0;
        int32_t new_cap =
            c->final_overlay_temp_buf_capacity == 0 ? 8 : c->final_overlay_temp_buf_capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(void *))
            return 0;
        void **nb =
            (void **)realloc(c->final_overlay_temp_buffers, (size_t)new_cap * sizeof(void *));
        if (!nb)
            return 0;
        c->final_overlay_temp_buffers = nb;
        c->final_overlay_temp_buf_capacity = new_cap;
    }
    c->final_overlay_temp_buffers[c->final_overlay_temp_buf_count++] = buffer;
    return 1;
}

static int canvas3d_untrack_final_overlay_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!c || !buffer)
        return 0;
    for (int32_t i = 0; i < c->final_overlay_temp_buf_count; ++i) {
        if (c->final_overlay_temp_buffers[i] == buffer) {
            for (int32_t j = i; j < c->final_overlay_temp_buf_count - 1; ++j)
                c->final_overlay_temp_buffers[j] = c->final_overlay_temp_buffers[j + 1];
            c->final_overlay_temp_buffers[--c->final_overlay_temp_buf_count] = NULL;
            return 1;
        }
    }
    return 0;
}

static void canvas3d_release_tracked_final_overlay_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!buffer)
        return;
    if (canvas3d_untrack_final_overlay_temp_buffer(c, buffer))
        free(buffer);
}

static void canvas3d_temp_object_set_clear(rt_canvas3d *c) {
    if (!c || !c->temp_object_set || c->temp_object_set_capacity <= 0)
        return;
    memset(c->temp_object_set, 0, (size_t)c->temp_object_set_capacity * sizeof(void *));
}

static int canvas3d_ensure_temp_object_set(rt_canvas3d *c, int32_t count_hint) {
    if (!c)
        return 0;
    int32_t needed = canvas3d_next_power_of_two_i32(count_hint > 0 ? count_hint * 2 : 32);
    if (needed < 32)
        needed = 32;
    if (c->temp_object_set_capacity >= needed)
        return 1;
    if ((size_t)needed > SIZE_MAX / sizeof(*c->temp_object_set))
        return 0;
    void **grown = (void **)realloc(c->temp_object_set, (size_t)needed * sizeof(*grown));
    if (!grown)
        return 0;
    c->temp_object_set = grown;
    c->temp_object_set_capacity = needed;
    canvas3d_temp_object_set_clear(c);
    for (int32_t i = 0; i < c->temp_obj_count; ++i) {
        void *existing = c->temp_objects[i];
        if (!existing)
            continue;
        int32_t mask = c->temp_object_set_capacity - 1;
        int32_t slot = (int32_t)(canvas3d_hash_u64((uintptr_t)existing) & (uint32_t)mask);
        for (int32_t probe = 0; probe < c->temp_object_set_capacity; ++probe) {
            if (!c->temp_object_set[slot]) {
                c->temp_object_set[slot] = existing;
                break;
            }
            slot = (slot + 1) & mask;
        }
    }
    return 1;
}

static int canvas3d_temp_object_set_contains(rt_canvas3d *c, void *obj) {
    if (!c || !obj || c->temp_obj_count <= 0)
        return 0;
    if (!c->temp_object_set || c->temp_object_set_capacity < c->temp_obj_count * 2) {
        if (!canvas3d_ensure_temp_object_set(c, c->temp_obj_count + 1)) {
            for (int32_t i = 0; i < c->temp_obj_count; ++i) {
                if (c->temp_objects[i] == obj)
                    return 1;
            }
            return 0;
        }
    }
    int32_t mask = c->temp_object_set_capacity - 1;
    int32_t slot = (int32_t)(canvas3d_hash_u64((uintptr_t)obj) & (uint32_t)mask);
    for (int32_t probe = 0; probe < c->temp_object_set_capacity; ++probe) {
        void *entry = c->temp_object_set[slot];
        if (!entry)
            return 0;
        if (entry == obj)
            return 1;
        slot = (slot + 1) & mask;
    }
    return 0;
}

static int canvas3d_temp_object_set_insert(rt_canvas3d *c, void *obj) {
    if (!c || !obj)
        return 0;
    if (!canvas3d_ensure_temp_object_set(c, c->temp_obj_count + 1))
        return 0;
    int32_t mask = c->temp_object_set_capacity - 1;
    int32_t slot = (int32_t)(canvas3d_hash_u64((uintptr_t)obj) & (uint32_t)mask);
    for (int32_t probe = 0; probe < c->temp_object_set_capacity; ++probe) {
        if (!c->temp_object_set[slot]) {
            c->temp_object_set[slot] = obj;
            return 1;
        }
        if (c->temp_object_set[slot] == obj)
            return 1;
        slot = (slot + 1) & mask;
    }
    return 0;
}

static void canvas3d_rebuild_temp_object_set(rt_canvas3d *c) {
    if (!c || !c->temp_object_set)
        return;
    canvas3d_temp_object_set_clear(c);
    for (int32_t i = 0; i < c->temp_obj_count; ++i)
        canvas3d_temp_object_set_insert(c, c->temp_objects[i]);
}

/// @brief Track a GC-managed object for end-of-frame release.
///
/// Retains `obj` immediately so it survives at least until the
/// frame ends, then releases at end-of-frame via `clear_temp_objects`.
static int canvas3d_track_temp_object(rt_canvas3d *c, void *obj) {
    if (!c || !obj)
        return 0;
    if (canvas3d_temp_object_set_contains(c, obj))
        return 1;
    if (c->temp_obj_count >= c->temp_obj_capacity) {
        if (c->temp_obj_capacity > INT32_MAX / 2)
            return 0;
        int32_t new_cap = c->temp_obj_capacity == 0 ? 8 : c->temp_obj_capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(void *))
            return 0;
        void **nb = (void **)realloc(c->temp_objects, (size_t)new_cap * sizeof(void *));
        if (!nb)
            return 0;
        c->temp_objects = nb;
        c->temp_obj_capacity = new_cap;
    }
    if (!canvas3d_temp_object_set_insert(c, obj))
        return 0;
    rt_obj_retain_maybe(obj);
    c->temp_objects[c->temp_obj_count++] = obj;
    return 1;
}

static void canvas3d_release_tracked_temp_object(rt_canvas3d *c, void *obj) {
    if (!c || !obj)
        return;
    for (int32_t i = 0; i < c->temp_obj_count; ++i) {
        if (c->temp_objects[i] == obj) {
            for (int32_t j = i; j < c->temp_obj_count - 1; ++j)
                c->temp_objects[j] = c->temp_objects[j + 1];
            c->temp_objects[--c->temp_obj_count] = NULL;
            canvas3d_rebuild_temp_object_set(c);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            return;
        }
    }
}

/// @brief Copy mesh vertex+index arrays into canvas-owned temp buffers.
/// @details Used when the mesh's owning heap object may be freed before the GPU
///          consumes the draw command — the snapshot lives on the canvas's temp
///          buffer list, freed at end-of-frame. Returns 1 on success, 0 on
///          allocation failure or invalid mesh state.
static int canvas3d_snapshot_mesh_geometry(rt_canvas3d *c,
                                           const rt_mesh3d *mesh,
                                           vgfx3d_vertex_t **out_vertices,
                                           uint32_t **out_indices) {
    vgfx3d_vertex_t *vertices;
    uint32_t *indices;
    size_t vertex_bytes;
    size_t index_bytes;
    if (!c || !mesh || !out_vertices || !out_indices || !mesh->vertices || !mesh->indices ||
        mesh->vertex_count == 0 || mesh->index_count == 0)
        return 0;
    if ((size_t)mesh->vertex_count > SIZE_MAX / sizeof(*vertices) ||
        (size_t)mesh->index_count > SIZE_MAX / sizeof(*indices))
        return 0;
    vertex_bytes = (size_t)mesh->vertex_count * sizeof(*vertices);
    index_bytes = (size_t)mesh->index_count * sizeof(*indices);
    vertices = (vgfx3d_vertex_t *)malloc(vertex_bytes);
    if (!vertices)
        return 0;
    indices = (uint32_t *)malloc(index_bytes);
    if (!indices) {
        free(vertices);
        return 0;
    }
    memcpy(vertices, mesh->vertices, vertex_bytes);
    memcpy(indices, mesh->indices, index_bytes);
    if (!canvas3d_track_temp_buffer(c, vertices)) {
        free(vertices);
        free(indices);
        return 0;
    }
    if (!canvas3d_track_temp_buffer(c, indices)) {
        canvas3d_release_tracked_temp_buffer(c, vertices);
        free(indices);
        return 0;
    }
    *out_vertices = vertices;
    *out_indices = indices;
    return 1;
}

static int canvas3d_reserve_mesh_snapshot_cache(rt_canvas3d *c, int32_t needed) {
    if (!c)
        return 0;
    if (needed <= c->mesh_snapshot_capacity)
        return 1;
    if (c->mesh_snapshot_capacity > INT32_MAX / 2)
        return 0;
    int32_t new_cap = c->mesh_snapshot_capacity == 0 ? 8 : c->mesh_snapshot_capacity * 2;
    while (new_cap < needed) {
        if (new_cap > INT32_MAX / 2)
            return 0;
        new_cap *= 2;
    }
    if ((size_t)new_cap > SIZE_MAX / sizeof(*c->mesh_snapshots))
        return 0;
    rt_canvas3d_mesh_snapshot_entry *entries =
        (rt_canvas3d_mesh_snapshot_entry *)realloc(c->mesh_snapshots,
                                                   (size_t)new_cap * sizeof(*entries));
    if (!entries)
        return 0;
    c->mesh_snapshots = entries;
    c->mesh_snapshot_capacity = new_cap;
    return 1;
}

static int canvas3d_snapshot_mesh_geometry_cached(rt_canvas3d *c,
                                                  const rt_mesh3d *mesh,
                                                  void *mesh_obj,
                                                  vgfx3d_vertex_t **out_vertices,
                                                  uint32_t **out_indices) {
    int can_cache = mesh_obj && rt_heap_is_payload(mesh_obj);
    if (can_cache) {
        for (int32_t i = 0; i < c->mesh_snapshot_count; ++i) {
            rt_canvas3d_mesh_snapshot_entry *entry = &c->mesh_snapshots[i];
            if (entry->source == mesh_obj && entry->geometry_revision == mesh->geometry_revision &&
                entry->vertex_count == mesh->vertex_count && entry->index_count == mesh->index_count) {
                *out_vertices = entry->vertices;
                *out_indices = entry->indices;
                return 1;
            }
        }
    }
    if (!canvas3d_snapshot_mesh_geometry(c, mesh, out_vertices, out_indices))
        return 0;
    if (can_cache) {
        if (!canvas3d_reserve_mesh_snapshot_cache(c, c->mesh_snapshot_count + 1))
            return 1;
        rt_canvas3d_mesh_snapshot_entry *entry = &c->mesh_snapshots[c->mesh_snapshot_count++];
        entry->source = mesh_obj;
        entry->geometry_revision = mesh->geometry_revision;
        entry->vertex_count = mesh->vertex_count;
        entry->index_count = mesh->index_count;
        entry->vertices = *out_vertices;
        entry->indices = *out_indices;
    }
    return 1;
}

/// @brief Decide whether to snapshot a mesh's geometry into canvas-owned buffers.
/// @details Heap meshes snapshot their vertex/index buffers so a user mutation after
///          enqueue cannot change submitted deferred geometry. Draw-time deformation
///          payloads stay on the original mesh so GPU skinning/morph paths can bind
///          their palettes and weights without allocating CPU geometry snapshots.
static int canvas3d_should_snapshot_geometry(const rt_mesh3d *mesh, void *mesh_obj) {
    if (!mesh || !mesh_obj)
        return 0;
    if (rt_heap_is_payload(mesh_obj))
        return 1;
    return 0;
}

/// @brief Free every tracked transient buffer (called at end of frame).
static void canvas3d_clear_temp_buffers(rt_canvas3d *c) {
    if (!c)
        return;
    for (int32_t i = 0; i < c->temp_buf_count; i++)
        free(c->temp_buffers[i]);
    c->temp_buf_count = 0;
    c->mesh_snapshot_count = 0;
}

/// @brief Discard recorded final-overlay commands and owned geometry buffers.
void canvas3d_clear_final_overlay(rt_canvas3d *c) {
    if (!c)
        return;
    for (int32_t i = 0; i < c->final_overlay_temp_buf_count; i++)
        free(c->final_overlay_temp_buffers[i]);
    c->final_overlay_temp_buf_count = 0;
    c->final_overlay_count = 0;
    c->final_overlay_recording = 0;
}

/// @brief Release every tracked transient GC object (called at end of frame).
static void canvas3d_clear_temp_objects(rt_canvas3d *c) {
    if (!c)
        return;
    for (int32_t i = 0; i < c->temp_obj_count; i++) {
        if (c->temp_objects[i] && rt_obj_release_check0(c->temp_objects[i]))
            rt_obj_free(c->temp_objects[i]);
        c->temp_objects[i] = NULL;
    }
    c->temp_obj_count = 0;
    canvas3d_temp_object_set_clear(c);
}

/// @brief Transform a local point by a row-major 4x4 model matrix.
static void canvas3d_transform_point(const float *model_matrix,
                                     const float local_point[3],
                                     float out_world[3]) {
    if (!model_matrix || !local_point || !out_world)
        return;
    out_world[0] = model_matrix[0] * local_point[0] + model_matrix[1] * local_point[1] +
                   model_matrix[2] * local_point[2] + model_matrix[3];
    out_world[1] = model_matrix[4] * local_point[0] + model_matrix[5] * local_point[1] +
                   model_matrix[6] * local_point[2] + model_matrix[7];
    out_world[2] = model_matrix[8] * local_point[0] + model_matrix[9] * local_point[1] +
                   model_matrix[10] * local_point[2] + model_matrix[11];
}

/// @brief Project a world-space point onto the camera's forward axis.
static float canvas3d_depth_along_view(const rt_canvas3d *c, const float world_point[3]) {
    float dx;
    float dy;
    float dz;

    if (!c || !world_point)
        return 0.0f;
    dx = world_point[0] - c->cached_cam_pos[0];
    dy = world_point[1] - c->cached_cam_pos[1];
    dz = world_point[2] - c->cached_cam_pos[2];
    return dx * c->cached_cam_forward[0] + dy * c->cached_cam_forward[1] +
           dz * c->cached_cam_forward[2];
}

/// @brief Compute the min/max view depth covered by a transformed local AABB.
static void canvas3d_compute_depth_range(const rt_canvas3d *c,
                                         const float *model_matrix,
                                         const float *local_bounds_min,
                                         const float *local_bounds_max,
                                         int8_t has_local_bounds,
                                         float *out_near_depth,
                                         float *out_far_depth) {
    float near_depth;
    float far_depth;

    if (out_near_depth)
        *out_near_depth = 0.0f;
    if (out_far_depth)
        *out_far_depth = 0.0f;
    if (!c || !model_matrix)
        return;

    near_depth = FLT_MAX;
    far_depth = -FLT_MAX;
    if (has_local_bounds && local_bounds_min && local_bounds_max) {
        for (int xi = 0; xi < 2; xi++) {
            for (int yi = 0; yi < 2; yi++) {
                for (int zi = 0; zi < 2; zi++) {
                    float local_point[3];
                    float world_point[3];
                    float depth;

                    local_point[0] = xi ? local_bounds_max[0] : local_bounds_min[0];
                    local_point[1] = yi ? local_bounds_max[1] : local_bounds_min[1];
                    local_point[2] = zi ? local_bounds_max[2] : local_bounds_min[2];
                    canvas3d_transform_point(model_matrix, local_point, world_point);
                    depth = canvas3d_depth_along_view(c, world_point);
                    if (depth < near_depth)
                        near_depth = depth;
                    if (depth > far_depth)
                        far_depth = depth;
                }
            }
        }
    } else {
        float origin[3] = {0.0f, 0.0f, 0.0f};
        float world_origin[3];
        canvas3d_transform_point(model_matrix, origin, world_origin);
        near_depth = far_depth = canvas3d_depth_along_view(c, world_origin);
    }

    if (out_near_depth)
        *out_near_depth = near_depth;
    if (out_far_depth)
        *out_far_depth = far_depth;
}

/// @brief Compute the deferred-sort key for one draw.
/// @details Opaque draws sort by nearest depth (front-to-back). Transparent
///          draws sort by farthest depth (back-to-front). When local bounds are
///          available, the key comes from the transformed AABB rather than the
///          model origin, which makes sorting more stable for large or offset
///          meshes.
static float canvas3d_compute_sort_key(const rt_canvas3d *c,
                                       const float *model_matrix,
                                       const float *local_bounds_min,
                                       const float *local_bounds_max,
                                       int8_t has_local_bounds,
                                       int8_t transparent) {
    float near_depth;
    float far_depth;

    canvas3d_compute_depth_range(c,
                                 model_matrix,
                                 local_bounds_min,
                                 local_bounds_max,
                                 has_local_bounds,
                                 &near_depth,
                                 &far_depth);
    return transparent ? far_depth : near_depth;
}

/// @brief Compute one opaque sort key for an instanced batch.
/// @details A single key can only approximate a spatially wide batch. This
///          uses the center of the aggregate world-space bounds instead of the
///          old minimum-per-instance depth, which biased large batches toward
///          whichever instance happened to be closest to the camera.
static float canvas3d_compute_instanced_batch_sort_key(const rt_canvas3d *c,
                                                       const float *instance_matrices,
                                                       int32_t instance_count,
                                                       const float *local_bounds_min,
                                                       const float *local_bounds_max) {
    enum { CANVAS3D_INSTANCED_SORT_FULL_SCAN_LIMIT = 256 };
    float world_min[3] = {0.0f, 0.0f, 0.0f};
    float world_max[3] = {0.0f, 0.0f, 0.0f};
    int8_t has_bounds = 0;
    int32_t sample_count;

    if (!c || !instance_matrices || instance_count <= 0 || !local_bounds_min || !local_bounds_max)
        return 0.0f;

    sample_count = instance_count <= CANVAS3D_INSTANCED_SORT_FULL_SCAN_LIMIT
                       ? instance_count
                       : CANVAS3D_INSTANCED_SORT_FULL_SCAN_LIMIT;
    for (int32_t sample = 0; sample < sample_count; sample++) {
        int32_t i = sample;
        double world_matrix[16];
        float instance_min[3];
        float instance_max[3];
        if (sample_count > 1 && sample_count != instance_count)
            i = (int32_t)(((int64_t)sample * (int64_t)(instance_count - 1)) /
                          (int64_t)(sample_count - 1));
        for (int j = 0; j < 16; j++)
            world_matrix[j] = (double)instance_matrices[(size_t)i * 16u + (size_t)j];
        vgfx3d_transform_aabb(
            local_bounds_min, local_bounds_max, world_matrix, instance_min, instance_max);
        if (!has_bounds) {
            memcpy(world_min, instance_min, sizeof(world_min));
            memcpy(world_max, instance_max, sizeof(world_max));
            has_bounds = 1;
        } else {
            for (int axis = 0; axis < 3; axis++) {
                if (instance_min[axis] < world_min[axis])
                    world_min[axis] = instance_min[axis];
                if (instance_max[axis] > world_max[axis])
                    world_max[axis] = instance_max[axis];
            }
        }
    }

    if (has_bounds) {
        float center[3] = {0.5f * (world_min[0] + world_max[0]),
                           0.5f * (world_min[1] + world_max[1]),
                           0.5f * (world_min[2] + world_max[2])};
        return canvas3d_depth_along_view(c, center);
    }
    return 0.0f;
}

/// @brief Extract a normalized forward vector (camera look direction) from a view matrix.
/// @details View row 2 holds the *backward* basis vector (cameras look
///          down −Z), so negating gives forward. Normalizes for safety
///          even though look-at construction already normalizes the
///          basis. Falls back to the conventional `(0, 0, −1)` if either
///          the input is null or the view is degenerate (zero forward),
///          which keeps downstream culling math safe.
static void canvas3d_extract_view_forward(const double *view, float *out_forward) {
    double fx = 0.0;
    double fy = 0.0;
    double fz = -1.0;
    double len;

    if (!out_forward)
        return;
    if (view) {
        fx = -view[8];
        fy = -view[9];
        fz = -view[10];
    }
    len = sqrt(fx * fx + fy * fy + fz * fz);
    if (len > 1e-12) {
        out_forward[0] = (float)(fx / len);
        out_forward[1] = (float)(fy / len);
        out_forward[2] = (float)(fz / len);
    } else {
        out_forward[0] = 0.0f;
        out_forward[1] = 0.0f;
        out_forward[2] = -1.0f;
    }
}

/// @brief Build a 2D-overlay camera (orthographic projection in pixels).
///
/// Used by `BeginOverlayFrame` so the 2D HUD layer can draw with
/// pixel coordinates (top-left origin, Y-down). The +2 padding on
/// each axis avoids edge-clipping at half-pixel coordinates.
static void canvas3d_build_ortho_camera(const rt_canvas3d *c, vgfx3d_camera_params_t *params) {
    float w;
    float h;
    int32_t out_w = 0;
    int32_t out_h = 0;

    if (!c || !params)
        return;
    memset(params, 0, sizeof(*params));
    canvas3d_active_output_size(c, &out_w, &out_h);
    if (out_w <= 0)
        out_w = c->width;
    if (out_h <= 0)
        out_h = c->height;
    w = (float)out_w + 2.0f;
    h = (float)out_h + 2.0f;
    params->projection[0] = 2.0f / w;
    params->projection[5] = -2.0f / h;
    params->projection[10] = -1.0f;
    params->projection[3] = -1.0f + 2.0f / w;
    params->projection[7] = 1.0f - 2.0f / h;
    params->projection[15] = 1.0f;
    params->view[0] = params->view[5] = params->view[10] = params->view[15] = 1.0f;
    params->position[2] = 1.0f;
    params->forward[2] = -1.0f;
    params->is_ortho = 1;
    params->fog_enabled = 0;
}

/// @brief Internal: begin a 2D overlay pass on top of the 3D scene.
///
/// Used by `Canvas3D` to draw HUD elements (text, sprites) on top of
/// the rendered scene. Switches to an orthographic projection,
/// preserves the existing color buffer (so the 3D scene stays
/// visible), and bypasses depth testing. Returns 0 if the canvas is
/// already in a frame or has no backend window.
int canvas3d_begin_overlay_frame(rt_canvas3d *c, int8_t preserve_existing_color) {
    vgfx3d_camera_params_t params;

    if (!c || !c->backend || !c->gfx_win || c->in_frame)
        return 0;
    if (c->backend->show_gpu_layer)
        c->backend->show_gpu_layer(c->backend_ctx);
    canvas3d_build_ortho_camera(c, &params);
    params.load_existing_color = preserve_existing_color ? 1 : 0;
    params.load_existing_depth = 0;
    if (!c->frame_postfx_state_latched)
        canvas3d_latch_gpu_postfx_state(c);
    else
        canvas3d_apply_gpu_postfx_state(c);
    c->cached_cam_pos[0] = 0.0f;
    c->cached_cam_pos[1] = 0.0f;
    c->cached_cam_pos[2] = 1.0f;
    c->cached_cam_forward[0] = params.forward[0];
    c->cached_cam_forward[1] = params.forward[1];
    c->cached_cam_forward[2] = params.forward[2];
    c->cached_cam_is_ortho = params.is_ortho;
    c->draw_count = 0;
    c->frame_is_2d = 1;
    for (int r = 0; r < 4; r++)
        for (int col = 0; col < 4; col++)
            c->cached_vp[r * 4 + col] = params.projection[r * 4 + 0] * params.view[0 * 4 + col] +
                                        params.projection[r * 4 + 1] * params.view[1 * 4 + col] +
                                        params.projection[r * 4 + 2] * params.view[2 * 4 + col] +
                                        params.projection[r * 4 + 3] * params.view[3 * 4 + col];
    c->backend->begin_frame(c->backend_ctx, &params);
    c->in_frame = 1;
    return 1;
}

/// @brief Internal: retrieve the active scene view-projection matrix.
///
/// Three-way fallback: in a 3D frame → current frame's VP; otherwise
/// in an overlay frame → last 3D frame's VP (so overlays project
/// using the camera that drew the scene); otherwise → 2D ortho VP.
/// Used by 3D-aware overlay drawables (e.g., world-space tooltips).
const float *canvas3d_active_scene_vp(const rt_canvas3d *c) {
    if (!c)
        return NULL;
    if (c->in_frame && !c->frame_is_2d)
        return c->cached_vp;
    if (c->has_last_scene_vp)
        return c->last_scene_vp;
    if (c->in_frame)
        return c->cached_vp;
    return NULL;
}

/// @brief Forward a draw command to the active backend's `submit_draw` op.
///
/// Thin wrapper that exists mostly so call sites read a single noun
/// ("submit a mesh") instead of a 7-arg backend method call.
static void canvas3d_submit_mesh(rt_canvas3d *c,
                                 const vgfx3d_draw_cmd_t *cmd,
                                 const vgfx3d_light_params_t *lights,
                                 int32_t light_count,
                                 const float *ambient,
                                 int8_t wireframe,
                                 int8_t backface_cull) {
    if (!c || !c->backend || !cmd)
        return;
    c->backend->submit_draw(
        c->backend_ctx, c->gfx_win, cmd, lights, light_count, ambient, wireframe, backface_cull);
}

/// @brief Decompose an instanced draw into N individual mesh draws.
///
/// Backends without `submit_draw_instanced` (e.g., software fallback)
/// can still render instanced data this way — it's slower but
/// preserves correctness. Per-instance matrices are unpacked into
/// individual `model_matrix`/`prev_model_matrix` pairs, with
/// `has_prev_instance_matrices` translated into per-mesh
/// `has_prev_model_matrix` flags.
static void canvas3d_submit_instanced_as_meshes(rt_canvas3d *c,
                                                const deferred_draw_t *dd,
                                                int shadow_only) {
    if (!c || !dd || !dd->instance_matrices || dd->instance_count <= 0)
        return;
    for (int32_t i = 0; i < dd->instance_count; i++) {
        vgfx3d_draw_cmd_t per_instance = dd->cmd;
        memcpy(per_instance.model_matrix,
               &dd->instance_matrices[(size_t)i * 16u],
               sizeof(per_instance.model_matrix));
        if (dd->cmd.has_prev_instance_matrices && dd->cmd.prev_instance_matrices) {
            memcpy(per_instance.prev_model_matrix,
                   &dd->cmd.prev_instance_matrices[(size_t)i * 16u],
                   sizeof(per_instance.prev_model_matrix));
            per_instance.has_prev_model_matrix = 1;
        } else {
            memcpy(per_instance.prev_model_matrix,
                   per_instance.model_matrix,
                   sizeof(per_instance.prev_model_matrix));
            per_instance.has_prev_model_matrix = 0;
        }
        if (shadow_only) {
            if (c->backend->shadow_draw)
                c->backend->shadow_draw(c->backend_ctx, &per_instance);
        } else {
            canvas3d_submit_mesh(c,
                                 &per_instance,
                                 dd->lights,
                                 dd->light_count,
                                 dd->ambient,
                                 dd->wireframe,
                                 dd->backface_cull);
        }
    }
}

/// @brief Fill a deferred-draw snapshot with all backend-visible state.
///
/// Captures every parameter the backend needs so the snapshot survives even if
/// caller-side state (lights, ambient) changes between enqueue and flush.
static void canvas3d_fill_deferred_draw(rt_canvas3d *c,
                                        deferred_draw_t *dd,
                                        const vgfx3d_draw_cmd_t *cmd,
                                        deferred_draw_kind_t kind,
                                        deferred_pass_t pass_kind,
                                        const float *instance_matrices,
                                        int32_t instance_count,
                                        int include_lights,
                                        int8_t wireframe,
                                        int8_t backface_cull,
                                        float sort_key,
                                        const float *local_bounds_min,
                                        const float *local_bounds_max) {
    memset(dd, 0, sizeof(*dd));
    dd->kind = kind;
    dd->pass_kind = pass_kind;
    dd->visible = 1;
    dd->cmd = *cmd;
    dd->requires_blend = canvas3d_cmd_requires_blend(cmd);
    dd->instance_matrices = instance_matrices;
    dd->instance_count = instance_count;
    dd->sort_key = sort_key;
    dd->wireframe = wireframe;
    dd->backface_cull = backface_cull;
    dd->ambient[0] = c->ambient[0];
    dd->ambient[1] = c->ambient[1];
    dd->ambient[2] = c->ambient[2];
    dd->light_count = include_lights ? build_light_params(c, dd->lights, VGFX3D_MAX_LIGHTS) : 0;
    if (local_bounds_min && local_bounds_max) {
        dd->has_local_bounds = 1;
        memcpy(dd->local_bounds_min, local_bounds_min, sizeof(dd->local_bounds_min));
        memcpy(dd->local_bounds_max, local_bounds_max, sizeof(dd->local_bounds_max));
    }
}

/// @brief Append a draw to the deferred-draw queue (transparency / sort path).
///
/// The queue is dispatched at end-of-frame in sorted order. Allocation failure
/// traps instead of submitting immediately, because bypassing the queue breaks
/// transparent ordering and can render a visibly incorrect frame.
static int canvas3d_enqueue_draw(rt_canvas3d *c,
                                 const vgfx3d_draw_cmd_t *cmd,
                                 deferred_draw_kind_t kind,
                                 deferred_pass_t pass_kind,
                                 const float *instance_matrices,
                                 int32_t instance_count,
                                 int include_lights,
                                 int8_t wireframe,
                                 int8_t backface_cull,
                                 float sort_key,
                                 const float *local_bounds_min,
                                 const float *local_bounds_max) {
    deferred_draw_t *dd;

    if (!c || !cmd)
        return 0;
    if (!ensure_deferred_capacity(&c->draw_cmds, &c->draw_capacity, c->draw_count + 1)) {
        (void)pass_kind;
        rt_trap("Canvas3D: deferred draw queue allocation failed");
        return 0;
    }

    dd = &((deferred_draw_t *)c->draw_cmds)[c->draw_count++];
    canvas3d_fill_deferred_draw(c,
                                dd,
                                cmd,
                                kind,
                                pass_kind,
                                instance_matrices,
                                instance_count,
                                include_lights,
                                wireframe,
                                backface_cull,
                                sort_key,
                                local_bounds_min,
                                local_bounds_max);
    return 1;
}

/// @brief Append a draw to the final-overlay queue for replay after post-FX.
static int canvas3d_enqueue_final_overlay_draw(rt_canvas3d *c,
                                               const vgfx3d_draw_cmd_t *cmd,
                                               const float *local_bounds_min,
                                               const float *local_bounds_max) {
    deferred_draw_t *dd;

    if (!c || !cmd)
        return 0;
    if (!ensure_deferred_capacity(
            &c->final_overlay_cmds, &c->final_overlay_capacity, c->final_overlay_count + 1))
        return 0;
    dd = &((deferred_draw_t *)c->final_overlay_cmds)[c->final_overlay_count++];
    canvas3d_fill_deferred_draw(c,
                                dd,
                                cmd,
                                DEFERRED_DRAW_MESH,
                                DEFERRED_PASS_MAIN,
                                NULL,
                                0,
                                0,
                                0,
                                0,
                                0.0f,
                                local_bounds_min,
                                local_bounds_max);
    return 1;
}

/// @brief Dispatch a single deferred draw to the backend.
///
/// Routes mesh kind directly to `submit_draw`. Routes instanced kind
/// to `submit_draw_instanced` if the backend supports it, otherwise
/// falls back to `canvas3d_submit_instanced_as_meshes`.
static void canvas3d_submit_deferred(rt_canvas3d *c, const deferred_draw_t *dd) {
    if (!c || !dd)
        return;
    if (dd->kind == DEFERRED_DRAW_INSTANCED) {
        if (c->backend->submit_draw_instanced && dd->instance_count > 0) {
            c->backend->submit_draw_instanced(c->backend_ctx,
                                              c->gfx_win,
                                              &dd->cmd,
                                              dd->instance_matrices,
                                              dd->instance_count,
                                              dd->lights,
                                              dd->light_count,
                                              dd->ambient,
                                              dd->wireframe,
                                              dd->backface_cull);
            return;
        }
        canvas3d_submit_instanced_as_meshes(c, dd, 0);
        return;
    }
    canvas3d_submit_mesh(
        c, &dd->cmd, dd->lights, dd->light_count, dd->ambient, dd->wireframe, dd->backface_cull);
}

/// @brief Submit screen-space overlay geometry without writing depth.
static void canvas3d_submit_screen_overlay_deferred(rt_canvas3d *c, const deferred_draw_t *dd) {
    deferred_draw_t overlay;

    if (!c || !dd)
        return;
    overlay = *dd;
    overlay.cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    canvas3d_submit_deferred(c, &overlay);
}

/// @brief Dispatch a deferred draw to the shadow-pass path.
///
/// Same dispatch shape as `submit_deferred` but routes through the
/// backend's depth-only shadow draw entry. Instanced shadow draws
/// always decompose to per-mesh draws (most backends don't have an
/// instanced shadow path).
static void canvas3d_shadow_deferred(rt_canvas3d *c, const deferred_draw_t *dd) {
    if (!c || !dd || !c->backend || !c->backend->shadow_draw)
        return;
    if (dd->kind == DEFERRED_DRAW_INSTANCED) {
        canvas3d_submit_instanced_as_meshes(c, dd, 1);
        return;
    }
    c->backend->shadow_draw(c->backend_ctx, &dd->cmd);
}

/// @brief In-place AABB union: `[io_min, io_max] ⊇ [mn, mx]`.
static void canvas3d_expand_bounds(float *io_min, float *io_max, const float *mn, const float *mx) {
    if (!io_min || !io_max || !mn || !mx)
        return;
    for (int i = 0; i < 3; i++) {
        if (mn[i] < io_min[i])
            io_min[i] = mn[i];
        if (mx[i] > io_max[i])
            io_max[i] = mx[i];
    }
}

/// @brief Row-major 4×4 matrix multiply: `out = a * b`.
static void canvas3d_mul_mat4(const float *a, const float *b, float *out) {
    if (!a || !b || !out)
        return;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
}

/// @brief Compute world-space AABB for a deferred draw, unioning into `[io_min, io_max]`.
///
/// Iterates per-instance for instanced draws (so the shadow VP fits
/// every instance) or just transforms the single mesh AABB for normal
/// draws. `io_has_bounds` flips to true on first contribution.
static void canvas3d_accumulate_deferred_world_bounds(const deferred_draw_t *dd,
                                                      float *io_min,
                                                      float *io_max,
                                                      int8_t *io_has_bounds) {
    if (!dd || !io_min || !io_max || !io_has_bounds || !dd->has_local_bounds)
        return;

    if (dd->kind == DEFERRED_DRAW_INSTANCED && dd->instance_matrices && dd->instance_count > 0) {
        for (int32_t i = 0; i < dd->instance_count; i++) {
            double world_matrix[16];
            float world_min[3];
            float world_max[3];
            for (int j = 0; j < 16; j++)
                world_matrix[j] = (double)dd->instance_matrices[(size_t)i * 16u + (size_t)j];
            vgfx3d_transform_aabb(
                dd->local_bounds_min, dd->local_bounds_max, world_matrix, world_min, world_max);
            if (!*io_has_bounds) {
                memcpy(io_min, world_min, sizeof(float) * 3);
                memcpy(io_max, world_max, sizeof(float) * 3);
                *io_has_bounds = 1;
            } else {
                canvas3d_expand_bounds(io_min, io_max, world_min, world_max);
            }
        }
        return;
    }

    {
        double world_matrix[16];
        float world_min[3];
        float world_max[3];
        for (int j = 0; j < 16; j++)
            world_matrix[j] = (double)dd->cmd.model_matrix[j];
        vgfx3d_transform_aabb(
            dd->local_bounds_min, dd->local_bounds_max, world_matrix, world_min, world_max);
        if (!*io_has_bounds) {
            memcpy(io_min, world_min, sizeof(float) * 3);
            memcpy(io_max, world_max, sizeof(float) * 3);
            *io_has_bounds = 1;
        } else {
            canvas3d_expand_bounds(io_min, io_max, world_min, world_max);
        }
    }
}

/// @brief Decide whether a queued deferred draw survives frustum culling.
/// @details Accumulates the union of the draw's world-space AABBs (one per
///   instance for instanced batches, otherwise the single transformed bound)
///   and tests it against the view frustum. Draws without usable bounds
///   (e.g. procedural geometry with no declared local AABB) conservatively
///   pass so nothing visible is clipped away.
/// @return 1 if the draw may be visible (keep it), 0 if definitely outside.
static int canvas3d_deferred_intersects_frustum(const deferred_draw_t *dd,
                                                const vgfx3d_frustum_t *frustum) {
    float world_min[3] = {0.0f, 0.0f, 0.0f};
    float world_max[3] = {0.0f, 0.0f, 0.0f};
    int8_t has_bounds = 0;

    if (!dd || !frustum)
        return 1;
    canvas3d_accumulate_deferred_world_bounds(dd, world_min, world_max, &has_bounds);
    if (!has_bounds)
        return 1;
    return vgfx3d_frustum_test_aabb(frustum, world_min, world_max) != 0;
}

/// @brief Build a tight orthographic shadow-map VP that bounds every opaque draw.
///
/// Algorithm:
///   1. Compute world-space AABB of all opaque draws.
///   2. Pick a light-space view position behind the AABB along the
///      light direction, looking at the AABB center.
///   3. Build the view matrix (right-handed, custom up vector).
///   4. Transform the 8 AABB corners into light space to find the
///      tight orthographic bounds.
///   5. Build the orthographic projection from those bounds.
/// Returns 0 if there are no opaque draws (nothing to shadow).
static int canvas3d_build_shadow_world_bounds(const deferred_draw_t *cmds,
                                              int32_t count,
                                              float *world_min,
                                              float *world_max) {
    int8_t has_bounds = 0;
    if (!cmds || count <= 0 || !world_min || !world_max)
        return 0;
    world_min[0] = world_min[1] = world_min[2] = 0.0f;
    world_max[0] = world_max[1] = world_max[2] = 0.0f;
    for (int32_t i = 0; i < count; i++) {
        if (cmds[i].pass_kind != DEFERRED_PASS_MAIN || cmds[i].requires_blend)
            continue;
        canvas3d_accumulate_deferred_world_bounds(&cmds[i], world_min, world_max, &has_bounds);
    }
    return has_bounds ? 1 : 0;
}

static int canvas3d_build_shadow_light_vp(const float *world_min,
                                          const float *world_max,
                                          const vgfx3d_light_params_t *dir_light,
                                          float *out_light_vp) {
    float center[3];
    float ldir[3];
    float eye[3];
    float fwd[3];
    float up[3] = {0.0f, 1.0f, 0.0f};
    float view[16];
    float proj[16];
    float corners[8][3];
    float ls_min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float ls_max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    if (!world_min || !world_max || !dir_light || !out_light_vp)
        return 0;

    center[0] = 0.5f * (world_min[0] + world_max[0]);
    center[1] = 0.5f * (world_min[1] + world_max[1]);
    center[2] = 0.5f * (world_min[2] + world_max[2]);

    ldir[0] = dir_light->direction[0];
    ldir[1] = dir_light->direction[1];
    ldir[2] = dir_light->direction[2];
    {
        float ll = sqrtf(ldir[0] * ldir[0] + ldir[1] * ldir[1] + ldir[2] * ldir[2]);
        if (ll > 1e-7f) {
            ldir[0] /= ll;
            ldir[1] /= ll;
            ldir[2] /= ll;
        } else {
            ldir[0] = 0.0f;
            ldir[1] = -1.0f;
            ldir[2] = 0.0f;
        }
    }

    {
        float dx = world_max[0] - world_min[0];
        float dy = world_max[1] - world_min[1];
        float dz = world_max[2] - world_min[2];
        float radius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
        if (radius < 1.0f)
            radius = 1.0f;
        eye[0] = center[0] - ldir[0] * (radius * 2.0f + 4.0f);
        eye[1] = center[1] - ldir[1] * (radius * 2.0f + 4.0f);
        eye[2] = center[2] - ldir[2] * (radius * 2.0f + 4.0f);
    }

    fwd[0] = center[0] - eye[0];
    fwd[1] = center[1] - eye[1];
    fwd[2] = center[2] - eye[2];
    {
        float fl = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
        float rx;
        float ry;
        float rz;
        float rl;
        float ux;
        float uy;
        float uz;

        if (fl > 1e-7f) {
            fwd[0] /= fl;
            fwd[1] /= fl;
            fwd[2] /= fl;
        } else {
            fwd[0] = 0.0f;
            fwd[1] = 0.0f;
            fwd[2] = -1.0f;
        }
        if (fabsf(fwd[0] * up[0] + fwd[1] * up[1] + fwd[2] * up[2]) > 0.99f) {
            up[0] = 0.0f;
            up[1] = 0.0f;
            up[2] = 1.0f;
        }

        rx = fwd[1] * up[2] - fwd[2] * up[1];
        ry = fwd[2] * up[0] - fwd[0] * up[2];
        rz = fwd[0] * up[1] - fwd[1] * up[0];
        rl = sqrtf(rx * rx + ry * ry + rz * rz);
        if (rl > 1e-7f) {
            rx /= rl;
            ry /= rl;
            rz /= rl;
        } else {
            rx = 1.0f;
            ry = rz = 0.0f;
        }

        ux = ry * fwd[2] - rz * fwd[1];
        uy = rz * fwd[0] - rx * fwd[2];
        uz = rx * fwd[1] - ry * fwd[0];

        view[0] = rx;
        view[1] = ry;
        view[2] = rz;
        view[3] = -(rx * eye[0] + ry * eye[1] + rz * eye[2]);
        view[4] = ux;
        view[5] = uy;
        view[6] = uz;
        view[7] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
        view[8] = fwd[0];
        view[9] = fwd[1];
        view[10] = fwd[2];
        view[11] = -(fwd[0] * eye[0] + fwd[1] * eye[1] + fwd[2] * eye[2]);
        view[12] = 0.0f;
        view[13] = 0.0f;
        view[14] = 0.0f;
        view[15] = 1.0f;
    }

    corners[0][0] = world_min[0];
    corners[0][1] = world_min[1];
    corners[0][2] = world_min[2];
    corners[1][0] = world_max[0];
    corners[1][1] = world_min[1];
    corners[1][2] = world_min[2];
    corners[2][0] = world_min[0];
    corners[2][1] = world_max[1];
    corners[2][2] = world_min[2];
    corners[3][0] = world_max[0];
    corners[3][1] = world_max[1];
    corners[3][2] = world_min[2];
    corners[4][0] = world_min[0];
    corners[4][1] = world_min[1];
    corners[4][2] = world_max[2];
    corners[5][0] = world_max[0];
    corners[5][1] = world_min[1];
    corners[5][2] = world_max[2];
    corners[6][0] = world_min[0];
    corners[6][1] = world_max[1];
    corners[6][2] = world_max[2];
    corners[7][0] = world_max[0];
    corners[7][1] = world_max[1];
    corners[7][2] = world_max[2];

    for (int i = 0; i < 8; i++) {
        float x = corners[i][0];
        float y = corners[i][1];
        float z = corners[i][2];
        float lx = view[0] * x + view[1] * y + view[2] * z + view[3];
        float ly = view[4] * x + view[5] * y + view[6] * z + view[7];
        float lz = view[8] * x + view[9] * y + view[10] * z + view[11];
        if (lx < ls_min[0])
            ls_min[0] = lx;
        if (ly < ls_min[1])
            ls_min[1] = ly;
        if (lz < ls_min[2])
            ls_min[2] = lz;
        if (lx > ls_max[0])
            ls_max[0] = lx;
        if (ly > ls_max[1])
            ls_max[1] = ly;
        if (lz > ls_max[2])
            ls_max[2] = lz;
    }

    {
        float pad_x = (ls_max[0] - ls_min[0]) * 0.05f + 1.0f;
        float pad_y = (ls_max[1] - ls_min[1]) * 0.05f + 1.0f;
        float pad_z = (ls_max[2] - ls_min[2]) * 0.10f + 2.0f;
        float left = ls_min[0] - pad_x;
        float right = ls_max[0] + pad_x;
        float bottom = ls_min[1] - pad_y;
        float top = ls_max[1] + pad_y;
        float near_z = ls_min[2] - pad_z;
        float far_z = ls_max[2] + pad_z;

        if (right - left < 1e-4f || top - bottom < 1e-4f || far_z - near_z < 1e-4f)
            return 0;

        memset(proj, 0, sizeof(proj));
        proj[0] = 2.0f / (right - left);
        proj[3] = -(right + left) / (right - left);
        proj[5] = 2.0f / (top - bottom);
        proj[7] = -(top + bottom) / (top - bottom);
        proj[10] = 2.0f / (far_z - near_z);
        proj[11] = -(far_z + near_z) / (far_z - near_z);
        proj[15] = 1.0f;
    }

    canvas3d_mul_mat4(proj, view, out_light_vp);
    return 1;
}

/// @brief Free every shadow-pair render target and zero the live count.
/// @details Each slot in `shadow_rts` owns its own color and depth buffers
///   (allocated by `canvas3d_ensure_shadow_targets`); this walks the array and
///   releases all three allocations per slot. Safe to call during finalization
///   or after `ensure` detects that the cached resolution no longer matches.
static void canvas3d_release_shadow_targets(rt_canvas3d *c) {
    if (!c)
        return;
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        if (!c->shadow_rts[slot])
            continue;
        free(c->shadow_rts[slot]->color_buf);
        free(c->shadow_rts[slot]->hdr_color_buf);
        free(c->shadow_rts[slot]->depth_buf);
        free(c->shadow_rts[slot]);
        c->shadow_rts[slot] = NULL;
    }
    c->shadow_count = 0;
}

/// @brief Lazily (re)allocate all shadow-pair depth targets at the requested resolution.
/// @details Passing `resolution <= 0` short-circuits to a "do we already have
///   any usable depth buffers?" query without reallocating. Otherwise, if any
///   slot is missing or has a mismatching size, every slot is freed and the
///   full set is reallocated as square `resolution x resolution` targets so
///   the array stays uniform. Color buffers are intentionally null — shadow
///   passes only write depth. OOM at any point during reallocation rolls back
///   to a fully-empty state so partial allocations can't leak into subsequent
///   draw calls.
/// @return 1 on success (targets ready), 0 on allocation failure.
static int canvas3d_ensure_shadow_targets(rt_canvas3d *c, int32_t resolution) {
    size_t depth_bytes;

    if (!c)
        return 0;
    if (resolution <= 0) {
        for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
            vgfx3d_rendertarget_t *rt = c->shadow_rts[slot];

            if (!rt)
                continue;
            if (rt->width > 0 && rt->height > 0 && rt->depth_buf)
                return 1;
        }
        return 0;
    }
    if ((size_t)resolution > SIZE_MAX / (size_t)resolution)
        return 0;
    depth_bytes = (size_t)resolution * (size_t)resolution;
    if (depth_bytes > SIZE_MAX / sizeof(float))
        return 0;
    depth_bytes *= sizeof(float);
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        vgfx3d_rendertarget_t *rt = c->shadow_rts[slot];

        if (!rt || rt->width != resolution || rt->height != resolution) {
            canvas3d_release_shadow_targets(c);
            for (int32_t alloc_slot = 0; alloc_slot < VGFX3D_MAX_SHADOW_LIGHTS; alloc_slot++) {
                vgfx3d_rendertarget_t *new_rt =
                    (vgfx3d_rendertarget_t *)calloc(1, sizeof(vgfx3d_rendertarget_t));
                if (!new_rt) {
                    canvas3d_release_shadow_targets(c);
                    return 0;
                }
                new_rt->width = resolution;
                new_rt->height = resolution;
                new_rt->stride = resolution * 4;
                new_rt->color_buf = NULL;
                new_rt->depth_buf = (float *)malloc(depth_bytes);
                if (!new_rt->depth_buf) {
                    free(new_rt);
                    canvas3d_release_shadow_targets(c);
                    return 0;
                }
                c->shadow_rts[alloc_slot] = new_rt;
            }
            return 1;
        }
    }
    return 1;
}

/// @brief Drop the cached CPU-rasterized skybox so the next frame re-renders it.
/// @details The CPU skybox is an expensive per-pixel raycast through the
///   cubemap; we cache the last result keyed on (resolution, camera VP,
///   cubemap generation). Call this when any of those inputs change in a way
///   `canvas3d_skybox_cache_matches` can't detect (e.g. destroying and
///   recreating the cubemap, or repointing the canvas at a different one).
void rt_canvas3d_invalidate_skybox_cache(rt_canvas3d *c) {
    if (!c)
        return;
    free(c->skybox_cpu_cache);
    c->skybox_cpu_cache = NULL;
    c->skybox_cpu_cache_w = 0;
    c->skybox_cpu_cache_h = 0;
    c->skybox_cpu_cache_generation = 0;
    c->skybox_cpu_cache_is_ortho = 0;
    memset(c->skybox_cpu_cache_vp, 0, sizeof(c->skybox_cpu_cache_vp));
    memset(c->skybox_cpu_cache_cam_pos, 0, sizeof(c->skybox_cpu_cache_cam_pos));
    memset(c->skybox_cpu_cache_forward, 0, sizeof(c->skybox_cpu_cache_forward));
}

/// @brief True if two float arrays match element-wise within @p eps; any non-finite
///   element or NULL/negative input fails the comparison.
static int canvas3d_float_array_close(const float *a, const float *b, int32_t count, float eps) {
    if (!a || !b || count < 0)
        return 0;
    for (int32_t i = 0; i < count; i++) {
        if (!isfinite(a[i]) || !isfinite(b[i]))
            return 0;
        if (fabsf(a[i] - b[i]) > eps)
            return 0;
    }
    return 1;
}

/// @brief CPU-rasterize the bound skybox cubemap into a destination pixel buffer.
/// @details For perspective cameras, unprojects each destination pixel from NDC
///   back to a world-space direction by multiplying through `inverse(VP)`, then
///   samples the cubemap along that ray. Orthographic cameras collapse to a
///   single-color fill along the camera forward direction because an ortho
///   projection has no per-pixel ray divergence — all pixels see the same
///   skybox direction. This is the slow fallback path used when the GPU
///   backend can't render the skybox directly (e.g. the software renderer).
/// @return 1 on success, 0 when the VP matrix is non-invertible or inputs
///   are otherwise malformed.
static int canvas3d_render_skybox_cpu(
    rt_canvas3d *c, uint8_t *dst_pixels, int32_t dst_w, int32_t dst_h, int32_t dst_stride) {
    if (!c || !c->skybox || !dst_pixels || !canvas3d_rgba8_stride_valid(dst_w, dst_h, dst_stride))
        return 0;

    if (c->cached_cam_is_ortho) {
        float r;
        float g;
        float b;

        rt_cubemap_sample(c->skybox,
                          c->cached_cam_forward[0],
                          c->cached_cam_forward[1],
                          c->cached_cam_forward[2],
                          &r,
                          &g,
                          &b);
        uint8_t r8 = canvas3d_clamp01_to_u8(r);
        uint8_t g8 = canvas3d_clamp01_to_u8(g);
        uint8_t b8 = canvas3d_clamp01_to_u8(b);
        size_t row_bytes = (size_t)dst_w * 4u;
        uint8_t *first_row = dst_pixels;
        for (int32_t x = 0; x < dst_w; x++) {
            uint8_t *dst = &first_row[(size_t)x * 4u];
            dst[0] = r8;
            dst[1] = g8;
            dst[2] = b8;
            dst[3] = 0xFF;
        }
        for (int32_t y = 1; y < dst_h; y++) {
            uint8_t *dst = &dst_pixels[(size_t)y * (size_t)dst_stride];
            memcpy(dst, first_row, row_bytes);
        }
        return 1;
    }

    float inv_vp[16];
    if (vgfx3d_invert_matrix4(c->cached_vp, inv_vp) != 0)
        return 0;

    float inv_w = 1.0f / (float)dst_w;
    float inv_h = 1.0f / (float)dst_h;
    for (int32_t y = 0; y < dst_h; y++) {
        float ndc_y = 1.0f - 2.0f * ((float)y + 0.5f) * inv_h;
        uint8_t *row = &dst_pixels[(size_t)y * (size_t)dst_stride];
        for (int32_t x = 0; x < dst_w; x++) {
            float ndc_x = 2.0f * ((float)x + 0.5f) * inv_w - 1.0f;
            float clip[4] = {ndc_x, ndc_y, 1.0f, 1.0f};
            float world[4];
            float dx;
            float dy;
            float dz;
            float dl;
            float r;
            float g;
            float b;
            uint8_t *dst;

            world[0] = inv_vp[0] * clip[0] + inv_vp[1] * clip[1] + inv_vp[2] * clip[2] +
                       inv_vp[3] * clip[3];
            world[1] = inv_vp[4] * clip[0] + inv_vp[5] * clip[1] + inv_vp[6] * clip[2] +
                       inv_vp[7] * clip[3];
            world[2] = inv_vp[8] * clip[0] + inv_vp[9] * clip[1] + inv_vp[10] * clip[2] +
                       inv_vp[11] * clip[3];
            world[3] = inv_vp[12] * clip[0] + inv_vp[13] * clip[1] + inv_vp[14] * clip[2] +
                       inv_vp[15] * clip[3];
            if (fabsf(world[3]) > 1e-7f) {
                world[0] /= world[3];
                world[1] /= world[3];
                world[2] /= world[3];
            }
            dx = world[0] - c->cached_cam_pos[0];
            dy = world[1] - c->cached_cam_pos[1];
            dz = world[2] - c->cached_cam_pos[2];
            dl = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dl > 1e-7f) {
                dx /= dl;
                dy /= dl;
                dz /= dl;
            }
            rt_cubemap_sample(c->skybox, dx, dy, dz, &r, &g, &b);
            dst = &row[(size_t)x * 4u];
            dst[0] = canvas3d_clamp01_to_u8(r);
            dst[1] = canvas3d_clamp01_to_u8(g);
            dst[2] = canvas3d_clamp01_to_u8(b);
            dst[3] = 0xFF;
        }
    }
    return 1;
}

/// @brief Check whether the cached CPU skybox can satisfy the current frame.
/// @details Composite key: viewport (w, h), cubemap content generation
///   (bumped whenever a face is uploaded), projection mode (ortho vs
///   perspective), and then either the camera forward vector (ortho) or the
///   full VP matrix + camera position (perspective). The split on projection
///   mode exists because ortho cameras fill the entire target with one
///   sampled direction, so only the forward vector needs to match — the VP
///   matrix can drift without affecting the rendered skybox.
/// @return 1 if the cache is valid for this frame, 0 if it must be re-rendered.
static int canvas3d_skybox_cache_matches(const rt_canvas3d *c,
                                         int32_t w,
                                         int32_t h,
                                         uint64_t generation) {
    if (!c || !c->skybox_cpu_cache || w <= 0 || h <= 0)
        return 0;
    if (c->skybox_cpu_cache_w != w || c->skybox_cpu_cache_h != h ||
        c->skybox_cpu_cache_generation != generation ||
        c->skybox_cpu_cache_is_ortho != c->cached_cam_is_ortho)
        return 0;
    if (c->cached_cam_is_ortho)
        return canvas3d_float_array_close(
            c->skybox_cpu_cache_forward, c->cached_cam_forward, 3, 1e-6f);
    return canvas3d_float_array_close(c->skybox_cpu_cache_vp, c->cached_vp, 16, 1e-6f) &&
           canvas3d_float_array_close(c->skybox_cpu_cache_cam_pos, c->cached_cam_pos, 3, 1e-6f);
}

/// @brief Populate (or refresh) the CPU skybox cache so it's ready for blitting.
/// @details If the cache already matches the current viewport/camera/generation
///   we return immediately (cheap fast path). Otherwise we (re)allocate the
///   backing buffer only when its pixel dimensions actually change, then call
///   `canvas3d_render_skybox_cpu` to paint the fresh image and snapshot the
///   cache key. OOM on reallocation invalidates the cache rather than leaving
///   it in a half-valid state. Also guards against `w * h * 4` overflow on
///   32-bit size_t targets.
/// @return 1 when a usable cache is ready, 0 on failure (caller should skip
///   the skybox for this frame rather than render garbage).
static int canvas3d_ensure_skybox_cpu_cache(rt_canvas3d *c, int32_t w, int32_t h) {
    uint64_t generation;
    size_t bytes;

    if (!c || !c->skybox || w <= 0 || h <= 0 || (int64_t)w > INT32_MAX / 4)
        return 0;
    generation = vgfx3d_get_cubemap_generation(c->skybox);
    if (canvas3d_skybox_cache_matches(c, w, h, generation))
        return 1;
    bytes = (size_t)w * (size_t)h * 4u;
    if (bytes / 4u / (size_t)w != (size_t)h)
        return 0;
    if (c->skybox_cpu_cache_w != w || c->skybox_cpu_cache_h != h || !c->skybox_cpu_cache) {
        uint8_t *new_cache = (uint8_t *)realloc(c->skybox_cpu_cache, bytes);
        if (!new_cache) {
            rt_canvas3d_invalidate_skybox_cache(c);
            return 0;
        }
        c->skybox_cpu_cache = new_cache;
    }
    c->skybox_cpu_cache_w = w;
    c->skybox_cpu_cache_h = h;
    if (!canvas3d_render_skybox_cpu(c, c->skybox_cpu_cache, w, h, (int32_t)((int64_t)w * 4))) {
        rt_canvas3d_invalidate_skybox_cache(c);
        return 0;
    }
    c->skybox_cpu_cache_generation = generation;
    c->skybox_cpu_cache_is_ortho = c->cached_cam_is_ortho;
    memcpy(c->skybox_cpu_cache_vp, c->cached_vp, sizeof(c->skybox_cpu_cache_vp));
    memcpy(c->skybox_cpu_cache_cam_pos, c->cached_cam_pos, sizeof(c->skybox_cpu_cache_cam_pos));
    memcpy(c->skybox_cpu_cache_forward, c->cached_cam_forward, sizeof(c->skybox_cpu_cache_forward));
    return 1;
}

/// @brief Copy the cached CPU skybox into the frame's destination buffer row-by-row.
/// @details Performs no rendering — it assumes `canvas3d_ensure_skybox_cpu_cache`
///   has already refreshed the cache for this frame. Silently no-ops when the
///   cache size doesn't match the destination, which is by design: the caller
///   uses it as a "try the fast path" hook before falling back to a full
///   CPU render.
static void canvas3d_blit_skybox_cpu_cache(
    rt_canvas3d *c, uint8_t *dst_pixels, int32_t dst_w, int32_t dst_h, int32_t dst_stride) {
    int32_t src_stride;

    if (!c || !c->skybox_cpu_cache || !dst_pixels || dst_w <= 0 || dst_h <= 0 ||
        c->skybox_cpu_cache_w != dst_w || c->skybox_cpu_cache_h != dst_h ||
        !canvas3d_rgba8_stride_valid(dst_w, dst_h, dst_stride))
        return;
    src_stride = (int32_t)((int64_t)dst_w * 4);
    for (int32_t y = 0; y < dst_h; y++)
        memcpy(
            &dst_pixels[y * dst_stride], &c->skybox_cpu_cache[y * src_stride], (size_t)src_stride);
}

/*==========================================================================
 * Canvas3D lifecycle
 *=========================================================================*/

/// @brief GC finalizer — release the backend context and every owned scratch buffer.
///
/// Walks the deferred-command, temp-buffer, temp-object, motion
/// history, and text-vertex arrays, freeing each. Backend contexts
/// (D3D11/OpenGL/Software) destroy themselves through their
/// virtual `destroy_ctx`. Idempotent: nulled pointers prevent
/// double-free if the GC sweeps the canvas twice during shutdown.
static void rt_canvas3d_finalize(void *obj) {
    rt_canvas3d *c = (rt_canvas3d *)obj;
    canvas3d_release_synthetic_input(c);
    if (c->backend && c->backend_ctx && c->render_target && c->backend->set_render_target)
        c->backend->set_render_target(c->backend_ctx, NULL);
    /* Destroy the backend context */
    if (c->backend && c->backend_ctx) {
        c->backend->destroy_ctx(c->backend_ctx);
        c->backend_ctx = NULL;
    }
    /* Free deferred draw command buffer */
    free(c->draw_cmds);
    c->draw_cmds = NULL;
    c->draw_count = c->draw_capacity = 0;
    free(c->trans_cmds);
    c->trans_cmds = NULL;
    c->trans_capacity = 0;
    canvas3d_clear_final_overlay(c);
    free(c->final_overlay_cmds);
    c->final_overlay_cmds = NULL;
    c->final_overlay_capacity = 0;
    free(c->final_overlay_temp_buffers);
    c->final_overlay_temp_buffers = NULL;
    c->final_overlay_temp_buf_capacity = 0;
    free(c->motion_history);
    c->motion_history = NULL;
    c->motion_history_count = c->motion_history_capacity = 0;
    free(c->motion_history_hash);
    c->motion_history_hash = NULL;
    c->motion_history_hash_capacity = 0;
    /* Free any leftover temp buffers (e.g., from skinned draws) */
    canvas3d_clear_temp_buffers(c);
    free(c->temp_buffers);
    c->temp_buffers = NULL;
    c->temp_buf_count = c->temp_buf_capacity = 0;
    free(c->mesh_snapshots);
    c->mesh_snapshots = NULL;
    c->mesh_snapshot_count = c->mesh_snapshot_capacity = 0;
    canvas3d_clear_temp_objects(c);
    free(c->temp_objects);
    c->temp_objects = NULL;
    c->temp_obj_count = c->temp_obj_capacity = 0;
    free(c->temp_object_set);
    c->temp_object_set = NULL;
    c->temp_object_set_capacity = 0;
    free(c->text_vertices);
    c->text_vertices = NULL;
    c->text_vertex_capacity = 0;
    free(c->text_indices);
    c->text_indices = NULL;
    c->text_index_capacity = 0;

    /* Free shadow render targets if allocated */
    canvas3d_release_shadow_targets(c);

    if (c->skybox) {
        if (rt_obj_release_check0(c->skybox))
            rt_obj_free(c->skybox);
        c->skybox = NULL;
    }
    rt_canvas3d_invalidate_skybox_cache(c);

    vgfx3d_postfx_chain_free(&c->frame_postfx_chain);
    canvas3d_release_owned_ref(&c->postfx);
    canvas3d_release_owned_ref((void **)&c->render_target_owner);
    c->render_target = NULL;
    for (int32_t i = 0; i < VGFX3D_MAX_LIGHTS; i++)
        canvas3d_release_owned_ref((void **)&c->lights[i]);

    if (c->gfx_win) {
        rt_canvas3d_detach_input(c->gfx_win);
        vgfx_destroy_window(c->gfx_win);
        c->gfx_win = NULL;
    }
}

/// @brief Tear down the canvas's platform window and flag it closed.
/// @details Detaches input, destroys the backing window, and sets `should_close`
///          so the next poll reports the canvas as closed. Safe on a NULL canvas.
static void canvas3d_close_window(rt_canvas3d *c) {
    if (!c)
        return;
    if (c->gfx_win) {
        rt_canvas3d_detach_input(c->gfx_win);
        vgfx_destroy_window(c->gfx_win);
        c->gfx_win = NULL;
    }
    c->should_close = 1;
}

/// @brief Create a new 3D rendering canvas (window + backend context).
/// @details Opens a platform window, selects the platform-default rendering backend
///          with software fallback if initialization fails, and initializes the framebuffer,
///          depth buffer, deferred draw queue, and motion blur history. The canvas
///          is the main entry point for 3D rendering — call Begin/DrawMesh/End/Flip
///          each frame. GC finalizer destroys the backend context and window.
/// @param title Window title (runtime string).
/// @param w     Window width in pixels (1–8192).
/// @param h     Window height in pixels (1–8192).
/// @return Opaque canvas handle, or NULL on failure.
void *rt_canvas3d_new(rt_string title, int64_t w, int64_t h) {
    vgfx_framebuffer_t fb;

    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        rt_trap("Canvas3D.New: dimensions must be 1-8192");
        return NULL;
    }
    int32_t initial_width = (int32_t)w;
    int32_t initial_height = (int32_t)h;
    int32_t initial_framebuffer_width = (int32_t)w;
    int32_t initial_framebuffer_height = (int32_t)h;

    rt_canvas3d *c =
        (rt_canvas3d *)rt_obj_new_i64(RT_G3D_CANVAS3D_CLASS_ID, (int64_t)sizeof(rt_canvas3d));
    if (!c) {
        rt_trap("Canvas3D.New: memory allocation failed");
        return NULL;
    }
    memset(c, 0, sizeof(rt_canvas3d));
    rt_obj_set_finalizer(c, rt_canvas3d_finalize);

    /* Create window */
    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = (int32_t)w;
    params.height = (int32_t)h;
    if (title)
        params.title = rt_string_cstr(title);

    c->gfx_win = vgfx_create_window(&params);
    if (!c->gfx_win) {
        if (rt_obj_release_check0(c))
            rt_obj_free(c);
        rt_trap("Canvas3D.New: failed to create window (display server unavailable?)");
        return NULL;
    }

    vgfx_set_coord_scale(c->gfx_win, vgfx_window_get_scale(c->gfx_win));
    if (vgfx_get_framebuffer(c->gfx_win, &fb) && fb.width > 0 && fb.height > 0) {
        initial_framebuffer_width = fb.width;
        initial_framebuffer_height = fb.height;
        if (!vgfx_get_size(c->gfx_win, &initial_width, &initial_height)) {
            initial_width = canvas3d_unscale_physical_size(c, fb.width);
            initial_height = canvas3d_unscale_physical_size(c, fb.height);
        }
    }

    c->width = initial_width;
    c->height = initial_height;
    c->framebuffer_width = initial_framebuffer_width;
    c->framebuffer_height = initial_framebuffer_height;

    /* Select and initialize the platform-default backend, with software fallback. */
    c->backend = vgfx3d_select_backend();
    if (!c->backend || !c->backend->create_ctx)
        c->backend = &vgfx3d_software_backend;
    if (!c->backend || !c->backend->create_ctx) {
        if (rt_obj_release_check0(c))
            rt_obj_free(c);
        rt_trap("Canvas3D.New: no 3D backend is available");
        return NULL;
    }
    c->backend_ctx =
        c->backend->create_ctx(c->gfx_win, initial_framebuffer_width, initial_framebuffer_height);
    if (!c->backend_ctx) {
        /* Selected backend failed — fall back to software. */
        c->backend = &vgfx3d_software_backend;
        c->backend_ctx = c->backend->create_ctx(
            c->gfx_win, initial_framebuffer_width, initial_framebuffer_height);
        if (!c->backend_ctx) {
            if (rt_obj_release_check0(c))
                rt_obj_free(c);
            rt_trap("Canvas3D.New: backend initialization failed");
            return NULL;
        }
    }
    vgfx_set_gpu_present(c->gfx_win, c->backend != &vgfx3d_software_backend);

    vgfx_set_resize_callback(c->gfx_win, rt_canvas3d_on_resize, c);

    c->ambient[0] = 0.1f;
    c->ambient[1] = 0.1f;
    c->ambient[2] = 0.1f;
    c->backface_cull = 1;
    c->render_target = NULL;
    c->render_target_owner = NULL;
    c->postfx = NULL;
    c->temp_buffers = NULL;
    c->temp_buf_count = c->temp_buf_capacity = 0;
    c->mesh_snapshots = NULL;
    c->mesh_snapshot_count = c->mesh_snapshot_capacity = 0;
    c->temp_objects = NULL;
    c->temp_obj_count = c->temp_obj_capacity = 0;
    c->fog_enabled = 0;
    c->fog_near = 10.0f;
    c->fog_far = 50.0f;
    c->fog_color[0] = c->fog_color[1] = c->fog_color[2] = 0.5f;
    c->shadows_enabled = 0;
    c->shadow_resolution = 1024;
    c->shadow_bias = 0.005f;
    c->shadow_count = 0;
    memset(c->shadow_rts, 0, sizeof(c->shadow_rts));
    memset(c->shadow_light_vps, 0, sizeof(c->shadow_light_vps));
    c->frame_serial = 0;
    c->motion_history = NULL;
    c->motion_history_count = 0;
    c->motion_history_capacity = 0;
    c->frame_is_2d = 0;
    c->has_last_scene_vp = 0;
    c->dt_max_ms = 100;
    c->quality_requested = RT_GRAPHICS3D_QUALITY_BALANCED;
    c->quality_active = RT_GRAPHICS3D_QUALITY_BALANCED;
    c->quality_fallback = 0;
    c->quality_fallback_reason = 0;
    c->input_source = 0;
    c->clock_source = 0;
    c->synthetic_dt_us = CANVAS3D_SYNTHETIC_DT_DEFAULT_US;

    rt_keyboard_set_canvas(c->gfx_win);
    rt_mouse_set_canvas(c->gfx_win);
    rt_pad_init();

    return c;
}

/*==========================================================================
 * Rendering — dispatches through backend vtable
 *=========================================================================*/

/// @brief Clear the framebuffer and depth buffer with the given background color.
/// @details Must be called at the start of each frame before Begin. Fog and
///          ambient light persist until explicitly changed.
void rt_canvas3d_clear(void *obj, double r, double g, double b) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (!c->gfx_win || !c->backend)
        return;
    if (c->frame_finalized) {
        canvas3d_clear_final_overlay(c);
        c->frame_finalized = 0;
        c->frame_presented_by_finalize = 0;
    }
    float cr = canvas3d_clamp01_f64(r);
    float cg = canvas3d_clamp01_f64(g);
    float cb = canvas3d_clamp01_f64(b);
    c->backend->clear(c->backend_ctx, c->gfx_win, cr, cg, cb);

    /* Also clear the software framebuffer so 2D overlay functions
     * (DrawText2D, DrawRect2D, DrawCrosshair, Screenshot) have correct
     * background content regardless of active backend. Uses memset for
     * stride-aligned rows instead of per-pixel loop (4x faster at 1080p). */
    if (c->backend != &vgfx3d_software_backend && !c->render_target) {
        vgfx_framebuffer_t fb;
        if (vgfx_get_framebuffer(c->gfx_win, &fb) && fb.pixels && fb.width > 0 && fb.height > 0 &&
            fb.stride >= fb.width * (int32_t)sizeof(uint32_t)) {
            uint8_t r8 = canvas3d_clamp01_to_u8(cr);
            uint8_t g8 = canvas3d_clamp01_to_u8(cg);
            uint8_t b8 = canvas3d_clamp01_to_u8(cb);
            size_t row_bytes = (size_t)fb.width * 4u;
            uint8_t *first_row = fb.pixels;
            for (int32_t x = 0; x < fb.width; x++) {
                uint8_t *px = &first_row[(size_t)x * 4u];
                px[0] = r8;
                px[1] = g8;
                px[2] = b8;
                px[3] = 0xFF;
            }
            for (int32_t y = 1; y < fb.height; y++) {
                uint8_t *row = &fb.pixels[(size_t)y * (size_t)fb.stride];
                memcpy(row, first_row, row_bytes);
            }
        }
    }
}

/// @brief Decide which deferred pass owns 2D screen-space draws based on canvas mode.
static deferred_pass_t canvas3d_screen_pass_kind(const rt_canvas3d *c) {
    return (c && c->frame_is_2d) ? DEFERRED_PASS_MAIN : DEFERRED_PASS_SCREEN_OVERLAY;
}

/// @brief Append a 2D triangle list (positions + UVs + color) to the deferred queue.
///
/// Used as the underlying primitive for `canvas3d_queue_screen_rect`
/// and `canvas3d_queue_screen_line`. Manages the auto-grow of the
/// command buffer and decides whether the geometry lands in the
/// pre-3D, post-3D, or HUD pass via `canvas3d_screen_pass_kind`.
static int canvas3d_queue_screen_geometry(rt_canvas3d *c,
                                          const vgfx3d_vertex_t *vertices,
                                          int32_t vertex_count,
                                          const uint32_t *indices,
                                          int32_t index_count,
                                          float r,
                                          float g,
                                          float b,
                                          float a) {
    size_t vertex_bytes;
    size_t index_bytes;
    uint8_t *block;
    vgfx3d_vertex_t *verts_copy;
    uint32_t *indices_copy;
    vgfx3d_draw_cmd_t cmd;

    if (!c || !c->in_frame || !vertices || vertex_count <= 0 || !indices || index_count <= 0)
        return 0;
    if ((size_t)vertex_count > SIZE_MAX / sizeof(vgfx3d_vertex_t) ||
        (size_t)index_count > SIZE_MAX / sizeof(uint32_t))
        return 0;
    vertex_bytes = (size_t)vertex_count * sizeof(vgfx3d_vertex_t);
    index_bytes = (size_t)index_count * sizeof(uint32_t);
    if (vertex_bytes > SIZE_MAX - index_bytes)
        return 0;
    block = (uint8_t *)malloc(vertex_bytes + index_bytes);
    if (!block)
        return 0;
    verts_copy = (vgfx3d_vertex_t *)block;
    indices_copy = (uint32_t *)(block + vertex_bytes);
    memcpy(verts_copy, vertices, vertex_bytes);
    memcpy(indices_copy, indices, index_bytes);

    memset(&cmd, 0, sizeof(cmd));
    cmd.vertices = verts_copy;
    cmd.vertex_count = (uint32_t)vertex_count;
    cmd.indices = indices_copy;
    cmd.index_count = (uint32_t)index_count;
    cmd.model_matrix[0] = cmd.model_matrix[5] = cmd.model_matrix[10] = cmd.model_matrix[15] = 1.0f;
    cmd.diffuse_color[0] = r;
    cmd.diffuse_color[1] = g;
    cmd.diffuse_color[2] = b;
    cmd.diffuse_color[3] = a;
    cmd.alpha = a;
    cmd.unlit = 1;
    cmd.disable_depth_test = 1;

    if (c->final_overlay_recording) {
        if (!canvas3d_track_final_overlay_temp_buffer(c, block)) {
            free(block);
            return 0;
        }
        if (!canvas3d_enqueue_final_overlay_draw(c, &cmd, NULL, NULL)) {
            canvas3d_release_tracked_final_overlay_temp_buffer(c, block);
            return 0;
        }
        return 1;
    }

    if (!canvas3d_track_temp_buffer(c, block)) {
        free(block);
        return 0;
    }

    if (!canvas3d_enqueue_draw(c,
                               &cmd,
                               DEFERRED_DRAW_MESH,
                               canvas3d_screen_pass_kind(c),
                               NULL,
                               0,
                               0,
                               0,
                               0,
                               0.0f,
                               NULL,
                               NULL)) {
        canvas3d_release_tracked_temp_buffer(c, block);
        return 0;
    }
    return 1;
}

/// @brief Convenience wrapper: queue a screen-space rectangle as two triangles.
int canvas3d_queue_screen_rect(
    rt_canvas3d *c, float x, float y, float w, float h, float r, float g, float b, float a) {
    vgfx3d_vertex_t verts[4];
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};

    memset(verts, 0, sizeof(verts));
    verts[0].pos[0] = x;
    verts[0].pos[1] = y;
    verts[1].pos[0] = x + w;
    verts[1].pos[1] = y;
    verts[2].pos[0] = x + w;
    verts[2].pos[1] = y + h;
    verts[3].pos[0] = x;
    verts[3].pos[1] = y + h;
    for (int i = 0; i < 4; i++) {
        verts[i].normal[2] = 1.0f;
        verts[i].color[0] = r;
        verts[i].color[1] = g;
        verts[i].color[2] = b;
        verts[i].color[3] = a;
    }
    return canvas3d_queue_screen_geometry(c, verts, 4, indices, 6, r, g, b, a);
}

/// @brief Queue a screen-space line as a thin quad (tessellated triangles).
///
/// Width is in screen pixels. The endpoints define the centerline;
/// the quad is built by extruding by `width/2` perpendicular to
/// the segment direction. Properly aligned for sub-pixel positions.
int canvas3d_queue_screen_line(rt_canvas3d *c,
                               float x0,
                               float y0,
                               float x1,
                               float y1,
                               float thickness,
                               float r,
                               float g,
                               float b,
                               float a) {
    float dx;
    float dy;
    float len;
    float px;
    float py;
    float half;
    vgfx3d_vertex_t verts[4];
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};

    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1))
        return 0;
    if (!isfinite(thickness) || thickness <= 0.0f)
        thickness = 1.0f;
    if (thickness > 4096.0f)
        thickness = 4096.0f;

    dx = x1 - x0;
    dy = y1 - y0;
    len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f)
        return canvas3d_queue_screen_rect(
            c, x0 - thickness * 0.5f, y0 - thickness * 0.5f, thickness, thickness, r, g, b, a);
    px = -dy / len;
    py = dx / len;
    half = thickness * 0.5f;
    memset(verts, 0, sizeof(verts));
    verts[0].pos[0] = x0 - px * half;
    verts[0].pos[1] = y0 - py * half;
    verts[1].pos[0] = x0 + px * half;
    verts[1].pos[1] = y0 + py * half;
    verts[2].pos[0] = x1 + px * half;
    verts[2].pos[1] = y1 + py * half;
    verts[3].pos[0] = x1 - px * half;
    verts[3].pos[1] = y1 - py * half;
    for (int i = 0; i < 4; i++) {
        verts[i].normal[2] = 1.0f;
        verts[i].color[0] = r;
        verts[i].color[1] = g;
        verts[i].color[2] = b;
        verts[i].color[3] = a;
    }
    return canvas3d_queue_screen_geometry(c, verts, 4, indices, 6, r, g, b, a);
}

/// @brief Switch the canvas into 2D-overlay mode for the next batch of draw calls.
///
/// All subsequent screen-space draws (rects, lines, sprites, text)
/// queue into the post-3D HUD pass, which renders after the 3D
/// scene, postFX, and tonemapping. Pair with `rt_canvas3d_end`
/// to finish the overlay pass.
void rt_canvas3d_begin_2d(void *obj) {
    vgfx3d_camera_params_t params;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (!c->backend)
        return;
    if (c->in_frame) {
        rt_trap("Canvas3D.Begin2D: Begin/End must not nest");
        return;
    }
    if (c->frame_finalized) {
        canvas3d_clear_final_overlay(c);
        c->frame_finalized = 0;
        c->frame_presented_by_finalize = 0;
    }
    if (c->backend->show_gpu_layer)
        c->backend->show_gpu_layer(c->backend_ctx);

    canvas3d_build_ortho_camera(c, &params);

    c->cached_cam_pos[0] = 0.0f;
    c->cached_cam_pos[1] = 0.0f;
    c->cached_cam_pos[2] = 1.0f;
    c->cached_cam_forward[0] = params.forward[0];
    c->cached_cam_forward[1] = params.forward[1];
    c->cached_cam_forward[2] = params.forward[2];
    c->cached_cam_is_ortho = params.is_ortho;
    c->frame_serial++;
    canvas3d_prune_motion_history(c);
    c->draw_count = 0;
    c->frame_is_2d = 1;
    // Cache the full VP product so cached_vp has consistent semantics between
    // Begin2D and Begin3D. Previously 2D mode stored only the projection —
    // overlay code that unprojected via cached_vp got inconsistent results
    // across modes. For an identity view this still equals projection, but
    // the shape of the math is now the same as Begin3D. (params.view and
    // params.projection are already float[16], so no conversion needed.)
    {
        const float *vf = params.view;
        const float *pf = params.projection;
        for (int r = 0; r < 4; r++)
            for (int col = 0; col < 4; col++)
                c->cached_vp[r * 4 + col] =
                    pf[r * 4 + 0] * vf[0 * 4 + col] + pf[r * 4 + 1] * vf[1 * 4 + col] +
                    pf[r * 4 + 2] * vf[2 * 4 + col] + pf[r * 4 + 3] * vf[3 * 4 + col];
    }

    canvas3d_latch_gpu_postfx_state(c);
    c->backend->begin_frame(c->backend_ctx, &params);
    c->in_frame = 1;
}

/// @brief Begin recording a final overlay pass that is composited after post-FX.
void rt_canvas3d_begin_overlay(void *obj) {
    vgfx3d_camera_params_t params;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (!c->backend)
        return;
    if (c->frame_finalized) {
        rt_trap("Canvas3D.BeginOverlay: frame is already finalized");
        return;
    }
    if (c->in_frame) {
        rt_trap("Canvas3D.BeginOverlay: Begin/End must not nest");
        return;
    }

    canvas3d_build_ortho_camera(c, &params);
    c->cached_cam_pos[0] = 0.0f;
    c->cached_cam_pos[1] = 0.0f;
    c->cached_cam_pos[2] = 1.0f;
    c->cached_cam_forward[0] = params.forward[0];
    c->cached_cam_forward[1] = params.forward[1];
    c->cached_cam_forward[2] = params.forward[2];
    c->cached_cam_is_ortho = params.is_ortho;
    c->frame_is_2d = 1;
    for (int r = 0; r < 4; r++)
        for (int col = 0; col < 4; col++)
            c->cached_vp[r * 4 + col] = params.projection[r * 4 + 0] * params.view[0 * 4 + col] +
                                        params.projection[r * 4 + 1] * params.view[1 * 4 + col] +
                                        params.projection[r * 4 + 2] * params.view[2 * 4 + col] +
                                        params.projection[r * 4 + 3] * params.view[3 * 4 + col];
    c->final_overlay_recording = 1;
    c->in_frame = 1;
}

/// @brief Finish recording a final overlay pass.
void rt_canvas3d_end_overlay(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (!c->final_overlay_recording)
        return;
    c->final_overlay_recording = 0;
    c->in_frame = 0;
    c->frame_is_2d = 0;
}

/// @brief Discard recorded final-overlay commands for the current frame.
void rt_canvas3d_clear_overlay(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    canvas3d_clear_final_overlay(c);
}

/// @brief Draw a filled rectangle through the 3D pipeline (screen-space coords).
/// Must be called between Begin2D/End or Begin/End.
void rt_canvas3d_draw_rect_3d(
    void *obj, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    float r;
    float g;
    float b;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (w <= 0 || h <= 0)
        return;
    if (!c->in_frame || !c->backend)
        return;
    r = (float)((color >> 16) & 0xFF) / 255.0f;
    g = (float)((color >> 8) & 0xFF) / 255.0f;
    b = (float)(color & 0xFF) / 255.0f;
    (void)canvas3d_queue_screen_rect(c, (float)x, (float)y, (float)w, (float)h, r, g, b, 1.0f);
}

/// @brief Draw text through the 3D pipeline using the 5×7 bitmap font.
/// Each character's "on" pixels are rendered as 2×2 quads batched into one mesh.
void rt_canvas3d_draw_text_3d(void *obj, int64_t x, int64_t y, rt_string text, int64_t color) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c || !text)
        return;
    if (!c->in_frame || !c->backend)
        return;

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    float r = (float)((color >> 16) & 0xFF) / 255.0f;
    float g = (float)((color >> 8) & 0xFF) / 255.0f;
    float b = (float)(color & 0xFF) / 255.0f;

    /* Reference the font data from draw_text2d (defined later in this file).
     * We duplicate the font table reference here for self-containment. */
    static const uint8_t font5x7[95][7] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00},
        {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x00, 0x00},
        {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, {0x19, 0x1A, 0x04, 0x0B, 0x13, 0x00, 0x00},
        {0x08, 0x14, 0x08, 0x15, 0x12, 0x0D, 0x00}, {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x02, 0x04, 0x04, 0x04, 0x04, 0x02, 0x00}, {0x08, 0x04, 0x04, 0x04, 0x04, 0x08, 0x00},
        {0x04, 0x15, 0x0E, 0x15, 0x04, 0x00, 0x00}, {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08}, {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00}, {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00},
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x0E, 0x00}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00},
        {0x0E, 0x11, 0x01, 0x06, 0x08, 0x1F, 0x00}, {0x0E, 0x11, 0x02, 0x01, 0x11, 0x0E, 0x00},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x00}, {0x1F, 0x10, 0x1E, 0x01, 0x11, 0x0E, 0x00},
        {0x06, 0x08, 0x1E, 0x11, 0x11, 0x0E, 0x00}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x00},
        {0x0E, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00}, {0x0E, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00},
        {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00}, {0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x08},
        {0x02, 0x04, 0x08, 0x04, 0x02, 0x00, 0x00}, {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00},
        {0x08, 0x04, 0x02, 0x04, 0x08, 0x00, 0x00}, {0x0E, 0x11, 0x02, 0x04, 0x00, 0x04, 0x00},
        {0x0E, 0x11, 0x17, 0x17, 0x16, 0x10, 0x0E}, {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00},
        {0x1E, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00}, {0x0E, 0x11, 0x10, 0x10, 0x11, 0x0E, 0x00},
        {0x1E, 0x11, 0x11, 0x11, 0x11, 0x1E, 0x00}, {0x1F, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00},
        {0x1F, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00}, {0x0E, 0x11, 0x10, 0x13, 0x11, 0x0E, 0x00},
        {0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00}, {0x0E, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00},
        {0x01, 0x01, 0x01, 0x01, 0x11, 0x0E, 0x00}, {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00}, {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x00},
        {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00}, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00},
        {0x1E, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00}, {0x0E, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00},
        {0x1E, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x00}, {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x1E, 0x00},
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00},
        {0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04, 0x00}, {0x11, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00},
        {0x11, 0x0A, 0x04, 0x04, 0x0A, 0x11, 0x00}, {0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00},
        {0x1F, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00}, {0x0E, 0x08, 0x08, 0x08, 0x08, 0x0E, 0x00},
        {0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00}, {0x0E, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x00},
        {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00},
        {0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x00},
        {0x10, 0x10, 0x1E, 0x11, 0x11, 0x1E, 0x00}, {0x00, 0x0E, 0x11, 0x10, 0x11, 0x0E, 0x00},
        {0x01, 0x01, 0x0F, 0x11, 0x11, 0x0F, 0x00}, {0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00},
        {0x06, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x00}, {0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E, 0x00},
        {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x00}, {0x04, 0x00, 0x0C, 0x04, 0x04, 0x0E, 0x00},
        {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}, {0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00},
        {0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, {0x00, 0x1A, 0x15, 0x15, 0x11, 0x11, 0x00},
        {0x00, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x00}, {0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00},
        {0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10, 0x00}, {0x00, 0x0F, 0x11, 0x0F, 0x01, 0x01, 0x00},
        {0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00}, {0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E, 0x00},
        {0x08, 0x1E, 0x08, 0x08, 0x0A, 0x04, 0x00}, {0x00, 0x11, 0x11, 0x11, 0x13, 0x0D, 0x00},
        {0x00, 0x11, 0x11, 0x0A, 0x0A, 0x04, 0x00}, {0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00},
        {0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00}, {0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E, 0x00},
        {0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00}, {0x02, 0x04, 0x0C, 0x04, 0x04, 0x02, 0x00},
        {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, {0x08, 0x04, 0x06, 0x04, 0x04, 0x08, 0x00},
        {0x00, 0x00, 0x0D, 0x12, 0x00, 0x00, 0x00},
    };

    /* Count "on" pixels to size the reusable scratch mesh exactly. */
    size_t quad_count = 0;
    for (const char *p = str; *p; p++) {
        int ch = *p;
        if (ch < 32 || ch > 126)
            ch = 32;
        const uint8_t *glyph = font5x7[ch - 32];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (glyph[row] & (1 << (4 - col))) {
                    if (quad_count >= (size_t)INT32_MAX / 6u) {
                        rt_trap("Canvas3D.DrawText3D: text is too large");
                        return;
                    }
                    quad_count++;
                }
    }

    if (quad_count == 0)
        return;

    int32_t vertex_count = (int32_t)(quad_count * 4u);
    int32_t index_count = (int32_t)(quad_count * 6u);
    if (!ensure_text_capacity(c, vertex_count, index_count))
        return;

    float scale = 2.0f; /* pixel size for each font dot */
    float cx = (float)x;
    int32_t quad_idx = 0;

    for (const char *p = str; *p; p++) {
        int ch = *p;
        if (ch < 32 || ch > 126)
            ch = 32;
        const uint8_t *glyph = font5x7[ch - 32];

        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph[row] & (1 << (4 - col))) {
                    float px = cx + col * scale;
                    float py = (float)y + row * scale;
                    int32_t vi = quad_idx * 4;
                    int32_t ii = quad_idx * 6;

                    /* 4 vertices for this pixel quad */
                    for (int v = 0; v < 4; v++) {
                        memset(&c->text_vertices[vi + v], 0, sizeof(vgfx3d_vertex_t));
                        c->text_vertices[vi + v].normal[2] = 1.0f;
                        c->text_vertices[vi + v].color[0] = r;
                        c->text_vertices[vi + v].color[1] = g;
                        c->text_vertices[vi + v].color[2] = b;
                        c->text_vertices[vi + v].color[3] = 1.0f;
                    }
                    c->text_vertices[vi + 0].pos[0] = px;
                    c->text_vertices[vi + 0].pos[1] = py;
                    c->text_vertices[vi + 1].pos[0] = px + scale;
                    c->text_vertices[vi + 1].pos[1] = py;
                    c->text_vertices[vi + 2].pos[0] = px + scale;
                    c->text_vertices[vi + 2].pos[1] = py + scale;
                    c->text_vertices[vi + 3].pos[0] = px;
                    c->text_vertices[vi + 3].pos[1] = py + scale;

                    c->text_indices[ii + 0] = (uint32_t)vi;
                    c->text_indices[ii + 1] = (uint32_t)(vi + 1);
                    c->text_indices[ii + 2] = (uint32_t)(vi + 2);
                    c->text_indices[ii + 3] = (uint32_t)vi;
                    c->text_indices[ii + 4] = (uint32_t)(vi + 2);
                    c->text_indices[ii + 5] = (uint32_t)(vi + 3);
                    quad_idx++;
                }
            }
        }
        cx += 6.0f * scale; /* char width + 1px spacing */
    }

    (void)canvas3d_queue_screen_geometry(
        c, c->text_vertices, vertex_count, c->text_indices, index_count, r, g, b, 1.0f);
}

/// @brief Begin a 3D rendering frame with the given camera.
/// @details Must be called after Clear and before any DrawMesh calls. Captures
///          the camera's view/projection matrices, resets the deferred draw queue,
///          and updates per-frame timing state. Begin/End must not be nested.
/// @param obj    Canvas handle.
/// @param camera Camera3D handle providing view and projection matrices.
void rt_canvas3d_begin(void *obj, void *camera) {
    vgfx3d_camera_params_t params;
    int32_t output_w = 0;
    int32_t output_h = 0;
    double render_aspect = 0.0;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    rt_camera3d *cam = rt_camera3d_checked_or_stack(camera);
    if (!c || !cam)
        return;
    if (!c->backend)
        return;
    if (c->in_frame) {
        rt_trap("Canvas3D.Begin: Begin/End must not nest");
        return;
    }
    if (c->frame_finalized) {
        canvas3d_clear_final_overlay(c);
        c->frame_finalized = 0;
        c->frame_presented_by_finalize = 0;
    }

    /* Show GPU layer for 3D rendering (in case it was hidden for 2D menu) */
    if (c->backend->show_gpu_layer)
        c->backend->show_gpu_layer(c->backend_ctx);

    canvas3d_active_output_size(c, &output_w, &output_h);
    if (output_w > 0 && output_h > 0)
        render_aspect = (double)output_w / (double)output_h;

    {
        double frame_dt = rt_canvas3d_get_delta_time_sec(c);
        if (frame_dt > 0.0)
            rt_camera3d_update_shake_for_frame(cam, frame_dt);
    }

    if (!canvas3d_mat4_d2f_checked(cam->view, params.view)) {
        rt_trap("Canvas3D.Begin: camera view matrix must contain finite float-range values");
        return;
    }
    rt_camera3d_get_render_projection(cam, render_aspect, params.projection);
    if (!canvas3d_matrices_f32_are_finite(params.projection, 1)) {
        rt_trap("Canvas3D.Begin: camera projection matrix must contain finite values");
        return;
    }
    double cam_x = cam->eye[0] + cam->shake_offset[0];
    double cam_y = cam->eye[1] + cam->shake_offset[1];
    double cam_z = cam->eye[2] + cam->shake_offset[2];
    if (!canvas3d_double_fits_float(cam_x) || !canvas3d_double_fits_float(cam_y) ||
        !canvas3d_double_fits_float(cam_z)) {
        rt_trap("Canvas3D.Begin: camera position must contain finite float-range values");
        return;
    }
    params.position[0] = (float)cam_x;
    params.position[1] = (float)cam_y;
    params.position[2] = (float)cam_z;
    canvas3d_extract_view_forward(cam->view, params.forward);
    params.is_ortho = cam->is_ortho;
    params.fog_enabled = c->fog_enabled;
    params.fog_near = c->fog_near;
    params.fog_far = c->fog_far;
    params.fog_color[0] = c->fog_color[0];
    params.fog_color[1] = c->fog_color[1];
    params.fog_color[2] = c->fog_color[2];
    params.load_existing_color = 0;
    params.load_existing_depth = 0;

    /* Cache camera position for transparency sort key computation */
    c->cached_cam_pos[0] = params.position[0];
    c->cached_cam_pos[1] = params.position[1];
    c->cached_cam_pos[2] = params.position[2];
    c->cached_cam_forward[0] = params.forward[0];
    c->cached_cam_forward[1] = params.forward[1];
    c->cached_cam_forward[2] = params.forward[2];
    c->cached_cam_is_ortho = params.is_ortho;

    /* Reset draw command queue for this frame */
    c->frame_serial++;
    canvas3d_prune_motion_history(c);
    c->draw_count = 0;
    c->frame_is_2d = 0;

    /* Cache VP matrix for debug drawing (backend-agnostic) */
    {
        /* VP = P * V (row-major) */
        for (int r = 0; r < 4; r++)
            for (int col = 0; col < 4; col++)
                c->cached_vp[r * 4 + col] =
                    params.projection[r * 4 + 0] * params.view[0 * 4 + col] +
                    params.projection[r * 4 + 1] * params.view[1 * 4 + col] +
                    params.projection[r * 4 + 2] * params.view[2 * 4 + col] +
                    params.projection[r * 4 + 3] * params.view[3 * 4 + col];
    }
    memcpy(c->last_scene_vp, c->cached_vp, sizeof(c->last_scene_vp));
    memcpy(c->last_scene_cam_pos, c->cached_cam_pos, sizeof(c->last_scene_cam_pos));
    c->has_last_scene_vp = 1;

    canvas3d_latch_gpu_postfx_state(c);
    c->backend->begin_frame(c->backend_ctx, &params);
    c->in_frame = 1;
}

/// @brief Monotonically-increasing per-frame serial; used for cache invalidation.
///
/// Resources keyed off `(canvas, serial)` know they're stale when
/// their cached serial is older than the canvas's current value.
int64_t rt_canvas3d_get_frame_serial(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    return c ? c->frame_serial : 0;
}

/// @brief Queue a 3D mesh draw with a model matrix and a sort key for transparency ordering.
///
/// `sort_key` is used by the deferred renderer to depth-sort
/// translucent draws back-to-front before flushing. Opaque
/// objects ignore it (the depth buffer handles their order).
void rt_canvas3d_draw_mesh_matrix_keyed(void *obj,
                                        void *mesh_obj,
                                        const double *model_matrix,
                                        void *material_obj,
                                        const void *motion_key,
                                        const float *prev_bone_palette,
                                        const float *prev_morph_weights) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    rt_mesh3d *mesh = (rt_mesh3d *)rt_g3d_checked_or_null(mesh_obj, RT_G3D_MESH3D_CLASS_ID);
    rt_material3d *mat =
        (rt_material3d *)rt_g3d_checked_or_null(material_obj, RT_G3D_MATERIAL3D_CLASS_ID);
    int8_t pending_has_splat = 0;
    const void *pending_splat_map = NULL;
    const void *pending_splat_layers[4] = {NULL, NULL, NULL, NULL};
    float pending_splat_layer_scales[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float validated_model_matrix[16];
    int mesh_obj_tracked = 0;
    int material_obj_tracked = 0;
    #if defined(RT_G3D_ALLOW_STACK_FIXTURES) && RT_G3D_ALLOW_STACK_FIXTURES
    if (!mesh && mesh_obj && !rt_heap_is_payload(mesh_obj))
        mesh = (rt_mesh3d *)mesh_obj;
    #endif
    if (!c)
        return;
    if (!mesh || !model_matrix || !mat) {
        canvas3d_clear_pending_splat(c);
        return;
    }
    if (!c->in_frame || !c->gfx_win || !c->backend) {
        canvas3d_clear_pending_splat(c);
        return;
    }
    if (!canvas3d_mat4_d2f_checked(model_matrix, validated_model_matrix)) {
        canvas3d_clear_pending_splat(c);
        rt_trap("Canvas3D.DrawMesh: model matrix must contain finite float-range values");
        return;
    }

    if (mesh->morph_targets_ref && mesh->morph_deltas == NULL && mesh->morph_weights == NULL &&
        mesh->morph_shape_count == 0) {
        rt_canvas3d_draw_mesh_matrix_morphed(
            obj, mesh_obj, model_matrix, material_obj, motion_key, mesh->morph_targets_ref);
        canvas3d_clear_pending_splat(c);
        return;
    }

    pending_has_splat = c->pending_has_splat;
    pending_splat_map = c->pending_splat_map;
    for (int i = 0; i < 4; i++) {
        pending_splat_layers[i] = c->pending_splat_layers[i];
        pending_splat_layer_scales[i] = c->pending_splat_layer_scales[i];
    }
    canvas3d_clear_pending_splat(c);

    if (mesh->vertex_count == 0 || mesh->index_count == 0)
        return;
    int needs_generated_tangents = canvas3d_prepare_normal_map_tangent_state(mesh, mat);
    rt_mesh3d_refresh_bounds(mesh);

    if (rt_heap_is_payload(mesh_obj)) {
        if (!canvas3d_track_temp_object(c, mesh_obj))
            return;
        mesh_obj_tracked = 1;
    }
    if (!canvas3d_track_temp_object(c, material_obj)) {
        if (mesh_obj_tracked)
            canvas3d_release_tracked_temp_object(c, mesh_obj);
        return;
    }
    material_obj_tracked = 1;

    deferred_draw_t queued;
    deferred_draw_t *dd = &queued;
    memset(dd, 0, sizeof(*dd));
    dd->kind = DEFERRED_DRAW_MESH;
    dd->pass_kind = DEFERRED_PASS_MAIN;
    dd->visible = 1;

    vgfx3d_vertex_t *queued_vertices = mesh->vertices;
    uint32_t *queued_indices = mesh->indices;
    if (needs_generated_tangents) {
        if (!canvas3d_snapshot_mesh_geometry(c, mesh, &queued_vertices, &queued_indices))
            goto fail_after_refs;
        if (!canvas3d_generate_snapshot_tangents(mesh, queued_vertices, queued_indices)) {
            canvas3d_release_tracked_temp_buffer(c, queued_vertices);
            canvas3d_release_tracked_temp_buffer(c, queued_indices);
            goto fail_after_refs;
        }
    } else if (canvas3d_should_snapshot_geometry(mesh, mesh_obj) &&
               !canvas3d_snapshot_mesh_geometry_cached(c, mesh, mesh_obj, &queued_vertices, &queued_indices)) {
        goto fail_after_refs;
    }

    /* Build draw command */
    dd->cmd.vertices = queued_vertices;
    dd->cmd.vertex_count = mesh->vertex_count;
    dd->cmd.indices = queued_indices;
    dd->cmd.index_count = mesh->index_count;
    dd->cmd.geometry_key = (rt_heap_is_payload(mesh_obj) && !needs_generated_tangents) ? mesh_obj : NULL;
    dd->cmd.geometry_revision = dd->cmd.geometry_key ? mesh->geometry_revision : 0;
    memcpy(dd->cmd.model_matrix, validated_model_matrix, sizeof(dd->cmd.model_matrix));
    canvas3d_resolve_previous_model(c,
                                    (uintptr_t)motion_key,
                                    dd->cmd.model_matrix,
                                    dd->cmd.prev_model_matrix,
                                    &dd->cmd.has_prev_model_matrix);
    canvas3d_fill_material_cmd(mat, &dd->cmd);

    /* Consume pending terrain splat data (if set by terrain draw path) */
    dd->cmd.has_splat = pending_has_splat;
    dd->cmd.splat_map = pending_splat_map;
    for (int i = 0; i < 4; i++) {
        dd->cmd.splat_layers[i] = pending_splat_layers[i];
        dd->cmd.splat_layer_scales[i] = pending_splat_layer_scales[i];
    }

    /* Pass through bone palette for GPU skinning (MTL-09) */
    dd->cmd.bone_palette = mesh->bone_palette;
    dd->cmd.prev_bone_palette = prev_bone_palette ? prev_bone_palette : mesh->prev_bone_palette;
    dd->cmd.bone_count = mesh->bone_count;

    /* GPU morph payloads are supplied by DrawMeshMorphed via transient mesh fields.
     * CPU morph paths leave these null. */
    dd->cmd.morph_deltas = mesh->morph_deltas;
    dd->cmd.morph_normal_deltas = mesh->morph_normal_deltas;
    dd->cmd.morph_weights = mesh->morph_weights;
    dd->cmd.prev_morph_weights = prev_morph_weights ? prev_morph_weights : mesh->prev_morph_weights;
    dd->cmd.morph_shape_count = mesh->morph_shape_count;
    dd->cmd.morph_key = mesh->morph_targets_ref;
    dd->cmd.morph_revision = mesh->morph_targets_ref
                                 ? rt_morphtarget3d_get_payload_generation(mesh->morph_targets_ref)
                                 : 0;

    /* Build light params */
    dd->light_count = build_light_params(c, dd->lights, VGFX3D_MAX_LIGHTS);
    dd->ambient[0] = c->ambient[0];
    dd->ambient[1] = c->ambient[1];
    dd->ambient[2] = c->ambient[2];
    dd->wireframe = c->wireframe;
    dd->backface_cull = canvas3d_material_backface_cull(c, mat);
    dd->requires_blend = canvas3d_cmd_requires_blend(&dd->cmd);
    dd->has_local_bounds = 1;
    memcpy(dd->local_bounds_min, mesh->aabb_min, sizeof(dd->local_bounds_min));
    memcpy(dd->local_bounds_max, mesh->aabb_max, sizeof(dd->local_bounds_max));

    dd->sort_key = canvas3d_compute_sort_key(c,
                                             dd->cmd.model_matrix,
                                             dd->local_bounds_min,
                                             dd->local_bounds_max,
                                             dd->has_local_bounds,
                                             dd->requires_blend);

    if (ensure_deferred_capacity(&c->draw_cmds, &c->draw_capacity, c->draw_count + 1)) {
        ((deferred_draw_t *)c->draw_cmds)[c->draw_count++] = queued;
        return;
    }
    if (material_obj_tracked)
        canvas3d_release_tracked_temp_object(c, material_obj);
    if (mesh_obj_tracked)
        canvas3d_release_tracked_temp_object(c, mesh_obj);
    rt_trap("Canvas3D.DrawMesh: deferred draw queue allocation failed");
    return;

fail_after_refs:
    if (material_obj_tracked)
        canvas3d_release_tracked_temp_object(c, material_obj);
    if (mesh_obj_tracked)
        canvas3d_release_tracked_temp_object(c, mesh_obj);
}

/// @brief Convenience: queue a mesh draw without an explicit sort key (uses default).
/// @see rt_canvas3d_draw_mesh_matrix_keyed
void rt_canvas3d_draw_mesh_matrix(void *obj,
                                  void *mesh_obj,
                                  const double *model_matrix,
                                  void *material_obj) {
    rt_canvas3d_draw_mesh_matrix_keyed(obj, mesh_obj, model_matrix, material_obj, NULL, NULL, NULL);
}

/// @brief Submit a mesh for drawing with the given transform and material.
/// @details Defers the draw into the per-frame queue. Actual rendering happens
///          in End(), which sorts opaque draws front-to-back and transparent draws
///          back-to-front for correct alpha blending. The mesh, transform, and
///          material pointers are borrowed (not retained).
void rt_canvas3d_draw_mesh(void *obj, void *mesh_obj, void *transform_obj, void *material_obj) {
    mat4_impl *transform = canvas3d_mat4_checked(transform_obj);
    uintptr_t motion_key;
    if (!transform)
        return;
    if (mesh_obj && !rt_g3d_has_class(mesh_obj, RT_G3D_MESH3D_CLASS_ID))
        return;
    motion_key = canvas3d_mesh_transform_motion_key(mesh_obj, material_obj, transform_obj);
    rt_canvas3d_draw_mesh_matrix_keyed(
        obj, mesh_obj, transform->m, material_obj, (const void *)motion_key, NULL, NULL);
}

/// @brief Queue an instanced draw — render `instance_count` copies of `mesh` with per-instance
/// transforms.
///
/// One draw call instead of `instance_count` separate calls.
/// Used for foliage, debris, particle clouds, and any scene with
/// many copies of the same mesh. The matrix array is owned by the
/// caller for the duration of the frame; the canvas keeps a
/// reference until the deferred queue flushes.
void rt_canvas3d_queue_instanced_batch(void *canvas_obj,
                                       void *mesh_obj,
                                       void *material_obj,
                                       const float *instance_matrices,
                                       int32_t instance_count,
                                       const float *prev_instance_matrices,
                                       int8_t has_prev_instance_matrices) {
    rt_canvas3d *c;
    rt_mesh3d *mesh;
    rt_material3d *mat;
    vgfx3d_draw_cmd_t base_cmd;
    int mesh_obj_tracked = 0;
    int material_obj_tracked = 0;

    if (!instance_matrices || instance_count <= 0)
        return;
    if (instance_count > CANVAS3D_MAX_INSTANCES) {
        rt_trap("Canvas3D.DrawMeshInstanced: instance count exceeds limit");
        return;
    }
    c = rt_canvas3d_checked_or_stack(canvas_obj);
    mesh = (rt_mesh3d *)rt_g3d_checked_or_null(mesh_obj, RT_G3D_MESH3D_CLASS_ID);
    #if defined(RT_G3D_ALLOW_STACK_FIXTURES) && RT_G3D_ALLOW_STACK_FIXTURES
    if (!mesh && mesh_obj && !rt_heap_is_payload(mesh_obj))
        mesh = (rt_mesh3d *)mesh_obj;
    #endif
    mat = (rt_material3d *)rt_g3d_checked_or_null(material_obj, RT_G3D_MATERIAL3D_CLASS_ID);
    if (!c || !mesh || !mat)
        return;
    if (!c->in_frame || !c->backend || mesh->vertex_count == 0 || mesh->index_count == 0)
        return;
    if (has_prev_instance_matrices && !prev_instance_matrices) {
        rt_trap("Canvas3D.DrawMeshInstanced: previous instance matrices pointer is required");
        return;
    }

    int needs_generated_tangents = canvas3d_prepare_normal_map_tangent_state(mesh, mat);
    rt_mesh3d_refresh_bounds(mesh);
    memset(&base_cmd, 0, sizeof(base_cmd));
    base_cmd.vertices = mesh->vertices;
    base_cmd.vertex_count = mesh->vertex_count;
    base_cmd.indices = mesh->indices;
    base_cmd.index_count = mesh->index_count;
    base_cmd.model_matrix[0] = base_cmd.model_matrix[5] = base_cmd.model_matrix[10] =
        base_cmd.model_matrix[15] = 1.0f;
    canvas3d_fill_material_cmd(mat, &base_cmd);
    base_cmd.bone_palette = mesh->bone_palette;
    base_cmd.prev_bone_palette = mesh->prev_bone_palette;
    base_cmd.bone_count = mesh->bone_count;
    base_cmd.morph_deltas = mesh->morph_deltas;
    base_cmd.morph_normal_deltas = mesh->morph_normal_deltas;
    base_cmd.morph_weights = mesh->morph_weights;
    base_cmd.prev_morph_weights = mesh->prev_morph_weights;
    base_cmd.morph_shape_count = mesh->morph_shape_count;
    base_cmd.morph_key = mesh->morph_targets_ref;
    base_cmd.morph_revision = mesh->morph_targets_ref
                                  ? rt_morphtarget3d_get_payload_generation(mesh->morph_targets_ref)
                                  : 0;

    int use_fallback_instances =
        canvas3d_cmd_requires_blend(&base_cmd) || !c->backend->submit_draw_instanced;
    if (use_fallback_instances && instance_count > CANVAS3D_MAX_FALLBACK_INSTANCES) {
        rt_trap("Canvas3D.DrawMeshInstanced: fallback instance count exceeds limit");
        return;
    }
    if (!canvas3d_matrices_f32_are_finite(instance_matrices, instance_count) ||
        (has_prev_instance_matrices && prev_instance_matrices &&
         !canvas3d_matrices_f32_are_finite(prev_instance_matrices, instance_count))) {
        rt_trap("Canvas3D.DrawMeshInstanced: instance matrices must contain finite values");
        return;
    }
    if (rt_heap_is_payload(mesh_obj)) {
        if (!canvas3d_track_temp_object(c, mesh_obj))
            return;
        mesh_obj_tracked = 1;
    }
    if (!canvas3d_track_temp_object(c, material_obj)) {
        if (mesh_obj_tracked)
            canvas3d_release_tracked_temp_object(c, mesh_obj);
        return;
    }
    material_obj_tracked = 1;

    vgfx3d_vertex_t *queued_vertices = mesh->vertices;
    uint32_t *queued_indices = mesh->indices;
    if (needs_generated_tangents) {
        if (!canvas3d_snapshot_mesh_geometry(c, mesh, &queued_vertices, &queued_indices))
            goto fail_after_refs;
        if (!canvas3d_generate_snapshot_tangents(mesh, queued_vertices, queued_indices)) {
            canvas3d_release_tracked_temp_buffer(c, queued_vertices);
            canvas3d_release_tracked_temp_buffer(c, queued_indices);
            goto fail_after_refs;
        }
    } else if (canvas3d_should_snapshot_geometry(mesh, mesh_obj) &&
               !canvas3d_snapshot_mesh_geometry_cached(c, mesh, mesh_obj, &queued_vertices, &queued_indices)) {
        goto fail_after_refs;
    }
    base_cmd.vertices = queued_vertices;
    base_cmd.indices = queued_indices;
    base_cmd.geometry_key =
        (rt_heap_is_payload(mesh_obj) && !needs_generated_tangents) ? mesh_obj : NULL;
    base_cmd.geometry_revision = base_cmd.geometry_key ? mesh->geometry_revision : 0;

    if (use_fallback_instances) {
        for (int32_t i = 0; i < instance_count; i++) {
            vgfx3d_draw_cmd_t per_instance = base_cmd;
            memcpy(per_instance.model_matrix,
                   &instance_matrices[(size_t)i * 16u],
                   sizeof(per_instance.model_matrix));
            if (has_prev_instance_matrices && prev_instance_matrices) {
                memcpy(per_instance.prev_model_matrix,
                       &prev_instance_matrices[(size_t)i * 16u],
                       sizeof(per_instance.prev_model_matrix));
                per_instance.has_prev_model_matrix = 1;
            } else {
                canvas3d_resolve_previous_model(
                    c,
                    canvas3d_instance_motion_key(
                        mesh_obj, material_obj, instance_matrices, instance_count, i),
                    per_instance.model_matrix,
                    per_instance.prev_model_matrix,
                    &per_instance.has_prev_model_matrix);
            }
            per_instance.prev_instance_matrices = NULL;
            per_instance.has_prev_instance_matrices = 0;
            if (!canvas3d_enqueue_draw(
                    c,
                    &per_instance,
                    DEFERRED_DRAW_MESH,
                    DEFERRED_PASS_MAIN,
                    NULL,
                    0,
                    1,
                    c->wireframe,
                    canvas3d_material_backface_cull(c, mat),
                    canvas3d_compute_sort_key(c,
                                              per_instance.model_matrix,
                                              mesh->aabb_min,
                                              mesh->aabb_max,
                                              1,
                                              canvas3d_cmd_requires_blend(&per_instance)),
                    mesh->aabb_min,
                    mesh->aabb_max)) {
                if (material_obj_tracked)
                    canvas3d_release_tracked_temp_object(c, material_obj);
                if (mesh_obj_tracked)
                    canvas3d_release_tracked_temp_object(c, mesh_obj);
                rt_trap("Canvas3D.DrawMeshInstanced: deferred draw queue allocation failed");
                return;
            }
        }
        return;
    }

    if ((size_t)instance_count > SIZE_MAX / (16u * sizeof(float))) {
        if (material_obj_tracked)
            canvas3d_release_tracked_temp_object(c, material_obj);
        if (mesh_obj_tracked)
            canvas3d_release_tracked_temp_object(c, mesh_obj);
        rt_trap("Canvas3D.DrawMeshInstanced: instance matrix allocation overflow");
        return;
    }
    size_t matrix_float_count = (size_t)instance_count * 16u;
    float *queued_instance_matrices =
        (float *)malloc(matrix_float_count * sizeof(*queued_instance_matrices));
    if (!queued_instance_matrices) {
        if (material_obj_tracked)
            canvas3d_release_tracked_temp_object(c, material_obj);
        if (mesh_obj_tracked)
            canvas3d_release_tracked_temp_object(c, mesh_obj);
        rt_trap("Canvas3D.DrawMeshInstanced: instance matrix allocation failed");
        return;
    }
    memcpy(queued_instance_matrices, instance_matrices, matrix_float_count * sizeof(float));
    if (!canvas3d_track_temp_buffer(c, queued_instance_matrices)) {
        free(queued_instance_matrices);
        if (material_obj_tracked)
            canvas3d_release_tracked_temp_object(c, material_obj);
        if (mesh_obj_tracked)
            canvas3d_release_tracked_temp_object(c, mesh_obj);
        return;
    }

    float *queued_prev_instance_matrices = NULL;
    int needs_motion_vectors = canvas3d_frame_needs_motion_vectors(c);
    if (needs_motion_vectors) {
        queued_prev_instance_matrices =
            (float *)malloc(matrix_float_count * sizeof(*queued_prev_instance_matrices));
        if (!queued_prev_instance_matrices) {
            canvas3d_release_tracked_temp_buffer(c, queued_instance_matrices);
            if (material_obj_tracked)
                canvas3d_release_tracked_temp_object(c, material_obj);
            if (mesh_obj_tracked)
                canvas3d_release_tracked_temp_object(c, mesh_obj);
            rt_trap("Canvas3D.DrawMeshInstanced: previous instance matrix allocation failed");
            return;
        }
        if (has_prev_instance_matrices && prev_instance_matrices) {
            memcpy(queued_prev_instance_matrices,
                   prev_instance_matrices,
                   matrix_float_count * sizeof(float));
        }
        if (!canvas3d_track_temp_buffer(c, queued_prev_instance_matrices)) {
            canvas3d_release_tracked_temp_buffer(c, queued_instance_matrices);
            free(queued_prev_instance_matrices);
            if (material_obj_tracked)
                canvas3d_release_tracked_temp_object(c, material_obj);
            if (mesh_obj_tracked)
                canvas3d_release_tracked_temp_object(c, mesh_obj);
            return;
        }
    }
    for (int32_t i = 0; i < instance_count; ++i) {
        int8_t has_prev = 0;
        float ignored_prev[16];
        float *dst_prev =
            needs_motion_vectors ? &queued_prev_instance_matrices[(size_t)i * 16u] : ignored_prev;
        const float *current = &instance_matrices[(size_t)i * 16u];
        if (needs_motion_vectors && has_prev_instance_matrices && prev_instance_matrices) {
            canvas3d_resolve_previous_model(
                c,
                canvas3d_instance_motion_key(
                    mesh_obj, material_obj, instance_matrices, instance_count, i),
                current,
                ignored_prev,
                &has_prev);
            continue;
        }
        canvas3d_resolve_previous_model(
            c,
            canvas3d_instance_motion_key(
                mesh_obj, material_obj, instance_matrices, instance_count, i),
            current,
            dst_prev,
            &has_prev);
        if (needs_motion_vectors && !has_prev)
            memcpy(dst_prev, current, 16u * sizeof(float));
    }

    base_cmd.prev_instance_matrices = queued_prev_instance_matrices;
    base_cmd.has_prev_instance_matrices = queued_prev_instance_matrices ? 1 : 0;
    if (!canvas3d_enqueue_draw(
            c,
            &base_cmd,
            DEFERRED_DRAW_INSTANCED,
            DEFERRED_PASS_MAIN,
            queued_instance_matrices,
            instance_count,
            1,
            c->wireframe,
            canvas3d_material_backface_cull(c, mat),
            canvas3d_compute_instanced_batch_sort_key(
                c, queued_instance_matrices, instance_count, mesh->aabb_min, mesh->aabb_max),
            mesh->aabb_min,
            mesh->aabb_max)) {
        rt_trap("Canvas3D.DrawMeshInstanced: deferred draw queue allocation failed");
        canvas3d_release_tracked_temp_buffer(c, queued_prev_instance_matrices);
        canvas3d_release_tracked_temp_buffer(c, queued_instance_matrices);
        if (material_obj_tracked)
            canvas3d_release_tracked_temp_object(c, material_obj);
        if (mesh_obj_tracked)
            canvas3d_release_tracked_temp_object(c, mesh_obj);
        return;
    }
    return;

fail_after_refs:
    if (material_obj_tracked)
        canvas3d_release_tracked_temp_object(c, material_obj);
    if (mesh_obj_tracked)
        canvas3d_release_tracked_temp_object(c, mesh_obj);
}

/// @brief End drawing for the current scene or 2D pass and flush deferred draws.
/// @details Processes the deferred queue built during Begin/DrawMesh calls
///          in this strict pass order:
///          1. Skybox (if attached) into the scene color buffer.
///          2. Shadow pass: build a tight ortho light-VP, depth-render
///             every opaque main draw into the shadow map.
///          3. Opaque main draws — front-to-back when occlusion culling
///             is enabled (maximizes early-Z rejection), unsorted otherwise.
///          4. Translucent main draws — sorted back-to-front so alpha
///             blends compose correctly.
///          5. Pre/post-3D HUD overlay draws via `begin_overlay_frame`.
///          Must be called after all DrawMesh calls and before finalization or Flip.
///          Side effects: increments `frame_serial`, clears `draw_count`,
///          drains temp-buffer + temp-object queues, resets `in_frame`.
///          Post-FX, final overlay replay, screenshots, and presentation happen
///          in `FinalizeFrame`, `ScreenshotFinal`, and `Flip`, not here.
void rt_canvas3d_end(void *obj) {
    deferred_draw_t *cmds;
    int32_t queued_draw_count;
    int32_t main_count = 0;
    int32_t overlay_count = 0;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (!c->in_frame)
        return;
    if (!c->backend) {
        c->in_frame = 0;
        c->frame_is_2d = 0;
        c->draw_count = 0;
        canvas3d_clear_temp_buffers(c);
        canvas3d_clear_temp_objects(c);
        return;
    }

    cmds = (deferred_draw_t *)c->draw_cmds;
    queued_draw_count = c->draw_count;

    if (!c->frame_is_2d && c->skybox) {
        uint8_t *out_pixels = NULL;
        int32_t out_w = 0;
        int32_t out_h = 0;
        int32_t out_stride = 0;

        if (c->backend->draw_skybox) {
            c->backend->draw_skybox(c->backend_ctx, c->skybox);
        } else {
            if (c->render_target) {
                if (vgfx3d_rendertarget_ensure_color(c->render_target)) {
                    out_pixels = c->render_target->color_buf;
                    out_w = c->render_target->width;
                    out_h = c->render_target->height;
                    out_stride = c->render_target->stride;
                }
            } else {
                vgfx_framebuffer_t fb;
                if (c->gfx_win && vgfx_get_framebuffer(c->gfx_win, &fb)) {
                    out_pixels = fb.pixels;
                    out_w = fb.width;
                    out_h = fb.height;
                    out_stride = fb.stride;
                }
            }
        }

        if (!c->backend->draw_skybox && out_pixels && !canvas3d_backend_owns_gpu_rtt(c) &&
            canvas3d_ensure_skybox_cpu_cache(c, out_w, out_h)) {
            canvas3d_blit_skybox_cpu_cache(c, out_pixels, out_w, out_h, out_stride);
            if (c->render_target)
                c->render_target->color_dirty = 1;
            if (c->render_target)
                c->render_target->hdr_color_valid = 0;
        }
    }

    for (int32_t i = 0; i < c->draw_count; i++) {
        if (cmds[i].pass_kind == DEFERRED_PASS_MAIN)
            main_count++;
        else if (cmds[i].pass_kind == DEFERRED_PASS_SCREEN_OVERLAY)
            overlay_count++;
    }

    if (main_count == 0 && overlay_count == 0) {
        c->backend->end_frame(c->backend_ctx);
        c->in_frame = 0;
        c->frame_is_2d = 0;
        c->draw_count = 0;
        canvas3d_clear_temp_buffers(c);
        canvas3d_clear_temp_objects(c);
        return;
    }

    c->shadow_count = 0;
    memset(c->shadow_light_vps, 0, sizeof(c->shadow_light_vps));
    if (!c->frame_is_2d && main_count > 0) {
        vgfx3d_light_params_t shadow_lights[VGFX3D_MAX_SHADOW_LIGHTS];

        memset(shadow_lights, 0, sizeof(shadow_lights));

        if (c->shadows_enabled && c->backend->shadow_begin && c->backend->shadow_draw &&
            c->backend->shadow_end && canvas3d_ensure_shadow_targets(c, c->shadow_resolution)) {
            int32_t selected_shadow_count = canvas3d_select_shadow_directional_lights_from_draws(
                cmds, c->draw_count, shadow_lights, VGFX3D_MAX_SHADOW_LIGHTS);
            float shadow_world_min[3];
            float shadow_world_max[3];
            int has_shadow_bounds =
                canvas3d_build_shadow_world_bounds(cmds, c->draw_count, shadow_world_min, shadow_world_max);

            for (int32_t slot = 0; has_shadow_bounds && slot < selected_shadow_count; slot++) {
                vgfx3d_rendertarget_t *shadow_rt;
                vgfx3d_light_params_t selected_light = shadow_lights[slot];
                float light_vp[16];

                shadow_rt = c->shadow_rts[c->shadow_count];
                if (!shadow_rt || !shadow_rt->depth_buf)
                    continue;
                if (!canvas3d_build_shadow_light_vp(
                        shadow_world_min, shadow_world_max, &selected_light, light_vp))
                    continue;

                memcpy(c->shadow_light_vps[c->shadow_count], light_vp, sizeof(light_vp));
                shadow_lights[c->shadow_count] = selected_light;
                shadow_lights[c->shadow_count].shadow_index = c->shadow_count;
                c->backend->shadow_begin(c->backend_ctx,
                                         c->shadow_count,
                                         shadow_rt->depth_buf,
                                         shadow_rt->width,
                                         shadow_rt->height,
                                         light_vp);
                for (int32_t i = 0; i < c->draw_count; i++) {
                    if (cmds[i].pass_kind != DEFERRED_PASS_MAIN || cmds[i].requires_blend)
                        continue;
                    canvas3d_shadow_deferred(c, &cmds[i]);
                }
                c->backend->shadow_end(c->backend_ctx, c->shadow_count, c->shadow_bias);
                c->shadow_count++;
            }
        }

        for (int32_t i = 0; i < c->draw_count; i++)
            canvas3d_apply_shadow_light_params(
                cmds[i].lights, cmds[i].light_count, shadow_lights, c->shadow_count);
    }

    if (main_count > 0) {
        vgfx3d_frustum_t visibility_frustum;

        if (c->occlusion_culling) {
            vgfx3d_frustum_extract(&visibility_frustum, c->cached_vp);
            for (int32_t i = 0; i < c->draw_count; i++) {
                if (cmds[i].pass_kind == DEFERRED_PASS_MAIN)
                    cmds[i].visible =
                        (int8_t)canvas3d_deferred_intersects_frustum(&cmds[i], &visibility_frustum);
            }
        } else {
            for (int32_t i = 0; i < c->draw_count; i++) {
                if (cmds[i].pass_kind == DEFERRED_PASS_MAIN)
                    cmds[i].visible = 1;
            }
        }

        {
            int32_t opaque_count = 0;
            for (int32_t i = 0; i < c->draw_count; i++) {
                if (cmds[i].pass_kind == DEFERRED_PASS_MAIN &&
                    !cmds[i].requires_blend && cmds[i].visible)
                    opaque_count++;
            }
            if (opaque_count > 0) {
                if (ensure_deferred_capacity(&c->trans_cmds, &c->trans_capacity, opaque_count)) {
                    deferred_draw_t *opaque = (deferred_draw_t *)c->trans_cmds;
                    int32_t oi = 0;
                    for (int32_t i = 0; i < c->draw_count; i++) {
                        if (cmds[i].pass_kind == DEFERRED_PASS_MAIN &&
                            !cmds[i].requires_blend && cmds[i].visible)
                            opaque[oi++] = cmds[i];
                    }
                    qsort(opaque, (size_t)opaque_count, sizeof(deferred_draw_t), cmp_front_to_back);
                    for (int32_t i = 0; i < opaque_count; i++)
                        canvas3d_submit_deferred(c, &opaque[i]);
                } else {
                    for (int32_t i = 0; i < c->draw_count; i++) {
                        if (cmds[i].pass_kind == DEFERRED_PASS_MAIN &&
                            !cmds[i].requires_blend && cmds[i].visible)
                            canvas3d_submit_deferred(c, &cmds[i]);
                    }
                }
            }
        }

        {
            int32_t trans_count = 0;
            for (int32_t i = 0; i < c->draw_count; i++) {
                if (cmds[i].pass_kind == DEFERRED_PASS_MAIN &&
                    cmds[i].requires_blend && cmds[i].visible)
                    trans_count++;
            }
            if (trans_count > 0) {
                if (ensure_deferred_capacity(&c->trans_cmds, &c->trans_capacity, trans_count)) {
                    deferred_draw_t *trans = (deferred_draw_t *)c->trans_cmds;
                    int32_t ti = 0;
                    for (int32_t i = 0; i < c->draw_count; i++) {
                        if (cmds[i].pass_kind == DEFERRED_PASS_MAIN &&
                            cmds[i].requires_blend && cmds[i].visible)
                            trans[ti++] = cmds[i];
                    }
                    qsort(trans, (size_t)trans_count, sizeof(deferred_draw_t), cmp_back_to_front);
                    for (int32_t i = 0; i < trans_count; i++)
                        canvas3d_submit_deferred(c, &trans[i]);
                } else {
                    for (int32_t i = 0; i < c->draw_count; i++) {
                        if (cmds[i].pass_kind == DEFERRED_PASS_MAIN &&
                            cmds[i].requires_blend && cmds[i].visible)
                            canvas3d_submit_deferred(c, &cmds[i]);
                    }
                }
            }
        }
    }

    c->backend->end_frame(c->backend_ctx);
    c->in_frame = 0;

    if (!c->frame_is_2d && overlay_count > 0) {
        if (canvas3d_begin_overlay_frame(c, 1)) {
            for (int32_t i = 0; i < queued_draw_count; i++) {
                if (cmds[i].pass_kind != DEFERRED_PASS_SCREEN_OVERLAY)
                    continue;
                canvas3d_submit_screen_overlay_deferred(c, &cmds[i]);
            }
            c->backend->end_frame(c->backend_ctx);
        }
    }

    c->in_frame = 0;
    c->frame_is_2d = 0;
    c->draw_count = 0;
    canvas3d_clear_temp_buffers(c);
    canvas3d_clear_temp_objects(c);
}

/*==========================================================================
 * Finalization and window lifecycle
 *=========================================================================*/

/// @brief Replay recorded final-overlay commands after post-FX.
static void canvas3d_replay_final_overlay(rt_canvas3d *c) {
    deferred_draw_t *cmds;

    if (!c || c->final_overlay_count <= 0 || !c->backend)
        return;
    if (!canvas3d_begin_overlay_frame(c, 1))
        return;
    cmds = (deferred_draw_t *)c->final_overlay_cmds;
    for (int32_t i = 0; i < c->final_overlay_count; i++)
        canvas3d_submit_screen_overlay_deferred(c, &cmds[i]);
    c->backend->end_frame(c->backend_ctx);
    c->in_frame = 0;
    c->frame_is_2d = 0;
}

/// @brief Apply post-FX and final overlay exactly once.
void rt_canvas3d_finalize_frame(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (!c->gfx_win)
        return;
    if (c->frame_finalized)
        return;

    if (c->final_overlay_recording)
        rt_canvas3d_end_overlay(obj);
    if (c->in_frame)
        rt_canvas3d_end(obj);

    if (canvas3d_backend_uses_gpu_postfx(c)) {
        if (c->frame_gpu_postfx_enabled) {
            if (canvas3d_backend_splits_gpu_postfx_present(c)) {
                c->backend->apply_postfx(c->backend_ctx, &c->frame_postfx_chain);
                canvas3d_replay_final_overlay(c);
                c->backend->present(c->backend_ctx);
            } else {
                canvas3d_replay_final_overlay(c);
                c->backend->present_postfx(c->backend_ctx, &c->frame_postfx_chain);
            }
            c->frame_presented_by_finalize = 1;
            c->frame_finalized = 1;
            return;
        }
    }

    rt_postfx3d_apply_to_canvas(obj);
    canvas3d_replay_final_overlay(c);
    c->frame_finalized = 1;
}

/// @brief Return whether the current frame has already been finalized.
int8_t rt_canvas3d_get_frame_finalized(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    return c && c->frame_finalized ? 1 : 0;
}

/// @brief Capture finalized frame pixels, finalizing first if needed.
void *rt_canvas3d_screenshot_final(void *obj) {
    rt_canvas3d_finalize_frame(obj);
    return rt_canvas3d_screenshot(obj);
}

/// @brief Present the rendered frame to the window (swaps buffers).
/// @details Finalizes the frame if needed, then presents finalized pixels.
///          Updates the FPS counter and delta-time calculation for the next
///          frame.
void rt_canvas3d_flip(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (!c->gfx_win)
        return;

    rt_canvas3d_finalize_frame(obj);

    /* Present the GPU drawable / swap the back buffer after all queued passes
     * for the frame have rendered into the backend's scene targets. */
    if (!c->frame_presented_by_finalize && c->backend && c->backend->present)
        c->backend->present(c->backend_ctx);

    /* Always call vgfx_update to keep the window alive and process display
     * refresh. GPU backends own the final on-screen present path. */
    vgfx_update(c->gfx_win);
    c->frame_postfx_state_latched = 0;
    c->frame_gpu_postfx_enabled = 0;
    c->frame_finalized = 0;
    c->frame_presented_by_finalize = 0;
    canvas3d_clear_final_overlay(c);
    vgfx3d_postfx_chain_reset(&c->frame_postfx_chain);

    if (c->clock_source == 1) {
        canvas3d_apply_synthetic_clock(c);
    } else if (c->frame_timing_updated_by_poll) {
        c->frame_timing_updated_by_poll = 0;
    } else {
        canvas3d_update_live_clock(c);
    }

    if (vgfx_close_requested(c->gfx_win))
        canvas3d_close_window(c);
}

/// @brief Notify the mouse subsystem from logical Canvas3D coordinates.
/// @details `vgfx_mouse_pos()` already returns logical coordinates once
///          Canvas3D enables the window coordinate scale, so live polling must
///          pass those values through without applying the scale a second time.
static void rt_canvas3d_update_mouse_from_logical(int32_t x, int32_t y) {
    rt_mouse_update_pos((int64_t)x, (int64_t)y);
}

/// @brief Translate physical pixel event coordinates to logical coordinates.
/// @details Platform mouse events carry physical backing-pixel coordinates.
///          Dividing by the per-window scale keeps event positions aligned
///          with Canvas3D's public logical coordinate space.
static void rt_canvas3d_update_mouse_from_physical(vgfx_window_t gfx_win, int32_t x, int32_t y) {
    float scale = vgfx_window_get_scale(gfx_win);
    if (!isfinite(scale) || scale < 0.001f)
        scale = 1.0f;
    rt_canvas3d_update_mouse_from_logical((int32_t)((double)x / (double)scale),
                                          (int32_t)((double)y / (double)scale));
}

/// @brief Pump platform events, advance per-frame input state, and return whether the window is
/// still open.
///
/// Called once per game-loop iteration. Drives keyboard/mouse/
/// gamepad/action input subsystems, updates the wall-clock dt
/// (capped at `dt_max` to prevent huge jumps after pauses), and
/// dispatches resize / focus / close events.
/// @return 1 if the window remains open, 0 if the user requested close.
int64_t rt_canvas3d_poll(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return 0;
    if (!c->gfx_win && c->input_source != 1)
        return 0;

    int8_t use_live = c->input_source != 1;
    int8_t use_synthetic = c->input_source != 0;
    int8_t captured = use_live ? rt_mouse_is_captured() : 0;
    c->last_event_type = VGFX_EVENT_NONE;

    /* Begin frame (resets per-frame state for keyboard/mouse/pad) */
    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_pad_begin_frame();

    if (c->clock_source == 0) {
        canvas3d_update_live_clock(c);
        c->frame_timing_updated_by_poll = 1;
    }

    if (use_live) {
        rt_pad_poll();

        if (!vgfx_pump_events(c->gfx_win)) {
            canvas3d_close_window(c);
            rt_action_update();
            return 0;
        }

        /* Read current platform mouse position. vgfx_mouse_pos() returns
         * logical coordinates because Canvas3D enables coord_scale at window
         * creation. Raw events below still arrive in physical pixels. */
        int32_t mx, my;
        vgfx_mouse_pos(c->gfx_win, &mx, &my);

        /* For captured (FPS) mode: compute delta as offset from window center.
         * This avoids issues with warp timing, stale events, and OS mouse tracking. */
        if (captured) {
            int32_t cw, ch;
            vgfx_get_size(c->gfx_win, &cw, &ch);
            int32_t cx = cw / 2, cy = ch / 2;
            int64_t dx = (int64_t)((double)mx - (double)cx);
            int64_t dy = (int64_t)((double)my - (double)cy);
            rt_mouse_force_delta(dx, dy);
        } else {
            rt_canvas3d_update_mouse_from_logical(mx, my);
        }

        /* Process events (keyboard + mouse buttons only — mouse moves handled above) */
        vgfx_event_t evt;
        while (vgfx_poll_event(c->gfx_win, &evt)) {
            canvas3d_record_event_type(c, (int64_t)evt.type);
            if (evt.type == VGFX_EVENT_KEY_DOWN)
                rt_keyboard_on_vgfx_key_down((int64_t)evt.data.key.key);
            else if (evt.type == VGFX_EVENT_KEY_UP)
                rt_keyboard_on_vgfx_key_up((int64_t)evt.data.key.key);
            else if (evt.type == VGFX_EVENT_TEXT_INPUT)
                rt_keyboard_text_input((int32_t)evt.data.text.codepoint);
            else if (evt.type == VGFX_EVENT_CLOSE) {
                canvas3d_close_window(c);
                break;
            } else if (!captured && evt.type == VGFX_EVENT_MOUSE_MOVE) {
                rt_canvas3d_update_mouse_from_physical(
                    c->gfx_win, evt.data.mouse_move.x, evt.data.mouse_move.y);
            } else if (evt.type == VGFX_EVENT_MOUSE_DOWN) {
                rt_canvas3d_update_mouse_from_physical(
                    c->gfx_win, evt.data.mouse_button.x, evt.data.mouse_button.y);
                rt_mouse_button_down((int64_t)evt.data.mouse_button.button);
            } else if (evt.type == VGFX_EVENT_MOUSE_UP) {
                rt_canvas3d_update_mouse_from_physical(
                    c->gfx_win, evt.data.mouse_button.x, evt.data.mouse_button.y);
                rt_mouse_button_up((int64_t)evt.data.mouse_button.button);
            } else if (evt.type == VGFX_EVENT_RESIZE) {
                rt_canvas3d_apply_resize(c,
                                         evt.data.resize.logical_width,
                                         evt.data.resize.logical_height,
                                         evt.data.resize.width,
                                         evt.data.resize.height);
            } else if (evt.type == VGFX_EVENT_SCROLL) {
                rt_canvas3d_update_mouse_from_physical(
                    c->gfx_win, evt.data.scroll.x, evt.data.scroll.y);
                rt_mouse_update_wheel((double)evt.data.scroll.delta_x,
                                      (double)evt.data.scroll.delta_y);
            }
        }

        if (!c->gfx_win) {
            rt_action_update();
            return 0;
        }

        if (!captured) {
            vgfx_mouse_pos(c->gfx_win, &mx, &my);
            rt_canvas3d_update_mouse_from_logical(mx, my);
        }
    }

    if (use_synthetic)
        canvas3d_apply_synthetic_input(c);

    /* Update action mapping state after input devices and event queues are
     * finalized so action queries observe this frame's input. */
    rt_action_update();

    if (c->clock_source == 1)
        canvas3d_apply_synthetic_clock(c);

    /* Warp cursor to center for next frame (only when captured) */
    if (use_live && captured) {
        int32_t cw, ch;
        vgfx_get_size(c->gfx_win, &cw, &ch);
        vgfx_warp_cursor(c->gfx_win, cw / 2, ch / 2);
    }

    return c->should_close ? 0 : 1;
}

int64_t rt_canvas3d_poll_event(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c || c->event_type_count <= 0)
        return VGFX_EVENT_NONE;
    int64_t type = c->event_type_queue[c->event_type_head];
    c->event_type_head = (c->event_type_head + 1) % RT_CANVAS3D_EVENT_QUEUE_CAPACITY;
    c->event_type_count--;
    return type;
}

/// @brief Check if the canvas window received a close request.
int8_t rt_canvas3d_should_close(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    return c ? c->should_close : 0;
}

/// @brief Enable or disable wireframe rendering mode.
void rt_canvas3d_set_wireframe(void *obj, int8_t enabled) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (c)
        c->wireframe = enabled ? 1 : 0;
}

/// @brief Enable or disable backface culling (CCW winding = front face).
void rt_canvas3d_set_backface_cull(void *obj, int8_t enabled) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (c)
        c->backface_cull = enabled ? 1 : 0;
}

/// @brief Park a `malloc`'d buffer for end-of-frame disposal.
///
/// Used by skinning / morph-target paths that allocate
/// per-draw vertex transforms — the canvas owns the lifetime
/// until after the GPU has consumed the data on `end()`.
int rt_canvas3d_add_temp_buffer(void *obj, void *buffer) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c || !buffer)
        return 0;
    return canvas3d_track_temp_buffer(c, buffer);
}

/// @brief Undo temp-buffer ownership transfer before frame cleanup.
int rt_canvas3d_remove_temp_buffer(void *obj, void *buffer) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c || !buffer)
        return 0;
    return canvas3d_untrack_temp_buffer(c, buffer);
}

/// @brief Park a GC-managed object reference for end-of-frame release.
///
/// Lets a draw call reference an object (mesh, material, pixels)
/// that might otherwise be collected before the deferred queue
/// flushes. The canvas drops the reference in `rt_canvas3d_end`.
int rt_canvas3d_add_temp_object(void *obj, void *value) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c || !value)
        return 0;
    return canvas3d_track_temp_object(c, value);
}

/// @brief Get the current canvas width in pixels (updates on window resize).
int64_t rt_canvas3d_get_width(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return 0;
    int32_t out_w = 0;
    canvas3d_active_output_size(c, &out_w, NULL);
    return out_w;
}

/// @brief Get the current canvas height in pixels (updates on window resize).
int64_t rt_canvas3d_get_height(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return 0;
    int32_t out_h = 0;
    canvas3d_active_output_size(c, NULL, &out_h);
    return out_h;
}

/// @brief Get the backing window's logical width, ignoring any bound render target.
int64_t rt_canvas3d_get_window_width(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    return c ? c->width : 0;
}

/// @brief Get the backing window's logical height, ignoring any bound render target.
int64_t rt_canvas3d_get_window_height(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    return c ? c->height : 0;
}

/// @brief Explicit alias for Width: the active output width, including render targets.
int64_t rt_canvas3d_get_active_output_width(void *obj) {
    return rt_canvas3d_get_width(obj);
}

/// @brief Explicit alias for Height: the active output height, including render targets.
int64_t rt_canvas3d_get_active_output_height(void *obj) {
    return rt_canvas3d_get_height(obj);
}

/// @brief Get the current frames-per-second (updated each Flip call).
int64_t rt_canvas3d_get_fps(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return 0;
    if (c->delta_time_us > 0)
        return (1000000 + c->delta_time_us / 2) / c->delta_time_us;
    return c->delta_time_ms > 0 ? 1000 / c->delta_time_ms : 0;
}

/// @brief Get the time elapsed since the last frame in milliseconds.
/// @details Clamped to dt_max (default 100ms) to prevent physics explosions
///          after long pauses (e.g., window drag, breakpoint, alt-tab).
int64_t rt_canvas3d_get_delta_time(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return 0;
    int64_t dt = c->delta_time_ms;
    if (c->dt_max_ms > 0) {
        if (dt <= 0)
            return 0;
        if (dt > c->dt_max_ms)
            dt = c->dt_max_ms;
    }
    return dt;
}

/// @brief Get the time elapsed since the last frame in seconds.
double rt_canvas3d_get_delta_time_sec(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return 0.0;
    if (c->delta_time_us > 0) {
        int64_t dt_us = c->delta_time_us;
        if (c->dt_max_ms > 0) {
            int64_t max_us = c->dt_max_ms <= INT64_MAX / 1000 ? c->dt_max_ms * 1000 : INT64_MAX;
            if (dt_us > max_us)
                dt_us = max_us;
        }
        return (double)dt_us / 1000000.0;
    }
    return (double)rt_canvas3d_get_delta_time(obj) / 1000.0;
}

/// @brief Cap the per-frame delta-time at `max_ms` milliseconds (prevents huge jumps after pauses).
void rt_canvas3d_set_dt_max(void *obj, int64_t max_ms) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (c)
        c->dt_max_ms = max_ms > 0 ? (max_ms > INT64_MAX / 1000 ? INT64_MAX / 1000 : max_ms) : 0;
}

/// @brief Select live, synthetic, or live+synthetic input.
void rt_canvas3d_set_input_source(void *obj, int64_t mode) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    int32_t next = canvas3d_input_source_from_mode(mode);
    if (next == 0 && c->input_source != 0)
        canvas3d_release_synthetic_input(c);
    else if (next != 0) {
        rt_canvas3d *owner = (rt_canvas3d *)__atomic_load_n(&g_canvas3d_synthetic_owner,
                                                            __ATOMIC_ACQUIRE);
        if (owner && owner != c)
            canvas3d_set_synthetic_owner(c);
    }
    c->input_source = next;
}

/// @brief Queue a synthetic keyboard transition for the next synthetic input frame.
void rt_canvas3d_push_synthetic_key(void *obj, int64_t key, int8_t down) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c || key <= 0 || key >= VIPER_KEY_MAX)
        return;
    if (c->synthetic_key_count >= CANVAS3D_SYNTHETIC_KEY_QUEUE_MAX)
        return;
    int32_t idx = c->synthetic_key_count++;
    c->synthetic_key_keys[idx] = key;
    c->synthetic_key_downs[idx] = down ? 1 : 0;
}

/// @brief Queue a synthetic mouse movement/button/wheel sample.
void rt_canvas3d_push_synthetic_mouse(
    void *obj, double dx, double dy, int64_t buttons, double wheel) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    c->synthetic_mouse_dx = canvas3d_accumulate_synthetic_mouse_delta(c->synthetic_mouse_dx, dx);
    c->synthetic_mouse_dy = canvas3d_accumulate_synthetic_mouse_delta(c->synthetic_mouse_dy, dy);
    c->synthetic_mouse_wheel_y =
        canvas3d_accumulate_synthetic_mouse_delta(c->synthetic_mouse_wheel_y, wheel);
    c->synthetic_mouse_buttons =
        buttons > 0 ? buttons & ((1LL << VIPER_MOUSE_BUTTON_MAX) - 1LL) : 0;
    c->synthetic_mouse_has_buttons = 1;
}

/// @brief Clear queued synthetic input and release keys/buttons held by the synthetic source.
void rt_canvas3d_clear_synthetic_input(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    canvas3d_release_synthetic_input(c);
}

/// @brief Select live wall-clock or fixed synthetic delta-time source.
void rt_canvas3d_set_clock_source(void *obj, int64_t mode) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    c->clock_source = canvas3d_clock_source_from_mode(mode);
    c->frame_timing_updated_by_poll = 0;
    if (c->clock_source == 1)
        canvas3d_apply_synthetic_clock(c);
}

/// @brief Set the fixed synthetic delta time in seconds.
void rt_canvas3d_set_synthetic_delta_time_sec(void *obj, double dt) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    c->synthetic_dt_us = canvas3d_synthetic_seconds_to_us(dt);
    if (c->clock_source == 1)
        canvas3d_apply_synthetic_clock(c);
}

/// @brief Advance one deterministic synthetic input/timing frame.
void rt_canvas3d_advance_synthetic_frame(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_pad_begin_frame();
    canvas3d_apply_synthetic_input(c);
    rt_action_update();
    if (c->clock_source == 1)
        canvas3d_apply_synthetic_clock(c);
}

/// @brief Assign a light to one of the per-canvas light slots.
/// @details Slot index must be in [0, VGFX3D_MAX_LIGHTS). Pass NULL to clear a slot.
void rt_canvas3d_set_light(void *obj, int64_t index, void *light) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (index < 0 || index >= VGFX3D_MAX_LIGHTS) {
        rt_trap("Canvas3D.SetLight: index out of range");
        return;
    }
    if (light && !rt_g3d_has_class(light, RT_G3D_LIGHT3D_CLASS_ID)) {
        rt_trap("Canvas3D.SetLight: light must be Light3D");
        return;
    }
    canvas3d_assign_owned_ref((void **)&c->lights[index], light);
}

/// @brief Clear every retained per-canvas light slot.
void rt_canvas3d_clear_lights(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    for (int32_t i = 0; i < VGFX3D_MAX_LIGHTS; i++)
        canvas3d_release_owned_ref((void **)&c->lights[i]);
}

/// @brief Count active per-canvas light slots.
int64_t rt_canvas3d_get_light_count(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    int64_t count = 0;
    if (!c)
        return 0;
    for (int32_t i = 0; i < VGFX3D_MAX_LIGHTS; i++) {
        if (c->lights[i] && c->lights[i]->enabled)
            count++;
    }
    return count;
}

/// @brief Install a readable key/fill/ambient setup without enabling implicit fallback lighting.
void rt_canvas3d_set_default_lighting(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    void *key_dir;
    void *fill_dir;
    void *key;
    void *fill;
    if (!c)
        return;

    rt_canvas3d_clear_lights(c);
    c->ambient[0] = 0.18f;
    c->ambient[1] = 0.18f;
    c->ambient[2] = 0.20f;
    if (c->shadows_enabled && (!isfinite(c->shadow_bias) || c->shadow_bias <= 0.0f))
        c->shadow_bias = 0.005f;

    key_dir = rt_vec3_new(-0.45, -0.75, -0.48);
    fill_dir = rt_vec3_new(0.65, -0.35, 0.55);
    key = key_dir ? rt_light3d_new_directional(key_dir, 1.0, 0.96, 0.88) : NULL;
    fill = fill_dir ? rt_light3d_new_directional(fill_dir, 0.55, 0.65, 1.0) : NULL;
    rt_light3d_set_intensity(key, 1.35);
    rt_light3d_set_intensity(fill, 0.35);
    canvas3d_assign_owned_ref((void **)&c->lights[0], key);
    canvas3d_assign_owned_ref((void **)&c->lights[1], fill);

    if (key && rt_obj_release_check0(key))
        rt_obj_free(key);
    if (fill && rt_obj_release_check0(fill))
        rt_obj_free(fill);
    if (key_dir && rt_obj_release_check0(key_dir))
        rt_obj_free(key_dir);
    if (fill_dir && rt_obj_release_check0(fill_dir))
        rt_obj_free(fill_dir);
}

/// @brief Set the global ambient light color for the canvas (applied to all surfaces).
void rt_canvas3d_set_ambient(void *obj, double r, double g, double b) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    c->ambient[0] = canvas3d_clamp01_f64(r);
    c->ambient[1] = canvas3d_clamp01_f64(g);
    c->ambient[2] = canvas3d_clamp01_f64(b);
}

/*==========================================================================
 * Debug drawing — transform 3D points to screen via backend VP
 *=========================================================================*/

/*==========================================================================
 * Fog — linear distance fog
 *=========================================================================*/

/// @brief Configure the global fog parameters (color, near/far, density).
///
/// Applied by the postFX stage as a depth-keyed blend toward
/// `fog_color`. Setting `fog_density` to 0 disables fog without
/// changing the queued draws.
void rt_canvas3d_set_fog(
    void *obj, double near_dist, double far_dist, double r, double g, double b) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    float sanitized_near = canvas3d_sanitize_nonnegative_f64(near_dist, 0.0f);
    float sanitized_far = canvas3d_sanitize_nonnegative_f64(far_dist, sanitized_near + 1.0f);
    if (sanitized_far <= sanitized_near + 1e-4f)
        sanitized_far = sanitized_near + 1.0f;
    c->fog_enabled = 1;
    c->fog_near = sanitized_near;
    c->fog_far = sanitized_far;
    c->fog_color[0] = canvas3d_clamp01_f64(r);
    c->fog_color[1] = canvas3d_clamp01_f64(g);
    c->fog_color[2] = canvas3d_clamp01_f64(b);
}

/// @brief Disable distance fog on the canvas.
void rt_canvas3d_clear_fog(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    c->fog_enabled = 0;
}

/*==========================================================================
 * Shadow Mapping
 *=========================================================================*/

/// @brief Enable shadow mapping with the given shadow map resolution.
/// @details Creates a shadow depth buffer and configures directional light shadow
///          casting. The shadow map is rendered from the light's perspective and
///          sampled during the main render pass.
void rt_canvas3d_enable_shadows(void *obj, int64_t resolution) {
    int ok;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (resolution < 64)
        resolution = 64;
    if (resolution > 4096)
        resolution = 4096;
    int32_t res = (int32_t)resolution;
    c->shadows_enabled = 1;
    c->shadow_resolution = res;
    ok = canvas3d_ensure_shadow_targets(c, res);
    if (!ok)
        c->shadows_enabled = 0;
}

/// @brief Disable shadow mapping and free the shadow depth buffer.
void rt_canvas3d_disable_shadows(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    c->shadows_enabled = 0;
    canvas3d_release_shadow_targets(c);
}

/// @brief Set the shadow map depth bias to reduce shadow acne artifacts.
void rt_canvas3d_set_shadow_bias(void *obj, double bias) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    c->shadow_bias = canvas3d_sanitize_nonnegative_f64(bias, 0.005f);
}

/// @brief Enable or disable coarse CPU frustum culling for draw submission.
void rt_canvas3d_set_frustum_culling(void *obj, int8_t enabled) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    c->occlusion_culling = enabled ? 1 : 0;
}

/// @brief Backwards-compatible alias for rt_canvas3d_set_frustum_culling().
void rt_canvas3d_set_occlusion_culling(void *obj, int8_t enabled) {
    rt_canvas3d_set_frustum_culling(obj, enabled);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
