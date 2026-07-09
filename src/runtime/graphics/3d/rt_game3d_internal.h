//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_internal.h
// Purpose: Private shared surface of the Viper.Game3D layer — tuning constants,
//   the effect-item discriminator, reused Camera3D/Scene/Decal/Particles entry
//   points, and every internal handle-payload struct. Included by rt_game3d.c
//   and its split sibling translation units (rt_game3d_*.c) so they share one
//   definition of the private types without exposing them publicly.
// Key invariants:
//   - Definitions only (no out-of-line code); safe to include from any Game3D TU.
//   - Struct layouts are private ABI — never referenced outside the Game3D TUs.
// Ownership/Lifetime:
//   - Pure declarations; owns no state.
// Links: rt_game3d.h (public API + RT_GAME3D_* constants), rt_gltf.h, rt_input.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rt_game3d.h"
#include "rt_game3d_diagnostics.h"
#include "rt_gltf.h"
#include "rt_graphics3d_ids.h"
#include "rt_input.h"
#include "rt_trap.h"

// Default tuning constants applied when callers omit a value or pass a
// non-finite one; chosen for a 60 Hz first-person feel with safe clip planes.
#define RT_GAME3D_DEFAULT_FOV_DEG 60.0    ///< Default vertical camera FOV (degrees).
#define RT_GAME3D_DEFAULT_NEAR 0.1        ///< Default near clip plane (world units).
#define RT_GAME3D_DEFAULT_FAR 1000.0      ///< Default far clip plane (world units).
#define RT_GAME3D_DEFAULT_DT (1.0 / 60.0) ///< Fallback frame delta when timing is invalid.
#define RT_GAME3D_MAX_DT 0.25             ///< Per-frame delta cap (smooths post-stall spikes).
#define RT_GAME3D_MAX_WORKERS 64          ///< Public WorkerCount clamp for internal jobs.
#define RT_GAME3D_DEFAULT_REBASE_THRESHOLD 10000.0 ///< Floating-origin threshold.
#define RT_GAME3D_MIN_REBASE_THRESHOLD 1.0         ///< Avoid constant tiny rebases.
#define RT_GAME3D_DEFAULT_MOVE_SPEED 6.0           ///< Default controller move speed (units/sec).
#define RT_GAME3D_DEFAULT_LOOK_SENSITIVITY 0.01    ///< Default mouse-look degrees per pixel.
#define RT_GAME3D_DEFAULT_JUMP_SPEED 5.5           ///< Default jump launch speed (units/sec).
#define RT_GAME3D_DEFAULT_GRAVITY 20.0            ///< Default downward character gravity magnitude.
#define RT_GAME3D_DEFAULT_FOLLOW_DAMPING 12.0     ///< Default follow-camera smoothing factor.
#define RT_GAME3D_DEFAULT_AUDIO_REF_DISTANCE 1.0  ///< Default audio full-volume radius.
#define RT_GAME3D_DEFAULT_AUDIO_MAX_DISTANCE 50.0 ///< Default audio silence radius.
#define RT_GAME3D_AUDIO_DISTANCE_MAX 1000000000.0 ///< Max finite audio attenuation radius.
#define RT_GAME3D_DEFAULT_AUDIO_VOLUME 100        ///< Default master audio volume (0–100).
#define RT_GAME3D_PI 3.14159265358979323846       ///< Pi (avoids relying on non-portable M_PI).
#define RT_GAME3D_ANIM_EVENT_MAX 64               ///< Max animation events buffered per update.
#define RT_GAME3D_MAX_FIXED_STEPS_PER_FRAME 8     ///< Fixed-loop spiral-of-death guard.
#define RT_GAME3D_COORD_ABS_MAX 1000000000000.0   ///< Max finite world coordinate accepted.
#define RT_GAME3D_SCALE_ABS_MAX 1000000.0         ///< Max absolute node/body scale.
#define RT_GAME3D_ANGLE_DEG_ABS_MAX 1000000.0     ///< Max finite Euler/orbit angle in degrees.
#define RT_GAME3D_CONTROLLER_SPEED_MAX 1000000.0  ///< Max controller speed/jump velocity.
#define RT_GAME3D_LOOK_SENSITIVITY_MAX 1000.0     ///< Max mouse-look sensitivity.
#define RT_GAME3D_DAMPING_MAX 1000.0              ///< Max camera damping factor.
#define RT_GAME3D_ANIM_BLEND_TIME_MAX 1000000.0   ///< Max animation transition duration.
#define RT_GAME3D_ANIM_STEP_MAX 1.0               ///< Max single Game3D animator update step.
#define RT_GAME3D_ANIM_SPEED_ABS_MAX 1000000.0    ///< Max animation playback speed multiplier.
#define RT_GAME3D_EFFECT_STEP_MAX 10.0            ///< Max single EffectRegistry3D update step.
#define RT_GAME3D_EFFECT_LIFETIME_MAX 86400.0     ///< Max effect auto-expire lifetime.
#ifndef RT_GAME3D_MODEL_CACHE_KEY_MAX
#define RT_GAME3D_MODEL_CACHE_KEY_MAX 4096 ///< Max bytes snapshotted for model cache/load paths.
#endif

/// @brief Internal effect-item discriminator stored in rt_game3d_effect_item.type.
enum {
    RT_GAME3D_EFFECT_PARTICLES = 1, ///< Item wraps a particle system.
    RT_GAME3D_EFFECT_DECAL = 2,     ///< Item wraps a decal.
};

// Camera3D entry points reused by the Game3D camera controllers; defined in
// render/rt_camera3d.c and forward-declared here to avoid a header dependency.
void rt_camera3d_look_at(void *obj, void *eye_v, void *target_v, void *up_v);
void rt_camera3d_fps_init(void *obj);
void rt_camera3d_fps_update(void *obj,
                            double yaw_delta,
                            double pitch_delta,
                            double move_fwd,
                            double move_right,
                            double move_up,
                            double speed,
                            double dt);
void rt_camera3d_orbit(void *obj, void *target_v, double distance, double yaw, double pitch);
void *rt_camera3d_get_position(void *obj);
int8_t rt_camera3d_get_position_components(void *obj, double *x, double *y, double *z);
void *rt_camera3d_get_forward(void *obj);
void *rt_camera3d_get_right(void *obj);
void rt_camera3d_look_at_components(void *obj,
                                    double eye_x,
                                    double eye_y,
                                    double eye_z,
                                    double target_x,
                                    double target_y,
                                    double target_z,
                                    double up_x,
                                    double up_y,
                                    double up_z);
void rt_camera3d_orbit_components(void *obj,
                                  double target_x,
                                  double target_y,
                                  double target_z,
                                  double distance,
                                  double yaw,
                                  double pitch);
void rt_camera3d_set_position(void *obj, void *pos);
int8_t rt_scene_node3d_get_world_position_components(void *node, double *x, double *y, double *z);
void rt_decal3d_rebase_origin(void *decal, double dx, double dy, double dz);
void rt_particles3d_rebase_origin(void *particles, double dx, double dy, double dz);

/// @brief LayerMask payload: a single bitfield of RT_GAME3D_LAYER_* bits.
typedef struct rt_game3d_layermask {
    int64_t bits;
} rt_game3d_layermask;

/// @brief Input3D payload: per-object look sensitivity plus an optional frame snapshot.
typedef struct rt_game3d_input {
    double look_sensitivity;
    int8_t has_snapshot;
    uint8_t key_down[VIPER_KEY_MAX];
    uint8_t key_pressed[VIPER_KEY_MAX];
    uint8_t key_released[VIPER_KEY_MAX];
    uint8_t mouse_down[VIPER_MOUSE_BUTTON_MAX];
    uint8_t mouse_pressed[VIPER_MOUSE_BUTTON_MAX];
    uint8_t mouse_released[VIPER_MOUSE_BUTTON_MAX];
    int64_t mouse_dx;
    int64_t mouse_dy;
    /* Sub-pixel mouse deltas (relative mouse mode); mirror mouse_dx/dy. */
    double mouse_fdx;
    double mouse_fdy;
    double wheel_y;
    /* Gamepad merge: index bound via Input3D.BindPad (-1 = none). Stick axes
     * are snapshotted per update so Move/LookAxis observe a coherent frame. */
    int64_t bound_pad;
    double pad_look_sensitivity;
    double pad_lx;
    double pad_ly;
    double pad_rx;
    double pad_ry;
    int8_t pad_connected;
} rt_game3d_input;

/// @brief Entity3D payload: scene node plus optional mesh/material/body/animator,
///   collision layer/mask, name, owning world, and a dynamic child array.
typedef struct rt_game3d_entity {
    int64_t id;
    void *node;
    void *mesh;
    void *material;
    void *body;
    void *anim;
    void *behavior; /* retained Behavior3D ticked each simulation step, or NULL */
    int64_t layer;
    int64_t collision_mask_bits;
    rt_string name;
    void *world;
    struct rt_game3d_entity *parent;
    struct rt_game3d_entity **children;
    int32_t child_count;
    int32_t child_capacity;
    int32_t registry_index; /* slot in owning world's dense entity array, -1 when unspawned */
    int8_t group;
    int8_t alive;
    int8_t spawned;
    int8_t destroyed;
} rt_game3d_entity;

/// @brief Return the entity's SceneNode3D slot only when it still has the expected class.
static inline void *game3d_entity_node_ref(const rt_game3d_entity *entity) {
    return entity ? rt_g3d_checked_or_null(entity->node, RT_G3D_SCENENODE3D_CLASS_ID) : NULL;
}

/// @brief Return the entity's Mesh3D slot only when it still has the expected class.
static inline void *game3d_entity_mesh_ref(const rt_game3d_entity *entity) {
    return entity ? rt_g3d_checked_or_null(entity->mesh, RT_G3D_MESH3D_CLASS_ID) : NULL;
}

/// @brief Return the entity's Material3D slot only when it still has the expected class.
static inline void *game3d_entity_material_ref(const rt_game3d_entity *entity) {
    return entity ? rt_g3d_checked_or_null(entity->material, RT_G3D_MATERIAL3D_CLASS_ID) : NULL;
}

/// @brief Return the entity's Physics3DBody slot only when it still has the expected class.
static inline void *game3d_entity_body_ref(const rt_game3d_entity *entity) {
    return entity ? rt_g3d_checked_or_null(entity->body, RT_G3D_BODY3D_CLASS_ID) : NULL;
}

/// @brief Return the entity's Animator3D slot only when it still has the expected class.
static inline void *game3d_entity_anim_ref(const rt_game3d_entity *entity) {
    return entity ? rt_g3d_checked_or_null(entity->anim, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID) : NULL;
}

/// @brief Safe number of child entity slots that may be read directly.
static inline int32_t game3d_entity_child_count(const rt_game3d_entity *entity) {
    if (!entity || !entity->children || entity->child_count <= 0 || entity->child_capacity <= 0)
        return 0;
    return entity->child_count < entity->child_capacity ? entity->child_count
                                                        : entity->child_capacity;
}

/// @brief Sound3D payload: listener, optional followed camera, a dynamic source
///   list, distance-attenuation radii, master volume, and follow-camera flag.
typedef struct rt_game3d_audio {
    void *listener;
    void *camera;
    void **sources;
    int32_t source_count;
    int32_t source_capacity;
    double ref_distance;
    double max_distance;
    int64_t volume;
    int8_t listener_follow_camera;
} rt_game3d_audio;

/// @brief One registered effect: its kind (RT_GAME3D_EFFECT_*), the wrapped
///   object, an auto-expire lifetime, and the accumulated age in seconds.
typedef struct rt_game3d_effect_item {
    int64_t type;
    void *object;
    double lifetime;
    double age;
} rt_game3d_effect_item;

/// @brief EffectRegistry3D payload: the world's post-FX stack plus a dynamic
///   array of live effect items advanced and retired each frame.
typedef struct rt_game3d_effects {
    void *postfx;
    rt_game3d_effect_item *items;
    int32_t count;
    int32_t capacity;
} rt_game3d_effects;

/// @brief EnvHandle payload: the target world plus the optional terrain and
///   water entities a fluent environment builder has attached.
typedef struct rt_game3d_env_handle {
    void *world;
    void *terrain_entity;
    void *water_entity;
    double terrain_size;
    int8_t has_terrain_size;
} rt_game3d_env_handle;

/// @brief BodyDef payload: shape kind and dimensions, mass/material properties,
///   collision layer/mask, sync mode, and dynamic/kinematic/trigger/CCD flags.
typedef struct rt_game3d_body_def {
    int64_t shape;
    double half_extents[3];
    double radius;
    double height;
    double mass;
    double friction;
    double restitution;
    int64_t layer;
    int64_t mask_bits;
    int64_t sync_mode;
    int8_t has_layer;
    int8_t has_mask;
    int8_t is_static;
    int8_t is_kinematic;
    int8_t is_trigger;
    int8_t use_ccd;
} rt_game3d_body_def;

/// @brief Collision3DEvent payload: the phase, the two participating entities,
///   and the underlying low-level physics collision event.
typedef struct rt_game3d_collision_event {
    int64_t phase;
    void *a;
    void *b;
    void *raw;
} rt_game3d_collision_event;

/// @brief Animator3D payload: optional skeletal controller, optional node animator,
///   plus names of skeletal events fired during the most recent update.
typedef struct rt_game3d_animator {
    void *controller;
    void *node_animator;
    rt_string events[RT_GAME3D_ANIM_EVENT_MAX];
    int32_t event_count;
} rt_game3d_animator;

/// @brief ModelTemplate payload: the source path, whether it is a packed asset,
///   and the loaded model that fresh instances are cloned from.
typedef struct rt_game3d_model_template {
    rt_string path;
    int8_t asset_path;
    void *model;
} rt_game3d_model_template;

/// @brief AssetHandle3D payload: request state plus either an entity result or
///   a reusable ModelTemplate result. Deferred handles schedule worker loading
///   on first observation and publish terminal results through the main-thread
///   commit queue.
typedef struct rt_game3d_asset_handle {
    int8_t ready;
    int8_t cancelled;
    int8_t deferred;
    int8_t async_started;
    int8_t asset_path;
    int8_t template_request;
    double progress;
    rt_string error;
    rt_string path;
    void *entity;
    void *model_template;
} rt_game3d_asset_handle;

/// @brief Async asset-load worker job: the target AssetHandle3D, snapshotted request
///   metadata safe for worker-thread reads, any preloaded glTF bundle or FBX root bytes,
///   the cache generation it was scheduled against, upload-byte progress counters, and a
///   fixed-size error buffer filled on worker-thread failure.
typedef struct rt_game3d_asset_async_job {
    rt_game3d_asset_handle *handle;
    char path[RT_GAME3D_MODEL_CACHE_KEY_MAX];
    int8_t asset_path;
    int8_t template_request;
    rt_gltf_preload_bundle *preloaded_gltf;
    uint8_t *preloaded_fbx_data;
    size_t preloaded_fbx_size;
    uint64_t cache_generation;
    uint64_t upload_total_bytes;
    uint64_t upload_prepared_bytes;
    char error[256];
} rt_game3d_asset_async_job;

/// @brief One streaming scene cell parsed from the cells manifest: spatial center/
///   radius, material/nav/layer/collision metadata, an optional malloc-owned binary
///   sidecar payload, and the loaded scene/entity plus residency bookkeeping.
typedef struct rt_game3d_stream_cell {
    rt_string name;
    rt_string path;
    double center[3];
    double radius;
    int64_t resident_bytes;
    int64_t measured_resident_bytes;
    rt_string material;
    rt_string nav_area;
    rt_string sidecar_path;
    int64_t layer;
    int64_t collision_mask;
    double traversal_cost;
    int8_t has_layer;
    int8_t has_collision_mask;
    int8_t collision_enabled;
    void *scene;
    void *entity;
    int8_t resident;
    void *sidecar_data;    /* loaded binary sidecar payload (malloc-owned), or NULL */
    int64_t sidecar_bytes; /* size in bytes of the loaded binary sidecar payload */
    int32_t reload_cooldown; /* recompute passes to wait before reloading after a
                                budget eviction (prevents load/unload thrash) */
} rt_game3d_stream_cell;

/// @brief Scratch entry used to sort manifest-backed streaming loads nearest-first.
/// @details WorldStream3D owns reusable arrays of these entries so steady-state
///   streaming updates do not allocate every frame. The index is stable within the
///   parsed manifest array and distance_sq is the sanitized squared focus distance.
typedef struct rt_game3d_stream_load_candidate {
    int32_t index;
    double distance_sq;
} rt_game3d_stream_load_candidate;

/// @brief One streaming terrain tile parsed from the terrain manifest: spatial
///   center/scale/radius and heightmap, material/nav/layer/collision metadata, an
///   optional malloc-owned binary sidecar payload, and the loaded terrain plus
///   collider/nav entities and residency bookkeeping.
typedef struct rt_game3d_stream_terrain_tile {
    rt_string name;
    rt_string path;
    rt_string heightmap_path;
    double center[3];
    double scale[3];
    double radius;
    int64_t width;
    int64_t depth;
    int64_t resident_bytes;
    rt_string material;
    rt_string nav_area;
    rt_string sidecar_path;
    int64_t layer;
    int64_t collision_mask;
    double traversal_cost;
    int8_t has_layer;
    int8_t has_collision_mask;
    int8_t collision_enabled;
    void *terrain;
    void *collider_entity;
    void *nav_entity;
    int8_t resident;
    void *sidecar_data;    /* loaded binary sidecar payload (malloc-owned), or NULL */
    int64_t sidecar_bytes; /* size in bytes of the loaded binary sidecar payload */
    int32_t reload_cooldown; /* recompute passes to wait before reloading after a
                                budget eviction (prevents load/unload thrash) */
} rt_game3d_stream_terrain_tile;

/// @brief WorldStream3D payload: streaming focus/radii, mounted manifest paths,
///   parsed scene-cell manifests, and deterministic resident telemetry.
typedef struct rt_game3d_world_stream {
    void *world;
    double center[3];
    double load_radius;
    double unload_radius;
    int64_t residency_budget_bytes;
    int64_t resident_cell_count;
    int64_t resident_terrain_tile_count;
    int64_t pending_request_count;
    int64_t resident_bytes;
    rt_string terrain_manifest;
    rt_string cells_manifest;
    rt_game3d_stream_cell *cells;
    rt_game3d_stream_terrain_tile *terrain_tiles;
    int32_t cell_count;
    int32_t cell_capacity;
    int32_t terrain_tile_count;
    int32_t terrain_tile_capacity;
    rt_game3d_stream_load_candidate *cell_candidates;
    int32_t cell_candidate_capacity;
    rt_game3d_stream_load_candidate *terrain_candidates;
    int32_t terrain_candidate_capacity;
    int8_t cells_manifest_loaded;
    int8_t terrain_manifest_loaded;
    int8_t retains_world;
} rt_game3d_world_stream;

/// @brief One entry in the process-wide model cache, keyed by path + asset flag
///   and mapping to its shared ModelTemplate.
typedef struct rt_game3d_model_cache_entry {
    rt_string path;
    int8_t asset_path;
    int8_t loading;
    void *model_template;
    uint64_t resident_bytes;
    uint64_t last_used;
    double residency_priority;
    double residency_distance;
} rt_game3d_model_cache_entry;

/// @brief CharacterController3D payload: owning world, driven entity, underlying
///   character object, movement tuning, and integrated vertical velocity / eye height.
typedef struct rt_game3d_character_controller {
    void *world;
    void *entity;
    void *character;
    double speed;
    double jump_speed;
    double gravity;
    double vertical_velocity;
    double eye_height;
} rt_game3d_character_controller;

/// @brief FirstPersonController payload: owning world, the character controller it
///   drives, move speed, look sensitivity, and whether the cursor is captured.
typedef struct rt_game3d_first_person_controller {
    void *world;
    void *character_controller;
    double speed;
    double look_sensitivity;
    int8_t capture_mouse;
} rt_game3d_first_person_controller;

/// @brief FreeFlyController payload: owning world, fly speed, look sensitivity,
///   and whether the cursor is captured.
typedef struct rt_game3d_free_fly_controller {
    void *world;
    double speed;
    double look_sensitivity;
    int8_t capture_mouse;
} rt_game3d_free_fly_controller;

/// @brief OrbitController payload: owning world, orbit target, clamped distance
///   range, current yaw/pitch, and orbit/zoom input sensitivities.
typedef struct rt_game3d_orbit_controller {
    void *world;
    void *target;
    double distance;
    double min_distance;
    double max_distance;
    double yaw;
    double pitch;
    double orbit_sensitivity;
    double zoom_sensitivity;
} rt_game3d_orbit_controller;

/// @brief FollowController payload: owning world, followed entity, position
///   offset relative to the target, and the smoothing damping factor.
typedef struct rt_game3d_follow_controller {
    void *world;
    void *target_entity;
    void *offset;
    double damping;
} rt_game3d_follow_controller;

/// @brief One body→entity index entry, mapping a physics body handle to the
///   Entity3D that owns it for fast collision-event entity lookup.
typedef struct {
    void *body;
    rt_game3d_entity *entity;
} rt_game3d_body_index_entry;

/// @brief World3D payload: the owned canvas/camera/scene/physics/input/audio/
///   effects subsystems, the active camera controller, the spawned-entity list,
///   per-frame timing/frame counters, window size, clear color, and the set of
///   debug-overlay toggles plus the destroyed flag.
typedef struct rt_game3d_world {
    void *canvas;
    void *camera;
    void *scene;
    void *physics;
    void *input;
    void *audio;
    void *effects;
    void *stream;
    void *camera_controller;
    rt_game3d_entity **entities;
    int32_t entity_count;
    int32_t entity_capacity;
    rt_game3d_body_index_entry *body_index_entries;
    int32_t body_index_count;
    int32_t body_index_capacity;
    uint64_t *name_index_hashes;
    rt_game3d_entity **name_index_entities;
    int32_t name_index_count;
    int32_t name_index_capacity;
    int8_t name_index_valid;
    void **animation_animators;
    int32_t animation_animator_capacity;
    void **animation_seen_set;
    size_t animation_seen_capacity;
    void *animation_jobs;
    int32_t animation_job_capacity;
    int64_t next_entity_id;
    double dt;
    double elapsed;
    int64_t frame;
    int64_t dropped_fixed_steps;
    int64_t worker_count;
    void *job_pool;
    int8_t jobs_enabled;
    int8_t floating_origin;
    double origin_rebase_threshold;
    double world_origin[3];
    int64_t width;
    int64_t height;
    double clear_r;
    double clear_g;
    double clear_b;
    void *debug_axis_origin;
    double debug_axis_size;
    double stream_camera_user_far;
    double stream_camera_effective_far;
    int8_t debug_overlay_enabled;
    int8_t debug_axes_enabled;
    int8_t debug_physics_enabled;
    int8_t debug_camera_enabled;
    int8_t debug_caps_enabled;
    int8_t stream_camera_far_active;
    int8_t destroyed;
    double fixed_interpolation_alpha;
} rt_game3d_world;

#if defined(_MSC_VER)
#define RT_GAME3D_THREAD_LOCAL __declspec(thread)
#else
#define RT_GAME3D_THREAD_LOCAL _Thread_local
#endif

/// @brief Build a stable lifetime diagnostic in the public `Game3D.Type.method: reason` form.
/// @details `method` strings often include an invalid-handle suffix; lifetime traps replace that
///          suffix with the actual destroyed-handle reason while keeping the qualified API name.
static inline const char *game3d_lifetime_diag(const char *method, const char *reason) {
    static RT_GAME3D_THREAD_LOCAL char message[256];
    const char *fallback_method = "Game3D";
    const char *fallback_reason = "invalid lifetime";
    const char *qualified = method && method[0] ? method : fallback_method;
    const char *why = reason && reason[0] ? reason : fallback_reason;
    const char *suffix = strchr(qualified, ':');
    int method_len = suffix ? (int)(suffix - qualified) : (int)strlen(qualified);
    if (method_len < 0)
        method_len = 0;
    snprintf(message, sizeof(message), "%.*s: %s", method_len, qualified, why);
    message[sizeof(message) - 1] = '\0';
    return message;
}

//=========================================================================
// Handle validators — each downcasts an opaque handle to its typed payload,
// trapping `method` (the caller's qualified API name) on a class-id mismatch
// or NULL handle. They centralize the "untrusted handle in, trusted pointer
// out" contract every public entry point relies on.
//=========================================================================

/// @brief Validate `obj` as a LayerMask handle, trapping `method` on mismatch.
static inline rt_game3d_layermask *game3d_layermask_checked(void *obj, const char *method) {
    rt_game3d_layermask *mask =
        (rt_game3d_layermask *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_LAYERMASK_CLASS_ID);
    if (!mask)
        rt_trap(method);
    return mask;
}

/// @brief Validate `obj` as an Input3D handle, trapping `method` on mismatch.
static inline rt_game3d_input *game3d_input_checked(void *obj, const char *method) {
    rt_game3d_input *input =
        (rt_game3d_input *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_INPUT_CLASS_ID);
    if (!input)
        rt_trap(method);
    return input;
}

/// @brief Validate `obj` as an Entity3D handle (allowing a despawned/destroyed one),
///   trapping `method` on class mismatch.
static inline rt_game3d_entity *game3d_entity_checked_allow_destroyed(void *obj,
                                                                      const char *method) {
    rt_game3d_entity *entity =
        (rt_game3d_entity *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    if (!entity)
        rt_trap(method);
    return entity;
}

/// @brief True when a typed Entity3D payload is still live for public API access.
static inline int game3d_entity_alive_or_record(const rt_game3d_entity *entity) {
    if (!entity)
        return 0;
    if (!entity->alive || entity->destroyed) {
        rt_game3d_diag_record_stale_entity_call();
        return 0;
    }
    return 1;
}

/// @brief Mark a retained Entity3D handle as dead after it leaves its world.
static inline void game3d_entity_mark_dead(rt_game3d_entity *entity) {
    if (!entity)
        return;
    entity->alive = 0;
    entity->spawned = 0;
    entity->destroyed = 1;
    entity->world = NULL;
    entity->registry_index = -1;
}

/// @brief Validate `obj` as a live Entity3D handle; stale handles record diagnostics
///   and resolve to NULL so callers can return neutral values or no-op.
static inline rt_game3d_entity *game3d_entity_checked(void *obj, const char *method) {
    rt_game3d_entity *entity = game3d_entity_checked_allow_destroyed(obj, method);
    if (!game3d_entity_alive_or_record(entity))
        return NULL;
    return entity;
}

/// @brief Validate `obj` as an Sound3D handle, trapping `method` on mismatch.
static inline rt_game3d_audio *game3d_audio_checked(void *obj, const char *method) {
    rt_game3d_audio *audio =
        (rt_game3d_audio *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_SOUND_CLASS_ID);
    if (!audio)
        rt_trap(method);
    return audio;
}

/// @brief Validate `obj` as an EffectRegistry3D handle, trapping `method` on mismatch.
static inline rt_game3d_effects *game3d_effects_checked(void *obj, const char *method) {
    rt_game3d_effects *effects =
        (rt_game3d_effects *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_EFFECTS_CLASS_ID);
    if (!effects)
        rt_trap(method);
    return effects;
}

/// @brief Validate `obj` as an EnvHandle, trapping `method` on mismatch.
static inline rt_game3d_env_handle *game3d_env_handle_checked(void *obj, const char *method) {
    rt_game3d_env_handle *env =
        (rt_game3d_env_handle *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_ENV_HANDLE_CLASS_ID);
    if (!env)
        rt_trap(method);
    return env;
}

/// @brief Validate `obj` as a BodyDef handle, trapping `method` on mismatch.
static inline rt_game3d_body_def *game3d_body_def_checked(void *obj, const char *method) {
    rt_game3d_body_def *def =
        (rt_game3d_body_def *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_BODYDEF_CLASS_ID);
    if (!def)
        rt_trap(method);
    return def;
}

/// @brief Validate `obj` as a Collision3DEvent handle, trapping `method` on mismatch.
static inline rt_game3d_collision_event *game3d_collision_event_checked(void *obj,
                                                                        const char *method) {
    rt_game3d_collision_event *event = (rt_game3d_collision_event *)rt_g3d_checked_or_null(
        obj, RT_G3D_GAME3D_COLLISION_EVENT_CLASS_ID);
    if (!event)
        rt_trap(method);
    return event;
}

/// @brief Validate `obj` as an Animator3D handle, trapping `method` on mismatch.
static inline rt_game3d_animator *game3d_animator_checked(void *obj, const char *method) {
    rt_game3d_animator *animator =
        (rt_game3d_animator *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID);
    if (!animator)
        rt_trap(method);
    return animator;
}

/// @brief Validate `obj` as a ModelTemplate handle, trapping `method` on mismatch.
static inline rt_game3d_model_template *game3d_model_template_checked(void *obj,
                                                                      const char *method) {
    rt_game3d_model_template *model_template = (rt_game3d_model_template *)rt_g3d_checked_or_null(
        obj, RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID);
    if (!model_template)
        rt_trap(method);
    return model_template;
}

/// @brief Validate `obj` as an AssetHandle3D handle, trapping `method` on mismatch.
static inline rt_game3d_asset_handle *game3d_asset_handle_checked(void *obj, const char *method) {
    rt_game3d_asset_handle *handle = (rt_game3d_asset_handle *)rt_g3d_checked_or_null(
        obj, RT_G3D_GAME3D_ASSET_HANDLE3D_CLASS_ID);
    if (!handle)
        rt_trap(method);
    return handle;
}

/// @brief Validate `obj` as a WorldStream3D handle, trapping `method` on mismatch.
static inline rt_game3d_world_stream *game3d_world_stream_checked(void *obj, const char *method) {
    rt_game3d_world_stream *stream = (rt_game3d_world_stream *)rt_g3d_checked_or_null(
        obj, RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID);
    if (!stream)
        rt_trap(method);
    return stream;
}

/// @brief Validate `obj` as a World3D handle (allowing a destroyed one), trapping
///   `method` on class mismatch.
static inline rt_game3d_world *game3d_world_checked_allow_destroyed(void *obj, const char *method) {
    rt_game3d_world *world =
        (rt_game3d_world *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_WORLD_CLASS_ID);
    if (!world)
        rt_trap(method);
    return world;
}

/// @brief Validate `obj` as a live World3D handle, additionally trapping if the
///   world has been destroyed.
static inline rt_game3d_world *game3d_world_checked(void *obj, const char *method) {
    rt_game3d_world *world = game3d_world_checked_allow_destroyed(obj, method);
    if (world && world->destroyed)
        rt_trap(game3d_lifetime_diag(method, "world is destroyed"));
    return world;
}

/// @brief Validate `obj` as a CharacterController3D handle, trapping `method` on mismatch.
static inline rt_game3d_character_controller *game3d_character_controller_checked(
    void *obj, const char *method) {
    rt_game3d_character_controller *controller =
        (rt_game3d_character_controller *)rt_g3d_checked_or_null(
            obj, RT_G3D_GAME3D_CHARACTER_CONTROLLER_CLASS_ID);
    if (!controller)
        rt_trap(method);
    return controller;
}

/// @brief Validate `obj` as a FirstPersonController handle, trapping `method` on mismatch.
static inline rt_game3d_first_person_controller *game3d_first_person_controller_checked(
    void *obj, const char *method) {
    rt_game3d_first_person_controller *controller =
        (rt_game3d_first_person_controller *)rt_g3d_checked_or_null(
            obj, RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID);
    if (!controller)
        rt_trap(method);
    return controller;
}

/// @brief Validate `obj` as a FreeFlyController handle, trapping `method` on mismatch.
static inline rt_game3d_free_fly_controller *game3d_free_fly_controller_checked(
    void *obj, const char *method) {
    rt_game3d_free_fly_controller *controller =
        (rt_game3d_free_fly_controller *)rt_g3d_checked_or_null(obj,
                                                                RT_G3D_GAME3D_FREEFLY_CLASS_ID);
    if (!controller)
        rt_trap(method);
    return controller;
}

/// @brief Validate `obj` as an OrbitController handle, trapping `method` on mismatch.
static inline rt_game3d_orbit_controller *game3d_orbit_controller_checked(void *obj,
                                                                          const char *method) {
    rt_game3d_orbit_controller *controller =
        (rt_game3d_orbit_controller *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_ORBIT_CLASS_ID);
    if (!controller)
        rt_trap(method);
    return controller;
}

/// @brief Validate `obj` as a FollowController handle, trapping `method` on mismatch.
static inline rt_game3d_follow_controller *game3d_follow_controller_checked(void *obj,
                                                                            const char *method) {
    rt_game3d_follow_controller *controller =
        (rt_game3d_follow_controller *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_FOLLOW_CLASS_ID);
    if (!controller)
        rt_trap(method);
    return controller;
}

//=========================================================================
// Shared internal helpers — defined in rt_game3d.c / rt_game3d_controllers.c,
// declared here for the split sibling translation units.
//=========================================================================
void game3d_assign_ref(void **slot, void *value);
void game3d_release_ref(void **slot);
void game3d_assign_typed_ref(void **slot, void *value, int64_t class_id);
void game3d_release_typed_ref(void **slot, int64_t class_id);
/// @brief Create an Animator3D wrapper that can hold both skeletal and node-animation drivers.
/// @details Used by model instantiation so imported assets with skeletal Animation3D clips,
/// NodeAnimation3D clips, or both expose a single Game3D Animator3D handle to scripts.
void *rt_game3d_animator_new_from_bindings(void *controller, void *node_animator);
/// @brief Return true when a Game3D animator must be advanced during World3D animation jobs.
/// @details NodeAnimator3D-only wrappers are stepped by Scene3D.SyncBindings because they mutate
/// scene node transforms directly; skeletal controllers still run in the Game3D animation phase.
int8_t rt_game3d_animator_needs_game_update(void *obj);
double game3d_clamp(double value, double lo, double hi);
double game3d_clamp_dt(double dt);
double game3d_finite_or(double value, double fallback);
double game3d_clamp_abs_or(double value, double fallback, double abs_max);
double game3d_clamp_coord_or(double value, double fallback);
double game3d_scale_or_unit(double value);
double game3d_nonnegative_or(double value, double fallback);
double game3d_nonnegative_clamped_or(double value, double fallback, double max_value);
double game3d_positive_or(double value, double fallback);
double game3d_positive_clamped_or(double value, double fallback, double max_value);
void game3d_normalize_xz(double *x, double *z, double fallback_x, double fallback_z);
void *game3d_camera_controller_get_world_ref(void *controller);
void game3d_camera_controller_bind_world_ref(void *controller, void *world);
void game3d_camera_controller_clear_world_ref(void *controller);
void game3d_camera_controller_clear_world_ref_if(void *controller, void *world);
int game3d_camera_controller_is_valid(void *controller);
int game3d_entity_world_position_components(rt_game3d_entity *entity, double out_pos[3]);
int64_t game3d_input_mouse_dx(const rt_game3d_input *input);
double game3d_input_mouse_fdx(const rt_game3d_input *input);
double game3d_input_mouse_fdy(const rt_game3d_input *input);
int64_t game3d_input_mouse_dy(const rt_game3d_input *input);
void game3d_input_move_axis_components(rt_game3d_input *input,
                                       double *out_x,
                                       double *out_y,
                                       double *out_z);
double game3d_input_wheel_y_snapshot(const rt_game3d_input *input);
void game3d_sync_body_from_entity_node(rt_game3d_entity *entity, int8_t force);
void game3d_world_body_index_remove(rt_game3d_world *world, void *body);
int game3d_world_body_index_add(rt_game3d_world *world, rt_game3d_entity *entity);
int game3d_world_name_index_add_entity(rt_game3d_world *world, rt_game3d_entity *entity);
rt_game3d_entity *game3d_world_name_index_find(rt_game3d_world *world, const char *name);
int8_t game3d_valid_layer(int64_t layer);
void *game3d_layermask_new_bits(int64_t bits);
void *game3d_body_def_create_body(rt_game3d_body_def *def);
int game3d_audio_reserve_sources(rt_game3d_audio *audio, int32_t needed);
void game3d_audio_repair_sources(rt_game3d_audio *audio);
void game3d_audio_track_source(rt_game3d_audio *audio, void *source);
void game3d_audio_prune_sources(rt_game3d_audio *audio);
int64_t game3d_clamp_i64(int64_t value, int64_t lo, int64_t hi);
void *game3d_audio_new(void *camera);
void game3d_effect_release_item(rt_game3d_effect_item *item);
void game3d_effects_repair(rt_game3d_effects *effects);
int game3d_effects_reserve(rt_game3d_effects *effects, int32_t needed);
int8_t game3d_read_vec3(void *vec, double *out, const char *method);
void *game3d_effects_new(void *canvas, int64_t quality);
void game3d_entity_detach_from_parent(rt_game3d_entity *child);
int32_t game3d_entity_find_child_index(rt_game3d_entity *parent, rt_game3d_entity *child);
int game3d_entity_grow_children(rt_game3d_entity *entity, int32_t need);
int game3d_entity_has_ancestor(rt_game3d_entity *entity, rt_game3d_entity *ancestor);
rt_game3d_entity *game3d_world_find_entity_by_body(rt_game3d_world *world, void *body);
int game3d_world_spawn_entity_tree(rt_game3d_world *world,
                                   rt_game3d_entity *entity,
                                   int attach_to_scene,
                                   int64_t *next_id);
int8_t game3d_input_key_down(const rt_game3d_input *input, int64_t key);
int8_t game3d_input_key_pressed(const rt_game3d_input *input, int64_t key);
int8_t game3d_input_key_released(const rt_game3d_input *input, int64_t key);
int8_t game3d_input_mouse_down(const rt_game3d_input *input, int64_t button);
int8_t game3d_input_mouse_pressed_snapshot(const rt_game3d_input *input, int64_t button);
void game3d_normalize_axis3(double *x, double *y, double *z);
void game3d_world_assign_postfx(rt_game3d_world *world, void *postfx);
void game3d_world_install_light(rt_game3d_world *world, int64_t slot, void *light);
void game3d_world_set_clear_color(rt_game3d_world *world, double r, double g, double b);
