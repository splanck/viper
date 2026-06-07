//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d.c
// Purpose: Implements the Viper.Game3D ergonomic layer declared in rt_game3d.h.
//   Composes the lower-level Graphics3D canvas/camera/scene, Physics3D world,
//   input, spatial audio, and post-FX subsystems into a single World3D plus
//   batteries-included entities, prefabs, presets, and camera controllers.
//
// Key invariants:
//   - All public handles are GC-managed runtime objects allocated via
//     rt_obj_new_i64 with a class id from rt_graphics3d_ids.h; helpers
//     game3d_assign_ref/game3d_release_ref keep owned-slot refcounts balanced.
//   - Scalar inputs are sanitized at the API boundary (game3d_finite_or,
//     game3d_clamp*, game3d_clamp_dt) so NaN/Inf never reach math or physics.
//   - Run-loop callbacks are validated as native executable pointers before
//     being called (game3d_callback_pointer_is_native), trapping otherwise.
//   - Angles are degrees, distances world units, time seconds, colors 0.0–1.0.
//
// Ownership/Lifetime:
//   - World3D owns and retains its canvas/camera/scene/physics/input/audio/
//     effects and its entity list; destroy/finalize releases every owned slot.
//   - Entities retain their node/mesh/material/body/anim and child entities.
//   - The process-wide model cache (g_game3d_model_cache) retains loaded model
//     templates until rt_game3d_assets_clear_cache drops them.
//
// Links: rt_game3d.h, rt_graphics3d_ids.h, render/rt_canvas3d.h,
//   physics/rt_physics3d.h, scene/rt_scene3d.h, render/rt_camera3d.c
//
//===----------------------------------------------------------------------===//

#if !defined(_WIN32)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include "rt_game3d.h"

#include "rt_animcontroller3d.h"
#include "rt_asset.h"
#include "rt_audio.h"
#include "rt_box.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_collider3d.h"
#include "rt_decal3d.h"
#include "rt_g3d_commit_queue.h"
#include "rt_gltf.h"
#include "rt_graphics3d_ids.h"
#include "rt_input.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_mat4.h"
#include "rt_model3d.h"
#include "rt_navmesh3d.h"
#include "rt_object.h"
#include "rt_parallel.h"
#include "rt_particles3d.h"
#include "rt_physics3d.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_platform.h"
#include "rt_postfx3d.h"
#include "rt_quat.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_seq.h"
#include "rt_sound3d.h"
#include "rt_soundlistener3d.h"
#include "rt_soundsource3d.h"
#include "rt_string.h"
#include "rt_terrain3d.h"
#include "rt_textureasset3d.h"
#include "rt_threadpool.h"
#include "rt_trap.h"
#include "rt_vec2.h"
#include "rt_vec3.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <unistd.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

extern void rt_trap_set_recovery(jmp_buf *buf);
extern void rt_trap_clear_recovery(void);
extern const char *rt_trap_get_error(void);

#include "rt_game3d_internal.h"

rt_game3d_entity *game3d_world_find_entity_by_body(rt_game3d_world *world, void *body);
static void game3d_world_rebuild_name_index(rt_game3d_world *world);
void game3d_audio_prune_sources(rt_game3d_audio *audio);

/// @brief Per-frame update callback signature: receives the frame delta in seconds.
typedef void (*rt_game3d_update_fn)(double dt);
/// @brief 2D overlay callback signature: invoked once per frame to draw HUD/UI.
typedef void (*rt_game3d_overlay_fn)(void);

// Process-wide model cache shared by all Assets3D loads, growable on demand.
static rt_game3d_model_cache_entry *g_game3d_model_cache = NULL;
static int32_t g_game3d_model_cache_count = 0;
static int32_t g_game3d_model_cache_capacity = 0;
static uint64_t g_game3d_model_cache_tick = 0;
static uint64_t g_game3d_model_cache_generation = 1;
static uint64_t g_game3d_model_resident_bytes = 0;
static uint64_t g_game3d_model_residency_budget_bytes = UINT64_MAX;
#define RT_GAME3D_MODEL_CACHE_MAX_ENTRIES 64
#define RT_GAME3D_MODEL_CACHE_KEY_MAX 4096
#define RT_GAME3D_MODEL_CACHE_WAIT_TIMEOUT_MS 5000u

// Process-wide async asset workers and main-thread commit queue. Workers stage
// model bytes/requests; the queue builds runtime objects and publishes handle
// results on the main thread.
static void *g_game3d_asset_async_pool = NULL;
static void *g_game3d_asset_commit_queue = NULL;
#define RT_GAME3D_ASSET_COMMIT_DRAIN_BUDGET 8
#define RT_GAME3D_ASSET_UPLOAD_BUDGET_DEFAULT_BYTES (16ull * 1024ull * 1024ull)
#define RT_GAME3D_ASSET_UPLOAD_SLICE_BYTES (64ull * 1024ull)
#define RT_GAME3D_ASSET_ASYNC_ROOT_BYTES_MAX (256ull * 1024ull * 1024ull)
#define RT_GAME3D_STREAM_MANIFEST_MAX_BYTES (16ull * 1024ull * 1024ull)
#if defined(__clang__) || defined(__GNUC__)
#define GAME3D_UNUSED_PRIVATE __attribute__((unused))
#else
#define GAME3D_UNUSED_PRIVATE
#endif
/* Animator3D updates can allocate/release runtime strings while sampling events and root motion.
 * Keep those updates on the main thread until those runtime paths are explicitly audited as
 * worker-thread safe. */
#define RT_GAME3D_PARALLEL_ANIMATOR_UPDATES 0
static volatile uint64_t g_game3d_asset_upload_budget_bytes =
    RT_GAME3D_ASSET_UPLOAD_BUDGET_DEFAULT_BYTES;

#if defined(_WIN32)
static INIT_ONCE g_game3d_model_cache_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_game3d_model_cache_lock;
static CONDITION_VARIABLE g_game3d_model_cache_cv;

/// @brief One-time initializer (run via InitOnceExecuteOnce) for the cache critical section.
static BOOL CALLBACK game3d_model_cache_init_once(PINIT_ONCE once,
                                                  PVOID parameter,
                                                  PVOID *context) {
    (void)once;
    (void)parameter;
    (void)context;
    InitializeCriticalSection(&g_game3d_model_cache_lock);
    InitializeConditionVariable(&g_game3d_model_cache_cv);
    return TRUE;
}

/// @brief Lazily initialize then acquire the process-wide model-cache lock.
static void game3d_model_cache_lock(void) {
    InitOnceExecuteOnce(&g_game3d_model_cache_once, game3d_model_cache_init_once, NULL, NULL);
    EnterCriticalSection(&g_game3d_model_cache_lock);
}

/// @brief Release the process-wide model-cache lock.
static void game3d_model_cache_unlock(void) {
    LeaveCriticalSection(&g_game3d_model_cache_lock);
}

static int game3d_model_cache_wait_locked_ms(uint32_t timeout_ms) {
    BOOL ok = SleepConditionVariableCS(&g_game3d_model_cache_cv,
                                       &g_game3d_model_cache_lock,
                                       timeout_ms == 0u ? 1u : (DWORD)timeout_ms);
    return ok ? 1 : 0;
}

/// @brief Wake all threads waiting on the model-cache condition variable.
static void game3d_model_cache_notify_all(void) {
    WakeAllConditionVariable(&g_game3d_model_cache_cv);
}
#else
static pthread_mutex_t g_game3d_model_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_game3d_model_cache_cv;
static pthread_once_t g_game3d_model_cache_once = PTHREAD_ONCE_INIT;

static void game3d_model_cache_init_once(void) {
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) == 0) {
#if defined(CLOCK_MONOTONIC) && !defined(__APPLE__)
        (void)pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
        (void)pthread_cond_init(&g_game3d_model_cache_cv, &attr);
        (void)pthread_condattr_destroy(&attr);
    } else {
        (void)pthread_cond_init(&g_game3d_model_cache_cv, NULL);
    }
}

/// @brief Acquire the process-wide model-cache lock (statically initialized mutex).
static void game3d_model_cache_lock(void) {
    pthread_once(&g_game3d_model_cache_once, game3d_model_cache_init_once);
    pthread_mutex_lock(&g_game3d_model_cache_lock);
}

/// @brief Release the process-wide model-cache lock.
static void game3d_model_cache_unlock(void) {
    pthread_mutex_unlock(&g_game3d_model_cache_lock);
}

static int game3d_model_cache_wait_locked_ms(uint32_t timeout_ms) {
    struct timespec deadline;
    uint32_t wait_ms = timeout_ms == 0u ? 1u : timeout_ms;
#if defined(__APPLE__)
    deadline.tv_sec = (time_t)(wait_ms / 1000u);
    deadline.tv_nsec = (long)((uint64_t)(wait_ms % 1000u) * 1000000ull);
    return pthread_cond_timedwait_relative_np(
               &g_game3d_model_cache_cv, &g_game3d_model_cache_lock, &deadline) == 0;
#else
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
        return 0;
#else
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return 0;
#endif
    uint64_t nsec;
    nsec = (uint64_t)deadline.tv_nsec + (uint64_t)wait_ms * 1000000ull;
    deadline.tv_sec += (time_t)(nsec / 1000000000ull);
    deadline.tv_nsec = (long)(nsec % 1000000000ull);
    return pthread_cond_timedwait(
               &g_game3d_model_cache_cv, &g_game3d_model_cache_lock, &deadline) == 0;
#endif
}

/// @brief Wake all threads waiting on the model-cache condition variable.
static void game3d_model_cache_notify_all(void) {
    pthread_once(&g_game3d_model_cache_once, game3d_model_cache_init_once);
    pthread_cond_broadcast(&g_game3d_model_cache_cv);
}
#endif

/// @brief True if `callback` looks like a genuine native code pointer (not a GC
///   heap payload or tagged value), so it is safe to call as a frontend function.
/// @details Frontends hand raw function pointers to the run-loop entry points. A
///   mis-passed managed object would otherwise be jumped into as code; this guard
///   rejects GC payloads and, where the platform exposes mapping metadata, verifies
///   that the target page is executable. NULL is treated as "native" (no-op).
static int game3d_callback_pointer_is_native(void *callback) {
    if (!callback)
        return 1;
    if (rt_heap_is_payload(callback))
        return 0;
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(callback, &info, sizeof(info)) == 0)
        return 0;
    DWORD protect = info.Protect & 0xffu;
    return protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
#elif defined(__APPLE__)
    mach_vm_address_t region = (mach_vm_address_t)(uintptr_t)callback;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info = {0};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    if (mach_vm_region(mach_task_self(),
                       &region,
                       &size,
                       VM_REGION_BASIC_INFO_64,
                       (vm_region_info_t)&info,
                       &count,
                       &object) != KERN_SUCCESS)
        return 0;
    if (object != MACH_PORT_NULL)
        mach_port_deallocate(mach_task_self(), object);
    return (info.protection & VM_PROT_EXECUTE) != 0;
#elif defined(__linux__)
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[512];
    uintptr_t needle = (uintptr_t)callback;
    if (!maps)
        return 0;
    while (fgets(line, sizeof(line), maps)) {
        unsigned long start = 0;
        unsigned long end = 0;
        char perms[5] = {0, 0, 0, 0, 0};
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3 && needle >= (uintptr_t)start &&
            needle < (uintptr_t)end) {
            fclose(maps);
            return strchr(perms, 'x') != NULL;
        }
    }
    fclose(maps);
    return 0;
#else
    return 1;
#endif
}

/// @brief Validate and cast a raw pointer to an update callback, trapping `method` if
///   the pointer is non-native; returns NULL for a NULL callback.
static rt_game3d_update_fn game3d_update_callback_checked(void *callback, const char *method) {
    if (!callback)
        return NULL;
    if (!game3d_callback_pointer_is_native(callback)) {
        rt_trap(method);
        return NULL;
    }
    return (rt_game3d_update_fn)callback;
}

/// @brief Validate and cast a raw pointer to an overlay callback, trapping `method` if
///   the pointer is non-native; returns NULL for a NULL callback.
static rt_game3d_overlay_fn game3d_overlay_callback_checked(void *callback, const char *method) {
    if (!callback)
        return NULL;
    if (!game3d_callback_pointer_is_native(callback)) {
        rt_trap(method);
        return NULL;
    }
    return (rt_game3d_overlay_fn)callback;
}

/// @brief Clamp a requested worker-thread count to the valid range [1, RT_GAME3D_MAX_WORKERS].
static int64_t game3d_clamp_worker_count(int64_t worker_count) {
    if (worker_count < 1)
        return 1;
    if (worker_count > RT_GAME3D_MAX_WORKERS)
        return RT_GAME3D_MAX_WORKERS;
    return worker_count;
}

/// @brief Default worker-thread count derived from the platform's parallelism, clamped to range.
static int64_t game3d_default_worker_count(void) {
    return game3d_clamp_worker_count(rt_parallel_default_workers());
}

/// @brief Restore the canvas input/clock settings temporarily replaced by runFrames.
static void game3d_world_restore_run_frames_canvas(void *canvas_obj,
                                                   int32_t input_source,
                                                   int32_t clock_source,
                                                   int64_t synthetic_dt_us) {
    if (!canvas_obj)
        return;
    rt_canvas3d_set_input_source(canvas_obj, input_source);
    rt_canvas3d_set_synthetic_delta_time_sec(canvas_obj, (double)synthetic_dt_us / 1000000.0);
    rt_canvas3d_set_clock_source(canvas_obj, clock_source);
}

/// @brief Release the object held in `*slot`, free it if its refcount hits zero,
///   and clear the slot to NULL. Safe on NULL slot or empty slot.
void game3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    void *obj = *slot;
    *slot = NULL;
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Retain `value` then release the previous occupant of `*slot` and store it.
/// @details Retain-before-release order makes self-assignment safe; no-ops when the
///   slot already holds `value`. Keeps owned-slot refcounts balanced.
void game3d_assign_ref(void **slot, void *value) {
    if (!slot || *slot == value)
        return;
    rt_obj_retain_maybe(value);
    game3d_release_ref(slot);
    *slot = value;
}

/// @brief Release an owned slot only when its current object still has the expected class.
/// @details Wrong-class private state is treated as corruption and cleared without touching the
///          referenced object's refcount, avoiding accidental release of an unrelated handle that
///          this slot cannot prove it retained.
void game3d_release_typed_ref(void **slot, int64_t class_id) {
    void *obj;
    if (!slot || !*slot)
        return;
    obj = *slot;
    *slot = NULL;
    if (rt_g3d_has_class(obj, class_id) && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Retain a typed value, release the previous typed occupant, and store it.
void game3d_assign_typed_ref(void **slot, void *value, int64_t class_id) {
    if (!slot || *slot == value)
        return;
    if (value && !rt_g3d_has_class(value, class_id))
        return;
    rt_obj_retain_maybe(value);
    game3d_release_typed_ref(slot, class_id);
    *slot = value;
}

/// @brief Reinterpret a task function pointer as a void* for storage in the job system.
/// @details Isolates the (technically implementation-defined) function-to-object pointer cast to
/// one
///          documented spot.
static void *game3d_task_fnptr(void (*fn)(void *)) {
    void *ptr;
    _Static_assert(sizeof(ptr) == sizeof(fn),
                   "Game3D task callback bridge requires equal function/data pointer sizes");
    memcpy(&ptr, &fn, sizeof(ptr));
    return ptr;
}

/// @brief Release the world's parallel job/worker pool and clear its handle.
static void game3d_world_release_job_pool(rt_game3d_world *world) {
    if (!world || !world->job_pool)
        return;
    void *pool = world->job_pool;
    world->job_pool = NULL;
    rt_threadpool_shutdown(pool);
    game3d_release_ref(&pool);
}

/// @brief Lazily create the world's job pool sized to its worker count.
/// @return 1 if a usable pool exists, 0 on allocation failure.
static int game3d_world_ensure_job_pool(rt_game3d_world *world) {
    if (!world || world->worker_count <= 1)
        return 0;
    if (world->job_pool) {
        if (!rt_threadpool_get_is_shutdown(world->job_pool) &&
            rt_threadpool_get_size(world->job_pool) == world->worker_count)
            return 1;
        game3d_world_release_job_pool(world);
    }
    world->job_pool = rt_threadpool_new(world->worker_count);
    if (!world->job_pool) {
        world->jobs_enabled = 0;
        return 0;
    }
    world->jobs_enabled = 1;
    return 1;
}

/// @brief Entity count clamped to the allocated registry capacity.
static int32_t game3d_world_safe_entity_count(const rt_game3d_world *world) {
    if (!world || !world->entities || world->entity_capacity <= 0 || world->entity_count <= 0)
        return 0;
    if (world->entity_count > world->entity_capacity)
        return world->entity_capacity;
    return world->entity_count;
}

/// @brief Clamp a mutable world registry count back to its allocated entity array.
static int32_t game3d_world_repair_entity_count(rt_game3d_world *world) {
    int32_t safe_count = game3d_world_safe_entity_count(world);
    if (world)
        world->entity_count = safe_count;
    return safe_count;
}

/// @brief True if `layer` is a single power-of-two bit (a valid individual layer).
int8_t game3d_valid_layer(int64_t layer) {
    return layer > 0 && (layer & (layer - 1)) == 0 ? 1 : 0;
}

/// @brief Coerce user mask bitfields; any negative input means "all layers".
static int64_t game3d_sanitize_mask_bits(int64_t bits) {
    return bits < 0 ? ~(int64_t)0 : bits;
}

/// @brief Return `value` when finite, else `fallback`. Scalar boundary sanitizer.
double game3d_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Return a finite value clamped to +/- abs_max, or fallback when invalid.
double game3d_clamp_abs_or(double value, double fallback, double abs_max) {
    fallback = game3d_finite_or(fallback, 0.0);
    abs_max = fabs(game3d_finite_or(abs_max, 0.0));
    if (abs_max <= 0.0)
        return fallback;
    value = game3d_finite_or(value, fallback);
    if (value < -abs_max)
        return -abs_max;
    if (value > abs_max)
        return abs_max;
    return value;
}

/// @brief Sanitize world coordinates before they enter scene, physics, or camera math.
double game3d_clamp_coord_or(double value, double fallback) {
    return game3d_clamp_abs_or(value, fallback, RT_GAME3D_COORD_ABS_MAX);
}

/// @brief Sanitize scale lanes: finite, non-zero, and capped to a stable range.
double game3d_scale_or_unit(double value) {
    value = game3d_clamp_abs_or(value, 1.0, RT_GAME3D_SCALE_ABS_MAX);
    return fabs(value) <= 1e-12 ? 1.0 : value;
}

/// @brief Sanitize a frame delta: non-finite/≤0 becomes the default, large spikes are
///   capped at RT_GAME3D_MAX_DT so a stall cannot tunnel physics or fling the camera.
double game3d_clamp_dt(double dt) {
    dt = game3d_finite_or(dt, RT_GAME3D_DEFAULT_DT);
    if (dt <= 0.0)
        return RT_GAME3D_DEFAULT_DT;
    if (dt > RT_GAME3D_MAX_DT)
        return RT_GAME3D_MAX_DT;
    return dt;
}

/// @brief Return `value` if finite and ≥ 0, else `fallback`. For non-negative knobs.
double game3d_nonnegative_or(double value, double fallback) {
    fallback = game3d_finite_or(fallback, 0.0);
    value = game3d_finite_or(value, fallback);
    return value < 0.0 ? fallback : value;
}

/// @brief Return a non-negative finite value capped at max_value; invalid/negative uses fallback.
double game3d_nonnegative_clamped_or(double value, double fallback, double max_value) {
    value = game3d_nonnegative_or(value, fallback);
    max_value = game3d_nonnegative_or(max_value, 0.0);
    return max_value > 0.0 && value > max_value ? max_value : value;
}

/// @brief Sanitize a floating-origin rebase distance, substituting the default for non-positive
/// input.
static double game3d_rebase_threshold_or_default(double meters) {
    meters = game3d_finite_or(meters, RT_GAME3D_DEFAULT_REBASE_THRESHOLD);
    if (meters < RT_GAME3D_MIN_REBASE_THRESHOLD)
        return RT_GAME3D_MIN_REBASE_THRESHOLD;
    return meters;
}

/// @brief Translate a physics body's position by the floating-origin rebase @p delta.
static void game3d_shift_body_position(void *body, const double delta[3]) {
    if (!rt_g3d_has_class(body, RT_G3D_BODY3D_CLASS_ID) || !delta)
        return;
    double position[3];
    rt_body3d_get_pose_raw(body, position, NULL, NULL);
    rt_body3d_set_position(body,
                           game3d_clamp_coord_or(position[0] - delta[0], 0.0),
                           game3d_clamp_coord_or(position[1] - delta[1], 0.0),
                           game3d_clamp_coord_or(position[2] - delta[2], 0.0));
}

/// @brief Shift a world's particle/decal effects by the floating-origin rebase @p delta.
static void game3d_effects_rebase_origin(void *effects_obj, const double delta[3]) {
    rt_game3d_effects *effects =
        (rt_game3d_effects *)rt_g3d_checked_or_null(effects_obj, RT_G3D_GAME3D_EFFECTS_CLASS_ID);
    if (!effects || !delta)
        return;
    game3d_effects_repair(effects);
    double clean_delta[3] = {
        game3d_clamp_coord_or(delta[0], 0.0),
        game3d_clamp_coord_or(delta[1], 0.0),
        game3d_clamp_coord_or(delta[2], 0.0),
    };
    for (int32_t i = 0; i < effects->count; ++i) {
        rt_game3d_effect_item *item = &effects->items[i];
        if (!item || !item->object)
            continue;
        if (item->type == RT_GAME3D_EFFECT_PARTICLES &&
            rt_g3d_has_class(item->object, RT_G3D_PARTICLES3D_CLASS_ID))
            rt_particles3d_rebase_origin(
                item->object, clean_delta[0], clean_delta[1], clean_delta[2]);
        else if (item->type == RT_GAME3D_EFFECT_DECAL &&
                 rt_g3d_has_class(item->object, RT_G3D_DECAL3D_CLASS_ID))
            rt_decal3d_rebase_origin(item->object, clean_delta[0], clean_delta[1], clean_delta[2]);
    }
}

/// @brief Shift WorldStream3D focus/manifest centers by a floating-origin delta.
/// @details Scene and physics systems rebase resident runtime objects separately.
///   This helper updates the stream's persisted local-space metadata so future
///   load/unload distance tests and terrain draw positions stay in the same
///   coordinate frame after the world origin moves.
static void game3d_world_stream_rebase_origin(rt_game3d_world_stream *stream,
                                              const double delta[3]) {
    if (!stream || !delta)
        return;
    double clean_delta[3] = {
        game3d_clamp_coord_or(delta[0], 0.0),
        game3d_clamp_coord_or(delta[1], 0.0),
        game3d_clamp_coord_or(delta[2], 0.0),
    };
    stream->center[0] = game3d_clamp_coord_or(stream->center[0] - clean_delta[0], 0.0);
    stream->center[1] = game3d_clamp_coord_or(stream->center[1] - clean_delta[1], 0.0);
    stream->center[2] = game3d_clamp_coord_or(stream->center[2] - clean_delta[2], 0.0);

    int32_t cell_count = 0;
    if (stream->cells && stream->cell_count > 0 && stream->cell_capacity > 0)
        cell_count =
            stream->cell_count < stream->cell_capacity ? stream->cell_count : stream->cell_capacity;
    for (int32_t i = 0; i < cell_count; ++i) {
        rt_game3d_stream_cell *cell = &stream->cells[i];
        cell->center[0] = game3d_clamp_coord_or(cell->center[0] - clean_delta[0], 0.0);
        cell->center[1] = game3d_clamp_coord_or(cell->center[1] - clean_delta[1], 0.0);
        cell->center[2] = game3d_clamp_coord_or(cell->center[2] - clean_delta[2], 0.0);
    }

    int32_t tile_count = 0;
    if (stream->terrain_tiles && stream->terrain_tile_count > 0 &&
        stream->terrain_tile_capacity > 0)
        tile_count = stream->terrain_tile_count < stream->terrain_tile_capacity
                         ? stream->terrain_tile_count
                         : stream->terrain_tile_capacity;
    for (int32_t i = 0; i < tile_count; ++i) {
        rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
        tile->center[0] = game3d_clamp_coord_or(tile->center[0] - clean_delta[0], 0.0);
        tile->center[1] = game3d_clamp_coord_or(tile->center[1] - clean_delta[1], 0.0);
        tile->center[2] = game3d_clamp_coord_or(tile->center[2] - clean_delta[2], 0.0);
    }
}

/// @brief Mark that the world must reach a safe boundary (e.g. end of step) before its next rebase.
static void game3d_world_require_rebase_boundary(rt_game3d_world *world) {
    if (!world || !world->canvas)
        return;
    rt_canvas3d *canvas = rt_canvas3d_checked_or_stack(world->canvas);
    if (canvas && canvas->in_frame)
        rt_trap("Game3D.World3D.rebaseOrigin: must be called between frames");
}

/// @brief Apply a floating-origin shift of @p delta across all of the world's subsystems.
/// @details Translates bodies, scene nodes, effects, the camera, and cached origins together so the
///          recenter is invisible to gameplay while restoring float precision near the camera.
static void game3d_world_apply_origin_rebase(rt_game3d_world *world, const double delta[3]) {
    if (!world || !delta)
        return;
    double clean_delta[3] = {
        game3d_clamp_coord_or(delta[0], 0.0),
        game3d_clamp_coord_or(delta[1], 0.0),
        game3d_clamp_coord_or(delta[2], 0.0),
    };
    if (clean_delta[0] == 0.0 && clean_delta[1] == 0.0 && clean_delta[2] == 0.0)
        return;
    game3d_world_require_rebase_boundary(world);
    void *scene = rt_g3d_checked_or_null(world->scene, RT_G3D_SCENE3D_CLASS_ID);
    void *physics = rt_g3d_checked_or_null(world->physics, RT_G3D_WORLD3D_CLASS_ID);
    void *camera = rt_g3d_checked_or_null(world->camera, RT_G3D_CAMERA3D_CLASS_ID);
    rt_game3d_audio *audio =
        (rt_game3d_audio *)rt_g3d_checked_or_null(world->audio, RT_G3D_GAME3D_SOUND_CLASS_ID);
    const int scene_rebased = scene != NULL;
    const int physics_rebased = physics != NULL;
    world->world_origin[0] =
        game3d_clamp_coord_or(world->world_origin[0] + clean_delta[0], world->world_origin[0]);
    world->world_origin[1] =
        game3d_clamp_coord_or(world->world_origin[1] + clean_delta[1], world->world_origin[1]);
    world->world_origin[2] =
        game3d_clamp_coord_or(world->world_origin[2] + clean_delta[2], world->world_origin[2]);

    if (scene_rebased)
        rt_scene3d_rebase_origin(scene, clean_delta[0], clean_delta[1], clean_delta[2]);
    if (physics_rebased)
        rt_world3d_rebase_origin(physics, clean_delta[0], clean_delta[1], clean_delta[2]);
    game3d_effects_rebase_origin(world->effects, clean_delta);
    game3d_world_stream_rebase_origin((rt_game3d_world_stream *)rt_g3d_checked_or_null(
                                          world->stream, RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID),
                                      clean_delta);

    int32_t entity_count = game3d_world_safe_entity_count(world);
    for (int32_t i = 0; i < entity_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        if (!entity || entity->destroyed)
            continue;
        void *node = game3d_entity_node_ref(entity);
        void *body = game3d_entity_body_ref(entity);
        if (!scene_rebased && !entity->parent && node) {
            void *pos = rt_scene_node3d_get_position(node);
            if (pos) {
                rt_scene_node3d_set_position(
                    node,
                    game3d_clamp_coord_or(rt_vec3_x(pos) - clean_delta[0], 0.0),
                    game3d_clamp_coord_or(rt_vec3_y(pos) - clean_delta[1], 0.0),
                    game3d_clamp_coord_or(rt_vec3_z(pos) - clean_delta[2], 0.0));
                game3d_release_ref(&pos);
            }
        }
        if (body && (!physics_rebased || !rt_world3d_contains_body(physics, body)))
            game3d_shift_body_position(body, clean_delta);
    }

    if (camera) {
        double camera_pos[3];
        if (rt_camera3d_get_position_components(
                camera, &camera_pos[0], &camera_pos[1], &camera_pos[2])) {
            void *shifted = rt_vec3_new(game3d_clamp_coord_or(camera_pos[0] - clean_delta[0], 0.0),
                                        game3d_clamp_coord_or(camera_pos[1] - clean_delta[1], 0.0),
                                        game3d_clamp_coord_or(camera_pos[2] - clean_delta[2], 0.0));
            if (shifted) {
                rt_camera3d_set_position(camera, shifted);
                game3d_release_ref(&shifted);
            }
        }
    }
    {
        rt_canvas3d *canvas = rt_canvas3d_checked_or_stack(world->canvas);
        if (canvas) {
            canvas3d_clear_motion_history(canvas);
            canvas3d_clear_occlusion_history(canvas);
            canvas->occlusion_state_valid = 0;
        }
    }
    if (audio) {
        void *listener = rt_g3d_checked_or_null(audio->listener, RT_G3D_SOUNDLISTENER3D_CLASS_ID);
        if (listener) {
            void *listener_pos = rt_soundlistener3d_get_position(listener);
            if (listener_pos) {
                rt_soundlistener3d_set_position_vec(
                    listener,
                    game3d_clamp_coord_or(rt_vec3_x(listener_pos) - clean_delta[0], 0.0),
                    game3d_clamp_coord_or(rt_vec3_y(listener_pos) - clean_delta[1], 0.0),
                    game3d_clamp_coord_or(rt_vec3_z(listener_pos) - clean_delta[2], 0.0));
                game3d_release_ref(&listener_pos);
            }
        }
    }
}

/// @brief Recenter the world's origin if the camera has drifted past the rebase threshold.
/// @details Computes the camera offset, and when it exceeds the threshold (and a boundary is
/// reached)
///          applies an origin rebase by that delta so coordinates stay within float-precise range.
static void game3d_world_rebase_if_needed(rt_game3d_world *world) {
    void *camera = NULL;
    if (!world || !world->floating_origin)
        return;
    camera = rt_g3d_checked_or_null(world->camera, RT_G3D_CAMERA3D_CLASS_ID);
    if (!camera)
        return;
    double eye[3] = {0.0, 0.0, 0.0};
    if (!rt_camera3d_get_position_components(camera, &eye[0], &eye[1], &eye[2]))
        return;
    eye[0] = game3d_clamp_coord_or(eye[0], 0.0);
    eye[1] = game3d_clamp_coord_or(eye[1], 0.0);
    eye[2] = game3d_clamp_coord_or(eye[2], 0.0);
    const double threshold = game3d_rebase_threshold_or_default(world->origin_rebase_threshold);
    const double max_abs = fmax(fabs(eye[0]), fmax(fabs(eye[1]), fabs(eye[2])));
    double dist = 0.0;
    if (max_abs > 0.0) {
        const double sx = eye[0] / max_abs;
        const double sy = eye[1] / max_abs;
        const double sz = eye[2] / max_abs;
        dist = max_abs * sqrt(sx * sx + sy * sy + sz * sz);
    }
    if (!isfinite(dist) || dist < threshold)
        return;
    game3d_world_apply_origin_rebase(world, eye);
}

/// @brief Normalize a 3D input axis so combined directions do not move faster.
void game3d_normalize_axis3(double *x, double *y, double *z) {
    double vx = game3d_finite_or(x ? *x : 0.0, 0.0);
    double vy = game3d_finite_or(y ? *y : 0.0, 0.0);
    double vz = game3d_finite_or(z ? *z : 0.0, 0.0);
    double max_abs = fmax(fabs(vx), fmax(fabs(vy), fabs(vz)));
    double len = 0.0;
    if (!isfinite(max_abs)) {
        vx = 0.0;
        vy = 0.0;
        vz = 0.0;
    } else if (max_abs > 0.0) {
        double sx = vx / max_abs;
        double sy = vy / max_abs;
        double sz = vz / max_abs;
        len = max_abs * sqrt(sx * sx + sy * sy + sz * sz);
        if (isfinite(len) && len > 1.0) {
            vx /= len;
            vy /= len;
            vz /= len;
        }
    }
    if (x)
        *x = vx;
    if (y)
        *y = vy;
    if (z)
        *z = vz;
}

/// @brief Clamp `value` into [lo, hi]; non-finite input falls back to `lo`.
double game3d_clamp(double value, double lo, double hi) {
    if (!isfinite(lo))
        lo = 0.0;
    if (!isfinite(hi))
        hi = lo;
    if (hi < lo) {
        double tmp = lo;
        lo = hi;
        hi = tmp;
    }
    value = game3d_finite_or(value, lo);
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/// @brief Clamp an integer `value` into [lo, hi].
int64_t game3d_clamp_i64(int64_t value, int64_t lo, int64_t hi) {
    if (hi < lo) {
        int64_t tmp = lo;
        lo = hi;
        hi = tmp;
    }
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/// @brief Read a Vec3 into `out[3]` with per-lane NaN/Inf scrubbed and finite extremes
///   capped to the Game3D coordinate range; traps `method` and returns 0 when `vec` is not
///   a Vec3, otherwise returns 1.
int8_t game3d_read_vec3(void *vec, double *out, const char *method) {
    if (!out)
        return 0;
    if (!rt_g3d_is_vec3(vec)) {
        if (method)
            rt_trap(method);
        out[0] = 0.0;
        out[1] = 0.0;
        out[2] = 0.0;
        return 0;
    }
    out[0] = game3d_clamp_coord_or(rt_vec3_x(vec), 0.0);
    out[1] = game3d_clamp_coord_or(rt_vec3_y(vec), 0.0);
    out[2] = game3d_clamp_coord_or(rt_vec3_z(vec), 0.0);
    return 1;
}

/// @brief Compute a doubled int32 capacity for an array while guarding integer
///   and byte-size overflow before the caller reaches realloc().
static int game3d_compute_capacity(
    int32_t current, int32_t needed, int32_t initial, size_t elem_size, int32_t *out_capacity) {
    int32_t capacity;
    if (!out_capacity || current < 0 || needed < 0 || initial <= 0 || elem_size == 0)
        return 0;
    if (current >= needed) {
        *out_capacity = current;
        return 1;
    }
    capacity = current > 0 ? current : initial;
    while (capacity < needed) {
        if (capacity > INT32_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    if (capacity < needed || (size_t)capacity > SIZE_MAX / elem_size)
        return 0;
    *out_capacity = capacity;
    return 1;
}

/// @brief Normalize the audio source array's count/capacity invariants (clamp a
///   negative capacity to zero, reset the count when the backing array is absent,
///   and bound the live count by the capacity). Defensive; returns void.
void game3d_audio_repair_sources(rt_game3d_audio *audio) {
    if (!audio)
        return;
    if (audio->source_capacity < 0)
        audio->source_capacity = 0;
    if (!audio->sources || audio->source_capacity == 0) {
        audio->source_count = 0;
        audio->source_capacity = audio->sources ? audio->source_capacity : 0;
        return;
    }
    if (audio->source_count < 0)
        audio->source_count = 0;
    if (audio->source_count > audio->source_capacity)
        audio->source_count = audio->source_capacity;
    int32_t write = 0;
    for (int32_t read = 0; read < audio->source_count; ++read) {
        void *source = audio->sources[read];
        if (rt_g3d_has_class(source, RT_G3D_SOUNDSOURCE3D_CLASS_ID)) {
            audio->sources[write++] = source;
        } else {
            audio->sources[read] = NULL;
        }
    }
    int32_t kept = write;
    while (write < audio->source_count)
        audio->sources[write++] = NULL;
    audio->source_count = kept;
}

/// @brief Ensure the audio source array can hold `needed` entries, doubling capacity
///   as needed; traps and returns 0 on allocation failure, else 1.
int game3d_audio_reserve_sources(rt_game3d_audio *audio, int32_t needed) {
    int32_t new_capacity;
    if (!audio)
        return 0;
    game3d_audio_repair_sources(audio);
    if (needed <= audio->source_capacity)
        return 1;
    if (!game3d_compute_capacity(
            audio->source_capacity, needed, 8, sizeof(void *), &new_capacity)) {
        rt_trap("Game3D.Sound3D: too many sources");
        return 0;
    }
    void **new_sources = (void **)realloc(audio->sources, (size_t)new_capacity * sizeof(void *));
    if (!new_sources) {
        rt_trap("Game3D.Sound3D: source list allocation failed");
        return 0;
    }
    audio->sources = new_sources;
    audio->source_capacity = new_capacity;
    return 1;
}

/// @brief Retain `source` and append it to the audio subsystem's tracked source list.
void game3d_audio_track_source(rt_game3d_audio *audio, void *source) {
    if (!audio || !source)
        return;
    if (!rt_g3d_has_class(source, RT_G3D_SOUNDSOURCE3D_CLASS_ID))
        return;
    game3d_audio_repair_sources(audio);
    game3d_audio_prune_sources(audio);
    for (int32_t i = 0; i < audio->source_count; ++i) {
        if (audio->sources[i] == source)
            return;
    }
    if (audio->source_count == INT32_MAX) {
        rt_trap("Game3D.Sound3D: too many sources");
        return;
    }
    if (!game3d_audio_reserve_sources(audio, audio->source_count + 1))
        return;
    rt_obj_retain_maybe(source);
    audio->sources[audio->source_count++] = source;
}

/// @brief Drop stopped/finished sources from the audio subsystem's retained list.
void game3d_audio_prune_sources(rt_game3d_audio *audio) {
    int32_t write = 0;
    int32_t kept;
    if (!audio)
        return;
    game3d_audio_repair_sources(audio);
    for (int32_t read = 0; read < audio->source_count; ++read) {
        void *source = audio->sources[read];
        if (rt_g3d_has_class(source, RT_G3D_SOUNDSOURCE3D_CLASS_ID) &&
            rt_soundsource3d_get_is_playing(source)) {
            audio->sources[write++] = source;
            continue;
        }
        game3d_release_typed_ref(&audio->sources[read], RT_G3D_SOUNDSOURCE3D_CLASS_ID);
    }
    kept = write;
    while (write < audio->source_count)
        audio->sources[write++] = NULL;
    audio->source_count = kept;
}

/// @brief Normalize and compact the effect-item array: clamp count/capacity
///   invariants, drop items whose object is no longer a valid particle/decal,
///   and clamp each survivor's lifetime/age into range. Defensive; returns void.
void game3d_effects_repair(rt_game3d_effects *effects) {
    if (!effects)
        return;
    if (effects->capacity < 0)
        effects->capacity = 0;
    if (!effects->items || effects->capacity == 0) {
        effects->count = 0;
        effects->capacity = effects->items ? effects->capacity : 0;
        return;
    }
    if (effects->count < 0)
        effects->count = 0;
    if (effects->count > effects->capacity)
        effects->count = effects->capacity;

    int32_t write = 0;
    for (int32_t read = 0; read < effects->count; ++read) {
        rt_game3d_effect_item item = effects->items[read];
        int valid = 0;
        if (item.type == RT_GAME3D_EFFECT_PARTICLES)
            valid = rt_g3d_has_class(item.object, RT_G3D_PARTICLES3D_CLASS_ID);
        else if (item.type == RT_GAME3D_EFFECT_DECAL)
            valid = rt_g3d_has_class(item.object, RT_G3D_DECAL3D_CLASS_ID);
        if (!valid) {
            if (item.type == RT_GAME3D_EFFECT_PARTICLES)
                game3d_release_typed_ref(&item.object, RT_G3D_PARTICLES3D_CLASS_ID);
            else if (item.type == RT_GAME3D_EFFECT_DECAL)
                game3d_release_typed_ref(&item.object, RT_G3D_DECAL3D_CLASS_ID);
            memset(&effects->items[read], 0, sizeof(effects->items[read]));
            continue;
        }

        item.lifetime = (isfinite(item.lifetime) && item.lifetime > 0.0)
                            ? game3d_positive_clamped_or(item.lifetime,
                                                         RT_GAME3D_EFFECT_LIFETIME_MAX,
                                                         RT_GAME3D_EFFECT_LIFETIME_MAX)
                            : -1.0;
        item.age = game3d_nonnegative_clamped_or(item.age, 0.0, RT_GAME3D_EFFECT_LIFETIME_MAX);
        if (write != read) {
            effects->items[write] = item;
            memset(&effects->items[read], 0, sizeof(effects->items[read]));
        } else {
            effects->items[write] = item;
        }
        write++;
    }
    effects->count = write;
}

/// @brief Ensure the effect-item array can hold `needed` entries, doubling capacity
///   as needed; traps and returns 0 on allocation failure, else 1.
int game3d_effects_reserve(rt_game3d_effects *effects, int32_t needed) {
    int32_t new_capacity;
    if (!effects)
        return 0;
    game3d_effects_repair(effects);
    if (needed <= effects->capacity)
        return 1;
    if (!game3d_compute_capacity(
            effects->capacity, needed, 16, sizeof(rt_game3d_effect_item), &new_capacity)) {
        rt_trap("Game3D.EffectRegistry3D: too many effects");
        return 0;
    }
    rt_game3d_effect_item *new_items = (rt_game3d_effect_item *)realloc(
        effects->items, (size_t)new_capacity * sizeof(rt_game3d_effect_item));
    if (!new_items) {
        rt_trap("Game3D.EffectRegistry3D: effect list allocation failed");
        return 0;
    }
    effects->items = new_items;
    effects->capacity = new_capacity;
    return 1;
}

/// @brief Tear down one effect item: stop a particle system if present, release the
///   wrapped object reference, and zero the slot for reuse.
void game3d_effect_release_item(rt_game3d_effect_item *item) {
    if (!item)
        return;
    if (item->type == RT_GAME3D_EFFECT_PARTICLES &&
        rt_g3d_has_class(item->object, RT_G3D_PARTICLES3D_CLASS_ID))
        rt_particles3d_clear(item->object);
    if (item->type == RT_GAME3D_EFFECT_PARTICLES)
        game3d_release_typed_ref(&item->object, RT_G3D_PARTICLES3D_CLASS_ID);
    else if (item->type == RT_GAME3D_EFFECT_DECAL)
        game3d_release_typed_ref(&item->object, RT_G3D_DECAL3D_CLASS_ID);
    else
        item->object = NULL;
    item->type = 0;
    item->lifetime = 0.0;
    item->age = 0.0;
}

/// @brief Return `value` if finite and strictly positive, else `fallback`.
double game3d_positive_or(double value, double fallback) {
    fallback = game3d_finite_or(fallback, 1.0);
    if (fallback <= 0.0)
        fallback = 1.0;
    value = game3d_finite_or(value, fallback);
    return value > 0.0 ? value : fallback;
}

/// @brief Return a positive finite value capped at max_value; invalid/non-positive uses fallback.
double game3d_positive_clamped_or(double value, double fallback, double max_value) {
    value = game3d_positive_or(value, fallback);
    max_value = game3d_positive_or(max_value, value);
    return value > max_value ? max_value : value;
}

/// @brief Normalize the (x, z) ground-plane vector in place; degenerate or near-zero
///   length inputs fall back to (fallback_x, fallback_z). Used for movement headings.
void game3d_normalize_xz(double *x, double *z, double fallback_x, double fallback_z) {
    fallback_x = game3d_finite_or(fallback_x, 0.0);
    fallback_z = game3d_finite_or(fallback_z, 0.0);
    double vx = game3d_finite_or(x ? *x : fallback_x, fallback_x);
    double vz = game3d_finite_or(z ? *z : fallback_z, fallback_z);
    double max_abs = fmax(fabs(vx), fabs(vz));
    double len = 0.0;
    if (isfinite(max_abs) && max_abs > 0.0) {
        double sx = vx / max_abs;
        double sz = vz / max_abs;
        len = max_abs * sqrt(sx * sx + sz * sz);
    }
    if (!isfinite(len) || len <= 1e-12) {
        vx = fallback_x;
        vz = fallback_z;
    } else {
        vx /= len;
        vz /= len;
    }
    if (x)
        *x = vx;
    if (z)
        *z = vz;
}

/// @brief Allocate a LayerMask handle initialized to the given (sanitized) bitfield;
///   traps on allocation failure.
void *game3d_layermask_new_bits(int64_t bits) {
    rt_game3d_layermask *mask = (rt_game3d_layermask *)rt_obj_new_i64(
        RT_G3D_GAME3D_LAYERMASK_CLASS_ID, (int64_t)sizeof(*mask));
    if (!mask) {
        rt_trap("Game3D.LayerMask.New: allocation failed");
        return NULL;
    }
    mask->bits = game3d_sanitize_mask_bits(bits);
    return mask;
}

//=========================================================================
// Input snapshot accessors — each reads a per-frame captured snapshot when the
// Input3D object has one latched (deterministic replay), otherwise falls
// through to the live keyboard/mouse runtime.
//=========================================================================

/// @brief Is `key` held this frame? Snapshot-aware, else live keyboard state.
int8_t game3d_input_key_down(const rt_game3d_input *input, int64_t key) {
    if (input && input->has_snapshot && key > 0 && key < VIPER_KEY_MAX)
        return input->key_down[key] ? 1 : 0;
    return rt_keyboard_is_down(key);
}

/// @brief Did `key` transition to down this frame? Snapshot-aware, else live.
int8_t game3d_input_key_pressed(const rt_game3d_input *input, int64_t key) {
    if (input && input->has_snapshot && key > 0 && key < VIPER_KEY_MAX)
        return input->key_pressed[key] ? 1 : 0;
    return rt_keyboard_was_pressed(key);
}

/// @brief Did `key` transition to up this frame? Snapshot-aware, else live.
int8_t game3d_input_key_released(const rt_game3d_input *input, int64_t key) {
    if (input && input->has_snapshot && key > 0 && key < VIPER_KEY_MAX)
        return input->key_released[key] ? 1 : 0;
    return rt_keyboard_was_released(key);
}

/// @brief Is mouse `button` held this frame? Snapshot-aware, else live mouse state.
int8_t game3d_input_mouse_down(const rt_game3d_input *input, int64_t button) {
    if (input && input->has_snapshot && button >= 0 && button < VIPER_MOUSE_BUTTON_MAX)
        return input->mouse_down[button] ? 1 : 0;
    return rt_mouse_is_down(button);
}

/// @brief Did mouse `button` transition to down this frame? Snapshot-aware, else live.
int8_t game3d_input_mouse_pressed_snapshot(const rt_game3d_input *input, int64_t button) {
    if (input && input->has_snapshot && button >= 0 && button < VIPER_MOUSE_BUTTON_MAX)
        return input->mouse_pressed[button] ? 1 : 0;
    return rt_mouse_was_pressed(button);
}

/// @brief This frame's mouse X delta. Snapshot-aware, else live mouse delta.
int64_t game3d_input_mouse_dx(const rt_game3d_input *input) {
    return input && input->has_snapshot ? input->mouse_dx : rt_mouse_delta_x();
}

/// @brief This frame's mouse Y delta. Snapshot-aware, else live mouse delta.
int64_t game3d_input_mouse_dy(const rt_game3d_input *input) {
    return input && input->has_snapshot ? input->mouse_dy : rt_mouse_delta_y();
}

/// @brief This frame's mouse wheel Y. Snapshot-aware, else live wheel value.
double game3d_input_wheel_y_snapshot(const rt_game3d_input *input) {
    return input && input->has_snapshot ? input->wheel_y : rt_mouse_wheel_yf();
}

/// @brief Create an empty layer mask (no bits set). See header.
void *rt_game3d_layermask_none(void) {
    return game3d_layermask_new_bits(0);
}

/// @brief Create a layer mask with all bits set. See header.
void *rt_game3d_layermask_all(void) {
    return game3d_layermask_new_bits(~(int64_t)0);
}

/// @brief Create a mask holding exactly `layer`; traps if it is not a single bit.
void *rt_game3d_layermask_of(int64_t layer) {
    if (!game3d_valid_layer(layer))
        rt_trap("Game3D.LayerMask.Of: layer must be a single positive bit");
    return game3d_layermask_new_bits(layer);
}

/// @brief Get the raw bitfield backing the mask (0 on invalid handle after trap).
int64_t rt_game3d_layermask_get_bits(void *obj) {
    rt_game3d_layermask *mask =
        game3d_layermask_checked(obj, "Game3D.LayerMask.get_Bits: invalid mask");
    return mask ? mask->bits : 0;
}

/// @brief Overwrite the mask bitfield with a sanitized copy of `bits`.
void rt_game3d_layermask_set_bits(void *obj, int64_t bits) {
    rt_game3d_layermask *mask =
        game3d_layermask_checked(obj, "Game3D.LayerMask.set_Bits: invalid mask");
    if (mask)
        mask->bits = game3d_sanitize_mask_bits(bits);
}

/// @brief OR `layer` into the mask and return the same handle (fluent); traps on a
///   non-single-bit layer.
void *rt_game3d_layermask_include(void *obj, int64_t layer) {
    rt_game3d_layermask *mask =
        game3d_layermask_checked(obj, "Game3D.LayerMask.include: invalid mask");
    if (!game3d_valid_layer(layer))
        rt_trap("Game3D.LayerMask.include: layer must be a single positive bit");
    if (mask)
        mask->bits |= layer;
    return obj;
}

/// @brief True if the mask contains `layer`; returns 0 for an invalid layer bit.
int8_t rt_game3d_layermask_includes(void *obj, int64_t layer) {
    rt_game3d_layermask *mask =
        game3d_layermask_checked(obj, "Game3D.LayerMask.includes: invalid mask");
    if (!game3d_valid_layer(layer))
        return 0;
    return mask && (mask->bits & layer) != 0 ? 1 : 0;
}

/// @brief Ensure an entity's child array can hold `need` entries, doubling capacity as
///   needed; returns 0 on allocation failure, else 1.
int game3d_entity_grow_children(rt_game3d_entity *entity, int32_t need) {
    int32_t new_cap;
    if (!entity)
        return 0;
    if (!entity->children || entity->child_capacity < 0)
        entity->child_capacity = 0;
    entity->child_count = game3d_entity_child_count(entity);
    if (entity->child_capacity >= need)
        return 1;
    if (!game3d_compute_capacity(
            entity->child_capacity, need, 4, sizeof(rt_game3d_entity *), &new_cap))
        return 0;
    rt_game3d_entity **grown =
        (rt_game3d_entity **)realloc(entity->children, (size_t)new_cap * sizeof(*grown));
    if (!grown)
        return 0;
    entity->children = grown;
    entity->child_capacity = new_cap;
    return 1;
}

int game3d_world_spawn_entity_tree(rt_game3d_world *world,
                                   rt_game3d_entity *entity,
                                   int attach_to_scene,
                                   int64_t *next_id);

/// @brief True when `ancestor` is already on `entity`'s parent chain.
int game3d_entity_has_ancestor(rt_game3d_entity *entity, rt_game3d_entity *ancestor) {
    int32_t depth = 0;
    for (rt_game3d_entity *cursor = entity; cursor && depth < 65536; cursor = cursor->parent) {
        if (cursor == ancestor)
            return 1;
        depth++;
    }
    return 0;
}

/// @brief Find a direct child index, or -1 when absent.
int32_t game3d_entity_find_child_index(rt_game3d_entity *parent, rt_game3d_entity *child) {
    int32_t child_count;
    if (!parent || !child)
        return -1;
    child_count = game3d_entity_child_count(parent);
    for (int32_t i = 0; i < child_count; ++i) {
        if (parent->children[i] == child)
            return i;
    }
    return -1;
}

/// @brief Remove a child from its Game3D parent list and scene-node parent.
/// @details The caller should hold its own retain if it needs `child` to
///          survive this unlink.
void game3d_entity_detach_from_parent(rt_game3d_entity *child) {
    rt_game3d_entity *parent;
    int32_t index;
    if (!child || !child->parent)
        return;
    parent = child->parent;
    index = game3d_entity_find_child_index(parent, child);
    if (index >= 0) {
        int32_t child_count = game3d_entity_child_count(parent);
        rt_game3d_entity *owned = parent->children[index];
        parent->child_count = child_count;
        for (int32_t i = index; i < child_count - 1; ++i)
            parent->children[i] = parent->children[i + 1];
        parent->children[--parent->child_count] = NULL;
        child->parent = NULL;
        void *parent_node = game3d_entity_node_ref(parent);
        void *child_node = game3d_entity_node_ref(child);
        if (parent_node && child_node && rt_scene_node3d_get_parent(child_node) == parent_node)
            rt_scene_node3d_remove_child(parent_node, child_node);
        game3d_release_ref((void **)&owned);
    } else {
        child->parent = NULL;
    }
}

/// @brief Store the world's background clear color, each channel clamped to [0, 1].
void game3d_world_set_clear_color(rt_game3d_world *world, double r, double g, double b) {
    if (!world)
        return;
    world->clear_r = game3d_clamp(r, 0.0, 1.0);
    world->clear_g = game3d_clamp(g, 0.0, 1.0);
    world->clear_b = game3d_clamp(b, 0.0, 1.0);
}

/// @brief Replace the world's post-FX stack, updating both the effect registry's owned
///   reference and the canvas binding.
void game3d_world_assign_postfx(rt_game3d_world *world, void *postfx) {
    if (!world || !world->canvas)
        return;
    rt_game3d_effects *effects = (rt_game3d_effects *)world->effects;
    if (effects)
        game3d_assign_ref(&effects->postfx, postfx);
    rt_canvas3d_set_post_fx(world->canvas, postfx);
}

/// @brief Bind a light into the given canvas light slot (no-op on NULL light).
void game3d_world_install_light(rt_game3d_world *world, int64_t slot, void *light) {
    if (!world || !world->canvas || !light)
        return;
    rt_canvas3d_set_light(world->canvas, slot, light);
}

//===----------------------------------------------------------------------===//
// Implementation split across cohesive .inc units compiled as one translation unit.
//===----------------------------------------------------------------------===//
#include "rt_game3d_cache.inc"
#include "rt_game3d_asset_load.inc"
#include "rt_game3d_world_sim.inc"
#include "rt_game3d_streaming.inc"
#include "rt_game3d_world_api.inc"
#include "rt_game3d_events.inc"

/// @brief Set the physics gravity vector. See header.
void rt_game3d_world_set_gravity(void *obj, double x, double y, double z) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setGravity: invalid world");
    if (world && world->physics)
        rt_world3d_set_gravity(world->physics, x, y, z);
}

/// @brief Begin a frame: poll the window, refresh input, advance timing/frame counters,
///   and sync the camera aspect; returns 0 when the window should close. See header.
int8_t rt_game3d_world_tick(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.tick: invalid world");
    if (!world || !world->canvas)
        return 0;
    game3d_asset_async_drain_commits();
    if (!rt_canvas3d_poll(world->canvas)) {
        world->width = rt_canvas3d_get_window_width(world->canvas);
        world->height = rt_canvas3d_get_window_height(world->canvas);
        return 0;
    }
    world->width = rt_canvas3d_get_window_width(world->canvas);
    world->height = rt_canvas3d_get_window_height(world->canvas);
    if (rt_canvas3d_should_close(world->canvas))
        return 0;
    rt_game3d_input_update(world->input);
    world->dt = game3d_clamp_dt(rt_canvas3d_get_delta_time_sec(world->canvas));
    world->elapsed += world->dt;
    world->frame += 1;
    if (world->camera && world->width > 0 && world->height > 0)
        rt_camera3d_sync_render_aspect(world->camera, (double)world->width / (double)world->height);
    return 1;
}

/// @brief Run one fixed simulation step: physics, animations, controllers, transform sync, and
/// events.
/// @details The core per-tick update that advances all world subsystems by a fixed delta and fires
/// the
///          resulting collision/trigger callbacks; called (possibly multiple times) from the world
///          step.
static void game3d_world_step_simulation_impl(rt_game3d_world *world,
                                              double step_sec,
                                              int8_t advance_time_counters) {
    if (!world)
        return;
    game3d_asset_async_drain_commits();
    double dt = game3d_clamp_dt(step_sec);
    world->dt = dt;
    if (advance_time_counters) {
        world->elapsed += dt;
        world->frame += 1;
    }
    game3d_world_update_controller(world, dt);
    game3d_world_update_animations(world, dt);
    if (world->physics)
        rt_world3d_step(world->physics, dt);
    if (world->scene)
        rt_scene3d_sync_bindings(world->scene, dt);
    if (world->audio)
        game3d_audio_prune_sources((rt_game3d_audio *)world->audio);
    if (world->effects)
        rt_game3d_effects_update(world->effects, dt);
    game3d_world_late_update_controller(world, dt);
    game3d_world_rebase_if_needed(world);
}

/// @brief Advance one simulation step by `step_sec`: camera-controller update, animator
///   update, physics step, scene/audio binding sync, effect update, then the camera
///   late-update. See header.
void rt_game3d_world_step_simulation(void *obj, double step_sec) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.stepSimulation: invalid world");
    game3d_world_step_simulation_impl(world, step_sec, 1);
}

/// @brief Draw a yellow wireframe AABB around each spawned entity's collider for the
///   physics debug overlay.
static void game3d_world_debug_draw_physics(rt_game3d_world *world) {
    if (!world || !world->canvas)
        return;
    int32_t entity_count = game3d_world_safe_entity_count(world);
    for (int32_t i = 0; i < entity_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        void *body = game3d_entity_body_ref(entity);
        if (!entity || entity->destroyed || !body)
            continue;
        void *collider = rt_body3d_get_collider(body);
        if (!collider)
            continue;
        double mn_raw[3] = {0.0, 0.0, 0.0};
        double mx_raw[3] = {0.0, 0.0, 0.0};
        double position[3] = {0.0, 0.0, 0.0};
        double rotation[4] = {0.0, 0.0, 0.0, 1.0};
        double scale[3] = {1.0, 1.0, 1.0};
        rt_body3d_get_pose_raw(body, position, rotation, scale);
        rt_collider3d_compute_world_aabb_raw(collider, position, rotation, scale, mn_raw, mx_raw);
        rt_canvas3d_draw_aabb_wire_raw(world->canvas, mn_raw, mx_raw, 0xFFCC33);
    }
}

/// @brief Draw a C-string as overlay text at (x, y) in the given color.
static void game3d_world_debug_text(
    rt_game3d_world *world, int64_t x, int64_t y, const char *text, int64_t color) {
    if (!world || !world->canvas || !text)
        return;
    rt_string line = rt_string_from_bytes(text, strlen(text));
    rt_canvas3d_draw_text2d(world->canvas, x, y, line, color);
    game3d_release_ref((void **)&line);
}

#if defined(__GNUC__) || defined(__clang__)
#define RT_GAME3D_PRINTF(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#else
#define RT_GAME3D_PRINTF(fmt_index, first_arg)
#endif

static void game3d_world_debug_textf(
    rt_game3d_world *world, int64_t x, int64_t y, int64_t color, const char *fmt, ...)
    RT_GAME3D_PRINTF(5, 6);

/// @brief printf-style overlay text helper: format into a fixed 192-byte buffer (always
///   NUL-terminated) and draw it via game3d_world_debug_text.
static void game3d_world_debug_textf(
    rt_game3d_world *world, int64_t x, int64_t y, int64_t color, const char *fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0)
        return;
    buf[sizeof(buf) - 1] = '\0';
    game3d_world_debug_text(world, x, y, buf, color);
}

#undef RT_GAME3D_PRINTF

/// @brief Map a quality preset id to its lowercase display name (defaults to "balanced").
static const char *game3d_quality_name(int64_t quality) {
    switch (quality) {
        case RT_GAME3D_QUALITY_PERFORMANCE:
            return "performance";
        case RT_GAME3D_QUALITY_CINEMATIC:
            return "cinematic";
        case RT_GAME3D_QUALITY_BALANCED:
        default:
            return "balanced";
    }
}

/// @brief Render the 2D debug HUD when enabled: backend/FPS, quality/fallback, node/cull/body
///   counts, effective clip planes, shadow/occlusion diagnostics, and optional camera/caps blocks.
static void game3d_world_draw_debug_overlay(rt_game3d_world *world) {
    if (!world || !world->canvas || !world->debug_overlay_enabled)
        return;
    rt_canvas3d_begin_overlay(world->canvas);
    rt_canvas3d_draw_rect2d(world->canvas, 8, 8, 360, 176, 0x111820);
    game3d_world_debug_text(world, 14, 14, "Game3D Debug", 0xFFFFFF);
    rt_string backend = rt_canvas3d_get_backend(world->canvas);
    const char *backend_cs = backend ? rt_string_cstr(backend) : "unknown";
    game3d_world_debug_textf(world,
                             14,
                             28,
                             0xD7E7FF,
                             "backend %s fps %lld",
                             backend_cs ? backend_cs : "unknown",
                             (long long)rt_canvas3d_get_fps(world->canvas));
    game3d_world_debug_textf(world,
                             14,
                             42,
                             0xD7E7FF,
                             "quality %s active %s%s",
                             game3d_quality_name(rt_canvas3d_get_quality_requested(world->canvas)),
                             game3d_quality_name(rt_canvas3d_get_quality_active(world->canvas)),
                             rt_canvas3d_get_quality_fallback(world->canvas) ? " fallback" : "");
    game3d_world_debug_textf(
        world,
        14,
        56,
        0xD7E7FF,
        "nodes %lld culled %lld bodies %lld",
        (long long)(world->scene ? rt_scene3d_get_node_count(world->scene) : 0),
        (long long)(world->scene ? rt_scene3d_get_culled_count(world->scene) : 0),
        (long long)(world->physics ? rt_world3d_body_count(world->physics) : 0));
    if (world->camera) {
        game3d_world_debug_textf(world,
                                 14,
                                 70,
                                 0xCDEECC,
                                 "clip %.3f %.1f",
                                 rt_camera3d_get_effective_near_plane(world->camera),
                                 rt_camera3d_get_effective_far_plane(world->camera));
    }
    {
        rt_canvas3d *canvas = rt_canvas3d_checked_or_stack(world->canvas);
        game3d_world_debug_textf(world,
                                 14,
                                 84,
                                 0xFFE5AA,
                                 "shadows %lld res %lld",
                                 (long long)(canvas ? canvas->shadow_count : 0),
                                 (long long)(canvas ? canvas->shadow_resolution : 0));
    }
    game3d_world_debug_textf(world,
                             14,
                             98,
                             0xFFE5AA,
                             "cull f %lld cpu %lld tested %lld",
                             (long long)rt_canvas3d_get_frustum_culled_draw_count(world->canvas),
                             (long long)rt_canvas3d_get_cpu_occluded_draw_count(world->canvas),
                             (long long)rt_canvas3d_get_occlusion_candidate_count(world->canvas));
    {
        int64_t terrain_total = 0;
        int64_t terrain_drawn = 0;
        int64_t terrain_frustum = 0;
        int64_t terrain_missing = 0;
        int64_t terrain_lod0 = 0;
        int64_t terrain_lod1 = 0;
        int64_t terrain_lod2 = 0;
        int64_t terrain_clamped = 0;
        rt_game3d_world_stream *stream =
            world->stream ? (rt_game3d_world_stream *)rt_g3d_checked_or_null(
                                world->stream, RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID)
                          : NULL;
        int32_t tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
        for (int32_t i = 0; i < tile_count; ++i) {
            void *terrain = game3d_stream_terrain_tile_terrain_ref(&stream->terrain_tiles[i]);
            if (!terrain)
                continue;
            terrain_total += rt_terrain3d_get_last_chunk_count(terrain);
            terrain_drawn += rt_terrain3d_get_last_drawn_chunk_count(terrain);
            terrain_frustum += rt_terrain3d_get_last_frustum_culled_chunk_count(terrain);
            terrain_missing += rt_terrain3d_get_last_missing_lod_count(terrain);
            terrain_lod0 += rt_terrain3d_get_last_lod0_chunk_count(terrain);
            terrain_lod1 += rt_terrain3d_get_last_lod1_chunk_count(terrain);
            terrain_lod2 += rt_terrain3d_get_last_lod2_chunk_count(terrain);
            terrain_clamped += rt_terrain3d_get_last_lod_clamped_chunk_count(terrain);
        }
        game3d_world_debug_textf(world,
                                 14,
                                 112,
                                 0xFFE5AA,
                                 "terrain %lld/%lld f %lld miss %lld lod %lld/%lld/%lld clamp %lld",
                                 (long long)terrain_drawn,
                                 (long long)terrain_total,
                                 (long long)terrain_frustum,
                                 (long long)terrain_missing,
                                 (long long)terrain_lod0,
                                 (long long)terrain_lod1,
                                 (long long)terrain_lod2,
                                 (long long)terrain_clamped);
    }
    if (world->debug_camera_enabled && world->camera) {
        void *pos = rt_camera3d_get_position(world->camera);
        game3d_world_debug_textf(world,
                                 14,
                                 126,
                                 0xCDEECC,
                                 "camera %.2f %.2f %.2f",
                                 pos ? rt_vec3_x(pos) : 0.0,
                                 pos ? rt_vec3_y(pos) : 0.0,
                                 pos ? rt_vec3_z(pos) : 0.0);
        game3d_release_ref(&pos);
    }
    if (world->debug_caps_enabled) {
        int64_t caps = rt_canvas3d_get_backend_capabilities(world->canvas);
        game3d_world_debug_textf(world, 14, 140, 0xFFE5AA, "caps 0x%llx", (long long)caps);
    }
    if (world->debug_physics_enabled)
        game3d_world_debug_text(world, 14, 154, "physics wire enabled", 0xFFE5AA);
    rt_canvas3d_end_overlay(world->canvas);
}

/// @brief Return true when two far-plane values are close enough to treat as unchanged.
static int game3d_camera_far_almost_equal(double a, double b) {
    double scale = fmax(fmax(fabs(a), fabs(b)), 1.0);
    return isfinite(a) && isfinite(b) && fabs(a - b) <= scale * 1e-9;
}

/// @brief Restore a camera far plane previously overridden for stream terrain.
/// @details If user code changed the far plane while the stream override was active, that value is
///          treated as the new user-authored far plane rather than forcing an older snapshot back.
static void game3d_world_restore_stream_camera_far(rt_game3d_world *world) {
    double current_far;
    double user_far;
    if (!world || !world->camera || !world->stream_camera_far_active)
        return;
    current_far = rt_camera3d_get_far_plane(world->camera);
    if (!game3d_camera_far_almost_equal(current_far, world->stream_camera_effective_far) &&
        isfinite(current_far) && current_far > 0.0)
        world->stream_camera_user_far = current_far;
    user_far = world->stream_camera_user_far;
    if (isfinite(user_far) && user_far > 0.0 &&
        !game3d_camera_far_almost_equal(current_far, user_far))
        rt_camera3d_set_far_plane(world->camera, user_far);
    world->stream_camera_far_active = 0;
    world->stream_camera_effective_far = 0.0;
    world->stream_camera_user_far = 0.0;
}

/// @brief Sync the camera far plane to the active stream terrain horizon without one-way growth.
/// @details Streamed terrain uses load/unload radii and tile radii to decide residency. If the
///          camera far plane is shorter than that horizon, valid resident floor chunks can be clipped
///          before Terrain3D or Canvas3D culling can keep them. This helper remembers the user's far
///          plane, applies `max(user_far, stream_horizon)`, and lowers/restores it when possible.
static void game3d_world_sync_camera_far_for_stream(rt_game3d_world *world) {
    rt_game3d_world_stream *stream;
    double desired_far;
    double current_far;
    double user_far;
    double effective_far;
    int32_t tile_count;
    if (!world || !world->camera)
        return;
    if (!world->stream) {
        game3d_world_restore_stream_camera_far(world);
        return;
    }
    stream = (rt_game3d_world_stream *)rt_g3d_checked_or_null(
        world->stream, RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID);
    if (!stream || !stream->terrain_manifest_loaded) {
        game3d_world_restore_stream_camera_far(world);
        return;
    }
    desired_far = game3d_clamp_coord_or(stream->load_radius, 0.0);
    tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
    for (int32_t i = 0; i < tile_count; ++i) {
        rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
        if (!tile || !tile->resident)
            continue;
        if (isfinite(tile->radius) && tile->radius > 0.0)
            desired_far = fmax(desired_far, stream->load_radius + tile->radius);
    }
    desired_far += 16.0;
    if (!isfinite(desired_far) || desired_far <= 0.0) {
        game3d_world_restore_stream_camera_far(world);
        return;
    }
    if (desired_far > RT_GAME3D_COORD_ABS_MAX)
        desired_far = RT_GAME3D_COORD_ABS_MAX;

    current_far = rt_camera3d_get_far_plane(world->camera);
    if (!world->stream_camera_far_active) {
        world->stream_camera_user_far =
            isfinite(current_far) && current_far > 0.0 ? current_far : RT_GAME3D_DEFAULT_FAR;
    } else if (!game3d_camera_far_almost_equal(current_far, world->stream_camera_effective_far) &&
               isfinite(current_far) && current_far > 0.0) {
        world->stream_camera_user_far = current_far;
    }
    user_far = isfinite(world->stream_camera_user_far) && world->stream_camera_user_far > 0.0
                   ? world->stream_camera_user_far
                   : RT_GAME3D_DEFAULT_FAR;
    effective_far = fmax(user_far, desired_far);
    if (effective_far > RT_GAME3D_COORD_ABS_MAX)
        effective_far = RT_GAME3D_COORD_ABS_MAX;
    if (!game3d_camera_far_almost_equal(current_far, effective_far))
        rt_camera3d_set_far_plane(world->camera, effective_far);
    world->stream_camera_far_active = 1;
    world->stream_camera_effective_far = effective_far;
}

/// @brief Shared begin-frame body: clear the back buffer to the world's clear color,
///   enable camera-relative upload for floating-origin worlds, and open the 3D pass
///   with the active camera.
/// @return Non-zero if the canvas frame opened successfully (canvas valid and in-frame).
static int game3d_world_begin_frame_impl(rt_game3d_world *world) {
    if (!world || !world->canvas || !world->camera)
        return 0;
    game3d_world_rebase_if_needed(world);
    game3d_world_sync_camera_far_for_stream(world);
    rt_canvas3d_clear(world->canvas, world->clear_r, world->clear_g, world->clear_b);
    rt_canvas3d_set_camera_relative_upload(world->canvas, world->floating_origin);
    rt_canvas3d_begin(world->canvas, world->camera);
    rt_canvas3d *canvas = rt_canvas3d_checked_or_stack(world->canvas);
    return canvas && canvas->in_frame;
}

/// @brief Clear the back buffer to the world's clear color and open the 3D pass with the
///   active camera. See header.
void rt_game3d_world_begin_frame(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.beginFrame: invalid world");
    (void)game3d_world_begin_frame_impl(world);
}

/// @brief Draw every resident streamed terrain tile through the world's canvas,
///   positioning each tile from its center and per-axis scale. No-op when the world has
///   no stream or the terrain manifest is not loaded.
static void game3d_world_draw_stream_terrain(rt_game3d_world *world) {
    rt_game3d_world_stream *stream;
    if (!world || !world->canvas || !world->stream)
        return;
    stream = (rt_game3d_world_stream *)rt_g3d_checked_or_null(
        world->stream, RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID);
    if (!stream || !stream->terrain_manifest_loaded)
        return;
    int32_t tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
    for (int32_t i = 0; i < tile_count; ++i) {
        rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
        void *terrain = game3d_stream_terrain_tile_terrain_ref(tile);
        if (!terrain)
            continue;
        double sx = game3d_stream_terrain_axis_scale(tile->scale[0]);
        double sz = game3d_stream_terrain_axis_scale(tile->scale[2]);
        double width = tile->width > 1 ? (double)(tile->width - 1) * sx : sx;
        double depth = tile->depth > 1 ? (double)(tile->depth - 1) * sz : sz;
        double cx = game3d_clamp_coord_or(tile->center[0], 0.0);
        double ty = game3d_clamp_coord_or(tile->center[1], 0.0);
        double cz = game3d_clamp_coord_or(tile->center[2], 0.0);
        double tx = game3d_clamp_coord_or(cx - width * 0.5, cx);
        double tz = game3d_clamp_coord_or(cz - depth * 0.5, cz);
        rt_canvas3d_draw_terrain_at(world->canvas, terrain, tx, ty, tz);
    }
}

/// @brief Draw the scene graph and any resident stream terrain through the active camera.
/// See header.
void rt_game3d_world_draw_scene(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.drawScene: invalid world");
    if (world && world->scene && world->canvas && world->camera) {
        rt_scene3d_draw(world->scene, world->canvas, world->camera);
        game3d_world_draw_stream_terrain(world);
    }
}

/// @brief Draw registered effects plus any enabled debug axis/physics gizmos. See header.
void rt_game3d_world_draw_effects(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.drawEffects: invalid world");
    if (!world || !world->canvas)
        return;
    if (world->debug_axes_enabled && world->debug_axis_origin)
        rt_canvas3d_draw_axis(world->canvas, world->debug_axis_origin, world->debug_axis_size);
    if (world->effects && world->camera)
        rt_game3d_effects_draw(world->effects, world->canvas, world->camera);
    if (world->debug_physics_enabled)
        game3d_world_debug_draw_physics(world);
}

/// @brief End the 3D pass and render the debug HUD overlay. See header.
void rt_game3d_world_end_scene(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.endScene: invalid world");
    if (world && world->canvas) {
        rt_canvas3d_end(world->canvas);
        game3d_world_draw_debug_overlay(world);
    }
}

/// @brief Run a native 2D overlay callback inside a fresh overlay pass; traps if the
///   callback pointer is not a native function. See header.
void rt_game3d_world_draw_overlay(void *obj, void *overlay) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.drawOverlay: invalid world");
    rt_game3d_overlay_fn fn = game3d_overlay_callback_checked(
        overlay,
        "Game3D.World3D.drawOverlay: callback must be a native function pointer; use manual "
        "overlay calls from interpreted Zia");
    if (!world || !world->canvas)
        return;
    rt_canvas3d_begin_overlay(world->canvas);
    if (fn)
        fn();
    rt_canvas3d_end_overlay(world->canvas);
}

/// @brief Capture the finalized frame as a Pixels image (NULL if invalid). See header.
void *rt_game3d_world_capture_final_frame(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.captureFinalFrame: invalid world");
    if (!world || !world->canvas)
        return NULL;
    return rt_canvas3d_screenshot_final(world->canvas);
}

/// @brief Present the finished frame to the window (flip buffers). See header.
void rt_game3d_world_present(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.present: invalid world");
    if (world && world->canvas)
        rt_canvas3d_flip(world->canvas);
}

/// @brief Run the 2D overlay pass for a frame: open the canvas overlay layer,
///   invoke the native overlay callback `fn` (if any), then close the layer.
static void game3d_world_draw_overlay_fn(rt_game3d_world *world, rt_game3d_overlay_fn fn) {
    if (!world || !world->canvas)
        return;
    rt_canvas3d_begin_overlay(world->canvas);
    if (fn)
        fn();
    rt_canvas3d_end_overlay(world->canvas);
}

/// @brief Render one complete frame: begin → draw scene → draw effects → end scene →
///   optional overlay → present. Shared by all run-loop variants.
static void game3d_world_render_once(rt_game3d_world *world, rt_game3d_overlay_fn overlay_fn) {
    if (!world || world->destroyed || !world->canvas)
        return;
    if (!game3d_world_begin_frame_impl(world))
        return;
    rt_game3d_world_draw_scene(world);
    rt_game3d_world_draw_effects(world);
    rt_game3d_world_end_scene(world);
    if (overlay_fn)
        game3d_world_draw_overlay_fn(world, overlay_fn);
    rt_game3d_world_present(world);
}

/// @brief Whether a world handle is non-NULL and not currently being torn down (safe to operate
/// on).
static int game3d_world_is_live(const rt_game3d_world *world) {
    return world && !world->destroyed && world->canvas;
}

/// @brief Run the blocking variable-timestep game loop until the window closes, calling
///   the native `update` callback once per frame before stepping and rendering. See header.
void rt_game3d_world_run(void *obj, void *update) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.run: invalid world");
    rt_game3d_update_fn fn = game3d_update_callback_checked(
        update,
        "Game3D.World3D.run: callback must be a native function pointer; use tick/step/manual "
        "frame APIs from interpreted Zia");
    while (world && rt_game3d_world_tick(world)) {
        if (fn)
            fn(world->dt);
        if (!game3d_world_is_live(world))
            break;
        game3d_world_step_simulation_impl(world, world->dt, 0);
        if (!game3d_world_is_live(world))
            break;
        game3d_world_render_once(world, NULL);
    }
}

/// @brief Variable-timestep loop with an extra native 2D overlay callback drawn each
///   frame. See header.
void rt_game3d_world_run_with_overlay(void *obj, void *update, void *overlay) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.runWithOverlay: invalid world");
    rt_game3d_update_fn fn = game3d_update_callback_checked(
        update,
        "Game3D.World3D.runWithOverlay: update callback must be a native function pointer; use "
        "tick/step/manual frame APIs from interpreted Zia");
    rt_game3d_overlay_fn overlay_fn = game3d_overlay_callback_checked(
        overlay,
        "Game3D.World3D.runWithOverlay: overlay callback must be a native function pointer; use "
        "manual overlay calls from interpreted Zia");
    while (world && rt_game3d_world_tick(world)) {
        if (fn)
            fn(world->dt);
        if (!game3d_world_is_live(world))
            break;
        game3d_world_step_simulation_impl(world, world->dt, 0);
        if (!game3d_world_is_live(world))
            break;
        game3d_world_render_once(world, overlay_fn);
    }
}

/// @brief Fixed-timestep loop with no overlay (delegates to the overlay variant). See header.
void rt_game3d_world_run_fixed(void *obj, double step_sec, void *update) {
    rt_game3d_world_run_fixed_with_overlay(obj, step_sec, update, NULL);
}

/// @brief Fixed-timestep game loop: accumulate real frame time and run `update` + a
///   physics step in fixed `step_sec` increments, rendering once per displayed frame
///   (with an optional overlay). Decouples simulation rate from frame rate. See header.
void rt_game3d_world_run_fixed_with_overlay(void *obj,
                                            double step_sec,
                                            void *update,
                                            void *overlay) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.runFixedWithOverlay: invalid world");
    rt_game3d_update_fn fn = game3d_update_callback_checked(
        update,
        "Game3D.World3D.runFixedWithOverlay: update callback must be a native function pointer; "
        "use tick/step/manual frame APIs from interpreted Zia");
    rt_game3d_overlay_fn overlay_fn = game3d_overlay_callback_checked(
        overlay,
        "Game3D.World3D.runFixedWithOverlay: overlay callback must be a native function pointer; "
        "use manual overlay calls from interpreted Zia");
    double fixed = game3d_clamp_dt(step_sec);
    double accumulator = 0.0;
    while (world && rt_game3d_world_tick(world)) {
        int steps = 0;
        accumulator += world->dt;
        if (accumulator > fixed * RT_GAME3D_MAX_FIXED_STEPS_PER_FRAME) {
            double dropped_time = accumulator - fixed * RT_GAME3D_MAX_FIXED_STEPS_PER_FRAME;
            if (dropped_time >= fixed)
                world->dropped_fixed_steps += (int64_t)floor(dropped_time / fixed);
            accumulator = fixed * RT_GAME3D_MAX_FIXED_STEPS_PER_FRAME;
        }
        while (accumulator >= fixed && steps < RT_GAME3D_MAX_FIXED_STEPS_PER_FRAME) {
            if (fn)
                fn(fixed);
            if (!game3d_world_is_live(world))
                break;
            game3d_world_step_simulation_impl(world, fixed, 0);
            if (!game3d_world_is_live(world))
                break;
            accumulator -= fixed;
            steps++;
        }
        if (!game3d_world_is_live(world))
            break;
        if (steps >= RT_GAME3D_MAX_FIXED_STEPS_PER_FRAME && accumulator >= fixed) {
            world->dropped_fixed_steps += (int64_t)floor(accumulator / fixed);
            accumulator = 0.0;
        }
        world->fixed_interpolation_alpha = fixed > 0.0 ? accumulator / fixed : 0.0;
        if (!isfinite(world->fixed_interpolation_alpha) || world->fixed_interpolation_alpha < 0.0)
            world->fixed_interpolation_alpha = 0.0;
        if (world->fixed_interpolation_alpha >= 1.0)
            world->fixed_interpolation_alpha = 0.999999;
        game3d_world_render_once(world, overlay_fn);
    }
}

/// @brief Deterministically run a fixed number of frames at a fixed step, driving the
///   canvas from synthetic input/clock sources for reproducible tests/recordings. See header.
void rt_game3d_world_run_frames(void *obj, int64_t frame_count, double step_sec, void *update) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.runFrames: invalid world");
    rt_game3d_update_fn fn = game3d_update_callback_checked(
        update,
        "Game3D.World3D.runFrames: callback must be a native function pointer; use "
        "runFramesOnly/manual frame APIs from interpreted Zia");
    double fixed = game3d_clamp_dt(step_sec);
    rt_canvas3d *canvas = NULL;
    void *canvas_obj = NULL;
    int32_t previous_input_source = 0;
    int32_t previous_clock_source = 0;
    int64_t previous_synthetic_dt_us = 0;
    int canvas_state_saved = 0;
    int recovery_set = 0;
    int trapped = 0;
    char trap_message[512] = "";
    jmp_buf recovery;
    if (!world || frame_count < 0)
        return;
    canvas_obj = world->canvas;
    canvas = rt_canvas3d_checked_or_stack(canvas_obj);
    if (canvas) {
        rt_obj_retain_maybe(canvas_obj);
        previous_input_source = canvas->input_source;
        previous_clock_source = canvas->clock_source;
        previous_synthetic_dt_us = canvas->synthetic_dt_us;
        canvas_state_saved = 1;
        rt_canvas3d_set_input_source(world->canvas, 1);
        rt_canvas3d_set_clock_source(world->canvas, 1);
        rt_canvas3d_set_synthetic_delta_time_sec(world->canvas, fixed);
    }
    rt_trap_set_recovery(&recovery);
    recovery_set = 1;
    if (setjmp(recovery) != 0) {
        const char *msg = rt_trap_get_error();
        snprintf(trap_message,
                 sizeof(trap_message),
                 "%s",
                 (msg && msg[0]) ? msg : "Game3D.World3D.runFrames: frame callback trapped");
        trapped = 1;
    }
    if (!trapped) {
        for (int64_t i = 0; i < frame_count; ++i) {
            if (canvas)
                rt_canvas3d_advance_synthetic_frame(world->canvas);
            rt_game3d_input_update(world->input);
            world->dt = fixed;
            world->elapsed += fixed;
            world->frame += 1;
            if (fn)
                fn(fixed);
            if (!game3d_world_is_live(world))
                break;
            game3d_world_step_simulation_impl(world, fixed, 0);
            if (!game3d_world_is_live(world))
                break;
            world->fixed_interpolation_alpha = 0.0;
            game3d_world_render_once(world, NULL);
        }
    }
    if (recovery_set)
        rt_trap_clear_recovery();
    if (canvas_state_saved) {
        game3d_world_restore_run_frames_canvas(
            canvas_obj, previous_input_source, previous_clock_source, previous_synthetic_dt_us);
        game3d_release_ref(&canvas_obj);
    }
    if (trapped)
        rt_trap(trap_message);
}

/// @brief Run a fixed number of frames with no update callback (pure simulation/render).
///   See header.
void rt_game3d_world_run_frames_only(void *obj, int64_t frame_count, double step_sec) {
    rt_game3d_world_run_frames(obj, frame_count, step_sec, NULL);
}
