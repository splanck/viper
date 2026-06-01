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

// Process-wide async asset workers and main-thread commit queue. Workers stage
// model bytes/requests; the queue builds runtime objects and publishes handle
// results on the main thread.
static void *g_game3d_asset_async_pool = NULL;
static void *g_game3d_asset_commit_queue = NULL;
#define RT_GAME3D_ASSET_COMMIT_DRAIN_BUDGET 8
#define RT_GAME3D_ASSET_UPLOAD_BUDGET_DEFAULT_BYTES (16ull * 1024ull * 1024ull)
#define RT_GAME3D_ASSET_UPLOAD_SLICE_BYTES (64ull * 1024ull)
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

/// @brief Wait on the model-cache condition variable, atomically releasing/reacquiring the lock.
static void game3d_model_cache_wait_locked(void) {
    SleepConditionVariableCS(&g_game3d_model_cache_cv, &g_game3d_model_cache_lock, INFINITE);
}

/// @brief Wake all threads waiting on the model-cache condition variable.
static void game3d_model_cache_notify_all(void) {
    WakeAllConditionVariable(&g_game3d_model_cache_cv);
}
#else
static pthread_mutex_t g_game3d_model_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_game3d_model_cache_cv = PTHREAD_COND_INITIALIZER;

/// @brief Acquire the process-wide model-cache lock (statically initialized mutex).
static void game3d_model_cache_lock(void) {
    pthread_mutex_lock(&g_game3d_model_cache_lock);
}

/// @brief Release the process-wide model-cache lock.
static void game3d_model_cache_unlock(void) {
    pthread_mutex_unlock(&g_game3d_model_cache_lock);
}

/// @brief Wait on the model-cache condition variable, atomically releasing/reacquiring the lock.
static void game3d_model_cache_wait_locked(void) {
    pthread_cond_wait(&g_game3d_model_cache_cv, &g_game3d_model_cache_lock);
}

/// @brief Wake all threads waiting on the model-cache condition variable.
static void game3d_model_cache_notify_all(void) {
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
    vm_region_basic_info_data_64_t info;
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
    rt_canvas3d_set_synthetic_delta_time_sec(canvas_obj,
                                             (double)synthetic_dt_us / 1000000.0);
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
    value = game3d_finite_or(value, fallback);
    return value < 0.0 ? fallback : value;
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
    if (!body)
        return;
    void *pos = rt_body3d_get_position(body);
    rt_body3d_set_position(
        body, rt_vec3_x(pos) - delta[0], rt_vec3_y(pos) - delta[1], rt_vec3_z(pos) - delta[2]);
}

/// @brief Shift a world's particle/decal effects by the floating-origin rebase @p delta.
static void game3d_effects_rebase_origin(void *effects_obj, const double delta[3]) {
    rt_game3d_effects *effects = (rt_game3d_effects *)effects_obj;
    if (!effects || !delta)
        return;
    for (int32_t i = 0; i < effects->count; ++i) {
        rt_game3d_effect_item *item = &effects->items[i];
        if (!item || !item->object)
            continue;
        if (item->type == RT_GAME3D_EFFECT_PARTICLES)
            rt_particles3d_rebase_origin(item->object, delta[0], delta[1], delta[2]);
        else if (item->type == RT_GAME3D_EFFECT_DECAL)
            rt_decal3d_rebase_origin(item->object, delta[0], delta[1], delta[2]);
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
        game3d_finite_or(delta[0], 0.0),
        game3d_finite_or(delta[1], 0.0),
        game3d_finite_or(delta[2], 0.0),
    };
    if (clean_delta[0] == 0.0 && clean_delta[1] == 0.0 && clean_delta[2] == 0.0)
        return;
    game3d_world_require_rebase_boundary(world);
    const int scene_rebased = world->scene != NULL;
    const int physics_rebased = world->physics != NULL;
    world->world_origin[0] += clean_delta[0];
    world->world_origin[1] += clean_delta[1];
    world->world_origin[2] += clean_delta[2];

    if (scene_rebased)
        rt_scene3d_rebase_origin(world->scene, clean_delta[0], clean_delta[1], clean_delta[2]);
    if (physics_rebased)
        rt_world3d_rebase_origin(world->physics, clean_delta[0], clean_delta[1], clean_delta[2]);
    game3d_effects_rebase_origin(world->effects, clean_delta);

    for (int32_t i = 0; i < world->entity_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        if (!entity || entity->destroyed)
            continue;
        if (!scene_rebased && !entity->parent && entity->node) {
            void *pos = rt_scene_node3d_get_position(entity->node);
            rt_scene_node3d_set_position(entity->node,
                                         rt_vec3_x(pos) - clean_delta[0],
                                         rt_vec3_y(pos) - clean_delta[1],
                                         rt_vec3_z(pos) - clean_delta[2]);
        }
        if (entity->body &&
            (!physics_rebased || !rt_world3d_contains_body(world->physics, entity->body)))
            game3d_shift_body_position(entity->body, clean_delta);
    }

    if (world->camera) {
        void *camera_pos = rt_camera3d_get_position(world->camera);
        void *shifted = rt_vec3_new(rt_vec3_x(camera_pos) - clean_delta[0],
                                    rt_vec3_y(camera_pos) - clean_delta[1],
                                    rt_vec3_z(camera_pos) - clean_delta[2]);
        rt_camera3d_set_position(world->camera, shifted);
    }
    if (world->audio) {
        rt_game3d_audio *audio = (rt_game3d_audio *)world->audio;
        if (audio->listener) {
            void *listener_pos = rt_soundlistener3d_get_position(audio->listener);
            rt_soundlistener3d_set_position_vec(audio->listener,
                                                rt_vec3_x(listener_pos) - clean_delta[0],
                                                rt_vec3_y(listener_pos) - clean_delta[1],
                                                rt_vec3_z(listener_pos) - clean_delta[2]);
        }
    }
}

/// @brief Recenter the world's origin if the camera has drifted past the rebase threshold.
/// @details Computes the camera offset, and when it exceeds the threshold (and a boundary is
/// reached)
///          applies an origin rebase by that delta so coordinates stay within float-precise range.
static void game3d_world_rebase_if_needed(rt_game3d_world *world) {
    if (!world || !world->floating_origin || !world->camera)
        return;
    double eye[3] = {0.0, 0.0, 0.0};
    if (!rt_camera3d_get_position_components(world->camera, &eye[0], &eye[1], &eye[2]))
        return;
    const double threshold = game3d_rebase_threshold_or_default(world->origin_rebase_threshold);
    const double dist_sq = eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2];
    if (dist_sq < threshold * threshold)
        return;
    game3d_world_apply_origin_rebase(world, eye);
}

/// @brief Normalize a 3D input axis so combined directions do not move faster.
void game3d_normalize_axis3(double *x, double *y, double *z) {
    double vx = game3d_finite_or(x ? *x : 0.0, 0.0);
    double vy = game3d_finite_or(y ? *y : 0.0, 0.0);
    double vz = game3d_finite_or(z ? *z : 0.0, 0.0);
    double len = sqrt(vx * vx + vy * vy + vz * vz);
    if (isfinite(len) && len > 1.0) {
        vx /= len;
        vy /= len;
        vz /= len;
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
    value = game3d_finite_or(value, lo);
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/// @brief Clamp an integer `value` into [lo, hi].
int64_t game3d_clamp_i64(int64_t value, int64_t lo, int64_t hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/// @brief Read a Vec3 into `out[3]` with per-lane NaN/Inf scrubbed to 0; traps `method`
///   and returns 0 when `vec` is not a Vec3, otherwise returns 1.
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
    out[0] = game3d_finite_or(rt_vec3_x(vec), 0.0);
    out[1] = game3d_finite_or(rt_vec3_y(vec), 0.0);
    out[2] = game3d_finite_or(rt_vec3_z(vec), 0.0);
    return 1;
}

/// @brief Compute a doubled int32 capacity for an array while guarding integer
///   and byte-size overflow before the caller reaches realloc().
static int game3d_compute_capacity(
    int32_t current, int32_t needed, int32_t initial, size_t elem_size, int32_t *out_capacity) {
    int32_t capacity;
    if (!out_capacity || needed < 0 || initial <= 0 || elem_size == 0)
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

/// @brief Ensure the audio source array can hold `needed` entries, doubling capacity
///   as needed; traps and returns 0 on allocation failure, else 1.
int game3d_audio_reserve_sources(rt_game3d_audio *audio, int32_t needed) {
    int32_t new_capacity;
    if (!audio)
        return 0;
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
    for (int32_t read = 0; read < audio->source_count; ++read) {
        void *source = audio->sources[read];
        if (source && rt_soundsource3d_get_is_playing(source)) {
            audio->sources[write++] = source;
            continue;
        }
        game3d_release_ref(&audio->sources[read]);
    }
    kept = write;
    while (write < audio->source_count)
        audio->sources[write++] = NULL;
    audio->source_count = kept;
}

/// @brief Ensure the effect-item array can hold `needed` entries, doubling capacity
///   as needed; traps and returns 0 on allocation failure, else 1.
int game3d_effects_reserve(rt_game3d_effects *effects, int32_t needed) {
    int32_t new_capacity;
    if (!effects)
        return 0;
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
    if (item->type == RT_GAME3D_EFFECT_PARTICLES && item->object)
        rt_particles3d_clear(item->object);
    game3d_release_ref(&item->object);
    item->type = 0;
    item->lifetime = 0.0;
    item->age = 0.0;
}

/// @brief Return `value` if finite and strictly positive, else `fallback`.
double game3d_positive_or(double value, double fallback) {
    value = game3d_finite_or(value, fallback);
    return value > 0.0 ? value : fallback;
}

/// @brief Normalize the (x, z) ground-plane vector in place; degenerate or near-zero
///   length inputs fall back to (fallback_x, fallback_z). Used for movement headings.
void game3d_normalize_xz(double *x, double *z, double fallback_x, double fallback_z) {
    double vx = game3d_finite_or(x ? *x : fallback_x, fallback_x);
    double vz = game3d_finite_or(z ? *z : fallback_z, fallback_z);
    double len = sqrt(vx * vx + vz * vz);
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
    for (rt_game3d_entity *cursor = entity; cursor; cursor = cursor->parent) {
        if (cursor == ancestor)
            return 1;
    }
    return 0;
}

/// @brief Find a direct child index, or -1 when absent.
int32_t game3d_entity_find_child_index(rt_game3d_entity *parent, rt_game3d_entity *child) {
    if (!parent || !child)
        return -1;
    for (int32_t i = 0; i < parent->child_count; ++i) {
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
        rt_game3d_entity *owned = parent->children[index];
        for (int32_t i = index; i < parent->child_count - 1; ++i)
            parent->children[i] = parent->children[i + 1];
        parent->children[--parent->child_count] = NULL;
        child->parent = NULL;
        if (parent->node && child->node && rt_scene_node3d_get_parent(child->node) == parent->node)
            rt_scene_node3d_remove_child(parent->node, child->node);
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

/// @brief GC finalizer for a ModelTemplate: release the loaded model and the path string.
static void game3d_model_template_finalize(void *obj) {
    rt_game3d_model_template *model_template = (rt_game3d_model_template *)obj;
    if (!model_template)
        return;
    game3d_release_ref(&model_template->model);
    game3d_release_ref((void **)&model_template->path);
}

/// @brief GC finalizer for AssetHandle3D: release terminal result and error text.
static void game3d_asset_handle_finalize(void *obj) {
    rt_game3d_asset_handle *handle = (rt_game3d_asset_handle *)obj;
    if (!handle)
        return;
    game3d_release_ref(&handle->entity);
    game3d_release_ref(&handle->model_template);
    game3d_release_ref((void **)&handle->error);
    game3d_release_ref((void **)&handle->path);
}

/// @brief Ensure the global model cache can hold `need` entries, doubling and zeroing
///   new slots; returns 0 on allocation failure.
static int game3d_model_cache_grow(int32_t need) {
    int32_t new_cap;
    if (g_game3d_model_cache_capacity >= need)
        return 1;
    if (!game3d_compute_capacity(
            g_game3d_model_cache_capacity, need, 8, sizeof(rt_game3d_model_cache_entry), &new_cap))
        return 0;
    rt_game3d_model_cache_entry *grown = (rt_game3d_model_cache_entry *)realloc(
        g_game3d_model_cache, (size_t)new_cap * sizeof(*grown));
    if (!grown)
        return 0;
    memset(grown + g_game3d_model_cache_capacity,
           0,
           (size_t)(new_cap - g_game3d_model_cache_capacity) * sizeof(*grown));
    g_game3d_model_cache = grown;
    g_game3d_model_cache_capacity = new_cap;
    return 1;
}

/// @brief Compare two runtime strings for byte equality (NULLs treated as "").
static int game3d_string_equals(rt_string a, rt_string b) {
    const char *as = a ? rt_string_cstr(a) : "";
    const char *bs = b ? rt_string_cstr(b) : "";
    return as && bs && strcmp(as, bs) == 0;
}

/// @brief True when the runtime string contains at least one byte.
static int game3d_string_has_bytes(rt_string value) {
    const char *bytes = value ? rt_string_cstr(value) : "";
    return bytes && bytes[0] != '\0';
}

/// @brief Whether @p path ends in a supported 3D model extension (.vscn/.gltf/.glb/.fbx/.obj),
/// case-insensitive.
static int game3d_path_has_model_extension(const char *path) {
    static const char *const exts[] = {".vscn", ".gltf", ".glb", ".fbx", ".obj"};
    if (!path)
        return 0;
    const char *dot = strrchr(path, '.');
    if (!dot || !dot[1])
        return 0;
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
        const char *a = dot;
        const char *b = exts[i];
        while (*a && *b) {
            if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
                break;
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0')
            return 1;
    }
    return 0;
}

/// @brief Whether @p path ends in a glTF extension (.gltf/.glb), case-insensitive.
static int game3d_path_has_gltf_extension(const char *path) {
    static const char *const exts[] = {".gltf", ".glb"};
    if (!path)
        return 0;
    const char *dot = strrchr(path, '.');
    if (!dot || !dot[1])
        return 0;
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
        const char *a = dot;
        const char *b = exts[i];
        while (*a && *b) {
            if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
                break;
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0')
            return 1;
    }
    return 0;
}

/// @brief Read an entire file into a malloc'd buffer (caller frees; @p out_size set). NULL on
/// failure.
static uint8_t *game3d_asset_read_file_bytes(const char *path, size_t *out_size) {
    FILE *file;
    long len;
    uint8_t *data;
    size_t read_len;
    if (out_size)
        *out_size = 0;
    if (!path)
        return NULL;
    file = fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    len = ftell(file);
    if (len <= 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = (uint8_t *)malloc((size_t)len);
    if (!data) {
        fclose(file);
        return NULL;
    }
    read_len = fread(data, 1, (size_t)len, file);
    fclose(file);
    if (read_len != (size_t)len) {
        free(data);
        return NULL;
    }
    if (out_size)
        *out_size = read_len;
    return data;
}

/// @brief Load a model's root bytes, preferring the packed asset system and falling back to disk.
static uint8_t *game3d_asset_load_root_bytes(rt_string path, int8_t asset_path, size_t *out_size) {
    const char *path_cstr = path ? rt_string_cstr(path) : NULL;
    uint8_t *data;
    if (out_size)
        *out_size = 0;
    if (!path_cstr)
        return NULL;
    if (asset_path) {
        data = rt_asset_load_raw(path, out_size);
        if (data || strncmp(path_cstr, "asset://", 8) == 0)
            return data;
    }
    return game3d_asset_read_file_bytes(path_cstr, out_size);
}

/// @brief Saturating unsigned 64-bit addition (clamps to UINT64_MAX instead of wrapping).
static uint64_t game3d_u64_saturating_add(uint64_t a, uint64_t b) {
    if (UINT64_MAX - a < b)
        return UINT64_MAX;
    return a + b;
}

/// @brief Saturating unsigned 64-bit multiplication (clamps to UINT64_MAX on overflow).
static uint64_t game3d_u64_saturating_mul_u64(uint64_t a, uint64_t b) {
    if (a != 0 && b > UINT64_MAX / a)
        return UINT64_MAX;
    return a * b;
}

/// @brief Convert an int64 to uint64, treating negative values as 0.
static uint64_t game3d_u64_from_i64_nonnegative(int64_t value) {
    return value > 0 ? (uint64_t)value : 0;
}

/// @brief Convert a uint64 to int64, saturating values above INT64_MAX.
static int64_t game3d_i64_from_u64_saturating(uint64_t value) {
    return value > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)value;
}

/// @brief Whether @p value is already in the @p seen pointer set (dedup guard for residency
/// counting).
static int game3d_seen_ptr_contains(void **seen, int32_t seen_count, void *value) {
    if (!value)
        return 1;
    for (int32_t i = 0; i < seen_count; ++i) {
        if (seen[i] == value)
            return 1;
    }
    return 0;
}

/// @brief Estimate the resident memory of a Pixels object (width × height × 4 bytes).
static uint64_t game3d_pixels_estimate_resident_bytes(void *pixels) {
    uint64_t width;
    uint64_t height;
    uint64_t texels;

    if (!pixels)
        return 0;
    width = game3d_u64_from_i64_nonnegative(rt_pixels_width(pixels));
    height = game3d_u64_from_i64_nonnegative(rt_pixels_height(pixels));
    texels = game3d_u64_saturating_mul_u64(width, height);
    return game3d_u64_saturating_mul_u64(texels, 4u);
}

/// @brief Add a texture's resident bytes to the total, but only the first time it is seen.
/// @details Records the texture in the seen-set so a texture shared by several materials is counted
/// once.
static uint64_t game3d_count_material_texture_once(void *texture,
                                                   void **seen_textures,
                                                   int32_t *seen_count,
                                                   int32_t seen_capacity) {
    if (!texture || !seen_textures || !seen_count)
        return 0;
    if (game3d_seen_ptr_contains(seen_textures, *seen_count, texture))
        return 0;
    if (*seen_count < seen_capacity)
        seen_textures[(*seen_count)++] = texture;
    if (rt_g3d_has_class(texture, RT_G3D_TEXTUREASSET3D_CLASS_ID))
        return game3d_u64_from_i64_nonnegative(rt_textureasset3d_get_resident_bytes(texture));
    return game3d_pixels_estimate_resident_bytes(texture);
}

/// @brief Estimate the resident texture bytes of a material across all its texture slots (deduped).
static uint64_t game3d_material_estimate_texture_bytes(rt_material3d *material,
                                                       void **seen_textures,
                                                       int32_t *seen_count,
                                                       int32_t seen_capacity) {
    uint64_t total = 0;
    if (!material)
        return 0;
    total =
        game3d_u64_saturating_add(total,
                                  game3d_count_material_texture_once(
                                      material->texture, seen_textures, seen_count, seen_capacity));
    total = game3d_u64_saturating_add(
        total,
        game3d_count_material_texture_once(
            material->normal_map, seen_textures, seen_count, seen_capacity));
    total = game3d_u64_saturating_add(
        total,
        game3d_count_material_texture_once(
            material->specular_map, seen_textures, seen_count, seen_capacity));
    total = game3d_u64_saturating_add(
        total,
        game3d_count_material_texture_once(
            material->emissive_map, seen_textures, seen_count, seen_capacity));
    total = game3d_u64_saturating_add(
        total,
        game3d_count_material_texture_once(
            material->metallic_roughness_map, seen_textures, seen_count, seen_capacity));
    total =
        game3d_u64_saturating_add(total,
                                  game3d_count_material_texture_once(
                                      material->ao_map, seen_textures, seen_count, seen_capacity));
    return total;
}

/// @brief Conservative resident-byte estimate for cache policy decisions.
static uint64_t game3d_model_template_estimate_resident_bytes(
    rt_game3d_model_template *model_template) {
    void *seen_textures[128];
    int32_t seen_texture_count = 0;
    uint64_t total = sizeof(*model_template);
    if (!model_template)
        return 1;
    memset(seen_textures, 0, sizeof(seen_textures));
    if (model_template->path) {
        const char *path = rt_string_cstr(model_template->path);
        if (path)
            total = game3d_u64_saturating_add(total, (uint64_t)strlen(path) + 1u);
    }
    if (model_template->model) {
        int64_t mesh_count = rt_model3d_get_mesh_count(model_template->model);
        int64_t material_count = rt_model3d_get_material_count(model_template->model);
        int64_t skeleton_count = rt_model3d_get_skeleton_count(model_template->model);
        int64_t animation_count = rt_model3d_get_animation_count(model_template->model);
        int64_t node_count = rt_model3d_get_node_count(model_template->model);
        total = game3d_u64_saturating_add(total, 256u);
        total = game3d_u64_saturating_add(
            total,
            game3d_u64_saturating_mul_u64(game3d_u64_from_i64_nonnegative(material_count), 256u));
        total = game3d_u64_saturating_add(
            total,
            game3d_u64_saturating_mul_u64(game3d_u64_from_i64_nonnegative(skeleton_count), 1024u));
        total = game3d_u64_saturating_add(
            total,
            game3d_u64_saturating_mul_u64(game3d_u64_from_i64_nonnegative(animation_count), 2048u));
        total = game3d_u64_saturating_add(
            total,
            game3d_u64_saturating_mul_u64(game3d_u64_from_i64_nonnegative(node_count), 256u));
        for (int64_t i = 0; i < material_count; ++i) {
            rt_material3d *material =
                (rt_material3d *)rt_model3d_get_material(model_template->model, i);
            total = game3d_u64_saturating_add(
                total,
                game3d_material_estimate_texture_bytes(
                    material,
                    seen_textures,
                    &seen_texture_count,
                    (int32_t)(sizeof(seen_textures) / sizeof(seen_textures[0]))));
        }
        for (int64_t i = 0; i < mesh_count; ++i) {
            void *mesh = rt_model3d_get_mesh(model_template->model, i);
            total = game3d_u64_saturating_add(
                total, game3d_u64_from_i64_nonnegative(rt_mesh3d_get_resident_bytes(mesh)));
        }
    }
    return total > 0 ? total : 1;
}

/// @brief Estimate a mesh's resident memory from its vertex and index buffer sizes.
static uint64_t game3d_mesh_estimate_resident_bytes(void *mesh) {
    if (!mesh)
        return 0;
    return game3d_u64_from_i64_nonnegative(rt_mesh3d_get_resident_bytes(mesh));
}

/// @brief Add a mesh's resident bytes to the total only the first time it is seen (dedup).
static uint64_t game3d_count_scene_mesh_once(void *mesh,
                                             void **seen_meshes,
                                             int32_t *seen_count,
                                             int32_t seen_capacity) {
    if (!mesh || !seen_meshes || !seen_count)
        return 0;
    if (game3d_seen_ptr_contains(seen_meshes, *seen_count, mesh))
        return 0;
    if (*seen_count < seen_capacity)
        seen_meshes[(*seen_count)++] = mesh;
    return game3d_mesh_estimate_resident_bytes(mesh);
}

/// @brief Add a material's resident texture bytes to the total only the first time it is seen
/// (dedup).
static uint64_t game3d_count_scene_material_once(rt_material3d *material,
                                                 void **seen_materials,
                                                 int32_t *seen_material_count,
                                                 int32_t seen_material_capacity,
                                                 void **seen_textures,
                                                 int32_t *seen_texture_count,
                                                 int32_t seen_texture_capacity) {
    uint64_t bytes;

    if (!material || !seen_materials || !seen_material_count)
        return 0;
    if (game3d_seen_ptr_contains(seen_materials, *seen_material_count, material))
        return 0;
    if (*seen_material_count < seen_material_capacity)
        seen_materials[(*seen_material_count)++] = material;
    bytes = 256u;
    bytes = game3d_u64_saturating_add(
        bytes,
        game3d_material_estimate_texture_bytes(
            material, seen_textures, seen_texture_count, seen_texture_capacity));
    return bytes;
}

/// @brief Estimate a scene's total resident memory by walking its nodes, deduping shared
/// meshes/materials.
static uint64_t game3d_scene_estimate_resident_bytes(void *scene_obj) {
    rt_scene3d *scene = (rt_scene3d *)rt_g3d_checked_or_null(scene_obj, RT_G3D_SCENE3D_CLASS_ID);
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    void *seen_meshes[256];
    void *seen_materials[256];
    void *seen_textures[256];
    int32_t seen_mesh_count = 0;
    int32_t seen_material_count = 0;
    int32_t seen_texture_count = 0;
    uint64_t total = 0;

    if (!scene || !scene->root)
        return 0;
    memset(seen_meshes, 0, sizeof(seen_meshes));
    memset(seen_materials, 0, sizeof(seen_materials));
    memset(seen_textures, 0, sizeof(seen_textures));

    capacity = 32u;
    stack = (rt_scene_node3d **)malloc(capacity * sizeof(*stack));
    if (!stack)
        return 0;
    stack[count++] = scene->root;

    while (count > 0) {
        rt_scene_node3d *node = stack[--count];
        if (!node)
            continue;

        total = game3d_u64_saturating_add(total, (uint64_t)sizeof(*node));
        total = game3d_u64_saturating_add(
            total,
            game3d_count_scene_mesh_once(node->mesh,
                                         seen_meshes,
                                         &seen_mesh_count,
                                         (int32_t)(sizeof(seen_meshes) / sizeof(seen_meshes[0]))));
        for (int32_t i = 0; i < node->lod_count; ++i) {
            total = game3d_u64_saturating_add(
                total,
                game3d_count_scene_mesh_once(
                    node->lod_levels[i].mesh,
                    seen_meshes,
                    &seen_mesh_count,
                    (int32_t)(sizeof(seen_meshes) / sizeof(seen_meshes[0]))));
        }
        if (node->has_impostor) {
            total = game3d_u64_saturating_add(
                total,
                game3d_count_scene_mesh_once(
                    node->impostor_mesh,
                    seen_meshes,
                    &seen_mesh_count,
                    (int32_t)(sizeof(seen_meshes) / sizeof(seen_meshes[0]))));
            total = game3d_u64_saturating_add(
                total,
                game3d_count_scene_material_once(
                    (rt_material3d *)node->impostor_material,
                    seen_materials,
                    &seen_material_count,
                    (int32_t)(sizeof(seen_materials) / sizeof(seen_materials[0])),
                    seen_textures,
                    &seen_texture_count,
                    (int32_t)(sizeof(seen_textures) / sizeof(seen_textures[0]))));
        }
        total = game3d_u64_saturating_add(
            total,
            game3d_count_scene_material_once(
                (rt_material3d *)node->material,
                seen_materials,
                &seen_material_count,
                (int32_t)(sizeof(seen_materials) / sizeof(seen_materials[0])),
                seen_textures,
                &seen_texture_count,
                (int32_t)(sizeof(seen_textures) / sizeof(seen_textures[0]))));

        for (int32_t i = 0; i < node->child_count; ++i) {
            if (count >= capacity) {
                size_t new_capacity = capacity * 2u;
                rt_scene_node3d **grown;
                if (new_capacity <= capacity || new_capacity > SIZE_MAX / sizeof(*stack)) {
                    free(stack);
                    return total;
                }
                grown = (rt_scene_node3d **)realloc(stack, new_capacity * sizeof(*stack));
                if (!grown) {
                    free(stack);
                    return total;
                }
                stack = grown;
                capacity = new_capacity;
            }
            stack[count++] = node->children[i];
        }
    }
    free(stack);
    return total;
}

/// @brief Return the next monotonic model-cache access tick (for LRU recency ordering).
static uint64_t game3d_model_cache_next_tick(void) {
    if (g_game3d_model_cache_tick == UINT64_MAX) {
        for (int32_t i = 0; i < g_game3d_model_cache_count; ++i)
            g_game3d_model_cache[i].last_used = 1;
        g_game3d_model_cache_tick = 1;
    }
    return ++g_game3d_model_cache_tick;
}

/// @brief Read the current model-cache generation counter (bumped to invalidate all entries at
/// once).
static uint64_t game3d_model_cache_current_generation(void) {
    uint64_t generation;
    game3d_model_cache_lock();
    generation = g_game3d_model_cache_generation;
    game3d_model_cache_unlock();
    return generation;
}

/// @brief Whether @p generation still equals the current cache generation (i.e. not invalidated).
static int game3d_model_cache_generation_matches(uint64_t generation) {
    int matches;
    game3d_model_cache_lock();
    matches = g_game3d_model_cache_generation == generation;
    game3d_model_cache_unlock();
    return matches;
}

/// @brief Bump the cache generation, logically invalidating all existing entries (lock held).
static void game3d_model_cache_advance_generation_locked(void) {
    if (g_game3d_model_cache_generation == UINT64_MAX)
        g_game3d_model_cache_generation = 1;
    else
        g_game3d_model_cache_generation++;
}

#if !defined(_WIN32)
/// @brief Normalize a path to a canonical POSIX form for use as a stable cache key.
/// @details Collapses separators and resolves "."/".." segments so equivalent paths share a key.
/// @return 1 on success, 0 if the result would overflow @p out.
static int game3d_normalize_posix_path(const char *input, char *out, size_t out_size) {
    size_t out_len = 0;
    const char *p = input;
    if (!input || !out || out_size == 0 || input[0] != '/')
        return 0;
    out[out_len++] = '/';
    out[out_len] = '\0';
    while (*p) {
        while (*p == '/')
            p++;
        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 0 || (len == 1 && start[0] == '.'))
            continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (out_len > 1) {
                if (out[out_len - 1] == '/')
                    out_len--;
                while (out_len > 1 && out[out_len - 1] != '/')
                    out_len--;
                out[out_len] = '\0';
            }
            continue;
        }
        if (out_len > 1) {
            if (out_len + 1 >= out_size)
                return 0;
            out[out_len++] = '/';
        }
        if (out_len + len >= out_size)
            return 0;
        memcpy(out + out_len, start, len);
        out_len += len;
        out[out_len] = '\0';
    }
    return 1;
}
#endif

/// @brief Build the stable cache key for a model path.
static rt_string game3d_model_cache_key_path(rt_string path, int8_t asset_path) {
    const char *raw = path ? rt_string_cstr(path) : "";
    if (!raw)
        raw = "";
    if (asset_path)
        return rt_string_ref(path ? path : rt_const_cstr(""));
#if defined(_WIN32)
    {
        char resolved[MAX_PATH];
        DWORD len = GetFullPathNameA(raw, (DWORD)sizeof(resolved), resolved, NULL);
        if (len > 0 && len < sizeof(resolved))
            return rt_string_from_bytes(resolved, (size_t)len);
    }
#else
    {
        char resolved[PATH_MAX];
        if (realpath(raw, resolved))
            return rt_string_from_bytes(resolved, strlen(resolved));
        char absolute[PATH_MAX];
        char normalized[PATH_MAX];
        if (raw[0] == '/') {
            if (strlen(raw) < sizeof(absolute)) {
                strcpy(absolute, raw);
                if (game3d_normalize_posix_path(absolute, normalized, sizeof(normalized)))
                    return rt_string_from_bytes(normalized, strlen(normalized));
            }
        } else {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd))) {
                size_t cwd_len = strlen(cwd);
                size_t raw_len = strlen(raw);
                if (cwd_len + 1u + raw_len < sizeof(absolute)) {
                    memcpy(absolute, cwd, cwd_len);
                    absolute[cwd_len] = '/';
                    memcpy(absolute + cwd_len + 1u, raw, raw_len + 1u);
                    if (game3d_normalize_posix_path(absolute, normalized, sizeof(normalized)))
                        return rt_string_from_bytes(normalized, strlen(normalized));
                    return rt_string_from_bytes(absolute, cwd_len + 1u + raw_len);
                }
            }
        }
    }
#endif
    return rt_string_ref(path ? path : rt_const_cstr(""));
}

/// @brief Look up a model-cache entry by path + asset flag; -1 if absent.
static int32_t game3d_model_cache_find_index(rt_string path, int8_t asset_path) {
    for (int32_t i = 0; i < g_game3d_model_cache_count; ++i) {
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[i];
        if (entry->asset_path == asset_path && game3d_string_equals(entry->path, path))
            return i;
    }
    return -1;
}

/// @brief Sanitize a residency priority value (finite, clamped) for cache victim ordering.
static double game3d_model_cache_sanitize_priority(double priority) {
    if (!isfinite(priority))
        return 0.0;
    if (priority > 1000000000.0)
        return 1000000000.0;
    if (priority < -1000000000.0)
        return -1000000000.0;
    return priority;
}

/// @brief Sanitize a residency distance value (finite, non-negative) for cache victim ordering.
static double game3d_model_cache_sanitize_distance(double distance) {
    if (!isfinite(distance) || distance < 0.0)
        return DBL_MAX;
    if (distance > DBL_MAX)
        return DBL_MAX;
    return distance;
}

/// @brief Reset a cache entry's residency hint (priority/distance) to the neutral defaults.
static void game3d_model_cache_set_default_residency_hint(rt_game3d_model_cache_entry *entry) {
    if (!entry)
        return;
    entry->residency_priority = 0.0;
    entry->residency_distance = DBL_MAX;
}

/// @brief Whether @p candidate is a better (worse-to-keep) eviction victim than @p current.
/// @details Ranks by lower residency priority, then greater distance, then least-recently used;
///          loading entries are never chosen.
static int game3d_model_cache_entry_is_worse_victim(const rt_game3d_model_cache_entry *candidate,
                                                    const rt_game3d_model_cache_entry *current) {
    if (!candidate || candidate->loading)
        return 0;
    if (!current)
        return 1;
    if (candidate->residency_priority < current->residency_priority)
        return 1;
    if (candidate->residency_priority > current->residency_priority)
        return 0;
    if (candidate->residency_distance > current->residency_distance)
        return 1;
    if (candidate->residency_distance < current->residency_distance)
        return 0;
    return candidate->last_used < current->last_used;
}

/// @brief Select the index of the best eviction victim in the model cache (-1 if none evictable).
static int32_t game3d_model_cache_select_victim(void) {
    int32_t victim = -1;
    const rt_game3d_model_cache_entry *victim_entry = NULL;
    for (int32_t i = 0; i < g_game3d_model_cache_count; ++i) {
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[i];
        if (game3d_model_cache_entry_is_worse_victim(entry, victim_entry)) {
            victim = i;
            victim_entry = entry;
        }
    }
    return victim;
}

/// @brief Drop one cache entry and compact the array.
static void game3d_model_cache_remove_at(int32_t index) {
    if (index < 0 || index >= g_game3d_model_cache_count)
        return;
    if (g_game3d_model_cache[index].resident_bytes > g_game3d_model_resident_bytes)
        g_game3d_model_resident_bytes = 0;
    else
        g_game3d_model_resident_bytes -= g_game3d_model_cache[index].resident_bytes;
    game3d_release_ref((void **)&g_game3d_model_cache[index].path);
    game3d_release_ref(&g_game3d_model_cache[index].model_template);
    for (int32_t i = index; i < g_game3d_model_cache_count - 1; ++i)
        g_game3d_model_cache[i] = g_game3d_model_cache[i + 1];
    g_game3d_model_cache_count--;
    memset(&g_game3d_model_cache[g_game3d_model_cache_count], 0, sizeof(g_game3d_model_cache[0]));
}

/// @brief Enforce the fixed model-cache entry cap by evicting the least-important entry.
static void game3d_model_cache_evict_if_full(void) {
    if (g_game3d_model_cache_count < RT_GAME3D_MODEL_CACHE_MAX_ENTRIES)
        return;
    int32_t victim = game3d_model_cache_select_victim();
    if (victim >= 0)
        game3d_model_cache_remove_at(victim);
}

/// @brief Evict least-important non-loading entries until the byte budget is met.
static void game3d_model_cache_evict_to_budget(void) {
    while (g_game3d_model_resident_bytes > g_game3d_model_residency_budget_bytes) {
        int32_t victim = game3d_model_cache_select_victim();
        if (victim < 0)
            break;
        game3d_model_cache_remove_at(victim);
    }
}

/// @brief Remove a cached template by object identity, preserving external refs.
static int game3d_model_cache_evict_template(void *model_template) {
    if (!model_template)
        return 0;
    for (int32_t i = 0; i < g_game3d_model_cache_count; ++i) {
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[i];
        if (!entry->loading && entry->model_template == model_template) {
            game3d_model_cache_remove_at(i);
            return 1;
        }
    }
    return 0;
}

/// @brief Update the cache entry backing @p model_template with a residency priority/distance hint.
/// @details Drives victim selection so high-priority/near models stay resident longer under budget.
static int game3d_model_cache_set_template_residency_hint(void *model_template,
                                                          double priority,
                                                          double distance) {
    if (!model_template)
        return 0;
    for (int32_t i = 0; i < g_game3d_model_cache_count; ++i) {
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[i];
        if (!entry->loading && entry->model_template == model_template) {
            entry->residency_priority = game3d_model_cache_sanitize_priority(priority);
            entry->residency_distance = game3d_model_cache_sanitize_distance(distance);
            entry->last_used = game3d_model_cache_next_tick();
            return 1;
        }
    }
    return 0;
}

/// @brief Allocate a ModelTemplate retaining the path and loaded model; traps on OOM.
static rt_game3d_model_template *game3d_model_template_new(rt_string path,
                                                           int8_t asset_path,
                                                           void *model) {
    rt_game3d_model_template *model_template = (rt_game3d_model_template *)rt_obj_new_i64(
        RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID, (int64_t)sizeof(*model_template));
    if (!model_template) {
        rt_trap("Game3D.ModelTemplate: allocation failed");
        return NULL;
    }
    memset(model_template, 0, sizeof(*model_template));
    rt_obj_set_finalizer(model_template, game3d_model_template_finalize);
    model_template->asset_path = asset_path ? 1 : 0;
    game3d_assign_ref((void **)&model_template->path, path ? path : rt_const_cstr(""));
    game3d_assign_ref(&model_template->model, model);
    return model_template;
}

/// @brief Allocate a terminal AssetHandle3D retaining its result object.
static rt_game3d_asset_handle *game3d_asset_handle_new(
    int8_t ready, double progress, rt_string error, void *entity, void *model_template) {
    rt_game3d_asset_handle *handle = (rt_game3d_asset_handle *)rt_obj_new_i64(
        RT_G3D_GAME3D_ASSET_HANDLE3D_CLASS_ID, (int64_t)sizeof(*handle));
    if (!handle) {
        rt_trap("Game3D.AssetHandle3D: allocation failed");
        return NULL;
    }
    memset(handle, 0, sizeof(*handle));
    rt_obj_set_finalizer(handle, game3d_asset_handle_finalize);
    handle->ready = ready ? 1 : 0;
    handle->progress = game3d_finite_or(progress, 0.0);
    if (handle->progress < 0.0)
        handle->progress = 0.0;
    if (handle->progress > 1.0)
        handle->progress = 1.0;
    game3d_assign_ref((void **)&handle->error, error ? error : rt_const_cstr(""));
    game3d_assign_ref(&handle->entity, entity);
    game3d_assign_ref(&handle->model_template, model_template);
    return handle;
}

/// @brief Create a deferred AssetHandle3D request that starts on first observation.
static rt_game3d_asset_handle *game3d_asset_handle_pending(rt_string path,
                                                           int8_t asset_path,
                                                           int8_t template_request) {
    rt_game3d_asset_handle *handle = game3d_asset_handle_new(0, 0.0, rt_const_cstr(""), NULL, NULL);
    if (!handle)
        return NULL;
    handle->deferred = 1;
    handle->asset_path = asset_path ? 1 : 0;
    handle->template_request = template_request ? 1 : 0;
    game3d_assign_ref((void **)&handle->path, path ? path : rt_const_cstr(""));
    return handle;
}

/// @brief Validate an async asset request up front, returning an error string if it cannot start.
/// @return NULL if the request is valid, else an owned error message.
static rt_string game3d_asset_handle_preflight_error(rt_game3d_asset_handle *handle) {
    const char *path = handle && handle->path ? rt_string_cstr(handle->path) : NULL;
    if (!path || !*path)
        return rt_const_cstr("invalid path");
    if (handle->asset_path) {
        if (!rt_asset_exists(handle->path)) {
            if (strncmp(path, "asset://", 8) == 0)
                return rt_const_cstr("asset not found");
            FILE *asset_file = fopen(path, "rb");
            if (!asset_file)
                return rt_const_cstr("asset not found");
            fclose(asset_file);
        }
        return game3d_path_has_model_extension(path) ? rt_const_cstr("")
                                                     : rt_const_cstr("unsupported file extension");
    }
    FILE *file = fopen(path, "rb");
    if (!file)
        return rt_const_cstr("cannot read file");
    fclose(file);
    return game3d_path_has_model_extension(path) ? rt_const_cstr("")
                                                 : rt_const_cstr("unsupported file extension");
}

/// @brief Return the load error recorded on an asset handle (NULL if it loaded successfully).
static rt_string game3d_asset_handle_load_error(rt_game3d_asset_handle *handle) {
    if (handle && handle->asset_path)
        return rt_const_cstr("failed to load model asset");
    return rt_const_cstr("failed to load model");
}

/// @brief Lazily initialize the shared async-asset runtime (worker thread + commit queue).
/// @return 1 if the runtime is ready, 0 on initialization failure.
static int game3d_asset_async_ensure_runtime(void) {
    if (!g_game3d_asset_commit_queue) {
        g_game3d_asset_commit_queue = rt_g3d_commit_queue_new();
        if (!g_game3d_asset_commit_queue)
            return 0;
    }
    if (!g_game3d_asset_async_pool) {
        g_game3d_asset_async_pool = rt_threadpool_new(game3d_default_worker_count());
        if (!g_game3d_asset_async_pool)
            return 0;
    }
    return 1;
}

/// @brief Run pending main-thread asset commits produced by the async worker (budget-paced).
static void game3d_asset_async_drain_commits(void) {
    if (g_game3d_asset_commit_queue) {
        uint64_t upload_budget =
            __atomic_load_n(&g_game3d_asset_upload_budget_bytes, __ATOMIC_RELAXED);
        (void)rt_g3d_commit_queue_drain_budget(
            g_game3d_asset_commit_queue, RT_GAME3D_ASSET_COMMIT_DRAIN_BUDGET, upload_budget);
    }
}

/// @brief Retain and return a cached model template if one is already loaded for the path (else
/// NULL).
static rt_game3d_model_template *game3d_model_cache_try_retain_ready(rt_string cache_path,
                                                                     int8_t asset_path) {
    rt_game3d_model_template *model_template = NULL;
    int32_t index;
    game3d_model_cache_lock();
    index = game3d_model_cache_find_index(cache_path, asset_path);
    if (index >= 0) {
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[index];
        if (!entry->loading && entry->model_template) {
            entry->last_used = game3d_model_cache_next_tick();
            rt_obj_retain_maybe(entry->model_template);
            model_template = (rt_game3d_model_template *)entry->model_template;
        }
    }
    game3d_model_cache_unlock();
    return model_template;
}

/// @brief Insert a freshly loaded model template into the cache (evicting a victim if at budget).
static rt_game3d_model_template *game3d_model_cache_store_loaded_template(rt_string cache_path,
                                                                          int8_t asset_path,
                                                                          void *loaded_model) {
    if (!loaded_model)
        return NULL;
    rt_game3d_model_template *model_template =
        game3d_model_template_new(cache_path, asset_path, loaded_model);
    if (!model_template)
        return NULL;

    game3d_model_cache_lock();
    int32_t index = game3d_model_cache_find_index(cache_path, asset_path);
    if (index >= 0) {
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[index];
        if (!entry->loading && entry->model_template) {
            rt_game3d_model_template *existing = (rt_game3d_model_template *)entry->model_template;
            entry->last_used = game3d_model_cache_next_tick();
            rt_obj_retain_maybe(existing);
            game3d_model_cache_unlock();
            if (rt_obj_release_check0(model_template))
                rt_obj_free(model_template);
            return existing;
        }
        game3d_model_cache_remove_at(index);
    }

    game3d_model_cache_evict_if_full();
    if (g_game3d_model_cache_count >= RT_GAME3D_MODEL_CACHE_MAX_ENTRIES ||
        !game3d_model_cache_grow(g_game3d_model_cache_count + 1)) {
        game3d_model_cache_unlock();
        if (rt_obj_release_check0(model_template))
            rt_obj_free(model_template);
        return NULL;
    }

    index = g_game3d_model_cache_count++;
    rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[index];
    memset(entry, 0, sizeof(*entry));
    game3d_model_cache_set_default_residency_hint(entry);
    game3d_assign_ref((void **)&entry->path, cache_path ? cache_path : rt_const_cstr(""));
    entry->asset_path = asset_path ? 1 : 0;
    game3d_assign_ref(&entry->model_template, model_template);
    entry->resident_bytes = game3d_model_template_estimate_resident_bytes(model_template);
    g_game3d_model_resident_bytes =
        game3d_u64_saturating_add(g_game3d_model_resident_bytes, entry->resident_bytes);
    entry->last_used = game3d_model_cache_next_tick();
    game3d_model_cache_evict_to_budget();
    game3d_model_cache_notify_all();
    game3d_model_cache_unlock();
    return model_template;
}

/// @brief Free an async asset job and all of its owned buffers/handles.
static void game3d_asset_async_job_free(rt_game3d_asset_async_job *job) {
    if (!job)
        return;
    rt_gltf_preload_bundle_free(job->preloaded_gltf);
    if (job->handle && rt_obj_release_check0(job->handle))
        rt_obj_free(job->handle);
    free(job);
}

/// @brief Estimate the main-thread cost (in budget units) of committing an async job's result.
static uint64_t game3d_asset_async_job_commit_cost(rt_game3d_asset_async_job *job) {
    size_t decoded_image_bytes;
    if (!job || job->error[0])
        return 0u;
    decoded_image_bytes = rt_gltf_preload_bundle_decoded_image_bytes(job->preloaded_gltf);
    if (decoded_image_bytes > UINT64_MAX)
        return UINT64_MAX;
    return decoded_image_bytes > 0u ? (uint64_t)decoded_image_bytes : 1u;
}

/// @brief Cost of the next incremental upload slice for an async job (for budget-paced streaming).
static uint64_t game3d_asset_async_next_upload_slice_cost(rt_game3d_asset_async_job *job) {
    size_t slice_bytes;
    if (!job || job->error[0] || !job->preloaded_gltf)
        return 0u;
    slice_bytes = rt_gltf_preload_bundle_next_decoded_image_slice_bytes(
        job->preloaded_gltf, (size_t)RT_GAME3D_ASSET_UPLOAD_SLICE_BYTES);
    if (slice_bytes > UINT64_MAX)
        return UINT64_MAX;
    return (uint64_t)slice_bytes;
}

static void game3d_asset_async_commit(void *user_data);
static void game3d_asset_async_prepare_upload_slice(void *user_data);

/// @brief Enqueue the next main-thread commit step for an async job onto the commit queue.
/// @return 1 if a commit was enqueued, 0 if the job is finished.
static int game3d_asset_async_enqueue_next_commit(rt_game3d_asset_async_job *job) {
    uint64_t slice_cost = game3d_asset_async_next_upload_slice_cost(job);
    if (slice_cost > 0u) {
        return rt_g3d_commit_queue_enqueue_cost(
            g_game3d_asset_commit_queue, game3d_asset_async_prepare_upload_slice, job, slice_cost);
    }
    return rt_g3d_commit_queue_enqueue_cost(g_game3d_asset_commit_queue,
                                            game3d_asset_async_commit,
                                            job,
                                            game3d_asset_async_job_commit_cost(job));
}

/// @brief Build the final Model3D on the main thread from an async job's preloaded glTF bundle.
/// @return The loaded model, or NULL on failure (error recorded on the job).
static void *game3d_asset_async_commit_load_model(rt_game3d_asset_async_job *job) {
    rt_game3d_asset_handle *handle = job ? job->handle : NULL;
    void *loaded_model = NULL;
    jmp_buf recovery;
    if (!job || !handle)
        return NULL;

    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        if (job->preloaded_gltf) {
            rt_gltf_preload_bundle *bundle = job->preloaded_gltf;
            job->preloaded_gltf = NULL;
            loaded_model =
                rt_model3d_load_preloaded_gltf_bundle(handle->path, bundle, handle->asset_path);
        } else {
            loaded_model = handle->asset_path ? rt_model3d_load_asset(handle->path)
                                              : rt_model3d_load(handle->path);
        }
        if (!loaded_model && !job->error[0]) {
            const char *fallback =
                handle->asset_path ? "failed to load model asset" : "failed to load model";
            snprintf(job->error, sizeof(job->error), "%s", fallback);
        }
    } else {
        const char *msg = rt_trap_get_error();
        snprintf(job->error,
                 sizeof(job->error),
                 "%s",
                 (msg && msg[0]) ? msg
                                 : (handle->asset_path ? "failed to load model asset"
                                                       : "failed to load model"));
    }
    rt_trap_clear_recovery();
    return loaded_model;
}

/// @brief Commit-queue callback: prepare one budgeted texture-upload slice for an async job.
static void game3d_asset_async_prepare_upload_slice(void *user_data) {
    rt_game3d_asset_async_job *job = (rt_game3d_asset_async_job *)user_data;
    rt_game3d_asset_handle *handle = job ? job->handle : NULL;
    size_t prepared = 0u;
    jmp_buf recovery;
    if (!job || !handle) {
        game3d_asset_async_job_free(job);
        return;
    }

    if (handle->ready || handle->cancelled) {
        game3d_asset_async_job_free(job);
        return;
    }

    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        prepared = rt_gltf_preload_bundle_prepare_decoded_image_slice(
            job->preloaded_gltf, (size_t)RT_GAME3D_ASSET_UPLOAD_SLICE_BYTES);
    } else {
        const char *msg = rt_trap_get_error();
        snprintf(job->error,
                 sizeof(job->error),
                 "%s",
                 (msg && msg[0]) ? msg : "failed to prepare texture upload");
    }
    rt_trap_clear_recovery();

    if (!job->error[0]) {
        if (prepared == 0u && game3d_asset_async_next_upload_slice_cost(job) > 0u)
            snprintf(job->error, sizeof(job->error), "%s", "failed to prepare texture upload");
        else {
            job->upload_prepared_bytes =
                game3d_u64_saturating_add(job->upload_prepared_bytes, (uint64_t)prepared);
            if (job->upload_total_bytes > 0u) {
                double ratio = (double)job->upload_prepared_bytes / (double)job->upload_total_bytes;
                if (ratio > 1.0)
                    ratio = 1.0;
                handle->progress = 0.10 + ratio * 0.70;
            } else {
                handle->progress = 0.50;
            }
        }
    }

    if (!game3d_asset_async_enqueue_next_commit(job))
        game3d_asset_async_job_free(job);
}

/// @brief Commit-queue callback: finalize an async job on the main thread (build model, store,
/// notify).
static void game3d_asset_async_commit(void *user_data) {
    rt_game3d_asset_async_job *job = (rt_game3d_asset_async_job *)user_data;
    rt_game3d_asset_handle *handle = job ? job->handle : NULL;
    void *loaded_model = NULL;
    if (!job || !handle) {
        game3d_asset_async_job_free(job);
        return;
    }

    if (!handle->ready && !handle->cancelled) {
        if (!job->error[0])
            loaded_model = game3d_asset_async_commit_load_model(job);
        if (loaded_model) {
            if (handle->template_request) {
                rt_string cache_path =
                    game3d_model_cache_key_path(handle->path, handle->asset_path);
                rt_game3d_model_template *model_template = NULL;
                if (game3d_model_cache_generation_matches(job->cache_generation)) {
                    model_template = game3d_model_cache_store_loaded_template(
                        cache_path, handle->asset_path, loaded_model);
                } else {
                    model_template =
                        game3d_model_template_new(cache_path, handle->asset_path, loaded_model);
                }
                if (model_template) {
                    game3d_assign_ref(&handle->model_template, model_template);
                    game3d_release_ref((void **)&model_template);
                } else {
                    game3d_assign_ref((void **)&handle->error,
                                      game3d_asset_handle_load_error(handle));
                }
                game3d_release_ref((void **)&cache_path);
            } else {
                rt_game3d_model_template *model_template =
                    game3d_model_template_new(handle->path, handle->asset_path, loaded_model);
                void *entity =
                    model_template ? rt_game3d_model_template_instantiate(model_template) : NULL;
                if (entity) {
                    game3d_assign_ref(&handle->entity, entity);
                    game3d_release_ref(&entity);
                } else {
                    game3d_assign_ref((void **)&handle->error,
                                      rt_const_cstr("failed to load model"));
                }
                if (model_template && rt_obj_release_check0(model_template))
                    rt_obj_free(model_template);
            }
        } else {
            if (job->error[0]) {
                rt_string error = rt_string_from_bytes(job->error, strlen(job->error));
                game3d_assign_ref((void **)&handle->error,
                                  error ? error : game3d_asset_handle_load_error(handle));
                game3d_release_ref((void **)&error);
            } else {
                game3d_assign_ref((void **)&handle->error, game3d_asset_handle_load_error(handle));
            }
        }
        handle->deferred = 0;
        handle->ready = 1;
        handle->progress = 1.0;
    }

    game3d_release_ref(&loaded_model);
    game3d_asset_async_job_free(job);
}

/// @brief Worker-thread entry point: stage a model's preload bundle off the main thread, then
/// enqueue commits.
static void game3d_asset_async_worker(void *user_data) {
    rt_game3d_asset_async_job *job = (rt_game3d_asset_async_job *)user_data;
    rt_game3d_asset_handle *handle = job ? job->handle : NULL;
    const char *path = handle && handle->path ? rt_string_cstr(handle->path) : NULL;
    uint8_t *root_data = NULL;
    size_t root_len = 0;
    if (!job || !handle)
        return;

    if (game3d_path_has_gltf_extension(path)) {
        root_data = game3d_asset_load_root_bytes(handle->path, handle->asset_path, &root_len);
        if (!root_data) {
            const char *fallback =
                handle->asset_path ? "failed to load model asset" : "failed to load model";
            snprintf(job->error, sizeof(job->error), "%s", fallback);
        } else {
            job->preloaded_gltf = rt_gltf_preload_bundle_create(handle->path,
                                                                root_data,
                                                                root_len,
                                                                handle->asset_path,
                                                                job->error,
                                                                sizeof(job->error));
            root_data = NULL;
            job->upload_total_bytes =
                (uint64_t)rt_gltf_preload_bundle_decoded_image_bytes(job->preloaded_gltf);
            if (!job->preloaded_gltf && !job->error[0]) {
                const char *fallback =
                    handle->asset_path ? "failed to load model asset" : "failed to load model";
                snprintf(job->error, sizeof(job->error), "%s", fallback);
            }
        }
    }

    if (!game3d_asset_async_enqueue_next_commit(job))
        game3d_asset_async_job_free(job);
}

/// @brief Kick off asynchronous loading for an asset handle (preflight, then dispatch a worker
/// job).
static void game3d_asset_handle_start_async(rt_game3d_asset_handle *handle) {
    if (!handle || handle->ready || handle->cancelled || !handle->deferred || handle->async_started)
        return;

    rt_string preflight_error = game3d_asset_handle_preflight_error(handle);
    if (game3d_string_has_bytes(preflight_error)) {
        handle->deferred = 0;
        handle->ready = 1;
        handle->progress = 1.0;
        game3d_assign_ref((void **)&handle->error, preflight_error);
        return;
    }

    if (handle->template_request) {
        rt_string cache_path = game3d_model_cache_key_path(handle->path, handle->asset_path);
        rt_game3d_model_template *cached =
            game3d_model_cache_try_retain_ready(cache_path, handle->asset_path);
        if (cached) {
            game3d_assign_ref(&handle->model_template, cached);
            game3d_release_ref((void **)&cached);
            handle->deferred = 0;
            handle->ready = 1;
            handle->progress = 1.0;
            game3d_release_ref((void **)&cache_path);
            return;
        }
        game3d_release_ref((void **)&cache_path);
    }

    if (!game3d_asset_async_ensure_runtime()) {
        handle->deferred = 0;
        handle->ready = 1;
        handle->progress = 1.0;
        game3d_assign_ref((void **)&handle->error, rt_const_cstr("failed to schedule asset load"));
        return;
    }

    rt_game3d_asset_async_job *job =
        (rt_game3d_asset_async_job *)calloc(1, sizeof(rt_game3d_asset_async_job));
    if (!job) {
        handle->deferred = 0;
        handle->ready = 1;
        handle->progress = 1.0;
        game3d_assign_ref((void **)&handle->error, rt_const_cstr("failed to schedule asset load"));
        return;
    }

    job->handle = handle;
    job->cache_generation = game3d_model_cache_current_generation();
    rt_obj_retain_maybe(handle);
    handle->async_started = 1;
    if (!rt_threadpool_submit(
            g_game3d_asset_async_pool, game3d_task_fnptr(game3d_asset_async_worker), job)) {
        handle->async_started = 0;
        handle->deferred = 0;
        handle->ready = 1;
        handle->progress = 1.0;
        game3d_assign_ref((void **)&handle->error, rt_const_cstr("failed to schedule asset load"));
        game3d_asset_async_job_free(job);
    }
}

/// @brief Advance an asset handle's async state: drain commits and, if requested, start loading.
static void game3d_asset_handle_service(rt_game3d_asset_handle *handle, int start_if_needed) {
    game3d_asset_async_drain_commits();
    if (start_if_needed)
        game3d_asset_handle_start_async(handle);
}

/// @brief Load a model from disk/asset and wrap it in a new ModelTemplate, bypassing the
///   cache; traps `method` if the model fails to load.
static rt_game3d_model_template *game3d_assets_load_template_uncached_impl(rt_string path,
                                                                           int8_t asset_path,
                                                                           const char *method,
                                                                           int trap_on_fail) {
    void *model = asset_path ? rt_model3d_load_asset(path) : rt_model3d_load(path);
    rt_game3d_model_template *model_template;
    if (!model) {
        if (trap_on_fail)
            rt_trap(method);
        return NULL;
    }
    model_template = game3d_model_template_new(path, asset_path, model);
    game3d_release_ref(&model);
    return model_template;
}

/// @brief Synchronously load a model template from a path, bypassing the cache.
/// @details Reads the root bytes, parses by extension (glTF/vscn/etc.), and wraps the result as a
///          model template. Returns NULL (with an error out-param) on failure.
static rt_game3d_model_template *game3d_assets_load_template_uncached(rt_string path,
                                                                      int8_t asset_path,
                                                                      const char *method) {
    return game3d_assets_load_template_uncached_impl(path, asset_path, method, 1);
}

/// @brief Return the cached ModelTemplate for (path, asset_path), loading and inserting
///   it on a miss; returns a retained template. Traps on load or cache-grow failure.
static rt_game3d_model_template *game3d_assets_load_template_cached_impl(rt_string path,
                                                                         int8_t asset_path,
                                                                         const char *method,
                                                                         int trap_on_fail) {
    rt_game3d_model_template *model_template;
    rt_string cache_path = game3d_model_cache_key_path(path, asset_path);
    int32_t pending_index = -1;
    for (;;) {
        int32_t index;
        game3d_model_cache_lock();
        index = game3d_model_cache_find_index(cache_path, asset_path);
        if (index >= 0) {
            rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[index];
            if (entry->loading) {
                game3d_model_cache_wait_locked();
                game3d_model_cache_unlock();
                continue;
            }
            if (entry->model_template) {
                rt_obj_retain_maybe(entry->model_template);
                entry->last_used = game3d_model_cache_next_tick();
                model_template = (rt_game3d_model_template *)entry->model_template;
                game3d_model_cache_unlock();
                game3d_release_ref((void **)&cache_path);
                return model_template;
            }
            game3d_model_cache_remove_at(index);
        }
        game3d_model_cache_evict_if_full();
        if (g_game3d_model_cache_count >= RT_GAME3D_MODEL_CACHE_MAX_ENTRIES) {
            game3d_model_cache_wait_locked();
            game3d_model_cache_unlock();
            continue;
        }
        if (!game3d_model_cache_grow(g_game3d_model_cache_count + 1)) {
            game3d_model_cache_unlock();
            game3d_release_ref((void **)&cache_path);
            rt_trap("Game3D.Assets3D: model cache allocation failed");
            return NULL;
        }
        pending_index = g_game3d_model_cache_count++;
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[pending_index];
        memset(entry, 0, sizeof(*entry));
        game3d_model_cache_set_default_residency_hint(entry);
        game3d_assign_ref((void **)&entry->path, cache_path ? cache_path : rt_const_cstr(""));
        entry->asset_path = asset_path ? 1 : 0;
        entry->loading = 1;
        entry->last_used = game3d_model_cache_next_tick();
        game3d_model_cache_unlock();
        break;
    }

    void *loaded_model =
        asset_path ? rt_model3d_load_asset(cache_path) : rt_model3d_load(cache_path);
    if (!loaded_model) {
        game3d_model_cache_lock();
        pending_index = game3d_model_cache_find_index(cache_path, asset_path);
        if (pending_index >= 0 && g_game3d_model_cache[pending_index].loading) {
            game3d_model_cache_remove_at(pending_index);
            game3d_model_cache_notify_all();
        }
        game3d_model_cache_unlock();
        game3d_release_ref((void **)&cache_path);
        if (trap_on_fail)
            rt_trap(method);
        return NULL;
    }
    model_template = game3d_model_template_new(cache_path, asset_path, loaded_model);
    game3d_release_ref(&loaded_model);
    if (!model_template) {
        game3d_model_cache_lock();
        pending_index = game3d_model_cache_find_index(cache_path, asset_path);
        if (pending_index >= 0 && g_game3d_model_cache[pending_index].loading) {
            game3d_model_cache_remove_at(pending_index);
            game3d_model_cache_notify_all();
        }
        game3d_model_cache_unlock();
        game3d_release_ref((void **)&cache_path);
        return NULL;
    }

    game3d_model_cache_lock();
    pending_index = game3d_model_cache_find_index(cache_path, asset_path);
    if (pending_index < 0) {
        game3d_model_cache_unlock();
        if (rt_obj_release_check0(model_template))
            rt_obj_free(model_template);
        game3d_release_ref((void **)&cache_path);
        rt_trap("Game3D.Assets3D: pending cache entry was lost");
        return NULL;
    }
    rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[pending_index];
    game3d_assign_ref(&entry->model_template, model_template);
    entry->resident_bytes = game3d_model_template_estimate_resident_bytes(model_template);
    g_game3d_model_resident_bytes =
        game3d_u64_saturating_add(g_game3d_model_resident_bytes, entry->resident_bytes);
    entry->loading = 0;
    entry->last_used = game3d_model_cache_next_tick();
    game3d_model_cache_evict_to_budget();
    game3d_model_cache_notify_all();
    game3d_model_cache_unlock();
    game3d_release_ref((void **)&cache_path);
    return model_template;
}

/// @brief Load a model template through the cache: return a retained ready entry or load-and-store
/// it.
static rt_game3d_model_template *game3d_assets_load_template_cached(rt_string path,
                                                                    int8_t asset_path,
                                                                    const char *method) {
    return game3d_assets_load_template_cached_impl(path, asset_path, method, 1);
}

/// @brief Wrap a loaded model's root node as an entity, attaching its animator if the
///   node carries one. Returns NULL on a NULL root.
static void *game3d_entity_from_model_root(void *root) {
    rt_game3d_entity *entity;
    void *animator;
    if (!root)
        return NULL;
    entity = (rt_game3d_entity *)rt_game3d_entity_from_node(root);
    if (!entity)
        return NULL;
    animator = rt_scene_node3d_get_animator(root);
    if (animator)
        rt_game3d_entity_attach_animator(entity, animator);
    return entity;
}

/// @brief Get the loaded model backing the template (NULL if invalid).
void *rt_game3d_model_template_get_model(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.get_model: invalid template");
    return model_template ? model_template->model : NULL;
}

/// @brief Get the source path the template was loaded from ("" if invalid).
rt_string rt_game3d_model_template_get_path(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.get_path: invalid template");
    return model_template && model_template->path ? model_template->path : rt_const_cstr("");
}

/// @brief True if the template was loaded from a packed asset (0 if invalid).
int8_t rt_game3d_model_template_get_is_asset(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.get_isAsset: invalid template");
    return model_template ? model_template->asset_path : 0;
}

/// @brief Instantiate a fresh entity by cloning the template's model. See header.
void *rt_game3d_model_template_instantiate(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.instantiate: invalid template");
    if (!model_template || !model_template->model)
        return NULL;
    void *root = rt_model3d_instantiate(model_template->model);
    void *entity = game3d_entity_from_model_root(root);
    game3d_release_ref(&root);
    return entity;
}

/// @brief Load a filesystem model (uncached) and return a ready entity. See header.
void *rt_game3d_assets_load_model(rt_string path) {
    rt_game3d_model_template *model_template = game3d_assets_load_template_uncached(
        path, 0, "Game3D.Assets3D.LoadModel: failed to load model");
    if (!model_template)
        return NULL;
    void *entity = rt_game3d_model_template_instantiate(model_template);
    if (rt_obj_release_check0(model_template))
        rt_obj_free(model_template);
    return entity;
}

/// @brief Load a packed-asset model (uncached) and return a ready entity. See header.
void *rt_game3d_assets_load_model_asset(rt_string path) {
    rt_game3d_model_template *model_template = game3d_assets_load_template_uncached(
        path, 1, "Game3D.Assets3D.LoadModelAsset: failed to load model asset");
    if (!model_template)
        return NULL;
    void *entity = rt_game3d_model_template_instantiate(model_template);
    if (rt_obj_release_check0(model_template))
        rt_obj_free(model_template);
    return entity;
}

/// @brief Load a filesystem model as a cached reusable template. See header.
void *rt_game3d_assets_load_model_template(rt_string path) {
    return game3d_assets_load_template_cached(
        path, 0, "Game3D.Assets3D.LoadModelTemplate: failed to load model");
}

/// @brief Load a packed-asset model as a cached reusable template. See header.
void *rt_game3d_assets_load_model_template_asset(rt_string path) {
    return game3d_assets_load_template_cached(
        path, 1, "Game3D.Assets3D.LoadModelTemplateAsset: failed to load model asset");
}

/// @brief Finalize a synchronous (deferred) asset handle whose result was produced inline.
static void game3d_asset_handle_complete_if_deferred(rt_game3d_asset_handle *handle) {
    game3d_asset_handle_service(handle, 1);
}

/// @brief Load a filesystem model and expose the terminal result through AssetHandle3D.
void *rt_game3d_assets_load_model_async(rt_string path) {
    return game3d_asset_handle_pending(path, 0, 0);
}

/// @brief Load a packed-asset model and expose the terminal result through AssetHandle3D.
void *rt_game3d_assets_load_model_asset_async(rt_string path) {
    return game3d_asset_handle_pending(path, 1, 0);
}

/// @brief Load a filesystem ModelTemplate and expose it through AssetHandle3D.
void *rt_game3d_assets_load_model_template_async(rt_string path) {
    return game3d_asset_handle_pending(path, 0, 1);
}

/// @brief Load a packed-asset ModelTemplate and expose it through AssetHandle3D.
void *rt_game3d_assets_load_model_template_asset_async(rt_string path) {
    return game3d_asset_handle_pending(path, 1, 1);
}

/// @brief Set the process-wide ModelTemplate cache budget. Negative means unlimited.
void rt_game3d_assets_set_residency_budget(int64_t bytes) {
    uint64_t budget = bytes < 0 ? UINT64_MAX : (uint64_t)bytes;
    game3d_model_cache_lock();
    g_game3d_model_residency_budget_bytes = budget;
    game3d_model_cache_evict_to_budget();
    game3d_model_cache_notify_all();
    game3d_model_cache_unlock();
}

/// @brief Return estimated bytes currently resident in the shared ModelTemplate cache.
int64_t rt_game3d_assets_get_resident_bytes(void) {
    uint64_t bytes;
    game3d_model_cache_lock();
    bytes = g_game3d_model_resident_bytes;
    game3d_model_cache_unlock();
    return game3d_i64_from_u64_saturating(bytes);
}

/// @brief Annotate a cached ModelTemplate for priority/distance-aware eviction.
void rt_game3d_assets_set_residency_hint(void *model_template_obj,
                                         double priority,
                                         double distance) {
    rt_game3d_model_template *model_template = game3d_model_template_checked(
        model_template_obj, "Game3D.Assets3D.SetResidencyHint: invalid template");
    if (!model_template)
        return;
    game3d_model_cache_lock();
    if (game3d_model_cache_set_template_residency_hint(model_template, priority, distance))
        game3d_model_cache_evict_to_budget();
    game3d_model_cache_notify_all();
    game3d_model_cache_unlock();
}

/// @brief Set the process-wide async asset upload budget. Negative means unlimited.
void rt_game3d_assets_set_upload_budget(int64_t bytes) {
    uint64_t budget = bytes < 0 ? UINT64_MAX : (uint64_t)bytes;
    __atomic_store_n(&g_game3d_asset_upload_budget_bytes, budget, __ATOMIC_RELAXED);
}

/// @brief Evict the cached template backing a ready template AssetHandle3D.
void rt_game3d_assets_evict(void *asset_handle) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(asset_handle, "Game3D.Assets3D.Evict: invalid handle");
    if (!handle || !handle->ready || handle->cancelled || game3d_string_has_bytes(handle->error) ||
        !handle->model_template)
        return;
    game3d_model_cache_lock();
    if (game3d_model_cache_evict_template(handle->model_template))
        game3d_model_cache_notify_all();
    game3d_model_cache_unlock();
}

/// @brief True once the request has completed or been cancelled. See header.
int8_t rt_game3d_asset_handle_get_ready(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.ready: invalid handle");
    game3d_asset_handle_complete_if_deferred(handle);
    return handle && handle->ready ? 1 : 0;
}

/// @brief Terminal or in-flight loading progress. See header.
double rt_game3d_asset_handle_get_progress(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.progress: invalid handle");
    game3d_asset_handle_service(handle, 1);
    return handle ? handle->progress : 0.0;
}

/// @brief Return terminal error text, or "" on success / pending work. See header.
rt_string rt_game3d_asset_handle_get_error(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.error: invalid handle");
    game3d_asset_handle_service(handle, 1);
    return handle && handle->error ? handle->error : rt_const_cstr("");
}

/// @brief Cancel a pending request. Current completed handles intentionally no-op.
void rt_game3d_asset_handle_cancel(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.cancel: invalid handle");
    if (!handle || handle->ready)
        return;
    handle->cancelled = 1;
    handle->deferred = 0;
    handle->ready = 1;
    handle->progress = 1.0;
    game3d_assign_ref((void **)&handle->error, rt_const_cstr("cancelled"));
    game3d_release_ref(&handle->entity);
    game3d_release_ref(&handle->model_template);
}

/// @brief Return the entity result for entity-mode requests, or NULL. See header.
void *rt_game3d_asset_handle_get_entity(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.getEntity: invalid handle");
    game3d_asset_handle_complete_if_deferred(handle);
    if (!handle || !handle->ready || handle->cancelled || game3d_string_has_bytes(handle->error))
        return NULL;
    return handle->entity;
}

/// @brief Return the template result for template-mode requests, or NULL. See header.
void *rt_game3d_asset_handle_get_template(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.getTemplate: invalid handle");
    game3d_asset_handle_complete_if_deferred(handle);
    if (!handle || !handle->ready || handle->cancelled || game3d_string_has_bytes(handle->error))
        return NULL;
    return handle->model_template;
}

/// @brief Warm the cache by scheduling a filesystem template load. See header.
void rt_game3d_assets_preload(rt_string path) {
    rt_game3d_asset_handle *handle = game3d_asset_handle_pending(path, 0, 1);
    if (!handle)
        return;
    game3d_asset_handle_service(handle, 1);
    game3d_release_ref((void **)&handle);
}

/// @brief Warm the cache by scheduling a packed-asset template load. See header.
void rt_game3d_assets_preload_asset(rt_string path) {
    rt_game3d_asset_handle *handle = game3d_asset_handle_pending(path, 1, 1);
    if (!handle)
        return;
    game3d_asset_handle_service(handle, 1);
    game3d_release_ref((void **)&handle);
}

/// @brief Release every cached model template and reset the cache count. See header.
void rt_game3d_assets_clear_cache(void) {
    rt_game3d_model_cache_entry *entries = NULL;
    int32_t entry_count = 0;
    for (;;) {
        int loading = 0;
        game3d_model_cache_lock();
        for (int32_t i = 0; i < g_game3d_model_cache_count; ++i) {
            if (g_game3d_model_cache[i].loading) {
                loading = 1;
                break;
            }
        }
        if (!loading)
            break;
        game3d_model_cache_wait_locked();
        game3d_model_cache_unlock();
    }
    entries = g_game3d_model_cache;
    entry_count = g_game3d_model_cache_count;
    g_game3d_model_cache = NULL;
    g_game3d_model_cache_count = 0;
    g_game3d_model_cache_capacity = 0;
    g_game3d_model_cache_tick = 0;
    g_game3d_model_resident_bytes = 0;
    game3d_model_cache_advance_generation_locked();
    game3d_model_cache_notify_all();
    game3d_model_cache_unlock();

    for (int32_t i = 0; i < entry_count; ++i) {
        game3d_release_ref((void **)&entries[i].path);
        game3d_release_ref(&entries[i].model_template);
    }
    free(entries);
}

/// @brief GC finalizer for an EnvHandle: release its water, terrain, and world refs.
static void game3d_env_handle_finalize(void *obj) {
    rt_game3d_env_handle *env = (rt_game3d_env_handle *)obj;
    if (!env)
        return;
    game3d_release_ref(&env->water_entity);
    game3d_release_ref(&env->terrain_entity);
    game3d_release_ref(&env->world);
}

/// @brief Allocate an EnvHandle bound to `world`; traps on OOM.
static void *game3d_env_handle_new(rt_game3d_world *world) {
    rt_game3d_env_handle *env = (rt_game3d_env_handle *)rt_obj_new_i64(
        RT_G3D_GAME3D_ENV_HANDLE_CLASS_ID, (int64_t)sizeof(*env));
    if (!env) {
        rt_trap("Game3D.Environment: allocation failed");
        return NULL;
    }
    memset(env, 0, sizeof(*env));
    rt_obj_set_finalizer(env, game3d_env_handle_finalize);
    game3d_assign_ref(&env->world, world);
    return env;
}

/// @brief Resolve and validate the world an EnvHandle targets; traps `method` if absent.
static rt_game3d_world *game3d_env_world(rt_game3d_env_handle *env, const char *method) {
    if (!env || !env->world)
        rt_trap(method);
    return game3d_world_checked(env->world, method);
}

/// @brief Fluent: spawn a ground-collider terrain entity (replacing any prior one) into
///   the environment's world and return the handle. See header.
void *rt_game3d_env_handle_with_terrain(void *obj, double size, double height) {
    rt_game3d_env_handle *env =
        game3d_env_handle_checked(obj, "Game3D.EnvHandle.withTerrain: invalid environment");
    rt_game3d_world *world = game3d_env_world(env, "Game3D.EnvHandle.withTerrain: invalid world");
    if (!env || !world)
        return obj;
    if (env->terrain_entity) {
        rt_game3d_entity *old = (rt_game3d_entity *)env->terrain_entity;
        if (!old->destroyed && old->spawned && old->world == world)
            rt_game3d_world_despawn(world, env->terrain_entity);
        game3d_release_ref(&env->terrain_entity);
    }

    double s = game3d_positive_or(size, 80.0);
    double y = game3d_finite_or(height, 0.0);
    void *mat = rt_game3d_materials_rubber(0.30, 0.45, 0.24);
    void *terrain = rt_game3d_prefab_ground(s, mat);
    if (terrain) {
        rt_game3d_entity_set_name(terrain, rt_const_cstr("Environment Terrain"));
        rt_game3d_entity_set_position(terrain, 0.0, y, 0.0);
        void *body = rt_body3d_new_aabb(s * 0.5, 0.001, s * 0.5, 0.0);
        rt_game3d_entity_attach_body(terrain, body);
        rt_game3d_world_spawn(world, terrain);
        game3d_assign_ref(&env->terrain_entity, terrain);
        env->terrain_size = s;
        env->has_terrain_size = 1;
        game3d_release_ref(&body);
    }
    game3d_release_ref(&terrain);
    game3d_release_ref(&mat);
    return obj;
}

/// @brief Fluent: spawn a translucent water plane at `level` (replacing any prior one)
///   and return the handle. See header.
void *rt_game3d_env_handle_with_water(void *obj, double level) {
    rt_game3d_env_handle *env =
        game3d_env_handle_checked(obj, "Game3D.EnvHandle.withWater: invalid environment");
    rt_game3d_world *world = game3d_env_world(env, "Game3D.EnvHandle.withWater: invalid world");
    if (!env || !world)
        return obj;
    if (env->water_entity) {
        rt_game3d_entity *old = (rt_game3d_entity *)env->water_entity;
        if (!old->destroyed && old->spawned && old->world == world)
            rt_game3d_world_despawn(world, env->water_entity);
        game3d_release_ref(&env->water_entity);
    }

    double y = game3d_finite_or(level, 0.0);
    double water_size = env->has_terrain_size ? env->terrain_size : 80.0;
    void *mat = rt_game3d_materials_glass(0.18, 0.42, 0.62, 0.48);
    void *water = rt_game3d_prefab_plane(water_size, water_size, mat);
    if (water) {
        rt_game3d_entity_set_name(water, rt_const_cstr("Environment Water"));
        rt_game3d_entity_set_position(water, 0.0, y, 0.0);
        rt_game3d_world_spawn(world, water);
        game3d_assign_ref(&env->water_entity, water);
    }
    game3d_release_ref(&water);
    game3d_release_ref(&mat);
    return obj;
}

/// @brief Fluent: enable distance fog between near/far planes using the world's clear
///   color and return the handle. See header.
void *rt_game3d_env_handle_with_fog(void *obj, double near_plane, double far_plane) {
    rt_game3d_env_handle *env =
        game3d_env_handle_checked(obj, "Game3D.EnvHandle.withFog: invalid environment");
    rt_game3d_world *world = game3d_env_world(env, "Game3D.EnvHandle.withFog: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_fog(world->canvas,
                            game3d_nonnegative_or(near_plane, 18.0),
                            game3d_positive_or(far_plane, 120.0),
                            world->clear_r,
                            world->clear_g,
                            world->clear_b);
    return obj;
}

/// @brief Build a bright daytime environment (sun lighting, terrain, fog) and return
///   its handle. See header.
void *rt_game3d_environment_outdoor(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Environment.Outdoor: invalid world");
    void *env = game3d_env_handle_new(world);
    void *sun = rt_vec3_new(-0.45, -1.00, -0.22);
    rt_game3d_lighting_outdoor(world, sun);
    rt_game3d_env_handle_with_terrain(env, 96.0, 0.0);
    rt_game3d_env_handle_with_fog(env, 45.0, 220.0);
    game3d_release_ref(&sun);
    return env;
}

/// @brief Build a warm low-sun sunset environment and return its handle. See header.
void *rt_game3d_environment_sunset(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Environment.Sunset: invalid world");
    void *env = game3d_env_handle_new(world);
    void *sun = rt_vec3_new(-0.75, -0.45, -0.20);
    rt_game3d_lighting_outdoor(world, sun);
    if (world && world->canvas) {
        rt_canvas3d_set_ambient(world->canvas, 0.42, 0.30, 0.24);
        game3d_world_set_clear_color(world, 0.90, 0.48, 0.30);
    }
    rt_game3d_env_handle_with_terrain(env, 96.0, 0.0);
    rt_game3d_env_handle_with_fog(env, 28.0, 160.0);
    game3d_release_ref(&sun);
    return env;
}

/// @brief Build a flat, diffuse overcast environment and return its handle. See header.
void *rt_game3d_environment_overcast(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.Environment.Overcast: invalid world");
    void *env = game3d_env_handle_new(world);
    rt_game3d_lighting_clear(world);
    if (world && world->canvas) {
        rt_canvas3d_set_ambient(world->canvas, 0.48, 0.50, 0.52);
        game3d_world_set_clear_color(world, 0.56, 0.60, 0.62);
    }
    void *dir = rt_vec3_new(-0.20, -1.0, 0.10);
    void *sun = rt_light3d_new_directional(dir, 0.74, 0.78, 0.82);
    if (sun)
        rt_light3d_set_intensity(sun, 0.70);
    game3d_world_install_light(world, 0, sun);
    rt_game3d_env_handle_with_terrain(env, 96.0, 0.0);
    rt_game3d_env_handle_with_fog(env, 20.0, 110.0);
    game3d_release_ref(&sun);
    game3d_release_ref(&dir);
    return env;
}

/// @brief Build a dark night environment (moonlight, terrain, fog) and return its
///   handle. See header.
void *rt_game3d_environment_night(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Environment.Night: invalid world");
    void *env = game3d_env_handle_new(world);
    rt_game3d_lighting_night(world);
    rt_game3d_env_handle_with_terrain(env, 96.0, 0.0);
    rt_game3d_env_handle_with_fog(env, 20.0, 95.0);
    return env;
}

/// @brief Toggle the on-screen debug stats overlay. See header.
void rt_game3d_debug_show_overlay(void *obj, int8_t enabled) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Debug3D.ShowOverlay: invalid world");
    if (world)
        world->debug_overlay_enabled = enabled ? 1 : 0;
}

/// @brief Enable an axis gizmo at the given Vec3 origin and size; traps on a non-Vec3
///   origin. See header.
void rt_game3d_debug_draw_axes(void *obj, void *origin, double size) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Debug3D.DrawAxes: invalid world");
    if (!origin || !rt_g3d_is_vec3(origin)) {
        rt_trap("Game3D.Debug3D.DrawAxes: origin must be Vec3");
        return;
    }
    if (world) {
        game3d_assign_ref(&world->debug_axis_origin, origin);
        world->debug_axis_size = game3d_positive_or(size, 1.0);
        world->debug_axes_enabled = 1;
    }
}

/// @brief Toggle physics collider/contact debug drawing. See header.
void rt_game3d_debug_draw_physics(void *obj, int8_t enabled) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Debug3D.DrawPhysics: invalid world");
    if (world)
        world->debug_physics_enabled = enabled ? 1 : 0;
}

/// @brief Toggle the on-screen camera info readout. See header.
void rt_game3d_debug_draw_camera_info(void *obj, int8_t enabled) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.Debug3D.DrawCameraInfo: invalid world");
    if (world)
        world->debug_camera_enabled = enabled ? 1 : 0;
}

/// @brief Toggle the backend-capabilities readout overlay. See header.
void rt_game3d_debug_draw_capabilities(void *obj, int8_t enabled) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.Debug3D.DrawCapabilities: invalid world");
    if (world)
        world->debug_caps_enabled = enabled ? 1 : 0;
}

/// @brief GC finalizer for a character controller: release its entity and character.

/// @brief Dispatch the per-frame update to the world's installed camera controller by
///   class id; traps if the controller is not a Game3D camera controller.
static void game3d_world_update_controller(rt_game3d_world *world, double dt) {
    if (!world || !world->camera_controller)
        return;
    switch (rt_obj_class_id(world->camera_controller)) {
        case RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID:
            rt_game3d_first_person_controller_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_FREEFLY_CLASS_ID:
            rt_game3d_free_fly_controller_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_ORBIT_CLASS_ID:
            rt_game3d_orbit_controller_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_FOLLOW_CLASS_ID:
            rt_game3d_follow_controller_update(world->camera_controller, world, dt);
            break;
        default:
            rt_trap("Game3D.World3D: camera controller must be a Game3D camera controller");
            return;
    }
}

/// @brief Advance every live, non-destroyed entity's animator by `dt`.
typedef struct {
    void **animators;
    int32_t start;
    int32_t end;
    double dt;
} rt_game3d_animation_job;

/// @brief Worker-job entry point: advance a contiguous slice of animators by the frame delta.
static void game3d_animation_job_run(void *arg) {
    rt_game3d_animation_job *job = (rt_game3d_animation_job *)arg;
    if (!job || !job->animators)
        return;
    for (int32_t i = job->start; i < job->end; ++i) {
        void *anim = job->animators[i];
        if (anim)
            rt_game3d_animator_update(anim, job->dt);
    }
}

/// @brief Choose how many parallel jobs to split an animator update across, given the worker count.
static int32_t game3d_animation_job_count(int64_t worker_count, int32_t animator_count) {
    if (worker_count <= 1 || animator_count <= 1)
        return 1;
    int64_t target = worker_count * 4;
    if (target < 1)
        target = 1;
    if (target > animator_count)
        target = animator_count;
    return target > INT32_MAX ? INT32_MAX : (int32_t)target;
}

/// @brief Update a list of animators sequentially (the single-threaded fallback path).
static void game3d_update_animator_list_serial(void **animators, int32_t count, double dt) {
    if (!animators)
        return;
    for (int32_t i = 0; i < count; ++i) {
        if (animators[i])
            rt_game3d_animator_update(animators[i], dt);
    }
}

/// @brief Advance all of the world's animators by @p dt, parallelizing across workers when
/// worthwhile.
static void game3d_world_update_animations(rt_game3d_world *world, double dt) {
    if (!world)
        return;
    int32_t animator_count = 0;
    for (int32_t i = 0; i < world->entity_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        if (!entity || entity->destroyed || !entity->anim)
            continue;
        if (rt_g3d_has_class(entity->anim, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID))
            animator_count++;
    }
    if (animator_count <= 0)
        return;
    if (!world->jobs_enabled || world->worker_count <= 1 || animator_count <= 1 ||
        !game3d_world_ensure_job_pool(world)) {
        for (int32_t i = 0; i < world->entity_count; ++i) {
            rt_game3d_entity *entity = world->entities[i];
            if (!entity || entity->destroyed || !entity->anim)
                continue;
            if (rt_g3d_has_class(entity->anim, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID))
                rt_game3d_animator_update(entity->anim, dt);
        }
        return;
    }

    void **animators = (void **)malloc((size_t)animator_count * sizeof(*animators));
    if (!animators) {
        for (int32_t i = 0; i < world->entity_count; ++i) {
            rt_game3d_entity *entity = world->entities[i];
            if (!entity || entity->destroyed || !entity->anim)
                continue;
            if (rt_g3d_has_class(entity->anim, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID))
                rt_game3d_animator_update(entity->anim, dt);
        }
        return;
    }
    int32_t write = 0;
    for (int32_t i = 0; i < world->entity_count && write < animator_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        if (!entity || entity->destroyed || !entity->anim)
            continue;
        if (rt_g3d_has_class(entity->anim, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID))
            animators[write++] = entity->anim;
    }
    animator_count = write;
    int32_t task_count = game3d_animation_job_count(world->worker_count, animator_count);
    rt_game3d_animation_job *tasks =
        (rt_game3d_animation_job *)calloc((size_t)task_count, sizeof(*tasks));
    if (!tasks || task_count <= 1) {
        game3d_update_animator_list_serial(animators, animator_count, dt);
        free(tasks);
        free(animators);
        return;
    }

    int submit_failed = 0;
    for (int32_t i = 0; i < task_count; ++i) {
        int32_t start = (int32_t)(((int64_t)i * animator_count) / task_count);
        int32_t end = (int32_t)(((int64_t)(i + 1) * animator_count) / task_count);
        tasks[i].animators = animators;
        tasks[i].start = start;
        tasks[i].end = end;
        tasks[i].dt = dt;
        if (!rt_threadpool_submit(
                world->job_pool, game3d_task_fnptr(game3d_animation_job_run), &tasks[i])) {
            submit_failed = 1;
            game3d_animation_job_run(&tasks[i]);
        }
    }
    rt_threadpool_wait(world->job_pool);
    if (submit_failed)
        world->jobs_enabled = 0;
    free(tasks);
    free(animators);
}

/// @brief Dispatch the post-physics late update to the world's camera controller by
///   class id; traps on an unrecognized controller class.
static void game3d_world_late_update_controller(rt_game3d_world *world, double dt) {
    if (!world || !world->camera_controller)
        return;
    switch (rt_obj_class_id(world->camera_controller)) {
        case RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID:
            rt_game3d_first_person_controller_late_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_FREEFLY_CLASS_ID:
            rt_game3d_free_fly_controller_late_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_ORBIT_CLASS_ID:
            rt_game3d_orbit_controller_late_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_FOLLOW_CLASS_ID:
            rt_game3d_follow_controller_late_update(world->camera_controller, world, dt);
            break;
        default:
            rt_trap("Game3D.World3D: camera controller must be a Game3D camera controller");
            return;
    }
}

/// @brief Ensure the world's entity registry can hold `need` entries, doubling capacity;
///   returns 0 on allocation failure.
static int game3d_world_grow_entities(rt_game3d_world *world, int32_t need) {
    int32_t new_cap;
    if (!world)
        return 0;
    if (world->entity_capacity >= need)
        return 1;
    if (!game3d_compute_capacity(
            world->entity_capacity, need, 16, sizeof(rt_game3d_entity *), &new_cap))
        return 0;
    rt_game3d_entity **grown =
        (rt_game3d_entity **)realloc(world->entities, (size_t)new_cap * sizeof(*grown));
    if (!grown)
        return 0;
    world->entities = grown;
    world->entity_capacity = new_cap;
    return 1;
}

/// @brief Linear-search the registry for `entity`; returns its index or -1.
static int32_t game3d_world_find_entity_index(rt_game3d_world *world, rt_game3d_entity *entity) {
    if (!world || !entity)
        return -1;
    for (int32_t i = 0; i < world->entity_count; ++i) {
        if (world->entities[i] == entity)
            return i;
    }
    return -1;
}

#include "rt_game3d_indices.inc"

/// @brief Find the spawned entity whose physics body is `body`; NULL if none. Used to
///   map collision events back to entities.
rt_game3d_entity *game3d_world_find_entity_by_body(rt_game3d_world *world, void *body) {
    int found = 0;
    int slot;
    if (!world || !body)
        return NULL;
    slot = game3d_world_body_index_find_slot(
        world->body_index_entries, world->body_index_capacity, body, &found);
    if (!found || slot < 0)
        return NULL;
    rt_game3d_entity *entity = world->body_index_entries[slot].entity;
    if (!entity || !entity->spawned || entity->world != world || entity->body != body ||
        (world->physics && !rt_world3d_contains_body(world->physics, body))) {
        game3d_world_body_index_remove(world, body);
        return NULL;
    }
    return entity;
}

/// @brief Remove `entity` from the registry without rebuilding the name index.
/// @details Used by tree operations that rebuild once after a batch.
static void game3d_world_registry_remove_no_rebuild(rt_game3d_world *world,
                                                    rt_game3d_entity *entity) {
    int32_t index = game3d_world_find_entity_index(world, entity);
    if (index < 0)
        return;
    if (entity && entity->body && game3d_world_find_entity_by_body(world, entity->body) == entity)
        game3d_world_body_index_remove(world, entity->body);
    rt_game3d_entity *owned = world->entities[index];
    for (int32_t i = index; i < world->entity_count - 1; ++i)
        world->entities[i] = world->entities[i + 1];
    world->entities[--world->entity_count] = NULL;
    game3d_release_ref((void **)&owned);
    world->name_index_valid = 0;
}

/// @brief Detach an entity's node from its parent, or from the scene root if it has no
///   parent.
static void game3d_entity_detach_node(rt_game3d_world *world, rt_game3d_entity *entity) {
    if (!world || !entity || !entity->node)
        return;
    void *parent = rt_scene_node3d_get_parent(entity->node);
    if (parent)
        rt_scene_node3d_remove_child(parent, entity->node);
    else if (world->scene)
        rt_scene3d_remove(world->scene, entity->node);
}

typedef struct game3d_entity_tree_item {
    rt_game3d_entity *entity;
    int8_t root_link;
    int8_t visited;
} game3d_entity_tree_item;

/// @brief Push an entity-tree traversal item onto the work list, growing it as needed.
static int game3d_entity_tree_push(game3d_entity_tree_item **items,
                                   int32_t *count,
                                   int32_t *capacity,
                                   rt_game3d_entity *entity,
                                   int root_link,
                                   int visited) {
    game3d_entity_tree_item *grown;
    int32_t new_capacity;
    if (!items || !count || !capacity)
        return 0;
    if (*count >= *capacity) {
        if (!game3d_compute_capacity(*capacity, *count + 1, 16, sizeof(**items), &new_capacity))
            return 0;
        grown = (game3d_entity_tree_item *)realloc(*items, (size_t)new_capacity * sizeof(**items));
        if (!grown)
            return 0;
        *items = grown;
        *capacity = new_capacity;
    }
    (*items)[*count].entity = entity;
    (*items)[*count].root_link = root_link ? 1 : 0;
    (*items)[*count].visited = visited ? 1 : 0;
    (*count)++;
    return 1;
}

/// @brief Despawn a single entity: detach it from physics/scene, unregister it, and release it.
static void game3d_world_despawn_entity_one(rt_game3d_world *world,
                                            rt_game3d_entity *entity,
                                            int detach_root) {
    if (!world || !entity)
        return;
    if (!entity->spawned || entity->world != world)
        return;
    if (entity->body && world->physics) {
        rt_world3d_remove(world->physics, entity->body);
        game3d_world_body_index_remove(world, entity->body);
    }
    if (detach_root)
        game3d_entity_detach_node(world, entity);
    entity->spawned = 0;
    entity->world = NULL;
    game3d_world_registry_remove_no_rebuild(world, entity);
}

/// @brief Despawn an entity tree with an explicit heap stack, avoiding C-stack overflow.
static void game3d_world_despawn_entity_tree(rt_game3d_world *world,
                                             rt_game3d_entity *entity,
                                             int detach_root) {
    game3d_entity_tree_item *stack = NULL;
    int32_t count = 0;
    int32_t capacity = 0;
    if (!world || !entity)
        return;
    if (!game3d_entity_tree_push(&stack, &count, &capacity, entity, detach_root, 0)) {
        rt_trap("Game3D.World3D.despawn: traversal stack allocation failed");
        return;
    }
    while (count > 0) {
        game3d_entity_tree_item item = stack[--count];
        if (!item.entity)
            continue;
        if (item.visited) {
            game3d_world_despawn_entity_one(world, item.entity, item.root_link);
            continue;
        }
        if (!game3d_entity_tree_push(&stack, &count, &capacity, item.entity, item.root_link, 1)) {
            free(stack);
            rt_trap("Game3D.World3D.despawn: traversal stack allocation failed");
            return;
        }
        for (int32_t i = item.entity->child_count - 1; i >= 0; --i) {
            if (!game3d_entity_tree_push(
                    &stack, &count, &capacity, item.entity->children[i], 0, 0)) {
                free(stack);
                rt_trap("Game3D.World3D.despawn: traversal stack allocation failed");
                return;
            }
        }
    }
    free(stack);
    game3d_world_rebuild_name_index(world);
}

/// @brief Spawn one entity in a tree walk; returns 1 when children should be visited,
///   0 when it was already spawned in this world, and -1 on a trapped failure.
static int game3d_world_spawn_entity_one(rt_game3d_world *world,
                                         rt_game3d_entity *entity,
                                         int attach_to_scene,
                                         int64_t *next_id) {
    if (!world || !entity)
        return 0;
    if (entity->destroyed) {
        rt_trap("Game3D.World3D.spawn: entity is destroyed");
        return -1;
    }
    if (entity->spawned && entity->world == world)
        return 0;
    if (entity->spawned || (entity->world && entity->world != world)) {
        rt_trap("Game3D.World3D.spawn: entity already belongs to a world");
        return -1;
    }
    if (world->entity_count == INT32_MAX ||
        !game3d_world_grow_entities(world, world->entity_count + 1)) {
        rt_trap("Game3D.World3D.spawn: registry allocation failed");
        return -1;
    }

    entity->spawned = 1;
    entity->world = world;
    rt_obj_retain_maybe(entity);
    world->entities[world->entity_count++] = entity;
    if (!game3d_world_name_index_add_entity(world, entity)) {
        entity->spawned = 0;
        entity->world = NULL;
        game3d_world_registry_remove_no_rebuild(world, entity);
        game3d_world_rebuild_name_index(world);
        return -1;
    }

    if (attach_to_scene && world->scene && entity->node &&
        !rt_scene3d_try_add(world->scene, entity->node)) {
        entity->spawned = 0;
        entity->world = NULL;
        game3d_world_registry_remove_no_rebuild(world, entity);
        game3d_world_rebuild_name_index(world);
        rt_trap("Game3D.World3D.spawn: scene attach failed");
        return -1;
    }
    if (entity->body && world->physics) {
        rt_body3d_set_collision_layer(entity->body, entity->layer);
        rt_body3d_set_collision_mask(entity->body, entity->collision_mask_bits);
        rt_game3d_entity *body_owner = game3d_world_find_entity_by_body(world, entity->body);
        if (body_owner && body_owner != entity) {
            if (attach_to_scene && world->scene && entity->node)
                rt_scene3d_remove(world->scene, entity->node);
            entity->spawned = 0;
            entity->world = NULL;
            game3d_world_registry_remove_no_rebuild(world, entity);
            game3d_world_rebuild_name_index(world);
            rt_trap("Game3D.World3D.spawn: body is already attached to another entity");
            return -1;
        }
        if (entity->node) {
            rt_scene_node3d_bind_body(entity->node, entity->body);
            game3d_sync_body_from_entity_node(entity, 1);
        }
        if (!rt_world3d_try_add(world->physics, entity->body)) {
            if (entity->node)
                rt_scene_node3d_clear_body_binding(entity->node);
            if (attach_to_scene && world->scene && entity->node)
                rt_scene3d_remove(world->scene, entity->node);
            entity->spawned = 0;
            entity->world = NULL;
            game3d_world_registry_remove_no_rebuild(world, entity);
            game3d_world_rebuild_name_index(world);
            rt_trap("Game3D.World3D.spawn: physics world add failed");
            return -1;
        }
        if (!game3d_world_body_index_add(world, entity)) {
            rt_world3d_remove(world->physics, entity->body);
            if (entity->node)
                rt_scene_node3d_clear_body_binding(entity->node);
            if (attach_to_scene && world->scene && entity->node)
                rt_scene3d_remove(world->scene, entity->node);
            entity->spawned = 0;
            entity->world = NULL;
            game3d_world_registry_remove_no_rebuild(world, entity);
            game3d_world_rebuild_name_index(world);
            rt_trap("Game3D.World3D.spawn: body index allocation failed");
            return -1;
        }
    }
    if (entity->id == 0 && next_id)
        entity->id = (*next_id)++;
    return 1;
}

/// @brief Iteratively spawn an entity and its children: assign a stable id, add the root
///   node to the scene, register and add bodies to physics, and mark the tree spawned.
/// @details Traps if the entity is destroyed or already belongs to another world. Only
///   the root attaches to the scene (`attach_to_scene`); children remain parented to it.
int game3d_world_spawn_entity_tree(rt_game3d_world *world,
                                   rt_game3d_entity *entity,
                                   int attach_to_scene,
                                   int64_t *next_id) {
    game3d_entity_tree_item *stack = NULL;
    int32_t count = 0;
    int32_t capacity = 0;
    if (!world || !entity)
        return 0;
    if (!game3d_entity_tree_push(&stack, &count, &capacity, entity, attach_to_scene, 0)) {
        rt_trap("Game3D.World3D.spawn: traversal stack allocation failed");
        return 0;
    }
    while (count > 0) {
        game3d_entity_tree_item item = stack[--count];
        int spawned = game3d_world_spawn_entity_one(world, item.entity, item.root_link, next_id);
        if (spawned < 0) {
            free(stack);
            game3d_world_despawn_entity_tree(world, entity, attach_to_scene);
            return 0;
        }
        if (spawned == 0)
            continue;
        for (int32_t i = item.entity->child_count - 1; i >= 0; --i) {
            if (!game3d_entity_tree_push(
                    &stack, &count, &capacity, item.entity->children[i], 0, 0)) {
                free(stack);
                game3d_world_despawn_entity_tree(world, entity, attach_to_scene);
                rt_trap("Game3D.World3D.spawn: traversal stack allocation failed");
                return 0;
            }
        }
    }
    free(stack);
    return 1;
}

/// @brief Despawn and release every entity (optionally marking them destroyed), then
///   release all owned subsystems. Shared by destroy() and the GC finalizer.
static void game3d_world_release_runtime(rt_game3d_world *world, int mark_entities_destroyed) {
    if (!world)
        return;
    while (world->entity_count > 0) {
        rt_game3d_entity *entity = world->entities[world->entity_count - 1];
        if (entity) {
            if (mark_entities_destroyed)
                entity->destroyed = 1;
            game3d_world_despawn_entity_tree(world, entity, 1);
        } else {
            world->entity_count--;
        }
    }
    game3d_camera_controller_clear_world_ref_if(world->camera_controller, world);
    game3d_release_ref(&world->camera_controller);
    game3d_release_ref(&world->debug_axis_origin);
    if (world->stream) {
        rt_game3d_world_stream *stream = (rt_game3d_world_stream *)rt_g3d_checked_or_null(
            world->stream, RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID);
        if (stream && !stream->retains_world)
            stream->world = NULL;
    }
    game3d_release_ref(&world->stream);
    game3d_release_ref(&world->effects);
    game3d_release_ref(&world->audio);
    game3d_release_ref(&world->input);
    game3d_release_ref(&world->physics);
    game3d_release_ref(&world->scene);
    game3d_release_ref(&world->camera);
    game3d_release_ref(&world->canvas);
    game3d_world_release_job_pool(world);
}

/// @brief Free all of the world's per-subsystem registries (entities, controllers, animators,
/// etc.).
static void game3d_world_free_registries(rt_game3d_world *world) {
    if (!world)
        return;
    free(world->entities);
    free(world->body_index_entries);
    free(world->name_index_hashes);
    free(world->name_index_entities);
    world->entities = NULL;
    world->body_index_entries = NULL;
    world->name_index_hashes = NULL;
    world->name_index_entities = NULL;
    world->entity_count = 0;
    world->entity_capacity = 0;
    world->body_index_count = 0;
    world->body_index_capacity = 0;
    world->name_index_count = 0;
    world->name_index_capacity = 0;
    world->name_index_valid = 0;
}

/// @brief GC finalizer for a world: release all runtime state and free the entity array.
static void game3d_world_finalize(void *obj) {
    rt_game3d_world *world = (rt_game3d_world *)obj;
    if (!world)
        return;
    game3d_world_release_runtime(world, 1);
    game3d_world_free_registries(world);
}

static int64_t game3d_stream_manifest_bytes(rt_string value);
static int64_t game3d_i64_saturating_add(int64_t a, int64_t b);

/// @brief Whether a parsed JSON value is a map/object.
static int game3d_json_is_map(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_MAP_CLASS_ID;
}

/// @brief Whether a parsed JSON value is an array/sequence.
static int game3d_json_is_seq(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_SEQ_CLASS_ID;
}

/// @brief Look up @p key in a parsed JSON map (NULL if absent or not a map).
static void *game3d_json_get(void *map, const char *key) {
    if (!game3d_json_is_map(map) || !key)
        return NULL;
    rt_string k = rt_string_from_bytes(key, strlen(key));
    if (!k)
        return NULL;
    void *value = rt_map_get(map, k);
    rt_string_unref(k);
    return value;
}

/// @brief Coerce a parsed JSON value to a double; returns 0 if it is not numeric.
static int game3d_json_number(void *obj, double *out) {
    if (!out)
        return 0;
    double f = 0.0;
    if (rt_box_try_to_f64(obj, &f)) {
        *out = f;
        return 1;
    }
    int64_t i = 0;
    if (rt_box_try_to_i64(obj, &i)) {
        *out = (double)i;
        return 1;
    }
    return 0;
}

/// @brief Read map[key] as an int64, returning @p fallback if absent or non-numeric.
static int64_t game3d_json_i64_or(void *map, const char *key, int64_t fallback) {
    void *value = game3d_json_get(map, key);
    int64_t i = 0;
    if (rt_box_try_to_i64(value, &i))
        return i;
    double f = 0.0;
    if (rt_box_try_to_f64(value, &f) && isfinite(f)) {
        if (f <= (double)INT64_MIN)
            return INT64_MIN;
        if (f >= (double)INT64_MAX)
            return INT64_MAX;
        return (int64_t)f;
    }
    return fallback;
}

/// @brief Read map[key] as a double, returning @p fallback if absent or non-numeric.
static double game3d_json_f64_or(void *map, const char *key, double fallback) {
    double v = 0.0;
    if (game3d_json_number(game3d_json_get(map, key), &v) && isfinite(v))
        return v;
    return fallback;
}

/// @brief Read map[key] as a boolean, accepting boxed bools and numeric 0/1 values.
static int8_t game3d_json_bool_or(void *map, const char *key, int8_t fallback) {
    void *value = game3d_json_get(map, key);
    int8_t b = 0;
    if (rt_box_try_to_i1(value, &b))
        return b ? 1 : 0;
    int64_t i = 0;
    if (rt_box_try_to_i64(value, &i))
        return i != 0 ? 1 : 0;
    double f = 0.0;
    if (rt_box_try_to_f64(value, &f) && isfinite(f))
        return f != 0.0 ? 1 : 0;
    return fallback;
}

/// @brief Read map[key] as a borrowed string reference (NULL if absent or not a string).
static rt_string game3d_json_string_ref(void *map, const char *key) {
    void *value = game3d_json_get(map, key);
    if (!value || !rt_string_is_handle(value))
        return NULL;
    return rt_string_ref((rt_string)value);
}

/// @brief Read map[key] as a 3-element number array into @p out, falling back to @p f on absence.
static int game3d_json_vec3_or(void *map,
                               const char *key,
                               double out[3],
                               const double fallback[3]) {
    if (!out)
        return 0;
    out[0] = fallback ? fallback[0] : 0.0;
    out[1] = fallback ? fallback[1] : 0.0;
    out[2] = fallback ? fallback[2] : 0.0;
    void *seq = game3d_json_get(map, key);
    if (!game3d_json_is_seq(seq) || rt_seq_len(seq) < 3)
        return 0;
    double v[3];
    for (int i = 0; i < 3; ++i) {
        if (!game3d_json_number(rt_seq_get(seq, i), &v[i]) || !isfinite(v[i]))
            return 0;
    }
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
    return 1;
}

/// @brief Read a text file into a runtime string, returning NULL on failure instead of trapping.
/// @details Used for optional manifest/sidecar files where a missing file is a normal, recoverable
/// case.
static rt_string game3d_read_text_file_no_trap(rt_string path) {
    const char *cpath = path ? rt_string_cstr(path) : NULL;
    if (!cpath || cpath[0] == '\0')
        return NULL;
    FILE *file = fopen(cpath, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || size > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *bytes = (char *)malloc((size_t)size + 1u);
    if (!bytes) {
        fclose(file);
        return NULL;
    }
    if (size > 0 && fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    bytes[size] = '\0';
    rt_string result = rt_string_from_bytes(bytes, (size_t)size);
    free(bytes);
    return result;
}

/// @brief Whether @p path is absolute (leading '/' on POSIX, drive/UNC prefix on Windows).
static int game3d_path_is_absolute(const char *path) {
    if (!path || path[0] == '\0')
        return 0;
    if (path[0] == '/' || path[0] == '\\')
        return 1;
#if defined(_WIN32)
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':')
        return 1;
#endif
    return 0;
}

/// @brief Resolve a manifest-relative resource path against the manifest's own directory.
/// @details Absolute references pass through; relative ones are joined to the manifest's folder so
/// a
///          streaming manifest can reference sibling assets portably.
static rt_string game3d_stream_resolve_manifest_path(rt_string manifest_path, rt_string payload) {
    const char *raw = payload ? rt_string_cstr(payload) : NULL;
    const char *manifest = manifest_path ? rt_string_cstr(manifest_path) : NULL;
    if (!raw || raw[0] == '\0')
        return rt_string_ref(rt_const_cstr(""));
    if (game3d_path_is_absolute(raw) || !manifest || manifest[0] == '\0')
        return rt_string_ref(payload);
    const char *slash = strrchr(manifest, '/');
    const char *backslash = strrchr(manifest, '\\');
    const char *sep =
        slash && backslash ? (slash > backslash ? slash : backslash) : (slash ? slash : backslash);
    if (!sep)
        return rt_string_ref(payload);
    size_t dir_len = (size_t)(sep - manifest + 1);
    size_t raw_len = strlen(raw);
    if (dir_len > SIZE_MAX - raw_len)
        return rt_string_ref(payload);
    char *joined = (char *)malloc(dir_len + raw_len + 1u);
    if (!joined)
        return rt_string_ref(payload);
    memcpy(joined, manifest, dir_len);
    memcpy(joined + dir_len, raw, raw_len + 1u);
    rt_string result = rt_string_from_bytes(joined, dir_len + raw_len);
    free(joined);
    return result;
}

/// @brief Resolve a manifest entry's "sidecar" (or fallback "binarySidecar") path against the
/// manifest.
/// @return Owned resolved path string, or an empty string when neither key is present.
static rt_string game3d_json_resolved_sidecar_ref(void *entry, rt_string manifest_path) {
    rt_string raw = game3d_json_string_ref(entry, "sidecar");
    if (!raw)
        raw = game3d_json_string_ref(entry, "binarySidecar");
    if (!raw)
        return rt_string_ref(rt_const_cstr(""));
    rt_string resolved = game3d_stream_resolve_manifest_path(manifest_path, raw);
    rt_string_unref(raw);
    return resolved;
}

/// @brief Return @p layer when it is a valid layer index, otherwise @p fallback.
static int64_t game3d_stream_layer_or_valid(int64_t layer, int64_t fallback) {
    return game3d_valid_layer(layer) ? layer : fallback;
}

/// @brief Parse collision metadata for a streaming cell/tile from its JSON manifest entry.
/// @details Reads the collision layer ("layer", with "collisionLayer" and a nested
/// "collision.layer"
///          taking precedence), the collision mask ("collisionMask" or nested "collision.mask",
///          sanitized to valid bits), and the enabled flag (defaulting on, overridden by a boolean
///          "collision" value or nested "collision.enabled"). Every out-pointer is optional; layer
///          and mask fall back to @p default_layer and all-bits when unspecified, and the matching
///          has-flag reports whether the manifest actually supplied a value.
static void game3d_stream_parse_collision_metadata(void *entry,
                                                   int64_t default_layer,
                                                   int64_t *out_layer,
                                                   int8_t *out_has_layer,
                                                   int64_t *out_mask,
                                                   int8_t *out_has_mask,
                                                   int8_t *out_enabled) {
    if (out_enabled)
        *out_enabled = 1;
    int64_t layer = game3d_json_i64_or(entry, "layer", 0);
    int8_t has_layer = game3d_valid_layer(layer) ? 1 : 0;
    int64_t collision_layer = game3d_json_i64_or(entry, "collisionLayer", 0);
    if (game3d_valid_layer(collision_layer)) {
        layer = collision_layer;
        has_layer = 1;
    }
    int64_t collision_mask = game3d_json_i64_or(entry, "collisionMask", 0);
    int8_t has_mask = game3d_json_get(entry, "collisionMask") ? 1 : 0;
    if (out_enabled && game3d_json_get(entry, "collision"))
        *out_enabled = game3d_json_bool_or(entry, "collision", 1);

    void *collision = game3d_json_get(entry, "collision");
    if (game3d_json_is_map(collision)) {
        if (out_enabled)
            *out_enabled = game3d_json_bool_or(collision, "enabled", 1);
        int64_t nested_layer = game3d_json_i64_or(collision, "layer", 0);
        if (game3d_valid_layer(nested_layer)) {
            layer = nested_layer;
            has_layer = 1;
        }
        int64_t nested_mask = game3d_json_i64_or(collision, "mask", 0);
        if (game3d_json_get(collision, "mask")) {
            collision_mask = nested_mask;
            has_mask = 1;
        }
    }

    if (out_layer)
        *out_layer = has_layer ? layer : default_layer;
    if (out_has_layer)
        *out_has_layer = has_layer;
    if (out_mask)
        *out_mask = has_mask ? game3d_sanitize_mask_bits(collision_mask) : ~(int64_t)0;
    if (out_has_mask)
        *out_has_mask = has_mask;
}

/// @brief Read a manifest entry's "traversalCost", defaulting to 1.0 and clamping to the range (0,
/// 1e6].
static double game3d_stream_traversal_cost_or_default(void *entry) {
    double cost = game3d_json_f64_or(entry, "traversalCost", 1.0);
    if (!isfinite(cost) || cost <= 0.0)
        cost = 1.0;
    if (cost > 1000000.0)
        cost = 1000000.0;
    return cost;
}

/// @brief Resident byte size attributed to a streaming cell (0 if not loaded).
static int64_t game3d_stream_cell_bytes(const rt_game3d_stream_cell *cell) {
    if (!cell)
        return 64 * 1024;
    if (cell->resident && cell->measured_resident_bytes > 0)
        return cell->measured_resident_bytes;
    if (cell->resident_bytes <= 0)
        return 64 * 1024;
    return cell->resident_bytes;
}

/// @brief Resident byte size attributed to a streaming terrain tile (0 if not loaded).
static int64_t game3d_stream_terrain_tile_bytes(const rt_game3d_stream_terrain_tile *tile) {
    if (!tile || tile->resident_bytes <= 0)
        return 256 * 1024;
    return tile->resident_bytes;
}

/// @brief Squared distance from the stream center to a cell's center (drives load/unload ordering).
static double game3d_stream_cell_distance_sq(const rt_game3d_world_stream *stream,
                                             const rt_game3d_stream_cell *cell) {
    double dx = stream->center[0] - cell->center[0];
    double dz = stream->center[2] - cell->center[2];
    return dx * dx + dz * dz;
}

/// @brief Squared distance from the stream center to a terrain tile's center.
static double game3d_stream_terrain_tile_distance_sq(const rt_game3d_world_stream *stream,
                                                     const rt_game3d_stream_terrain_tile *tile) {
    double dx = stream->center[0] - tile->center[0];
    double dz = stream->center[2] - tile->center[2];
    return dx * dx + dz * dz;
}

/// @brief Sanitize a terrain axis scale to a positive finite value (defaulting when invalid).
static double game3d_stream_terrain_axis_scale(double value) {
    if (!isfinite(value) || value <= 0.0)
        return 1.0;
    if (value > 1000000.0)
        return 1000000.0;
    return value;
}

/// @brief Compute the render-space bounds of a terrain tile from its manifest center + scale.
static void game3d_stream_terrain_tile_bounds(const rt_game3d_stream_terrain_tile *tile,
                                              double *min_x,
                                              double *max_x,
                                              double *min_z,
                                              double *max_z) {
    double sx = game3d_stream_terrain_axis_scale(tile ? tile->scale[0] : 1.0);
    double sz = game3d_stream_terrain_axis_scale(tile ? tile->scale[2] : 1.0);
    double width = tile && tile->width > 1 ? (double)(tile->width - 1) * sx : sx;
    double depth = tile && tile->depth > 1 ? (double)(tile->depth - 1) * sz : sz;
    double cx = tile ? tile->center[0] : 0.0;
    double cz = tile ? tile->center[2] : 0.0;
    if (!isfinite(cx))
        cx = 0.0;
    if (!isfinite(cz))
        cz = 0.0;
    if (!isfinite(width) || width < 0.0)
        width = 0.0;
    if (!isfinite(depth) || depth < 0.0)
        depth = 0.0;
    if (min_x)
        *min_x = cx - width * 0.5;
    if (max_x)
        *max_x = cx + width * 0.5;
    if (min_z)
        *min_z = cz - depth * 0.5;
    if (max_z)
        *max_z = cz + depth * 0.5;
}

/// @brief Floating-point tolerance for deciding whether two tile edges touch.
static double game3d_stream_terrain_seam_tolerance(const rt_game3d_stream_terrain_tile *a,
                                                   const rt_game3d_stream_terrain_tile *b) {
    double tol = 0.00001;
    if (a) {
        double ax = game3d_stream_terrain_axis_scale(a->scale[0]) * 0.0001;
        double az = game3d_stream_terrain_axis_scale(a->scale[2]) * 0.0001;
        if (ax > tol)
            tol = ax;
        if (az > tol)
            tol = az;
    }
    if (b) {
        double bx = game3d_stream_terrain_axis_scale(b->scale[0]) * 0.0001;
        double bz = game3d_stream_terrain_axis_scale(b->scale[2]) * 0.0001;
        if (bx > tol)
            tol = bx;
        if (bz > tol)
            tol = bz;
    }
    return tol;
}

/// @brief Finite-aware approximate equality: true when both values are finite and within @p
/// tolerance.
static int game3d_stream_almost_equal(double a, double b, double tolerance) {
    return isfinite(a) && isfinite(b) && fabs(a - b) <= tolerance;
}

/// @brief True when two [min,max] ranges match end-to-end within @p tolerance (terrain seam test).
static int game3d_stream_range_matches(
    double a_min, double a_max, double b_min, double b_max, double tolerance) {
    return game3d_stream_almost_equal(a_min, b_min, tolerance) &&
           game3d_stream_almost_equal(a_max, b_max, tolerance);
}

/// @brief Total resident bytes across all currently-loaded streaming cells.
static int64_t game3d_world_stream_resident_cell_bytes(const rt_game3d_world_stream *stream) {
    int64_t bytes = game3d_stream_manifest_bytes(stream ? stream->cells_manifest : NULL);
    if (!stream)
        return 0;
    for (int32_t i = 0; i < stream->cell_count; ++i) {
        if (stream->cells[i].resident)
            bytes = game3d_i64_saturating_add(bytes, game3d_stream_cell_bytes(&stream->cells[i]));
    }
    return bytes;
}

static void game3d_world_stream_unload_terrain_tile(rt_game3d_world_stream *stream,
                                                    rt_game3d_stream_terrain_tile *tile);

/// @brief Despawn/release collider and nav entities attached to a resident terrain tile.
static void game3d_world_stream_detach_terrain_spatial_sources(
    rt_game3d_world_stream *stream, rt_game3d_stream_terrain_tile *tile) {
    if (!tile)
        return;
    if (tile->nav_entity && stream && stream->world) {
        rt_game3d_entity *entity = (rt_game3d_entity *)rt_g3d_checked_or_null(
            tile->nav_entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
        if (entity && entity->spawned && entity->world == stream->world)
            rt_game3d_world_despawn(stream->world, tile->nav_entity);
    }
    game3d_release_ref(&tile->nav_entity);
    if (tile->collider_entity && stream && stream->world) {
        rt_game3d_entity *entity = (rt_game3d_entity *)rt_g3d_checked_or_null(
            tile->collider_entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
        if (entity && entity->spawned && entity->world == stream->world)
            rt_game3d_world_despawn(stream->world, tile->collider_entity);
    }
    game3d_release_ref(&tile->collider_entity);
}

/// @brief Unload and free every terrain tile tracked by the stream.
static void game3d_world_stream_clear_terrain_tiles(rt_game3d_world_stream *stream) {
    if (!stream)
        return;
    for (int32_t i = 0; i < stream->terrain_tile_count; ++i) {
        rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
        game3d_world_stream_unload_terrain_tile(stream, tile);
        game3d_release_ref((void **)&tile->name);
        game3d_release_ref((void **)&tile->path);
        game3d_release_ref((void **)&tile->heightmap_path);
        game3d_release_ref((void **)&tile->material);
        game3d_release_ref((void **)&tile->nav_area);
        game3d_release_ref((void **)&tile->sidecar_path);
    }
    free(stream->terrain_tiles);
    stream->terrain_tiles = NULL;
    stream->terrain_tile_count = 0;
    stream->terrain_tile_capacity = 0;
    stream->terrain_manifest_loaded = 0;
}

/// @brief Unload a single terrain tile: detach its collider/nav source and release its resources.
static void game3d_world_stream_unload_terrain_tile(rt_game3d_world_stream *stream,
                                                    rt_game3d_stream_terrain_tile *tile) {
    if (!tile)
        return;
    game3d_world_stream_detach_terrain_spatial_sources(stream, tile);
    game3d_release_ref(&tile->terrain);
    tile->resident = 0;
}

/// @brief Advance past ASCII whitespace in a heightmap text buffer.
static const char *game3d_heightmap_skip_space(const char *p) {
    while (p && *p && isspace((unsigned char)*p))
        ++p;
    return p;
}

/// @brief Read the next whitespace-delimited token from a heightmap text buffer into @p out.
/// @return 1 on success, 0 at end of input or if the token would overflow.
static int game3d_heightmap_read_word(const char **cursor, char *out, size_t out_size) {
    if (!cursor || !*cursor || !out || out_size == 0)
        return 0;
    const char *p = game3d_heightmap_skip_space(*cursor);
    if (!p || *p == '\0')
        return 0;
    size_t len = 0;
    while (p[len] && !isspace((unsigned char)p[len]))
        ++len;
    if (len == 0 || len >= out_size)
        return 0;
    memcpy(out, p, len);
    out[len] = '\0';
    *cursor = p + len;
    return 1;
}

/// @brief Read the next integer token from a heightmap text buffer (0 if none/invalid).
static int game3d_heightmap_read_i64(const char **cursor, int64_t *out) {
    if (!cursor || !*cursor || !out)
        return 0;
    char *end = NULL;
    const char *p = game3d_heightmap_skip_space(*cursor);
    long long value = strtoll(p, &end, 10);
    if (!end || end == p)
        return 0;
    *cursor = end;
    *out = (int64_t)value;
    return 1;
}

/// @brief Read the next floating-point token from a heightmap text buffer (0 if none/invalid).
static int game3d_heightmap_read_f64(const char **cursor, double *out) {
    if (!cursor || !*cursor || !out)
        return 0;
    char *end = NULL;
    const char *p = game3d_heightmap_skip_space(*cursor);
    double value = strtod(p, &end);
    if (!end || end == p || !isfinite(value))
        return 0;
    *cursor = end;
    *out = value;
    return 1;
}

/// @brief Map a normalized height value to a grayscale RGBA color for the heightmap preview image.
static uint32_t game3d_heightmap_sample_rgba(double h) {
    if (h < 0.0)
        h = 0.0;
    else if (h > 1.0)
        h = 1.0;
    uint32_t sample = (uint32_t)llround(h * 65535.0);
    if (sample > 65535u)
        sample = 65535u;
    return ((sample >> 8) & 0xFFu) << 24 | (sample & 0xFFu) << 16 | 0x000000FFu;
}

/// @brief Load a terrain tile's heightmap sidecar file into a height grid (and optional preview
/// image).
/// @details Parses the text heightmap format (dimensions + height samples); returns NULL if the
///          sidecar is missing or malformed.
static void *game3d_load_heightmap_sidecar(rt_string path,
                                           int64_t target_width,
                                           int64_t target_depth) {
    if (!game3d_string_has_bytes(path))
        return NULL;
    rt_string text = game3d_read_text_file_no_trap(path);
    if (!text)
        return NULL;

    const char *cursor = rt_string_cstr(text);
    char magic[32];
    int64_t width = 0;
    int64_t depth = 0;
    if (!game3d_heightmap_read_word(&cursor, magic, sizeof(magic)) ||
        strcmp(magic, "viper-heightmap-v1") != 0 || !game3d_heightmap_read_i64(&cursor, &width) ||
        !game3d_heightmap_read_i64(&cursor, &depth) || width < 1 || depth < 1 || width > 4096 ||
        depth > 4096) {
        rt_string_unref(text);
        return NULL;
    }

    size_t sample_count = (size_t)width * (size_t)depth;
    if (sample_count > SIZE_MAX / sizeof(double)) {
        rt_string_unref(text);
        return NULL;
    }
    double *samples = (double *)calloc(sample_count, sizeof(double));
    if (!samples) {
        rt_string_unref(text);
        return NULL;
    }

    for (int64_t z = 0; z < depth; ++z) {
        for (int64_t x = 0; x < width; ++x) {
            double h = 0.0;
            if (!game3d_heightmap_read_f64(&cursor, &h)) {
                free(samples);
                rt_string_unref(text);
                return NULL;
            }
            if (h < 0.0)
                h = 0.0;
            else if (h > 1.0)
                h = 1.0;
            samples[(size_t)z * (size_t)width + (size_t)x] = h;
        }
    }

    if (target_width < 1 || target_width > 4096)
        target_width = width;
    if (target_depth < 1 || target_depth > 4096)
        target_depth = depth;
    void *pixels = rt_pixels_new(target_width, target_depth);
    if (!pixels) {
        free(samples);
        rt_string_unref(text);
        return NULL;
    }

    for (int64_t z = 0; z < target_depth; ++z) {
        int64_t src_z = z * depth / target_depth;
        if (src_z >= depth)
            src_z = depth - 1;
        for (int64_t x = 0; x < target_width; ++x) {
            int64_t src_x = x * width / target_width;
            if (src_x >= width)
                src_x = width - 1;
            double h = samples[(size_t)src_z * (size_t)width + (size_t)src_x];
            rt_pixels_set(pixels, x, z, (int64_t)game3d_heightmap_sample_rgba(h));
        }
    }

    free(samples);
    rt_string_unref(text);
    return pixels;
}

/// @brief Build the unique collider name for a terrain tile (used to register/find its physics
/// collider).
static rt_string game3d_terrain_collider_name(const rt_game3d_stream_terrain_tile *tile) {
    const char *base = tile && tile->name ? rt_string_cstr(tile->name) : "terrain_tile";
    const char *suffix = "_heightfield_collider";
    size_t base_len = strlen(base);
    size_t suffix_len = strlen(suffix);
    if (base_len > SIZE_MAX - suffix_len - 1u)
        return rt_string_ref(rt_const_cstr("terrain_tile_heightfield_collider"));
    char *name = (char *)malloc(base_len + suffix_len + 1u);
    if (!name)
        return rt_string_ref(rt_const_cstr("terrain_tile_heightfield_collider"));
    memcpy(name, base, base_len);
    memcpy(name + base_len, suffix, suffix_len + 1u);
    rt_string result = rt_string_from_bytes(name, base_len + suffix_len);
    free(name);
    return result;
}

/// @brief Create and register a heightfield collider for a loaded terrain tile.
static void game3d_world_stream_attach_terrain_collider(rt_game3d_world_stream *stream,
                                                        rt_game3d_stream_terrain_tile *tile,
                                                        void *heightmap) {
    if (!stream || !tile || !heightmap || tile->collider_entity)
        return;
    if (!tile->collision_enabled)
        return;
    rt_game3d_world *world =
        (rt_game3d_world *)rt_g3d_checked_or_null(stream->world, RT_G3D_GAME3D_WORLD_CLASS_ID);
    if (!world || world->destroyed || !world->physics)
        return;

    void *collider =
        rt_collider3d_new_heightfield(heightmap, tile->scale[0], tile->scale[1], tile->scale[2]);
    if (!collider)
        return;
    void *body = rt_body3d_new(0.0);
    if (!body) {
        game3d_release_ref(&collider);
        return;
    }
    rt_body3d_set_static(body, 1);
    rt_body3d_set_collision_layer(body,
                                  game3d_stream_layer_or_valid(tile->layer, RT_GAME3D_LAYER_WORLD));
    rt_body3d_set_collision_mask(body,
                                 tile->has_collision_mask ? tile->collision_mask : ~(int64_t)0);
    rt_body3d_set_collider(body, collider);

    void *entity = rt_game3d_entity_new();
    if (entity) {
        rt_string name = game3d_terrain_collider_name(tile);
        rt_game3d_entity_set_name(entity, name);
        rt_string_unref(name);
        rt_game3d_entity_set_position(entity, tile->center[0], tile->center[1], tile->center[2]);
        rt_game3d_entity_attach_body(entity, body);
        rt_game3d_world_spawn(stream->world, entity);
        game3d_assign_ref(&tile->collider_entity, entity);
    }

    game3d_release_ref(&entity);
    game3d_release_ref(&body);
    game3d_release_ref(&collider);
}

/// @brief Build the unique nav-source name for a terrain tile (used to register/find its nav
/// geometry).
static rt_string game3d_terrain_nav_source_name(const rt_game3d_stream_terrain_tile *tile) {
    const char *base = tile && tile->name ? rt_string_cstr(tile->name) : "terrain_tile";
    const char *suffix = "_navmesh_source";
    size_t base_len = strlen(base);
    size_t suffix_len = strlen(suffix);
    if (base_len > SIZE_MAX - suffix_len - 1u)
        return rt_string_ref(rt_const_cstr("terrain_tile_navmesh_source"));
    char *name = (char *)malloc(base_len + suffix_len + 1u);
    if (!name)
        return rt_string_ref(rt_const_cstr("terrain_tile_navmesh_source"));
    memcpy(name, base, base_len);
    memcpy(name + base_len, suffix, suffix_len + 1u);
    rt_string result = rt_string_from_bytes(name, base_len + suffix_len);
    free(name);
    return result;
}

/// @brief Register a loaded terrain tile's geometry as a navmesh source for pathfinding.
static void game3d_world_stream_attach_terrain_nav_source(rt_game3d_world_stream *stream,
                                                          rt_game3d_stream_terrain_tile *tile) {
    if (!stream || !tile || !tile->terrain || tile->nav_entity)
        return;
    rt_game3d_world *world =
        (rt_game3d_world *)rt_g3d_checked_or_null(stream->world, RT_G3D_GAME3D_WORLD_CLASS_ID);
    if (!world || world->destroyed || !world->scene)
        return;

    void *mesh = rt_terrain3d_build_nav_mesh(tile->terrain, 4);
    if (!mesh)
        return;
    void *entity = rt_game3d_entity_new();
    if (entity) {
        rt_game3d_entity_set_mesh(entity, mesh);
        rt_string name = game3d_terrain_nav_source_name(tile);
        rt_game3d_entity_set_name(entity, name);
        rt_string_unref(name);
        double sx = game3d_stream_terrain_axis_scale(tile->scale[0]);
        double sz = game3d_stream_terrain_axis_scale(tile->scale[2]);
        double tx = tile->center[0] - ((double)(tile->width - 1) * sx) * 0.5;
        double tz = tile->center[2] - ((double)(tile->depth - 1) * sz) * 0.5;
        rt_game3d_entity_set_position(entity, tx, tile->center[1], tz);
        rt_game3d_entity *impl = (rt_game3d_entity *)entity;
        if (impl->node)
            rt_scene_node3d_set_visible(impl->node, 0);
        rt_game3d_world_spawn(stream->world, entity);
        game3d_assign_ref(&tile->nav_entity, entity);
    }
    game3d_release_ref(&entity);
    game3d_release_ref(&mesh);
}

/// @brief Recreate collider/nav sources so they reflect the terrain's post-stitch height grid.
static void game3d_world_stream_refresh_terrain_spatial_sources(
    rt_game3d_world_stream *stream, rt_game3d_stream_terrain_tile *tile) {
    if (!stream || !tile || !tile->resident || !tile->terrain)
        return;
    game3d_world_stream_detach_terrain_spatial_sources(stream, tile);
    void *heightmap = rt_terrain3d_build_heightmap_pixels(tile->terrain);
    if (heightmap) {
        game3d_world_stream_attach_terrain_collider(stream, tile, heightmap);
        game3d_release_ref(&heightmap);
    }
    game3d_world_stream_attach_terrain_nav_source(stream, tile);
}

/// @brief Stitch a pair of resident terrain tiles when their full manifest edges touch.
static int64_t game3d_world_stream_stitch_terrain_pair(rt_game3d_stream_terrain_tile *a,
                                                       rt_game3d_stream_terrain_tile *b) {
    if (!a || !b || !a->resident || !b->resident || !a->terrain || !b->terrain)
        return 0;
    double a_min_x, a_max_x, a_min_z, a_max_z;
    double b_min_x, b_max_x, b_min_z, b_max_z;
    game3d_stream_terrain_tile_bounds(a, &a_min_x, &a_max_x, &a_min_z, &a_max_z);
    game3d_stream_terrain_tile_bounds(b, &b_min_x, &b_max_x, &b_min_z, &b_max_z);
    double tolerance = game3d_stream_terrain_seam_tolerance(a, b);
    if (game3d_stream_range_matches(a_min_z, a_max_z, b_min_z, b_max_z, tolerance)) {
        if (game3d_stream_almost_equal(a_max_x, b_min_x, tolerance)) {
            return rt_terrain3d_stitch_edge(
                a->terrain, RT_TERRAIN3D_EDGE_EAST, b->terrain, RT_TERRAIN3D_EDGE_WEST);
        }
        if (game3d_stream_almost_equal(b_max_x, a_min_x, tolerance)) {
            return rt_terrain3d_stitch_edge(
                a->terrain, RT_TERRAIN3D_EDGE_WEST, b->terrain, RT_TERRAIN3D_EDGE_EAST);
        }
    }
    if (game3d_stream_range_matches(a_min_x, a_max_x, b_min_x, b_max_x, tolerance)) {
        if (game3d_stream_almost_equal(a_max_z, b_min_z, tolerance)) {
            return rt_terrain3d_stitch_edge(
                a->terrain, RT_TERRAIN3D_EDGE_SOUTH, b->terrain, RT_TERRAIN3D_EDGE_NORTH);
        }
        if (game3d_stream_almost_equal(b_max_z, a_min_z, tolerance)) {
            return rt_terrain3d_stitch_edge(
                a->terrain, RT_TERRAIN3D_EDGE_NORTH, b->terrain, RT_TERRAIN3D_EDGE_SOUTH);
        }
    }
    return 0;
}

/// @brief Stitch @p tile against all currently resident terrain neighbors.
static int64_t game3d_world_stream_stitch_loaded_terrain_neighbors(
    rt_game3d_world_stream *stream, rt_game3d_stream_terrain_tile *tile) {
    if (!stream || !tile || !tile->resident || !tile->terrain)
        return 0;
    int64_t changed = 0;
    for (int32_t i = 0; i < stream->terrain_tile_count; ++i) {
        rt_game3d_stream_terrain_tile *neighbor = &stream->terrain_tiles[i];
        if (neighbor == tile)
            continue;
        int64_t delta = game3d_world_stream_stitch_terrain_pair(tile, neighbor);
        if (delta > 0) {
            if (changed > INT64_MAX - delta)
                changed = INT64_MAX;
            else
                changed += delta;
        }
    }
    return changed;
}

/// @brief Refresh all resident streamed terrain collider/nav sources after seam changes.
static void game3d_world_stream_refresh_all_terrain_spatial_sources(
    rt_game3d_world_stream *stream) {
    if (!stream)
        return;
    for (int32_t i = 0; i < stream->terrain_tile_count; ++i) {
        rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
        if (tile->resident && tile->terrain)
            game3d_world_stream_refresh_terrain_spatial_sources(stream, tile);
    }
}

/// @brief Load a terrain tile: read its heightmap, build the mesh, and attach collider + nav
/// source.
/// @return 1 on success, 0 on load failure (tile left unloaded).
static int game3d_world_stream_load_terrain_tile(rt_game3d_world_stream *stream,
                                                 rt_game3d_stream_terrain_tile *tile) {
    if (!tile)
        return 0;
    if (tile->resident && tile->terrain)
        return 1;
    game3d_release_ref(&tile->terrain);
    if (tile->collider_entity || tile->nav_entity)
        game3d_world_stream_unload_terrain_tile(stream, tile);
    int64_t width = tile->width;
    int64_t depth = tile->depth;
    if (width < 2)
        width = 2;
    if (depth < 2)
        depth = 2;
    if (width > 4096)
        width = 4096;
    if (depth > 4096)
        depth = 4096;
    void *terrain = rt_terrain3d_new(width, depth);
    if (!terrain)
        return 0;
    void *heightmap = game3d_load_heightmap_sidecar(tile->heightmap_path, width, depth);
    if (!heightmap)
        heightmap = rt_pixels_new(width, depth);
    if (heightmap)
        rt_terrain3d_set_heightmap(terrain, heightmap);
    rt_terrain3d_set_scale(terrain, tile->scale[0], tile->scale[1], tile->scale[2]);
    void *material = rt_material3d_new_color(0.20, 0.34, 0.22);
    if (material) {
        rt_material3d_set_shading_model(material, RT_GAME3D_SHADING_PBR);
        rt_material3d_set_metallic(material, 0.0);
        rt_material3d_set_roughness(material, 0.88);
        rt_material3d_set_ao(material, 1.0);
        rt_terrain3d_set_material(terrain, material);
        game3d_release_ref(&material);
    }
    tile->terrain = terrain;
    tile->resident = 1;
    int64_t stitched = game3d_world_stream_stitch_loaded_terrain_neighbors(stream, tile);
    game3d_release_ref(&heightmap);
    if (stitched > 0)
        game3d_world_stream_refresh_all_terrain_spatial_sources(stream);
    else
        game3d_world_stream_refresh_terrain_spatial_sources(stream, tile);
    return 1;
}

/// @brief Unload a streaming cell: despawn its spawned entities/nodes and release its scene.
static void game3d_world_stream_unload_cell(rt_game3d_world_stream *stream,
                                            rt_game3d_stream_cell *cell) {
    if (!cell)
        return;
    if (cell->resident && cell->entity && stream && stream->world) {
        rt_game3d_entity *entity =
            (rt_game3d_entity *)rt_g3d_checked_or_null(cell->entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
        if (entity && entity->spawned && entity->world == stream->world)
            game3d_world_despawn_entity_tree((rt_game3d_world *)stream->world, entity, 1);
    }
    game3d_release_ref(&cell->entity);
    game3d_release_ref(&cell->scene);
    cell->resident = 0;
    cell->measured_resident_bytes = 0;
}

/// @brief Load a streaming cell: load its scene/model and spawn its content into the world.
/// @return 1 on success, 0 on load failure.
static int game3d_world_stream_load_cell(rt_game3d_world_stream *stream,
                                         rt_game3d_stream_cell *cell) {
    if (!stream || !cell)
        return 0;
    if (cell->resident)
        return 1;
    rt_game3d_world *world =
        (rt_game3d_world *)rt_g3d_checked_or_null(stream->world, RT_G3D_GAME3D_WORLD_CLASS_ID);
    if (!world || world->destroyed || !cell->path)
        return 0;
    void *scene = rt_scene3d_load(cell->path);
    if (!scene)
        return 0;
    void *root = rt_scene3d_get_root(scene);
    void *entity = rt_game3d_entity_from_node(root);
    if (entity && cell->name)
        rt_game3d_entity_set_name(entity, cell->name);
    if (entity) {
        if (cell->has_layer)
            rt_game3d_entity_set_layer(entity, cell->layer);
        if (cell->has_collision_mask) {
            void *mask = game3d_layermask_new_bits(cell->collision_mask);
            rt_game3d_entity_set_collision_mask(entity, mask);
            game3d_release_ref(&mask);
        }
        rt_game3d_entity_set_position(entity, cell->center[0], cell->center[1], cell->center[2]);
        int64_t next_id = world->next_entity_id;
        if (game3d_world_spawn_entity_tree(world, (rt_game3d_entity *)entity, 1, &next_id)) {
            world->next_entity_id = next_id;
            cell->scene = scene;
            cell->entity = entity;
            cell->resident = 1;
            uint64_t measured = game3d_scene_estimate_resident_bytes(scene);
            cell->measured_resident_bytes = game3d_i64_from_u64_saturating(measured);
            if (cell->measured_resident_bytes <= 0)
                cell->measured_resident_bytes =
                    cell->resident_bytes > 0 ? cell->resident_bytes : 64 * 1024;
            scene = NULL;
        } else {
            game3d_release_ref(&entity);
        }
    }
    game3d_release_ref(&scene);
    return cell->resident ? 1 : 0;
}

/// @brief Remeasure a loaded cell's scene residency after mesh/texture resident-state changes.
static void game3d_world_stream_refresh_cell_residency_bytes(rt_game3d_stream_cell *cell) {
    uint64_t measured;
    if (!cell || !cell->resident || !cell->scene)
        return;
    measured = game3d_scene_estimate_resident_bytes(cell->scene);
    cell->measured_resident_bytes = game3d_i64_from_u64_saturating(measured);
    if (cell->measured_resident_bytes <= 0)
        cell->measured_resident_bytes = cell->resident_bytes > 0 ? cell->resident_bytes : 64 * 1024;
}

/// @brief Unload and free every streaming cell tracked by the stream.
static void game3d_world_stream_clear_cells(rt_game3d_world_stream *stream) {
    if (!stream)
        return;
    for (int32_t i = 0; i < stream->cell_count; ++i) {
        rt_game3d_stream_cell *cell = &stream->cells[i];
        game3d_world_stream_unload_cell(stream, cell);
        game3d_release_ref((void **)&cell->name);
        game3d_release_ref((void **)&cell->path);
        game3d_release_ref((void **)&cell->material);
        game3d_release_ref((void **)&cell->nav_area);
        game3d_release_ref((void **)&cell->sidecar_path);
    }
    free(stream->cells);
    stream->cells = NULL;
    stream->cell_count = 0;
    stream->cell_capacity = 0;
    stream->cells_manifest_loaded = 0;
}

/// @brief Parse the cells manifest (JSON) into the stream's cell table (name, center, path per
/// cell).
/// @return 1 on success, 0 on a missing or malformed manifest.
static int game3d_world_stream_parse_cells_manifest(rt_game3d_world_stream *stream) {
    if (!stream || !game3d_string_has_bytes(stream->cells_manifest))
        return 0;
    rt_string text = game3d_read_text_file_no_trap(stream->cells_manifest);
    if (!text)
        return 0;
    void *root = NULL;
    if (!rt_json_try_parse(text, &root, NULL, NULL, NULL) || !game3d_json_is_map(root)) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    void *cells = game3d_json_get(root, "cells");
    if (!game3d_json_is_seq(cells)) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    int64_t count64 = rt_seq_len(cells);
    if (count64 < 0 || count64 > INT32_MAX) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    rt_game3d_stream_cell *parsed = NULL;
    int32_t parsed_count = 0;
    if (count64 > 0) {
        parsed = (rt_game3d_stream_cell *)calloc((size_t)count64, sizeof(*parsed));
        if (!parsed) {
            rt_string_unref(text);
            game3d_release_ref(&root);
            return 0;
        }
    }
    for (int64_t i = 0; i < count64; ++i) {
        void *entry = rt_seq_get(cells, i);
        if (!game3d_json_is_map(entry))
            continue;
        rt_string raw_path = game3d_json_string_ref(entry, "path");
        if (!raw_path)
            continue;
        rt_game3d_stream_cell *cell = &parsed[parsed_count++];
        cell->path = game3d_stream_resolve_manifest_path(stream->cells_manifest, raw_path);
        rt_string_unref(raw_path);
        cell->name = game3d_json_string_ref(entry, "name");
        cell->material = game3d_json_string_ref(entry, "material");
        cell->nav_area = game3d_json_string_ref(entry, "navArea");
        cell->sidecar_path = game3d_json_resolved_sidecar_ref(entry, stream->cells_manifest);
        game3d_stream_parse_collision_metadata(entry,
                                               0,
                                               &cell->layer,
                                               &cell->has_layer,
                                               &cell->collision_mask,
                                               &cell->has_collision_mask,
                                               &cell->collision_enabled);
        cell->traversal_cost = game3d_stream_traversal_cost_or_default(entry);
        double zero[3] = {0.0, 0.0, 0.0};
        (void)game3d_json_vec3_or(entry, "center", cell->center, zero);
        cell->radius = game3d_json_f64_or(entry, "radius", 0.0);
        if (!isfinite(cell->radius) || cell->radius < 0.0)
            cell->radius = 0.0;
        cell->resident_bytes = game3d_json_i64_or(entry, "bytes", 64 * 1024);
        if (cell->resident_bytes < 0)
            cell->resident_bytes = 0;
    }
    game3d_world_stream_clear_cells(stream);
    stream->cells = parsed;
    stream->cell_count = parsed_count;
    stream->cell_capacity = (int32_t)count64;
    stream->cells_manifest_loaded = 1;
    rt_string_unref(text);
    game3d_release_ref(&root);
    return 1;
}

/// @brief Parse the terrain manifest (JSON) into the stream's terrain-tile table.
/// @return 1 on success, 0 on a missing or malformed manifest.
static int game3d_world_stream_parse_terrain_manifest(rt_game3d_world_stream *stream) {
    if (!stream || !game3d_string_has_bytes(stream->terrain_manifest))
        return 0;
    rt_string text = game3d_read_text_file_no_trap(stream->terrain_manifest);
    if (!text)
        return 0;
    void *root = NULL;
    if (!rt_json_try_parse(text, &root, NULL, NULL, NULL) || !game3d_json_is_map(root)) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    void *tiles = game3d_json_get(root, "tiles");
    if (!game3d_json_is_seq(tiles)) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    int64_t count64 = rt_seq_len(tiles);
    if (count64 < 0 || count64 > INT32_MAX) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    rt_game3d_stream_terrain_tile *parsed = NULL;
    int32_t parsed_count = 0;
    if (count64 > 0) {
        parsed = (rt_game3d_stream_terrain_tile *)calloc((size_t)count64, sizeof(*parsed));
        if (!parsed) {
            rt_string_unref(text);
            game3d_release_ref(&root);
            return 0;
        }
    }
    for (int64_t i = 0; i < count64; ++i) {
        void *entry = rt_seq_get(tiles, i);
        if (!game3d_json_is_map(entry))
            continue;
        rt_string raw_path = game3d_json_string_ref(entry, "path");
        if (!raw_path)
            continue;
        rt_game3d_stream_terrain_tile *tile = &parsed[parsed_count++];
        tile->path = game3d_stream_resolve_manifest_path(stream->terrain_manifest, raw_path);
        rt_string_unref(raw_path);
        rt_string raw_heightmap = game3d_json_string_ref(entry, "heightmap");
        tile->heightmap_path =
            raw_heightmap
                ? game3d_stream_resolve_manifest_path(stream->terrain_manifest, raw_heightmap)
                : rt_string_ref(rt_const_cstr(""));
        rt_string_unref(raw_heightmap);
        tile->name = game3d_json_string_ref(entry, "name");
        tile->material = game3d_json_string_ref(entry, "material");
        tile->nav_area = game3d_json_string_ref(entry, "navArea");
        tile->sidecar_path = game3d_json_resolved_sidecar_ref(entry, stream->terrain_manifest);
        game3d_stream_parse_collision_metadata(entry,
                                               RT_GAME3D_LAYER_WORLD,
                                               &tile->layer,
                                               &tile->has_layer,
                                               &tile->collision_mask,
                                               &tile->has_collision_mask,
                                               &tile->collision_enabled);
        tile->traversal_cost = game3d_stream_traversal_cost_or_default(entry);
        double zero[3] = {0.0, 0.0, 0.0};
        (void)game3d_json_vec3_or(entry, "center", tile->center, zero);
        double unit[3] = {1.0, 1.0, 1.0};
        (void)game3d_json_vec3_or(entry, "scale", tile->scale, unit);
        tile->width = game3d_json_i64_or(entry, "width", 2);
        tile->depth = game3d_json_i64_or(entry, "depth", tile->width);
        if (tile->width < 2)
            tile->width = 2;
        if (tile->depth < 2)
            tile->depth = 2;
        if (tile->width > 4096)
            tile->width = 4096;
        if (tile->depth > 4096)
            tile->depth = 4096;
        tile->radius = game3d_json_f64_or(entry, "radius", 0.0);
        if (!isfinite(tile->radius) || tile->radius < 0.0)
            tile->radius = 0.0;
        tile->resident_bytes = game3d_json_i64_or(entry, "bytes", 256 * 1024);
        if (tile->resident_bytes < 0)
            tile->resident_bytes = 0;
    }
    game3d_world_stream_clear_terrain_tiles(stream);
    stream->terrain_tiles = parsed;
    stream->terrain_tile_count = parsed_count;
    stream->terrain_tile_capacity = (int32_t)count64;
    stream->terrain_manifest_loaded = 1;
    rt_string_unref(text);
    game3d_release_ref(&root);
    return 1;
}

/// @brief GC finalizer for WorldStream3D: release world and manifest references.
static void game3d_world_stream_finalize(void *obj) {
    rt_game3d_world_stream *stream = (rt_game3d_world_stream *)obj;
    if (!stream)
        return;
    game3d_world_stream_clear_cells(stream);
    game3d_world_stream_clear_terrain_tiles(stream);
    if (stream->retains_world)
        game3d_release_ref(&stream->world);
    else
        stream->world = NULL;
    game3d_release_ref((void **)&stream->terrain_manifest);
    game3d_release_ref((void **)&stream->cells_manifest);
}

/// @brief Convert a world-space load/unload radius to a cell-count radius (ceil of radius /
/// cell_size).
static int64_t game3d_stream_radius_slots(double radius, double cell_size) {
    if (!isfinite(radius) || radius <= 0.0)
        return 1;
    double slots = ceil(radius / cell_size);
    if (slots < 0.0)
        slots = 0.0;
    if (slots > 32767.0)
        slots = 32767.0;
    int64_t r = (int64_t)slots;
    int64_t side = r * 2 + 1;
    if (side > INT64_MAX / side)
        return INT64_MAX;
    return side * side;
}

/// @brief Parse a manifest byte-size value (e.g. residency budget) into an int64 (0 if invalid).
static int64_t game3d_stream_manifest_bytes(rt_string value) {
    const char *bytes = value ? rt_string_cstr(value) : "";
    if (!bytes || bytes[0] == '\0')
        return 0;
    size_t len = bytes ? strlen(bytes) : 0;
    if (len > (size_t)INT64_MAX - 1u)
        return INT64_MAX;
    return (int64_t)len + 1;
}

/// @brief Saturating signed 64-bit addition (clamps to INT64_MIN/MAX instead of overflowing).
static int64_t game3d_i64_saturating_add(int64_t a, int64_t b) {
    if (a > INT64_MAX - b)
        return INT64_MAX;
    return a + b;
}

/// @brief Saturating signed 64-bit multiplication (clamps to INT64_MIN/MAX on overflow).
static int64_t game3d_i64_saturating_mul(int64_t a, int64_t b) {
    if (a <= 0 || b <= 0)
        return 0;
    if (a > INT64_MAX / b)
        return INT64_MAX;
    return a * b;
}

/// @brief Compute this frame's streaming load budget (bytes) from the frame delta @p dt.
/// @details Caps how much may be streamed per frame so loading spikes don't stall the simulation.
static int64_t game3d_world_stream_update_load_budget(double dt) {
    if (!isfinite(dt) || dt <= 0.0)
        return 1;
    double budget = ceil(dt * 60.0);
    if (budget < 1.0)
        budget = 1.0;
    if (budget > 8.0)
        budget = 8.0;
    return (int64_t)budget;
}

/// @brief Reconcile streamed content with the camera: load in-radius cells/tiles and unload far
/// ones.
/// @details Loads nearest-first within the per-frame byte budget and the residency budget, and
/// unloads
///          anything beyond the unload radius — the heart of the open-world streaming loop.
static void game3d_world_stream_recompute(rt_game3d_world_stream *stream, int64_t load_budget) {
    if (!stream)
        return;
    int64_t pending = 0;
    int64_t cells = 0;
    int64_t cell_bytes = 0;
    if (stream->cells_manifest_loaded) {
        int64_t manifest_bytes = game3d_stream_manifest_bytes(stream->cells_manifest);
        int64_t budget = stream->residency_budget_bytes;
        if (budget < 0 || manifest_bytes <= budget) {
            cell_bytes = manifest_bytes;
            for (int32_t i = 0; i < stream->cell_count; ++i) {
                rt_game3d_stream_cell *cell = &stream->cells[i];
                double radius = cell->radius;
                double threshold =
                    (cell->resident ? stream->unload_radius : stream->load_radius) + radius;
                if (!isfinite(threshold) || threshold < 0.0)
                    threshold = radius;
                int desired = game3d_stream_cell_distance_sq(stream, cell) <= threshold * threshold;
                if (cell->resident)
                    game3d_world_stream_refresh_cell_residency_bytes(cell);
                int64_t bytes = game3d_stream_cell_bytes(cell);
                if (!desired || (budget >= 0 && cell_bytes > budget - bytes)) {
                    game3d_world_stream_unload_cell(stream, cell);
                    continue;
                }
                if (cell->resident) {
                    cells++;
                    cell_bytes = game3d_i64_saturating_add(cell_bytes, bytes);
                    continue;
                }
                if (load_budget == 0) {
                    pending++;
                    continue;
                }
                if (load_budget > 0)
                    load_budget--;
                if (game3d_world_stream_load_cell(stream, cell)) {
                    bytes = game3d_stream_cell_bytes(cell);
                    if (budget >= 0 && cell_bytes > budget - bytes) {
                        game3d_world_stream_unload_cell(stream, cell);
                        continue;
                    }
                    cells++;
                    cell_bytes = game3d_i64_saturating_add(cell_bytes, bytes);
                }
            }
        } else {
            for (int32_t i = 0; i < stream->cell_count; ++i)
                game3d_world_stream_unload_cell(stream, &stream->cells[i]);
            cell_bytes = 0;
        }
    } else {
        cells = game3d_string_has_bytes(stream->cells_manifest)
                    ? game3d_stream_radius_slots(stream->load_radius, 128.0)
                    : 0;
        cell_bytes = game3d_i64_saturating_add(game3d_stream_manifest_bytes(stream->cells_manifest),
                                               game3d_i64_saturating_mul(cells, 64 * 1024));
    }
    int64_t budget = stream->residency_budget_bytes;
    int64_t terrain = 0;
    int64_t terrain_bytes = 0;
    if (stream->terrain_manifest_loaded) {
        int64_t manifest_bytes = game3d_stream_manifest_bytes(stream->terrain_manifest);
        int terrain_unlimited = budget < 0;
        int64_t remaining_budget =
            terrain_unlimited ? INT64_MAX : (budget >= cell_bytes ? budget - cell_bytes : -1);
        if (terrain_unlimited || (remaining_budget >= 0 && manifest_bytes <= remaining_budget)) {
            terrain_bytes = manifest_bytes;
            for (int32_t i = 0; i < stream->terrain_tile_count; ++i) {
                rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
                double radius = tile->radius;
                double threshold =
                    (tile->resident ? stream->unload_radius : stream->load_radius) + radius;
                if (!isfinite(threshold) || threshold < 0.0)
                    threshold = radius;
                int desired =
                    game3d_stream_terrain_tile_distance_sq(stream, tile) <= threshold * threshold;
                int64_t tile_bytes = game3d_stream_terrain_tile_bytes(tile);
                if (!desired ||
                    (!terrain_unlimited && terrain_bytes > remaining_budget - tile_bytes)) {
                    game3d_world_stream_unload_terrain_tile(stream, tile);
                    continue;
                }
                if (tile->resident && tile->terrain) {
                    terrain++;
                    terrain_bytes = game3d_i64_saturating_add(terrain_bytes, tile_bytes);
                    continue;
                }
                if (load_budget == 0) {
                    pending++;
                    continue;
                }
                if (load_budget > 0)
                    load_budget--;
                if (game3d_world_stream_load_terrain_tile(stream, tile)) {
                    terrain++;
                    terrain_bytes = game3d_i64_saturating_add(terrain_bytes, tile_bytes);
                }
            }
        } else {
            for (int32_t i = 0; i < stream->terrain_tile_count; ++i)
                game3d_world_stream_unload_terrain_tile(stream, &stream->terrain_tiles[i]);
            terrain_bytes = 0;
        }
    } else {
        terrain = game3d_string_has_bytes(stream->terrain_manifest)
                      ? game3d_stream_radius_slots(stream->load_radius, 256.0)
                      : 0;
        terrain_bytes =
            game3d_i64_saturating_add(game3d_stream_manifest_bytes(stream->terrain_manifest),
                                      game3d_i64_saturating_mul(terrain, 256 * 1024));
        int64_t bytes = game3d_i64_saturating_add(cell_bytes, terrain_bytes);
        while (budget >= 0 && bytes > budget && (cells > 0 || terrain > 0)) {
            if (!stream->cells_manifest_loaded && cells >= terrain && cells > 0)
                cells--;
            else if (terrain > 0)
                terrain--;
            else if (stream->cells_manifest_loaded)
                break;
            if (stream->cells_manifest_loaded)
                cell_bytes = game3d_world_stream_resident_cell_bytes(stream);
            else
                cell_bytes =
                    game3d_i64_saturating_add(game3d_stream_manifest_bytes(stream->cells_manifest),
                                              game3d_i64_saturating_mul(cells, 64 * 1024));
            terrain_bytes =
                game3d_i64_saturating_add(game3d_stream_manifest_bytes(stream->terrain_manifest),
                                          game3d_i64_saturating_mul(terrain, 256 * 1024));
            bytes = game3d_i64_saturating_add(cell_bytes, terrain_bytes);
        }
    }

    int64_t bytes = game3d_i64_saturating_add(cell_bytes, terrain_bytes);
    if (budget >= 0 && bytes > budget) {
        if (stream->cells_manifest_loaded) {
            for (int32_t i = 0; i < stream->cell_count; ++i)
                game3d_world_stream_unload_cell(stream, &stream->cells[i]);
        }
        if (stream->terrain_manifest_loaded) {
            for (int32_t i = 0; i < stream->terrain_tile_count; ++i)
                game3d_world_stream_unload_terrain_tile(stream, &stream->terrain_tiles[i]);
        }
        cells = 0;
        terrain = 0;
        bytes = 0;
    }
    stream->resident_cell_count = cells;
    stream->resident_terrain_tile_count = terrain;
    stream->resident_bytes = bytes;
    stream->pending_request_count = pending;
}

/// @brief Point the world's camera at the origin from a default over-the-shoulder pose.
static void game3d_world_install_default_camera(rt_game3d_world *world) {
    rt_camera3d_look_at_components(world->camera, 0.0, 2.0, 6.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0);
}

/// @brief Create a world with default camera parameters. See header.
void *rt_game3d_world_new(rt_string title, int64_t width, int64_t height) {
    return rt_game3d_world_new_with_camera(title,
                                           width,
                                           height,
                                           RT_GAME3D_DEFAULT_FOV_DEG,
                                           RT_GAME3D_DEFAULT_NEAR,
                                           RT_GAME3D_DEFAULT_FAR);
}

/// @brief Create a world: open the canvas window and build the scene, camera, physics,
///   input, audio, and effects subsystems, then apply default lighting/quality/ambient.
///   Traps on non-positive dimensions or any component allocation failure. See header.
void *rt_game3d_world_new_with_camera(rt_string title,
                                      int64_t width,
                                      int64_t height,
                                      double fov_deg,
                                      double near_plane,
                                      double far_plane) {
    if (width <= 0 || height <= 0)
        rt_trap("Game3D.World3D.New: dimensions must be positive");
    if (width <= 0 || height <= 0)
        return NULL;
    rt_game3d_world *world =
        (rt_game3d_world *)rt_obj_new_i64(RT_G3D_GAME3D_WORLD_CLASS_ID, (int64_t)sizeof(*world));
    if (!world) {
        rt_trap("Game3D.World3D.New: allocation failed");
        return NULL;
    }
    memset(world, 0, sizeof(*world));
    rt_obj_set_finalizer(world, game3d_world_finalize);
    world->width = width;
    world->height = height;
    world->next_entity_id = 1;
    world->dt = RT_GAME3D_DEFAULT_DT;
    world->worker_count = game3d_default_worker_count();
    world->jobs_enabled = world->worker_count > 1 ? 1 : 0;
    world->origin_rebase_threshold = RT_GAME3D_DEFAULT_REBASE_THRESHOLD;
    world->clear_r = 0.04;
    world->clear_g = 0.055;
    world->clear_b = 0.065;
    world->debug_axis_size = 1.0;

    world->canvas = rt_canvas3d_new(title, width, height);
    world->scene = rt_scene3d_new();
    world->camera = rt_camera3d_new(fov_deg, (double)width / (double)height, near_plane, far_plane);
    world->physics = rt_world3d_new(0.0, -9.81, 0.0);
    world->input = rt_game3d_input_new();
    world->audio = game3d_audio_new(world->camera);
    world->effects = game3d_effects_new(world->canvas, RT_GAME3D_QUALITY_BALANCED);

    if (!world->canvas || !world->scene || !world->camera || !world->physics || !world->input ||
        !world->audio || !world->effects) {
        game3d_world_release_runtime(world, 0);
        if (rt_obj_release_check0(world))
            rt_obj_free(world);
        rt_trap("Game3D.World3D.New: component allocation failed");
        return NULL;
    }
    game3d_world_install_default_camera(world);
    rt_canvas3d_set_default_lighting(world->canvas);
    rt_canvas3d_set_quality(world->canvas, RT_GAME3D_QUALITY_BALANCED);
    rt_canvas3d_set_frustum_culling(world->canvas, 1);
    rt_canvas3d_set_ambient(world->canvas, 0.28, 0.30, 0.34);
    return world;
}

/// @brief Allocate and initialize a world-stream object bound to a world, with default
/// radii/budget.
static rt_game3d_world_stream *game3d_world_stream_create(rt_game3d_world *world,
                                                          int8_t retain_world,
                                                          const char *method) {
    if (!world) {
        rt_trap(method ? method : "Game3D.WorldStream3D.New: invalid world");
        return NULL;
    }
    rt_game3d_world_stream *stream = (rt_game3d_world_stream *)rt_obj_new_i64(
        RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID, (int64_t)sizeof(*stream));
    if (!stream) {
        rt_trap(method ? method : "Game3D.WorldStream3D.New: allocation failed");
        return NULL;
    }
    memset(stream, 0, sizeof(*stream));
    rt_obj_set_finalizer(stream, game3d_world_stream_finalize);
    stream->load_radius = 0.0;
    stream->unload_radius = 0.0;
    stream->residency_budget_bytes = -1;
    stream->retains_world = retain_world ? 1 : 0;
    if (stream->retains_world)
        game3d_assign_ref(&stream->world, world);
    else
        stream->world = world;
    game3d_world_stream_recompute(stream, -1);
    return stream;
}

/// @brief Create a WorldStream3D handle bound to a live World3D. See header.
void *rt_game3d_world_stream_new(void *world_obj) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.WorldStream3D.New: invalid world");
    if (!world)
        return NULL;
    return game3d_world_stream_create(world, 1, "Game3D.WorldStream3D.New: allocation failed");
}

/// @brief Number of streaming cells currently resident (loaded).
int64_t rt_game3d_world_stream_get_resident_cell_count(void *obj) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.get_residentCellCount: invalid stream");
    return stream ? stream->resident_cell_count : 0;
}

/// @brief Number of terrain tiles currently resident (loaded).
int64_t rt_game3d_world_stream_get_resident_terrain_tile_count(void *obj) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.get_residentTerrainTileCount: invalid stream");
    return stream ? stream->resident_terrain_tile_count : 0;
}

/// @brief Bounds-checked accessor for the cell at @p index (NULL if out of range).
static rt_game3d_stream_cell *game3d_world_stream_cell_at(rt_game3d_world_stream *stream,
                                                          int64_t index) {
    if (!stream || index < 0 || index >= stream->cell_count)
        return NULL;
    return &stream->cells[index];
}

/// @brief Bounds-checked accessor for the terrain tile at @p index (NULL if out of range).
static rt_game3d_stream_terrain_tile *game3d_world_stream_terrain_tile_at(
    rt_game3d_world_stream *stream, int64_t index) {
    if (!stream || index < 0 || index >= stream->terrain_tile_count)
        return NULL;
    return &stream->terrain_tiles[index];
}

/// @brief Get the @p index-th resident terrain tile's runtime handle (NULL if out of range).
void *rt_game3d_world_stream_get_resident_terrain_tile(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getResidentTerrainTile: invalid stream");
    if (!stream || index < 0)
        return NULL;
    int64_t resident_index = 0;
    for (int32_t i = 0; i < stream->terrain_tile_count; ++i) {
        rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
        if (!tile->resident || !tile->terrain)
            continue;
        if (resident_index == index)
            return tile->terrain;
        resident_index++;
    }
    return NULL;
}

/// @brief Total number of cells declared in the stream's manifest (resident or not).
int64_t rt_game3d_world_stream_get_cell_count(void *obj) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellCount: invalid stream");
    return stream ? stream->cell_count : 0;
}

/// @brief Name of the cell at @p index ("" if out of range).
rt_string rt_game3d_world_stream_get_cell_name(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellName: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return rt_string_ref(cell && cell->name ? cell->name : rt_const_cstr(""));
}

/// @brief World-space center of the cell at @p index as a fresh Vec3 (origin if out of range).
void *rt_game3d_world_stream_get_cell_center(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellCenter: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    if (!cell)
        return NULL;
    return rt_vec3_new(cell->center[0], cell->center[1], cell->center[2]);
}

/// @brief Whether the cell at @p index is currently loaded.
int8_t rt_game3d_world_stream_get_cell_resident(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellResident: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell && cell->resident ? 1 : 0;
}

/// @brief Resident byte size of the cell at @p index (0 if not loaded / out of range).
int64_t rt_game3d_world_stream_get_cell_bytes(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellBytes: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell ? game3d_stream_cell_bytes(cell) : 0;
}

rt_string rt_game3d_world_stream_get_cell_material(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellMaterial: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return rt_string_ref(cell && cell->material ? cell->material : rt_const_cstr(""));
}

rt_string rt_game3d_world_stream_get_cell_sidecar(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellSidecar: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return rt_string_ref(cell && cell->sidecar_path ? cell->sidecar_path : rt_const_cstr(""));
}

int64_t rt_game3d_world_stream_get_cell_layer(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellLayer: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell && cell->has_layer ? cell->layer : 0;
}

int64_t rt_game3d_world_stream_get_cell_collision_mask(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getCellCollisionMask: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell && cell->has_collision_mask ? cell->collision_mask : ~(int64_t)0;
}

int8_t rt_game3d_world_stream_get_cell_collision_enabled(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getCellCollisionEnabled: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell ? cell->collision_enabled : 0;
}

rt_string rt_game3d_world_stream_get_cell_nav_area(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellNavArea: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return rt_string_ref(cell && cell->nav_area ? cell->nav_area : rt_const_cstr(""));
}

double rt_game3d_world_stream_get_cell_traversal_cost(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getCellTraversalCost: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell ? cell->traversal_cost : 0.0;
}

/// @brief Total number of terrain tiles declared in the stream's manifest.
int64_t rt_game3d_world_stream_get_terrain_tile_count(void *obj) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileCount: invalid stream");
    return stream ? stream->terrain_tile_count : 0;
}

/// @brief Name of the terrain tile at @p index ("" if out of range).
rt_string rt_game3d_world_stream_get_terrain_tile_name(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getTerrainTileName: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return rt_string_ref(tile && tile->name ? tile->name : rt_const_cstr(""));
}

/// @brief Heightmap path of the terrain tile at @p index ("" if out of range).
rt_string rt_game3d_world_stream_get_terrain_tile_heightmap(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileHeightmap: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return rt_string_ref(tile && tile->heightmap_path ? tile->heightmap_path : rt_const_cstr(""));
}

/// @brief World-space center of the terrain tile at @p index as a fresh Vec3 (origin if out of
/// range).
void *rt_game3d_world_stream_get_terrain_tile_center(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileCenter: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    if (!tile)
        return NULL;
    return rt_vec3_new(tile->center[0], tile->center[1], tile->center[2]);
}

/// @brief Whether the terrain tile at @p index is currently loaded.
int8_t rt_game3d_world_stream_get_terrain_tile_resident(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileResident: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile && tile->resident ? 1 : 0;
}

/// @brief Resident byte size of the terrain tile at @p index (0 if not loaded / out of range).
int64_t rt_game3d_world_stream_get_terrain_tile_bytes(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileBytes: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile ? game3d_stream_terrain_tile_bytes(tile) : 0;
}

rt_string rt_game3d_world_stream_get_terrain_tile_material(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileMaterial: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return rt_string_ref(tile && tile->material ? tile->material : rt_const_cstr(""));
}

rt_string rt_game3d_world_stream_get_terrain_tile_sidecar(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileSidecar: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return rt_string_ref(tile && tile->sidecar_path ? tile->sidecar_path : rt_const_cstr(""));
}

int64_t rt_game3d_world_stream_get_terrain_tile_layer(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileLayer: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile ? game3d_stream_layer_or_valid(tile->layer, RT_GAME3D_LAYER_WORLD) : 0;
}

int64_t rt_game3d_world_stream_get_terrain_tile_collision_mask(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileCollisionMask: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile && tile->has_collision_mask ? tile->collision_mask : ~(int64_t)0;
}

int8_t rt_game3d_world_stream_get_terrain_tile_collision_enabled(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileCollisionEnabled: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile ? tile->collision_enabled : 0;
}

rt_string rt_game3d_world_stream_get_terrain_tile_nav_area(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileNavArea: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return rt_string_ref(tile && tile->nav_area ? tile->nav_area : rt_const_cstr(""));
}

double rt_game3d_world_stream_get_terrain_tile_traversal_cost(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileTraversalCost: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile ? tile->traversal_cost : 0.0;
}

/// @brief Number of cells/tiles queued for loading but not yet resident.
int64_t rt_game3d_world_stream_get_pending_request_count(void *obj) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.get_pendingRequestCount: invalid stream");
    return stream ? stream->pending_request_count : 0;
}

/// @brief Total resident bytes across all loaded cells and terrain tiles.
int64_t rt_game3d_world_stream_get_resident_bytes(void *obj) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.get_residentBytes: invalid stream");
    return stream ? stream->resident_bytes : 0;
}

/// @brief Set the streaming focus point (usually the camera/player position) from a Vec3.
void rt_game3d_world_stream_set_center(void *obj, void *position) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.setCenter: invalid stream");
    double pos[3];
    if (!stream)
        return;
    if (!game3d_read_vec3(position, pos, "Game3D.WorldStream3D.setCenter: center must be Vec3"))
        return;
    stream->center[0] = pos[0];
    stream->center[1] = pos[1];
    stream->center[2] = pos[2];
}

/// @brief Set the load and unload radii (in world units) that bound which content streams in/out.
/// @details The unload radius should exceed the load radius to provide hysteresis against
/// thrashing.
void rt_game3d_world_stream_set_radii(void *obj, double load_radius, double unload_radius) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.setRadii: invalid stream");
    if (!stream)
        return;
    load_radius = game3d_finite_or(load_radius, 0.0);
    unload_radius = game3d_finite_or(unload_radius, load_radius);
    if (load_radius < 0.0)
        load_radius = 0.0;
    if (unload_radius < load_radius)
        unload_radius = load_radius;
    stream->load_radius = load_radius;
    stream->unload_radius = unload_radius;
    game3d_world_stream_recompute(stream, -1);
}

/// @brief Set the maximum total bytes of streamed content allowed resident at once.
void rt_game3d_world_stream_set_residency_budget(void *obj, int64_t bytes) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.setResidencyBudget: invalid stream");
    if (!stream)
        return;
    stream->residency_budget_bytes = bytes < 0 ? -1 : bytes;
    game3d_world_stream_recompute(stream, -1);
}

/// @brief Mount a tiled-terrain manifest, parsing its tile table so terrain streams with the world.
void rt_game3d_world_stream_mount_tiled_terrain(void *obj, rt_string manifest_path) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.mountTiledTerrain: invalid stream");
    if (!stream)
        return;
    game3d_world_stream_clear_terrain_tiles(stream);
    game3d_assign_ref((void **)&stream->terrain_manifest,
                      manifest_path ? manifest_path : rt_const_cstr(""));
    (void)game3d_world_stream_parse_terrain_manifest(stream);
    game3d_world_stream_recompute(stream, -1);
}

/// @brief Mount a cells manifest, parsing its cell table so scene chunks stream with the world.
void rt_game3d_world_stream_mount_cells(void *obj, rt_string manifest_path) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.mountCells: invalid stream");
    if (!stream)
        return;
    game3d_world_stream_clear_cells(stream);
    game3d_assign_ref((void **)&stream->cells_manifest,
                      manifest_path ? manifest_path : rt_const_cstr(""));
    (void)game3d_world_stream_parse_cells_manifest(stream);
    game3d_world_stream_recompute(stream, -1);
}

/// @brief Advance the streamer one frame: compute the budget and reconcile resident content.
/// @details Call once per frame after updating the center; loads/unloads cells and terrain tiles to
///          track the focus point within the configured radii and budget.
void rt_game3d_world_stream_update(void *obj, double dt) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.update: invalid stream");
    if (!stream)
        return;
    dt = game3d_finite_or(dt, 0.0);
    game3d_world_stream_recompute(stream, game3d_world_stream_update_load_budget(dt));
}

/// @brief Tear down the world's runtime (despawn entities, release subsystems) and mark
///   it destroyed; idempotent. See header.
void rt_game3d_world_destroy(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked_allow_destroyed(obj, "Game3D.World3D.destroy: invalid world");
    if (!world || world->destroyed)
        return;
    game3d_world_release_runtime(world, 1);
    game3d_world_free_registries(world);
    world->destroyed = 1;
}

/// @brief True if the world has been destroyed. See header.
int8_t rt_game3d_world_is_destroyed(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked_allow_destroyed(obj, "Game3D.World3D.isDestroyed: invalid world");
    return world && world->destroyed ? 1 : 0;
}

/// @brief Get the world's rendering canvas (NULL if invalid).
void *rt_game3d_world_get_canvas(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Canvas: invalid world");
    return world ? world->canvas : NULL;
}

/// @brief Get the world's active camera (NULL if invalid).
void *rt_game3d_world_get_camera(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Camera: invalid world");
    return world ? world->camera : NULL;
}

/// @brief Get the world's scene graph (NULL if invalid).
void *rt_game3d_world_get_scene(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Scene: invalid world");
    return world ? world->scene : NULL;
}

/// @brief Get the world's physics simulation (NULL if invalid).
void *rt_game3d_world_get_physics(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Physics: invalid world");
    return world ? world->physics : NULL;
}

/// @brief Get the world's input-state object (NULL if invalid).
void *rt_game3d_world_get_input(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Input: invalid world");
    return world ? world->input : NULL;
}

/// @brief Get the world's audio subsystem (NULL if invalid).
void *rt_game3d_world_get_audio(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Audio: invalid world");
    return world ? world->audio : NULL;
}

/// @brief Get the world's effect registry (NULL if invalid).
void *rt_game3d_world_get_effects(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Effects: invalid world");
    return world ? world->effects : NULL;
}

/// @brief Get the world's owned stream controller, creating it on first access.
void *rt_game3d_world_get_stream(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_stream: invalid world");
    if (!world)
        return NULL;
    if (!world->stream) {
        rt_game3d_world_stream *stream =
            game3d_world_stream_create(world, 0, "Game3D.World3D.get_stream: allocation failed");
        if (!stream)
            return NULL;
        game3d_assign_ref(&world->stream, stream);
        game3d_release_ref((void **)&stream);
    }
    return world->stream;
}

/// @brief Get the most recent frame delta in seconds (0 if invalid).
double rt_game3d_world_get_dt(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Dt: invalid world");
    return world ? world->dt : 0.0;
}

/// @brief Get total elapsed time in seconds since the world started (0 if invalid).
double rt_game3d_world_get_elapsed(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Elapsed: invalid world");
    return world ? world->elapsed : 0.0;
}

/// @brief Get the current frame counter (0 if invalid).
int64_t rt_game3d_world_get_frame(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Frame: invalid world");
    return world ? world->frame : 0;
}

/// @brief Get how many fixed simulation steps were discarded by the spiral guard.
int64_t rt_game3d_world_get_dropped_fixed_steps(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_DroppedFixedSteps: invalid world");
    return world ? world->dropped_fixed_steps : 0;
}

/// @brief Count spawned Entity3D objects currently owned by the world.
int64_t rt_game3d_world_get_entity_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_entityCount: invalid world");
    return world ? world->entity_count : 0;
}

/// @brief Count physics bodies currently registered through spawned entities.
int64_t rt_game3d_world_get_body_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_bodyCount: invalid world");
    return (world && world->physics) ? rt_world3d_body_count(world->physics) : 0;
}

/// @brief Count main 3D draw submissions queued by the latest ended frame.
int64_t rt_game3d_world_get_draw_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_drawCount: invalid world");
    return (world && world->canvas) ? rt_canvas3d_get_draw_count(world->canvas) : 0;
}

/// @brief Count drawable scene nodes submitted by the latest scene draw.
int64_t rt_game3d_world_get_visible_node_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_visibleNodeCount: invalid world");
    return (world && world->scene) ? rt_scene3d_get_visible_node_count(world->scene) : 0;
}

/// @brief Count draw submissions skipped by latest visibility culling.
int64_t rt_game3d_world_get_occluded_draw_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_occludedDrawCount: invalid world");
    return (world && world->canvas) ? rt_canvas3d_get_occluded_draw_count(world->canvas) : 0;
}

/// @brief Count bytes resident in the world-owned stream controller, if any.
int64_t rt_game3d_world_get_stream_resident_bytes(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_streamResidentBytes: invalid world");
    return (world && world->stream) ? rt_game3d_world_stream_get_resident_bytes(world->stream) : 0;
}

/// @brief Get the configured worker count for internal deterministic jobs.
int64_t rt_game3d_world_get_worker_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_workerCount: invalid world");
    return world ? world->worker_count : 1;
}

/// @brief True when internal jobs are allowed to use worker threads.
int8_t rt_game3d_world_get_jobs_enabled(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_jobsEnabled: invalid world");
    return world ? world->jobs_enabled : 0;
}

/// @brief Set the internal worker count; values <= 1 keep jobs disabled.
void rt_game3d_world_set_worker_count(void *obj, int64_t worker_count) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.setWorkerCount: invalid world");
    if (!world)
        return;
    world->worker_count = game3d_clamp_worker_count(worker_count);
    world->jobs_enabled = world->worker_count > 1 ? 1 : 0;
    if (world->worker_count <= 1) {
        game3d_world_release_job_pool(world);
    } else if (world->job_pool && rt_threadpool_get_size(world->job_pool) != world->worker_count) {
        game3d_world_release_job_pool(world);
    }
}

/// @brief True when floating-origin rebasing is enabled.
int8_t rt_game3d_world_get_floating_origin(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_floatingOrigin: invalid world");
    return world ? world->floating_origin : 0;
}

/// @brief Enable or disable camera-relative floating-origin rebasing.
void rt_game3d_world_set_floating_origin(void *obj, int8_t enabled) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.set_floatingOrigin: invalid world");
    if (world)
        world->floating_origin = enabled ? 1 : 0;
}

/// @brief Get the accumulated world-origin offset as a Vec3.
void *rt_game3d_world_get_world_origin(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_worldOrigin: invalid world");
    if (!world)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(world->world_origin[0], world->world_origin[1], world->world_origin[2]);
}

/// @brief Set the camera-distance threshold that triggers a floating-origin rebase.
void rt_game3d_world_set_origin_rebase_threshold(void *obj, double meters) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.setOriginRebaseThreshold: invalid world");
    if (world)
        world->origin_rebase_threshold = game3d_rebase_threshold_or_default(meters);
}

/// @brief Manually apply one floating-origin rebase boundary between frames.
void rt_game3d_world_rebase_origin(void *obj, double dx, double dy, double dz) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.rebaseOrigin: invalid world");
    rt_canvas3d *canvas = world ? rt_canvas3d_checked_or_stack(world->canvas) : NULL;
    if (canvas && canvas->in_frame) {
        rt_trap("Game3D.World3D.rebaseOrigin: must be called between frames");
        return;
    }
    double delta[3] = {dx, dy, dz};
    game3d_world_apply_origin_rebase(world, delta);
}

/// @brief Spawn an entity (and its child tree) into the world; returns the entity. See header.
void *rt_game3d_world_spawn(void *world_obj, void *entity_obj) {
    rt_game3d_world *world = game3d_world_checked(world_obj, "Game3D.World3D.spawn: invalid world");
    rt_game3d_entity *entity =
        game3d_entity_checked(entity_obj, "Game3D.World3D.spawn: entity must be Entity3D");
    int64_t next_id;
    if (!world || !entity)
        return entity_obj;
    next_id = world->next_entity_id;
    if (game3d_world_spawn_entity_tree(world, entity, 1, &next_id))
        world->next_entity_id = next_id;
    return entity_obj;
}

/// @brief Despawn an entity (and its child tree); traps if it is not spawned in this
///   world. See header.
void rt_game3d_world_despawn(void *world_obj, void *entity_obj) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.World3D.despawn: invalid world");
    rt_game3d_entity *entity =
        game3d_entity_checked(entity_obj, "Game3D.World3D.despawn: entity must be Entity3D");
    if (!world || !entity)
        return;
    if (!entity->spawned || entity->world != world) {
        rt_trap("Game3D.World3D.despawn: entity is not spawned in this world");
        return;
    }
    game3d_world_despawn_entity_tree(world, entity, 1);
}

/// @brief Find a scene node by name (NULL if absent). See header.
void *rt_game3d_world_find_node(void *obj, rt_string name) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.findNode: invalid world");
    return world && world->scene ? rt_scene3d_find(world->scene, name) : NULL;
}

/// @brief Find a spawned entity by name using the world's name index; NULL if absent.
void *rt_game3d_world_find_entity(void *obj, rt_string name) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.findEntity: invalid world");
    const char *needle = name ? rt_string_cstr(name) : "";
    if (!world || !needle)
        return NULL;
    if (!world->name_index_valid)
        game3d_world_rebuild_name_index(world);
    rt_game3d_entity *indexed = game3d_world_name_index_find(world, needle);
    if (indexed && indexed->spawned && indexed->world == world)
        return indexed;
    if (indexed)
        world->name_index_valid = 0;
    if (world->name_index_valid)
        return NULL;
    for (int32_t i = 0; i < world->entity_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        const char *entity_name = entity && entity->name ? rt_string_cstr(entity->name) : "";
        if (entity_name && strcmp(entity_name, needle) == 0)
            return entity;
    }
    return NULL;
}

/// @brief Install a camera controller (or NULL to clear); traps on a non-Game3D
///   controller. See header.
void rt_game3d_world_set_camera_controller(void *obj, void *controller) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.setCameraController: invalid world");
    if (!game3d_camera_controller_is_valid(controller)) {
        rt_trap("Game3D.World3D.setCameraController: controller must be a built-in Game3D camera "
                "controller");
        return;
    }
    if (world) {
        void *controller_guard = NULL;
        rt_game3d_world *previous_world =
            (rt_game3d_world *)game3d_camera_controller_get_world_ref(controller);
        if (controller) {
            controller_guard = controller;
            rt_obj_retain_maybe(controller_guard);
        }
        if (previous_world && previous_world != world &&
            previous_world->camera_controller == controller)
            game3d_release_ref(&previous_world->camera_controller);
        if (world->camera_controller && world->camera_controller != controller)
            game3d_camera_controller_clear_world_ref_if(world->camera_controller, world);
        if (controller)
            game3d_camera_controller_bind_world_ref(controller, world);
        game3d_assign_ref(&world->camera_controller, controller);
        game3d_release_ref(&controller_guard);
    }
}

/// @brief Aim the camera at a Vec3 target from its current position; traps on a non-Vec3
///   target. See header.
void rt_game3d_world_look_at(void *obj, void *target) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.lookAt: invalid world");
    if (!rt_g3d_is_vec3(target)) {
        rt_trap("Game3D.World3D.lookAt: target must be Vec3");
        return;
    }
    if (world && world->camera) {
        double eye[3];
        if (rt_camera3d_get_position_components(world->camera, &eye[0], &eye[1], &eye[2])) {
            rt_camera3d_look_at_components(world->camera,
                                           eye[0],
                                           eye[1],
                                           eye[2],
                                           rt_vec3_x(target),
                                           rt_vec3_y(target),
                                           rt_vec3_z(target),
                                           0.0,
                                           1.0,
                                           0.0);
        }
    }
}

/// @brief Record a window resize and update the camera's render aspect ratio; traps on
///   non-positive dimensions. See header.
void rt_game3d_world_on_resize(void *obj, int64_t width, int64_t height) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.onResize: invalid world");
    if (width <= 0 || height <= 0) {
        rt_trap("Game3D.World3D.onResize: dimensions must be positive");
        return;
    }
    if (!world)
        return;
    world->width = width;
    world->height = height;
    if (world->canvas)
        rt_canvas3d_resize(world->canvas, width, height);
    if (world->camera)
        rt_camera3d_sync_render_aspect(world->camera, (double)width / (double)height);
}

/// @brief Set the global ambient light color. See header.
void rt_game3d_world_set_ambient(void *obj, double r, double g, double b) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setAmbient: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_ambient(world->canvas, r, g, b);
}

/// @brief Bind a light into the given canvas light slot. See header.
void rt_game3d_world_add_light(void *obj, int64_t slot, void *light) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.addLight: invalid world");
    if (light && !rt_g3d_has_class(light, RT_G3D_LIGHT3D_CLASS_ID)) {
        rt_trap("Game3D.World3D.addLight: light must be Light3D or null");
        return;
    }
    if (world && world->canvas)
        rt_canvas3d_set_light(world->canvas, slot, light);
}

/// @brief Clear all bound light slots. See header.
void rt_game3d_world_clear_lights(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.clearLights: invalid world");
    if (world && world->canvas)
        rt_canvas3d_clear_lights(world->canvas);
}

/// @brief Set the skybox from a cubemap, or clear it when passed NULL. See header.
void rt_game3d_world_set_skybox(void *obj, void *cubemap) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setSkybox: invalid world");
    if (cubemap && !rt_g3d_has_class(cubemap, RT_G3D_CUBEMAP3D_CLASS_ID)) {
        rt_trap("Game3D.World3D.setSkybox: cubemap must be CubeMap3D or null");
        return;
    }
    if (world && world->canvas) {
        if (cubemap)
            rt_canvas3d_set_skybox(world->canvas, cubemap);
        else
            rt_canvas3d_clear_skybox(world->canvas);
    }
}

/// @brief Configure distance fog (RGB color + near/far planes). See header.
void rt_game3d_world_set_fog(
    void *obj, double r, double g, double b, double near_plane, double far_plane) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setFog: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_fog(world->canvas, near_plane, far_plane, r, g, b);
}

/// @brief Apply a render quality preset to the canvas and rebuild the quality-scaled
///   post-FX stack. See header.
void rt_game3d_world_set_quality(void *obj, int64_t quality) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setQuality: invalid world");
    void *new_postfx;
    if (!world || !world->canvas)
        return;
    rt_canvas3d_set_quality(world->canvas, quality);
    rt_game3d_effects *effects = (rt_game3d_effects *)world->effects;
    if (effects) {
        new_postfx = rt_postfx3d_new_quality(world->canvas, quality);
        if (new_postfx) {
            game3d_assign_ref(&effects->postfx, new_postfx);
            rt_canvas3d_set_post_fx(world->canvas, effects->postfx);
            game3d_release_ref(&new_postfx);
        } else if (effects->postfx) {
            rt_canvas3d_set_post_fx(world->canvas, effects->postfx);
        }
    }
}

/// @brief Bake a NavMesh3D from the world's current scene.
void *rt_game3d_world_bake_nav_mesh(
    void *obj, double agent_radius, double agent_height, double max_slope, double cell_size) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.bakeNavMesh: invalid world");
    if (!world || !world->scene)
        return NULL;
    return rt_navmesh3d_bake(world->scene, agent_radius, agent_height, max_slope, cell_size);
}

/// @brief Bake a tiled NavMesh3D from the world's current scene.
void *rt_game3d_world_bake_tiled_nav_mesh(void *obj,
                                          double tile_size,
                                          double agent_radius,
                                          double agent_height,
                                          double max_slope,
                                          double cell_size) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.bakeTiledNavMesh: invalid world");
    if (!world || !world->scene)
        return NULL;
    return rt_navmesh3d_bake_tiled(
        world->scene, tile_size, agent_radius, agent_height, max_slope, cell_size);
}

/// @brief Count collision events for the given phase (ANY sums enter+stay+exit). See header.
int64_t rt_game3d_world_collision_event_count(void *obj, int64_t phase) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.collisionEventCount: invalid world");
    if (!world || !world->physics)
        return 0;
    switch (phase) {
        case RT_GAME3D_COLLISION_ENTER:
            return rt_world3d_get_enter_event_count(world->physics);
        case RT_GAME3D_COLLISION_STAY:
            return rt_world3d_get_stay_event_count(world->physics);
        case RT_GAME3D_COLLISION_EXIT:
            return rt_world3d_get_exit_event_count(world->physics);
        case RT_GAME3D_COLLISION_ANY:
        default:
            return rt_world3d_get_enter_event_count(world->physics) +
                   rt_world3d_get_stay_event_count(world->physics) +
                   rt_world3d_get_exit_event_count(world->physics);
    }
}

/// @brief GC finalizer for a collision event: release the raw event and both entities.
static void game3d_collision_event_finalize(void *obj) {
    rt_game3d_collision_event *event = (rt_game3d_collision_event *)obj;
    if (!event)
        return;
    game3d_release_ref(&event->raw);
    game3d_release_ref(&event->b);
    game3d_release_ref(&event->a);
}

/// @brief Wrap a raw physics collision event into a Game3D Collision3DEvent, resolving
///   each body back to its owning entity; consumes `raw_event` and traps on OOM.
static void *game3d_collision_event_wrap(rt_game3d_world *world, int64_t phase, void *raw_event) {
    rt_game3d_collision_event *event;
    if (!raw_event)
        return NULL;
    event = (rt_game3d_collision_event *)rt_obj_new_i64(RT_G3D_GAME3D_COLLISION_EVENT_CLASS_ID,
                                                        (int64_t)sizeof(*event));
    if (!event) {
        game3d_release_ref(&raw_event);
        rt_trap("Game3D.Collision3DEvent: allocation failed");
        return NULL;
    }
    memset(event, 0, sizeof(*event));
    rt_obj_set_finalizer(event, game3d_collision_event_finalize);
    event->phase = phase;
    event->raw = raw_event;
    void *body_a = rt_collision_event3d_get_body_a(raw_event);
    void *body_b = rt_collision_event3d_get_body_b(raw_event);
    game3d_assign_ref(&event->a, game3d_world_find_entity_by_body(world, body_a));
    game3d_assign_ref(&event->b, game3d_world_find_entity_by_body(world, body_b));
    return event;
}

/// @brief Fetch the raw physics collision event for (phase, index); for the ANY phase it
///   walks enter→stay→exit ranges and reports the resolved phase via `actual_phase`.
static void *game3d_world_raw_collision_event(rt_game3d_world *world,
                                              int64_t phase,
                                              int64_t index,
                                              int64_t *actual_phase) {
    if (!world || !world->physics || index < 0)
        return NULL;
    if (actual_phase)
        *actual_phase = phase;
    switch (phase) {
        case RT_GAME3D_COLLISION_ENTER:
            return rt_world3d_get_enter_event(world->physics, index);
        case RT_GAME3D_COLLISION_STAY:
            return rt_world3d_get_stay_event(world->physics, index);
        case RT_GAME3D_COLLISION_EXIT:
            return rt_world3d_get_exit_event(world->physics, index);
        case RT_GAME3D_COLLISION_ANY:
        default: {
            int64_t enter_count = rt_world3d_get_enter_event_count(world->physics);
            if (index < enter_count) {
                if (actual_phase)
                    *actual_phase = RT_GAME3D_COLLISION_ENTER;
                return rt_world3d_get_enter_event(world->physics, index);
            }
            index -= enter_count;
            int64_t stay_count = rt_world3d_get_stay_event_count(world->physics);
            if (index < stay_count) {
                if (actual_phase)
                    *actual_phase = RT_GAME3D_COLLISION_STAY;
                return rt_world3d_get_stay_event(world->physics, index);
            }
            index -= stay_count;
            if (actual_phase)
                *actual_phase = RT_GAME3D_COLLISION_EXIT;
            return rt_world3d_get_exit_event(world->physics, index);
        }
    }
}

/// @brief Fetch and wrap the i-th collision event for the phase (NULL if out of range). See header.
void *rt_game3d_world_collision_event(void *obj, int64_t phase, int64_t index) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.collisionEvent: invalid world");
    if (!world || !world->physics)
        return NULL;
    int64_t actual_phase = phase;
    void *raw_event = game3d_world_raw_collision_event(world, phase, index, &actual_phase);
    return game3d_collision_event_wrap(world, actual_phase, raw_event);
}

/// @brief Clear the physics world's recorded collision-event buffers. See header.
void rt_game3d_world_clear_collision_events(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.clearCollisionEvents: invalid world");
    if (world && world->physics)
        rt_world3d_clear_collision_events(world->physics);
}

/// @brief Get the event phase (defaults to ANY if invalid). See header.
int64_t rt_game3d_collision_event_get_phase(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_phase: invalid event");
    return event ? event->phase : RT_GAME3D_COLLISION_ANY;
}

/// @brief Get the first participating entity (NULL if unresolved/invalid). See header.
void *rt_game3d_collision_event_get_a(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_a: invalid event");
    return event ? event->a : NULL;
}

/// @brief Get the second participating entity (NULL if unresolved/invalid). See header.
void *rt_game3d_collision_event_get_b(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_b: invalid event");
    return event ? event->b : NULL;
}

/// @brief Get the underlying low-level physics collision event (NULL if invalid). See header.
void *rt_game3d_collision_event_get_raw(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_raw: invalid event");
    return event ? event->raw : NULL;
}

/// @brief True if either body in the contact is a trigger. See header.
int8_t rt_game3d_collision_event_get_is_trigger(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_isTrigger: invalid event");
    return event && event->raw ? rt_collision_event3d_get_is_trigger(event->raw) : 0;
}

/// @brief Get the relative approach speed at contact (0 if invalid). See header.
double rt_game3d_collision_event_get_relative_speed(void *obj) {
    rt_game3d_collision_event *event = game3d_collision_event_checked(
        obj, "Game3D.Collision3DEvent.get_relativeSpeed: invalid event");
    return event && event->raw ? rt_collision_event3d_get_relative_speed(event->raw) : 0.0;
}

/// @brief Get the resolution normal impulse magnitude (0 if invalid). See header.
double rt_game3d_collision_event_get_normal_impulse(void *obj) {
    rt_game3d_collision_event *event = game3d_collision_event_checked(
        obj, "Game3D.Collision3DEvent.get_normalImpulse: invalid event");
    return event && event->raw ? rt_collision_event3d_get_normal_impulse(event->raw) : 0.0;
}

/// @brief Get the number of contacts on the wrapped raw event. See header.
int64_t rt_game3d_collision_event_get_contact_count(void *obj) {
    rt_game3d_collision_event *event = game3d_collision_event_checked(
        obj, "Game3D.Collision3DEvent.get_contactCount: invalid event");
    return event && event->raw ? rt_collision_event3d_get_contact_count(event->raw) : 0;
}

/// @brief Get the first contact point as a Vec3 (origin fallback). See header.
void *rt_game3d_collision_event_point(void *obj) {
    return rt_game3d_collision_event_contact_point(obj, 0);
}

/// @brief Get the first contact normal as a Vec3 (+Y fallback). See header.
void *rt_game3d_collision_event_normal(void *obj) {
    return rt_game3d_collision_event_contact_normal(obj, 0);
}

/// @brief Get indexed contact point as a Vec3 (origin fallback). See header.
void *rt_game3d_collision_event_contact_point(void *obj, int64_t index) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.contactPoint: invalid event");
    return event && event->raw ? rt_collision_event3d_get_contact_point(event->raw, index)
                               : rt_vec3_new(0.0, 0.0, 0.0);
}

/// @brief Get indexed contact normal as a Vec3 (+Y fallback). See header.
void *rt_game3d_collision_event_contact_normal(void *obj, int64_t index) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.contactNormal: invalid event");
    return event && event->raw ? rt_collision_event3d_get_contact_normal(event->raw, index)
                               : rt_vec3_new(0.0, 1.0, 0.0);
}

/// @brief Get indexed contact separation (0 fallback). See header.
double rt_game3d_collision_event_contact_separation(void *obj, int64_t index) {
    rt_game3d_collision_event *event = game3d_collision_event_checked(
        obj, "Game3D.Collision3DEvent.contactSeparation: invalid event");
    return event && event->raw ? rt_collision_event3d_get_contact_separation(event->raw, index)
                               : 0.0;
}

/// @brief Given one participant, return the other entity in the contact (NULL if `entity`
///   is not a participant). See header.
void *rt_game3d_collision_event_other(void *obj, void *entity_obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.other: invalid event");
    rt_game3d_entity *entity =
        game3d_entity_checked(entity_obj, "Game3D.Collision3DEvent.other: entity must be Entity3D");
    if (!event || !entity)
        return NULL;
    if (event->a == entity)
        return event->b;
    if (event->b == entity)
        return event->a;
    return NULL;
}

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
    rt_game3d_input_update(world->input);
    world->dt = game3d_clamp_dt(rt_canvas3d_get_delta_time_sec(world->canvas));
    world->elapsed += world->dt;
    world->frame += 1;
    if (world->camera && world->width > 0 && world->height > 0)
        rt_camera3d_sync_render_aspect(world->camera, (double)world->width / (double)world->height);
    return rt_canvas3d_should_close(world->canvas) ? 0 : 1;
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
    for (int32_t i = 0; i < world->entity_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        if (!entity || entity->destroyed || !entity->body)
            continue;
        void *collider = rt_body3d_get_collider(entity->body);
        if (!collider)
            continue;
        double mn_raw[3] = {0.0, 0.0, 0.0};
        double mx_raw[3] = {0.0, 0.0, 0.0};
        double position[3] = {0.0, 0.0, 0.0};
        double rotation[4] = {0.0, 0.0, 0.0, 1.0};
        double scale[3] = {1.0, 1.0, 1.0};
        rt_body3d_get_pose_raw(entity->body, position, rotation, scale);
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

/// @brief Render the 2D debug HUD when enabled: backend/FPS, quality (with fallback),
///   node/cull/body counts, and an optional camera-info block.
static void game3d_world_draw_debug_overlay(rt_game3d_world *world) {
    if (!world || !world->canvas || !world->debug_overlay_enabled)
        return;
    rt_canvas3d_begin_overlay(world->canvas);
    rt_canvas3d_draw_rect2d(world->canvas, 8, 8, 250, 106, 0x111820);
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
    if (world->debug_camera_enabled && world->camera) {
        void *pos = rt_camera3d_get_position(world->camera);
        game3d_world_debug_textf(world,
                                 14,
                                 70,
                                 0xCDEECC,
                                 "camera %.2f %.2f %.2f",
                                 pos ? rt_vec3_x(pos) : 0.0,
                                 pos ? rt_vec3_y(pos) : 0.0,
                                 pos ? rt_vec3_z(pos) : 0.0);
        game3d_release_ref(&pos);
    }
    if (world->debug_caps_enabled) {
        int64_t caps = rt_canvas3d_get_backend_capabilities(world->canvas);
        game3d_world_debug_textf(world, 14, 84, 0xFFE5AA, "caps 0x%llx", (long long)caps);
    }
    if (world->debug_physics_enabled)
        game3d_world_debug_text(world, 14, 98, "physics wire enabled", 0xFFE5AA);
    rt_canvas3d_end_overlay(world->canvas);
}

/// @brief Clear the back buffer to the world's clear color and open the 3D pass with the
///   active camera. See header.
void rt_game3d_world_begin_frame(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.beginFrame: invalid world");
    if (!world || !world->canvas || !world->camera)
        return;
    rt_canvas3d_clear(world->canvas, world->clear_r, world->clear_g, world->clear_b);
    rt_canvas3d_set_camera_relative_upload(world->canvas, world->floating_origin);
    rt_canvas3d_begin(world->canvas, world->camera);
}

/// @brief Draw the scene graph through the canvas with the active camera. See header.
static void game3d_world_draw_stream_terrain(rt_game3d_world *world) {
    rt_game3d_world_stream *stream;
    if (!world || !world->canvas || !world->stream)
        return;
    stream = (rt_game3d_world_stream *)rt_g3d_checked_or_null(
        world->stream, RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID);
    if (!stream || !stream->terrain_manifest_loaded)
        return;
    for (int32_t i = 0; i < stream->terrain_tile_count; ++i) {
        rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
        if (!tile->resident || !tile->terrain)
            continue;
        double sx = game3d_stream_terrain_axis_scale(tile->scale[0]);
        double sz = game3d_stream_terrain_axis_scale(tile->scale[2]);
        double tx = tile->center[0] - ((double)(tile->width - 1) * sx) * 0.5;
        double ty = tile->center[1];
        double tz = tile->center[2] - ((double)(tile->depth - 1) * sz) * 0.5;
        rt_canvas3d_draw_terrain_at(world->canvas, tile->terrain, tx, ty, tz);
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
    rt_game3d_world_begin_frame(world);
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
    if (canvas_state_saved) {
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) != 0) {
            const char *msg = rt_trap_get_error();
            snprintf(trap_message,
                     sizeof(trap_message),
                     "%s",
                     (msg && msg[0]) ? msg
                                     : "Game3D.World3D.runFrames: frame callback trapped");
            trapped = 1;
        }
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
            game3d_world_render_once(world, NULL);
        }
    }
    if (canvas_state_saved) {
        rt_trap_clear_recovery();
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
