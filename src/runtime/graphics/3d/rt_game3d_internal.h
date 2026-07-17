//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_internal.h
// Purpose: Private shared surface of the Zanna.Game3D layer — tuning constants,
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
#define RT_GAME3D_DEFAULT_GRAVITY 20.0         ///< Default downward character gravity magnitude.
#define RT_GAME3D_DEFAULT_FOLLOW_DAMPING 12.0  ///< Default follow-camera smoothing factor.
#define RT_GAME3D_TP_DEFAULT_DISTANCE 4.0      ///< Third-person default boom length.
#define RT_GAME3D_TP_DEFAULT_MIN_DISTANCE 0.75 ///< Third-person boom pull-in floor.
#define RT_GAME3D_TP_DEFAULT_MAX_DISTANCE 8.0  ///< Third-person boom length ceiling.
#define RT_GAME3D_TP_DEFAULT_PIVOT_HEIGHT 1.5  ///< Third-person pivot above entity origin.
#define RT_GAME3D_TP_DEFAULT_SHOULDER_X 0.35   ///< Third-person lateral shoulder offset.
#define RT_GAME3D_TP_DEFAULT_PITCH_MIN (-60.0) ///< Third-person pitch clamp floor (deg).
#define RT_GAME3D_TP_DEFAULT_PITCH_MAX 75.0    ///< Third-person pitch clamp ceiling (deg).
#define RT_GAME3D_TP_DEFAULT_COLLISION_RADIUS 0.25 ///< Third-person boom sweep sphere radius.
#define RT_GAME3D_TP_DEFAULT_AIM_DISTANCE 1.6      ///< Third-person aim-mode boom length.
#define RT_GAME3D_TP_DEFAULT_AIM_FOV 45.0          ///< Third-person aim-mode camera FOV (deg).
#define RT_GAME3D_TP_BOOM_SKIN 0.05                ///< Boom hit back-off epsilon.
#define RT_GAME3D_TP_AIM_BLEND_RATE 6.0            ///< Aim blend speed (fraction per second).
#define RT_GAME3D_TP_FADE_ALPHA 0.35               ///< Occluder fade target alpha.
#define RT_GAME3D_TP_FADE_RATE 8.0                 ///< Occluder fade exponential rate (1/sec).
#define RT_GAME3D_TL_DEFAULT_MAX_DISTANCE 18.0     ///< TargetLock3D acquisition radius.
#define RT_GAME3D_TL_DEFAULT_CONE_DEGREES 65.0     ///< TargetLock3D half-angle cone (deg).
#define RT_GAME3D_TL_DEFAULT_STICKINESS 1.25       ///< TargetLock3D current-target score bonus.
#define RT_GAME3D_TL_DEFAULT_LOS_GRACE 0.5         ///< TargetLock3D LoS-break grace (seconds).
#define RT_GAME3D_DEFAULT_AUDIO_REF_DISTANCE 1.0   ///< Default audio full-volume radius.
#define RT_GAME3D_DEFAULT_AUDIO_MAX_DISTANCE 50.0  ///< Default audio silence radius.
#define RT_GAME3D_AUDIO_DISTANCE_MAX 1000000000.0  ///< Max finite audio attenuation radius.
#define RT_GAME3D_DEFAULT_AUDIO_VOLUME 100         ///< Default master audio volume (0–100).
#define RT_GAME3D_PI 3.14159265358979323846        ///< Pi (avoids relying on non-portable M_PI).
#define RT_GAME3D_ANIM_EVENT_MAX 64                ///< Max animation events buffered per update.
#define RT_GAME3D_MAX_FIXED_STEPS_PER_FRAME 8      ///< Fixed-loop spiral-of-death guard.
#define RT_GAME3D_COORD_ABS_MAX 1000000000000.0    ///< Max finite world coordinate accepted.
#define RT_GAME3D_SCALE_ABS_MAX 1000000.0          ///< Max absolute node/body scale.
#define RT_GAME3D_ANGLE_DEG_ABS_MAX 1000000.0      ///< Max finite Euler/orbit angle in degrees.
#define RT_GAME3D_CONTROLLER_SPEED_MAX 1000000.0   ///< Max controller speed/jump velocity.
#define RT_GAME3D_LOOK_SENSITIVITY_MAX 1000.0      ///< Max mouse-look sensitivity.
#define RT_GAME3D_DAMPING_MAX 1000.0               ///< Max camera damping factor.
#define RT_GAME3D_ANIM_BLEND_TIME_MAX 1000000.0    ///< Max animation transition duration.
#define RT_GAME3D_ANIM_STEP_MAX 1.0                ///< Max single Game3D animator update step.
#define RT_GAME3D_ANIM_SPEED_ABS_MAX 1000000.0     ///< Max animation playback speed multiplier.
#define RT_GAME3D_EFFECT_STEP_MAX 10.0             ///< Max single EffectRegistry3D update step.
#define RT_GAME3D_EFFECT_LIFETIME_MAX 86400.0      ///< Max effect auto-expire lifetime.
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
    uint8_t key_down[ZANNA_KEY_MAX];
    uint8_t key_pressed[ZANNA_KEY_MAX];
    uint8_t key_released[ZANNA_KEY_MAX];
    uint8_t mouse_down[ZANNA_MOUSE_BUTTON_MAX];
    uint8_t mouse_pressed[ZANNA_MOUSE_BUTTON_MAX];
    uint8_t mouse_released[ZANNA_MOUSE_BUTTON_MAX];
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
    void *behavior;  /* retained Behavior3D ticked each simulation step, or NULL */
    void **hitboxes; /* retained Hitbox3D array (combat volumes), or NULL */
    int32_t hitbox_count;
    int32_t hitbox_capacity;
    void *health;       /* retained Health3D component, or NULL */
    void *ragdoll;      /* retained Ragdoll3D built by enableRagdoll, or NULL */
    void *lipsync;      /* retained LipSync3D component, or NULL */
    void *footsteps;    /* retained Footsteps3D component, or NULL */
    void *interactable; /* retained Interactable3D component, or NULL */
    void *interactor;   /* retained Interactor3D component, or NULL */
    void *perception;   /* retained Perception3D component, or NULL */
    void *btree;        /* retained BehaviorTreeInstance3D, or NULL */
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
    /* Fixed-step render interpolation (world->render_interpolation): node pose captured
     * before the latest fixed simulation step, plus scratch to restore the authoritative
     * sim pose after an interpolated render. */
    double interp_prev_position[3];
    double interp_prev_rotation[4];
    double interp_saved_position[3];
    double interp_saved_rotation[4];
    int8_t interp_has_prev;
    int8_t interp_pose_blended;
    rt_string persistent_key; ///< Retained persistence key, or NULL (plan 17).
    int64_t state_tag;        ///< Free-form persisted state tag.
    /// Last world sweep stamp that ticked this entity (despawn-safe sweeps).
    /// Appended at the end: test fixtures mirror prefixes of this layout.
    uint32_t sim_tick_stamp;
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
    /* Audio immersion (plan 24): reverb zones, occlusion raycasts, ambient beds. */
    void **reverb_zones;          ///< Retained ReverbZone3D handles.
    int32_t reverb_zone_count;    ///< Number of registered zones.
    int32_t reverb_zone_capacity; ///< Zone array capacity.
    int64_t reverb_group;         ///< Lazily-registered "g3d_reverb" group (-1 unset).
    int64_t reverb_fx;            ///< Reverb insert id on the group (-1 unset).
    double reverb_blend;          ///< Zone parameter blend time in seconds.
    double reverb_room;           ///< Current eased room size.
    double reverb_damp;           ///< Current eased damping.
    double reverb_wet;            ///< Current eased wet mix.
    int8_t reverb_routing;        ///< Route new positional voices to the reverb group.
    int8_t occlusion_enabled;     ///< Listener->source raycast occlusion on/off.
    int64_t occlusion_mask;       ///< Raycast layer mask for occlusion probes.
    double occlusion_amount;      ///< Occlusion applied to a blocked source (0..1).
    int32_t occlusion_budget;     ///< Max raycasts per world step.
    int32_t occlusion_cursor;     ///< Round-robin cursor over tracked sources.
    int64_t dialogue_group;       ///< Lazily-registered "g3d_dialogue" group (-1 unset).
    void *ambient_bed;            ///< Retained AmbientBed3D (NULL when unused).
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
    void *sidecar_data;        /* loaded binary sidecar payload (malloc-owned), or NULL */
    int64_t sidecar_bytes;     /* size in bytes of the loaded binary sidecar payload */
    int32_t reload_cooldown;   /* recompute passes to wait before reloading after a
                                  budget eviction (prevents load/unload thrash) */
    int8_t staging;            /* async: a worker staging job is in flight */
    int8_t staged;             /* async: worker payload landed, awaiting main commit */
    int8_t staged_error;       /* async: staging failed (missing/corrupt payload) */
    int8_t prefetched;         /* staged/staging due to velocity prefetch only */
    char *staged_text;         /* staged .vscn text (malloc-owned), or NULL */
    size_t staged_text_len;    /* byte length of staged_text */
    uint8_t *staged_sidecar;   /* staged sidecar bytes (malloc-owned), or NULL */
    size_t staged_sidecar_len; /* byte length of staged_sidecar */
    /* --- HLOD proxy ring (cell-level merged low-poly stand-in) --- */
    rt_string proxy_path;         /* optional manifest "proxy" .vscn path, or NULL */
    int64_t proxy_bytes;          /* manifest "proxyBytes" estimate */
    int64_t measured_proxy_bytes; /* measured proxy residency after load */
    void *proxy_scene;            /* loaded proxy Scene3D while ProxyResident */
    void *proxy_entity;           /* spawned proxy entity subtree while ProxyResident */
    int8_t proxy_resident;        /* proxy subtree currently attached */
    int8_t proxy_staging;         /* async proxy staging job in flight */
    int8_t proxy_staged;          /* staged proxy text awaiting commit */
    char *staged_proxy_text;      /* staged proxy .vscn text (malloc-owned) */
    size_t staged_proxy_text_len;
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
    void *sidecar_data;        /* loaded binary sidecar payload (malloc-owned), or NULL */
    int64_t sidecar_bytes;     /* size in bytes of the loaded binary sidecar payload */
    int32_t reload_cooldown;   /* recompute passes to wait before reloading after a
                                  budget eviction (prevents load/unload thrash) */
    int8_t staging;            /* async: a worker staging job is in flight */
    int8_t staged;             /* async: worker payload landed, awaiting main commit */
    int8_t staged_error;       /* async: staging failed (missing/corrupt payload) */
    int8_t prefetched;         /* staged/staging due to velocity prefetch only */
    double *staged_heights;    /* staged POD height grid (malloc-owned), or NULL */
    int64_t staged_hm_width;   /* source width of staged_heights */
    int64_t staged_hm_depth;   /* source depth of staged_heights */
    uint8_t *staged_sidecar;   /* staged sidecar bytes (malloc-owned), or NULL */
    size_t staged_sidecar_len; /* byte length of staged_sidecar */
    double (*holes)[4];        /* manifest-authored hole rects (tile-local units) */
    int32_t manifest_hole_count;
} rt_game3d_stream_terrain_tile;

/// @brief WorldStream3D payload: streaming focus/radii, mounted manifest paths,
///   parsed scene-cell manifests, and deterministic resident telemetry.
#define RT_GAME3D_MAX_HITCHES 256
#define RT_GAME3D_HITCH_SOURCE_STREAM_COMMIT 0
#define RT_GAME3D_HITCH_SOURCE_FRAME_TOTAL 3

/// @brief One recorded hitch: frame index, source constant, and wall ms.
typedef struct rt_game3d_hitch_entry {
    int64_t frame;  ///< World frame counter when the hitch was recorded.
    int64_t source; ///< RT_GAME3D_HITCH_SOURCE_* constant.
    double ms;      ///< Measured milliseconds.
} rt_game3d_hitch_entry;

/// @brief One persisted-entity delta record (plan 17): keyed pose + liveness.
typedef struct rt_game3d_persist_record {
    rt_string key;          ///< Retained game-stable persistence key.
    int8_t alive;           ///< 0 once the keyed entity was despawned/killed.
    int8_t applied_pending; ///< Loaded from a snapshot, not yet applied to an entity.
    double position[3];     ///< Last captured world position.
    double rotation[4];     ///< Last captured world rotation quaternion (xyzw).
    double scale[3];        ///< Reserved (identity in v1).
    int64_t state_tag;      ///< Free-form game state tag.
} rt_game3d_persist_record;

/// @brief One per-cell persisted flag (door-opened / chest-looted style).
typedef struct rt_game3d_cell_flag {
    rt_string cell; ///< Retained cell name (manifest key).
    rt_string key;  ///< Retained flag key.
    int64_t value;  ///< Flag value.
} rt_game3d_cell_flag;

#define RT_GAME3D_STREAM_MAX_LOADED_EVENTS 32

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
    /* --- worker-backed streaming (plan: async cell/tile staging) --- */
    int8_t async_streaming;          /* 1 = worker staging + budgeted main commits (default) */
    uint64_t cell_generation;        /* bumped on cell mount/clear so late results drop */
    uint64_t terrain_generation;     /* bumped on terrain mount/clear so late results drop */
    int64_t commit_budget_bytes;     /* staged bytes committed per update; -1 = unlimited,
                                        0 = hold commits pending */
    double prefetch_lookahead;       /* seconds of center velocity to prefetch along */
    double prev_center[3];           /* center at the previous update (velocity estimate) */
    double velocity[3];              /* smoothed center velocity (units/sec) */
    int8_t has_prev_center;          /* prev_center is valid */
    double stream_stall_ms;          /* worst single commit-slice wall ms since mount */
    int64_t prefetched_cell_count;   /* cells staged/staging from prefetch only */
    double proxy_radius;             /* HLOD proxy ring radius; <=0 = auto (4x load) */
    int64_t proxy_resident_count;    /* cells currently holding only their proxy */
    int64_t proxy_resident_bytes;    /* measured bytes of resident proxy subtrees */
    rt_game3d_cell_flag *cell_flags; /* persisted per-cell flags (plan 17) */
    int32_t cell_flag_count;         /* number of flags */
    int32_t cell_flag_capacity;      /* flag array capacity */
    rt_string loaded_events[RT_GAME3D_STREAM_MAX_LOADED_EVENTS]; /* just-loaded cell names */
    int32_t loaded_event_count;                                  /* buffered loaded-cell events */
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
///   character object, movement tuning, integrated vertical velocity / eye height,
///   and the crouch/stand capsule heights.
typedef struct rt_game3d_character_controller {
    void *world;
    void *entity;
    void *character;
    double speed;
    double jump_speed;
    double gravity;
    double vertical_velocity;
    double eye_height;
    double stand_height;   ///< Capsule height restored by SetCrouching(false).
    double crouch_height;  ///< Capsule height applied by SetCrouching(true).
    double capsule_radius; ///< Capsule radius captured at creation (probe sugar).
    int8_t crouching;      ///< Current requested crouch state.
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

/// @brief One occluder-fade bookkeeping entry for the third-person controller:
///   the scene node whose material was swapped, the original material handle to
///   restore, the faded instance clone, and the current animated alpha.
typedef struct rt_game3d_tp_fade_entry {
    void *node;              ///< Retained scene node whose material is swapped.
    void *original_material; ///< Retained original material to restore.
    void *fade_material;     ///< Retained Material3D.MakeInstance clone being faded.
    double alpha;            ///< Current animated alpha on the clone.
    double original_alpha;   ///< Alpha of the original material at capture time.
    int8_t occluding;        ///< Marked each late update while still occluding.
} rt_game3d_tp_fade_entry;

/// @brief ThirdPersonController payload: owning world, orbited target entity,
///   optional character-drive slot, spring-arm orbit state (yaw/pitch/distance),
///   shoulder/pivot framing, boom collision tuning, aim-mode blend state, and
///   occluder-fade bookkeeping.
typedef struct rt_game3d_thirdperson_controller {
    void *world;               ///< Weak-style retained world back-ref (controller-slot pattern).
    void *target;              ///< Entity3D the camera orbits (usually the player).
    void *character;           ///< Optional CharacterController3D drive slot.
    void *lock;                ///< Optional TargetLock3D framing source (plan 02).
    double yaw;                ///< Camera orbit yaw in degrees.
    double pitch;              ///< Camera orbit pitch in degrees.
    double distance;           ///< Desired boom length.
    double min_distance;       ///< Boom pull-in floor.
    double max_distance;       ///< Boom length ceiling.
    double shoulder_offset[3]; ///< Local-space offset from the target pivot.
    double pivot_height;       ///< Pivot height above the entity origin.
    double pitch_min;          ///< Pitch clamp floor in degrees.
    double pitch_max;          ///< Pitch clamp ceiling in degrees.
    double damping;            ///< Exponential smoothing rate for boom release.
    double collision_radius;   ///< Boom sweep sphere radius.
    int64_t collision_mask;    ///< Layers the boom collides with.
    int8_t occlusion_fade;     ///< Opt-in occluder fading toggle.
    int8_t aiming;             ///< Aim-mode request flag.
    double aim_blend;          ///< 0..1 current aim interpolation.
    double aim_distance;       ///< Boom length while aiming.
    double aim_fov;            ///< Camera FOV while aiming (degrees).
    double aim_shoulder_offset[3];  ///< Shoulder offset while aiming.
    double base_fov;                ///< Camera FOV captured when aim blend engages.
    int8_t base_fov_valid;          ///< True while base_fov holds a captured value.
    double current_distance;        ///< Smoothed post-collision boom length.
    rt_game3d_tp_fade_entry *fades; ///< Occluder-fade bookkeeping array.
    int32_t fade_count;             ///< Live fade entries.
    int32_t fade_capacity;          ///< Allocated fade entries.
} rt_game3d_thirdperson_controller;

#define RT_GAME3D_TL_MAX_MARKERS_PER_STEP 64 ///< Marker events buffered per tick.
#define RT_GAME3D_TL_TEXT_MAX 256            ///< Subtitle/name text capacity per track.

/// @brief Timeline3D track kinds.
enum {
    RT_GAME3D_TL_CAMERA_CUT = 0,
    RT_GAME3D_TL_CAMERA_MOVE = 1,
    RT_GAME3D_TL_FOV_RAMP = 2,
    RT_GAME3D_TL_ANIM = 3,
    RT_GAME3D_TL_AUDIO = 4,
    RT_GAME3D_TL_SUBTITLE = 5,
    RT_GAME3D_TL_LETTERBOX = 6,
    RT_GAME3D_TL_FADE = 7,
    RT_GAME3D_TL_MARKER = 8,
};

/// @brief One Timeline3D track record (flat union of per-kind fields).
typedef struct rt_game3d_tl_track {
    int8_t type;                        ///< RT_GAME3D_TL_*.
    double t0;                          ///< Start time (fire time for point tracks).
    double t1;                          ///< End time (== t0 for point tracks).
    int8_t fired;                       ///< Fire-once latch, reset by play()/stop().
    int8_t ease;                        ///< 0 linear, 1 smoothstep, 2 ease-in, 3 ease-out.
    double vec_a[3];                    ///< Cut position / audio position.
    double vec_b[3];                    ///< Cut look-at point.
    double scalar_a;                    ///< Cut/ramp fov0 / letterbox amount / fade a0 / crossfade.
    double scalar_b;                    ///< Ramp fov1 / fade a1.
    void *obj_a;                        ///< Retained Path3D (move) or audio clip.
    void *obj_b;                        ///< Retained look target (Vec3 | Entity3D | Path3D).
    int8_t positional;                  ///< Audio: play at vec_a instead of 2D.
    int64_t marker_id;                  ///< Marker payload.
    char text_a[RT_GAME3D_TL_TEXT_MAX]; ///< Anim entity name / subtitle text.
    char text_b[RT_GAME3D_TL_TEXT_MAX]; ///< Anim state name.
} rt_game3d_tl_track;

/// @brief Timeline3D payload: track list + playhead + polled marker buffer.
typedef struct rt_game3d_timeline {
    void *world; ///< Retained world while installed as active timeline.
    rt_game3d_tl_track *tracks;
    int32_t track_count;
    int32_t track_capacity;
    double time;
    double duration;
    int8_t playing;
    int8_t finished;
    int8_t just_finished;
    int8_t skippable;
    int8_t sorted;
    int8_t has_camera_tracks; ///< Suspends the installed camera controller.
    int64_t fired_markers[RT_GAME3D_TL_MAX_MARKERS_PER_STEP];
    int32_t fired_marker_count;
    char active_subtitle[RT_GAME3D_TL_TEXT_MAX];
    double letterbox_amount; ///< Current letterbox fraction (overlay pass).
    double fade_alpha;       ///< Current full-screen fade alpha (overlay pass).
} rt_game3d_timeline;

#define RT_GAME3D_DLG_MAX_LINES 32  ///< Queued dialogue lines per conversation.
#define RT_GAME3D_DLG_MAX_CHOICES 8 ///< Options per choice prompt.
#define RT_GAME3D_DLG_NAME_MAX 64   ///< Speaker-name capacity.

#define RT_GAME3D_LS_MAX_SHAPES 4 ///< Mouth shapes per LipSync3D binding.

/// @brief One bound mouth shape (name + per-shape weight scale).
typedef struct rt_game3d_ls_shape {
    char name[RT_GAME3D_DLG_NAME_MAX];
    double scale;
    rt_string name_interned; ///< Retained shape name; avoids a per-frame rt_const_cstr alloc.
} rt_game3d_ls_shape;

/// @brief LipSync3D payload: amplitude-envelope mouth drive + procedural blink
///   + gaze sugar over LookAt IK.
typedef struct rt_game3d_lipsync {
    void *entity;      ///< Owner backref (plain; cleared at entity teardown).
    void *morph;       ///< Retained MorphTarget3D driven by the bindings.
    void *gaze_solver; ///< Retained LookAt IKSolver3D, or NULL.
    void *gaze_target; ///< Retained Vec3 target handed to the solver.
    rt_game3d_ls_shape shapes[RT_GAME3D_LS_MAX_SHAPES];
    int32_t shape_count;
    int64_t voice_id; ///< Metered voice being tracked, or -1.
    double envelope;  ///< Smoothed level (attack 0.04 s / release 0.12 s).
    int8_t driving;   ///< Voice drive active.
    /* Blink layer (seeded LCG so replays match). */
    int8_t blink_enabled;
    char blink_shape[RT_GAME3D_DLG_NAME_MAX];
    rt_string blink_interned; ///< Retained blink shape name; avoids a per-frame alloc.
    double blink_min_interval;
    double blink_max_interval;
    double blink_timer;  ///< Countdown to the next blink.
    double blink_phase;  ///< Active blink progress (0 = idle).
    uint64_t blink_seed; ///< LCG state.
    double gaze_weight;  ///< Eased IK weight.
    int8_t gaze_active;
} rt_game3d_lipsync;

/// @brief One queued dialogue line (text resolved at say() time).
typedef struct rt_game3d_dlg_line {
    char speaker[RT_GAME3D_DLG_NAME_MAX];
    char text[RT_GAME3D_TL_TEXT_MAX];
    void *voice_clip; ///< Retained clip played when the line starts, or NULL.
} rt_game3d_dlg_line;

/// @brief Dialogue3D payload: line queue + typewriter reveal + choice prompt +
///   speaker anchoring + localization binding + style knobs.
typedef struct rt_game3d_dialogue {
    void *world;          ///< Retained world back-ref.
    void *bundle;         ///< Retained MessageBundle for key resolution, or NULL.
    void *speaker_entity; ///< Retained anchor entity, or NULL.
    rt_game3d_dlg_line lines[RT_GAME3D_DLG_MAX_LINES];
    int32_t line_count;
    int32_t line_index;
    double reveal_chars;   ///< Characters revealed on the current line.
    double reveal_speed;   ///< Characters per second (default 40).
    double hold_remaining; ///< Auto-advance hold after the reveal completes.
    int8_t active;         ///< Shown and consuming the overlay.
    int8_t anchored;       ///< Bubble above the speaker entity when visible.
    int8_t auto_advance;   ///< Advance lines automatically after reveal + hold.
    int8_t line_started;   ///< Voice fired for the current line.
    char choices[RT_GAME3D_DLG_MAX_CHOICES][RT_GAME3D_TL_TEXT_MAX];
    int32_t choice_count;
    int32_t choice_selected;
    int8_t choice_active; ///< Blocks advance until confirmed.
    int8_t choice_made;   ///< One-shot: a choice was confirmed.
    int64_t last_choice;  ///< Index confirmed by the last choice prompt.
    double panel_alpha;   ///< Bottom-panel opacity (default 0.65).
    int64_t name_color;   ///< Speaker-name color (default 0xFFD75A).
} rt_game3d_dialogue;

#define RT_GAME3D_RAIL_MAX_KEYS 16 ///< FOV/roll keys per rail camera.

/// @brief One rail-camera key: value at arclength-normalized t.
typedef struct rt_game3d_rail_key {
    double t;
    double value;
} rt_game3d_rail_key;

/// @brief RailCamera3D payload: owning world, spline path, look target (entity,
///   point, or second path), progress/auto-advance state, damping, and
///   piecewise FOV/roll keys.
typedef struct rt_game3d_rail_camera {
    void *world;             ///< Retained world back-ref (controller-slot pattern).
    void *path;              ///< Retained Path3D the camera rides.
    void *look_entity;       ///< Retained Entity3D look target, or NULL.
    void *look_point;        ///< Retained Vec3 look target, or NULL.
    void *look_path;         ///< Retained Path3D look target, or NULL.
    double progress;         ///< Requested arclength-normalized position [0,1].
    double smoothed;         ///< Damped progress actually applied.
    double speed;            ///< Auto-advance in units/sec along arclength (0 = manual).
    double position_damping; ///< Exponential smoothing for progress jumps (0 = snap).
    int8_t key_ease;         ///< 0 = linear keys, 1 = smoothstep between keys.
    rt_game3d_rail_key fov_keys[RT_GAME3D_RAIL_MAX_KEYS];
    int32_t fov_key_count;
    rt_game3d_rail_key roll_keys[RT_GAME3D_RAIL_MAX_KEYS];
    int32_t roll_key_count;
} rt_game3d_rail_camera;

/// @brief TargetLock3D payload: owning world, owner entity (the player), the
///   currently locked target, acquisition tuning (range/cone/mask/LoS), and the
///   one-shot acquired/lost polling flags with the LoS grace timer.
typedef struct rt_game3d_targetlock {
    void *world;              ///< Retained World3D back-ref.
    void *owner;              ///< Retained owner Entity3D (scoring origin).
    void *target;             ///< Retained locked Entity3D or NULL.
    double max_distance;      ///< Acquisition radius.
    double cone_degrees;      ///< Half-angle cone from camera forward.
    int64_t candidate_mask;   ///< Layers that are targetable.
    int8_t require_los;       ///< Reject candidates without line of sight.
    double stickiness;        ///< Score multiplier for the current target.
    double break_distance;    ///< Auto-release distance.
    double los_grace_seconds; ///< LoS-break grace before auto-release.
    double los_broken_time;   ///< Accumulated seconds the LoS has been broken.
    int8_t just_acquired;     ///< One-shot poll flag set on acquisition.
    int8_t just_lost;         ///< One-shot poll flag set on release.
} rt_game3d_targetlock;

#define RT_GAME3D_HITBOX_MAX_WINDOWS 4     ///< Animation windows per hitbox.
#define RT_GAME3D_HITBOX_MAX_VICTIMS 16    ///< Rehit-suppression ring per activation.
#define RT_GAME3D_HITBOX_STATE_NAME_MAX 64 ///< Window state-name capacity.
#define RT_GAME3D_HITBOX_KIND_HURT 0       ///< Damageable region volume.
#define RT_GAME3D_HITBOX_KIND_HIT 1        ///< Attack volume.

/// @brief One animation-window binding: the hitbox is live while the owner's
///   animator base state matches @p state and its time is within [t0, t1].
typedef struct rt_game3d_hitbox_window {
    char state[RT_GAME3D_HITBOX_STATE_NAME_MAX];
    double t0;
    double t1;
} rt_game3d_hitbox_window;

/// @brief Hitbox3D payload: owner backref (plain pointer cleared at entity
///   teardown), retained collider shape, bone/entity attachment, combat filters,
///   activation state, window bindings, and the per-activation rehit ring.
typedef struct rt_game3d_hitbox {
    struct rt_game3d_entity *entity; ///< Owner; NULLed when the entity tears down.
    void *collider;                  ///< Retained Collider3D shape.
    int64_t bone_index;              ///< -1 = entity-space attachment.
    double local_offset[3];          ///< Offset in bone/entity space.
    int8_t kind;                     ///< RT_GAME3D_HITBOX_KIND_*.
    int64_t team;                    ///< Same-team pairs are skipped unless friendly fire.
    int64_t channel;                 ///< Bitmask; hit×hurt require overlapping channels.
    int8_t friendly_fire;            ///< Allow same-team hits from this attacker.
    int8_t active;                   ///< Manual activation switch.
    int8_t was_live;                 ///< Previous-step liveness (rehit reset edge).
    rt_game3d_hitbox_window windows[RT_GAME3D_HITBOX_MAX_WINDOWS];
    int32_t window_count;
    /// Previous liveness sample so a coarse step cannot jump OVER a narrow
    /// window (liveness tests [prev_time, now] crossing, loop-aware).
    double window_prev_time;
    int8_t window_prev_valid;
    int8_t window_prev_playing[RT_GAME3D_HITBOX_MAX_WINDOWS];
    /// Victims already hit during the current activation (one hit per swing).
    struct rt_game3d_entity *hit_victims[RT_GAME3D_HITBOX_MAX_VICTIMS];
    int32_t hit_victim_count;
} rt_game3d_hitbox;

/// @brief Health3D payload: owner backref (plain pointer cleared at entity
///   teardown), hit points, i-frame state, and one-shot damage/death flags.
typedef struct rt_game3d_health {
    struct rt_game3d_entity *entity; ///< Owner; NULLed when the entity tears down.
    double max_hp;
    double hp;
    double invuln_seconds;   ///< I-frame duration granted per applied damage.
    double invuln_remaining; ///< Ticked down by the world combat pass.
    int8_t dead;             ///< Latched at hp <= 0 until Revive.
    int8_t just_died;        ///< One-shot flag, cleared next combat pass.
    int8_t just_damaged;     ///< One-shot flag, cleared next combat pass.
    double last_damage;      ///< Most recent applied amount.
    int64_t last_tag;        ///< Caller-supplied damage tag.
} rt_game3d_health;

/// @brief One buffered hit event: retained handles released when the buffer clears.
typedef struct rt_game3d_hit_event_rec {
    void *attacker; ///< Retained Entity3D.
    void *victim;   ///< Retained Entity3D.
    void *hitbox;   ///< Retained attacking Hitbox3D.
    void *hurtbox;  ///< Retained victim Hitbox3D.
    double point[3];
    double normal[3];
} rt_game3d_hit_event_rec;

/// @brief One buffered damage event: retained handles released when the buffer clears.
typedef struct rt_game3d_damage_event_rec {
    void *victim; ///< Retained Entity3D.
    void *source; ///< Retained Entity3D or NULL.
    double amount;
    int64_t tag;
    int8_t was_lethal;
} rt_game3d_damage_event_rec;

/// @brief Boxed HitEvent3D handle returned by World3D.hitEvent (fail-closed).
typedef struct rt_game3d_hit_event {
    void *attacker;
    void *victim;
    void *hitbox;
    void *hurtbox;
    double point[3];
    double normal[3];
} rt_game3d_hit_event;

/// @brief Boxed DamageEvent3D handle returned by World3D.damageEvent (fail-closed).
typedef struct rt_game3d_damage_event {
    void *victim;
    void *source;
    double amount;
    int64_t tag;
    int8_t was_lethal;
} rt_game3d_damage_event;

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
    double time_scale;        /* world time multiplier, clamped [0, 4], default 1 */
    int8_t paused;            /* latched pause: effective scale 0 while set */
    double hitstop_remaining; /* one-shot freeze, decays by REAL (unscaled) dt */
    double unscaled_dt;       /* real clamped frame step (UI/menus) */
    double unscaled_elapsed;  /* real elapsed seconds (UI/menus) */
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
    void *active_timeline;               /* retained Timeline3D playing in this world, or NULL */
    void *active_dialogue;               /* retained Dialogue3D shown in this world, or NULL */
    rt_game3d_hit_event_rec *hit_events; /* combat-pass hit buffer, cleared each step */
    int32_t hit_event_count;
    int32_t hit_event_capacity;
    void *combat_scratch; /* lazily allocated per-world combat volume scratch (combat.c) */
    rt_game3d_damage_event_rec *damage_events; /* damage buffer, cleared each step */
    int32_t damage_event_count;
    int32_t damage_event_capacity;
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
    /* Opt-in fixed-step render interpolation: entity node poses are blended between
     * the previous and current fixed steps by fixed_interpolation_alpha during render,
     * then restored — 60 Hz physics stays smooth on 120/144 Hz displays. */
    int8_t render_interpolation;
    void **cloths;                             /* retained Cloth3D handles ticked per step */
    int32_t cloth_count;                       /* number of registered cloths */
    int32_t cloth_capacity;                    /* cloth array capacity */
    rt_game3d_persist_record *persist_records; /* keyed entity-state deltas (plan 17) */
    int32_t persist_count;                     /* number of records */
    int32_t persist_capacity;                  /* record array capacity */
    rt_game3d_hitch_entry hitches[RT_GAME3D_MAX_HITCHES]; /* hitch ring (plan 30) */
    int32_t hitch_count;               /* live entries (<= RT_GAME3D_MAX_HITCHES) */
    int32_t hitch_head;                /* oldest entry index once the ring wraps */
    double hitch_threshold_ms;         /* FrameTotal threshold (default 25) */
    double hitch_last_stream_stall_ms; /* stream stall watermark at last step */
    /* Monotonic stamp for despawn-safe entity sweeps (see
     * game3d_world_sweep_entities): each sweep bumps it; entities record the
     * stamp when ticked so swap-remove compaction can neither double-tick a
     * survivor nor skip one moved into an already-visited slot. Appended at
     * the end: test fixtures mirror prefixes of this layout. */
    uint32_t sim_tick_stamp;
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

/// @brief Validate `obj` as a ThirdPersonController handle, trapping `method` on mismatch.
static inline rt_game3d_thirdperson_controller *game3d_thirdperson_controller_checked(
    void *obj, const char *method) {
    rt_game3d_thirdperson_controller *controller =
        (rt_game3d_thirdperson_controller *)rt_g3d_checked_or_null(
            obj, RT_G3D_GAME3D_THIRDPERSON_CLASS_ID);
    if (!controller)
        rt_trap(method);
    return controller;
}

/// @brief Validate `obj` as a LipSync3D handle, trapping `method` on mismatch.
static inline rt_game3d_lipsync *game3d_lipsync_checked(void *obj, const char *method) {
    rt_game3d_lipsync *lipsync =
        (rt_game3d_lipsync *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_LIPSYNC_CLASS_ID);
    if (!lipsync)
        rt_trap(method);
    return lipsync;
}

/// @brief Validate `obj` as a Dialogue3D handle, trapping `method` on mismatch.
static inline rt_game3d_dialogue *game3d_dialogue_checked(void *obj, const char *method) {
    rt_game3d_dialogue *dialogue =
        (rt_game3d_dialogue *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_DIALOGUE_CLASS_ID);
    if (!dialogue)
        rt_trap(method);
    return dialogue;
}

/// @brief Validate `obj` as a Timeline3D handle, trapping `method` on mismatch.
static inline rt_game3d_timeline *game3d_timeline_checked(void *obj, const char *method) {
    rt_game3d_timeline *timeline =
        (rt_game3d_timeline *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_TIMELINE_CLASS_ID);
    if (!timeline)
        rt_trap(method);
    return timeline;
}

/// @brief Validate `obj` as a RailCamera3D handle, trapping `method` on mismatch.
static inline rt_game3d_rail_camera *game3d_rail_camera_checked(void *obj, const char *method) {
    rt_game3d_rail_camera *rail =
        (rt_game3d_rail_camera *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_RAILCAMERA_CLASS_ID);
    if (!rail)
        rt_trap(method);
    return rail;
}

/// @brief Validate `obj` as a TargetLock3D handle, trapping `method` on mismatch.
static inline rt_game3d_targetlock *game3d_targetlock_checked(void *obj, const char *method) {
    rt_game3d_targetlock *lock =
        (rt_game3d_targetlock *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_TARGETLOCK_CLASS_ID);
    if (!lock)
        rt_trap(method);
    return lock;
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
/// @brief Trap unless @p controller is detached or bound to @p world.
int game3d_camera_controller_validate_world(void *controller,
                                            rt_game3d_world *world,
                                            const char *api_name);
/// @brief Trap unless the CharacterController3D's retained world matches @p world.
int game3d_character_controller_validate_world(rt_game3d_character_controller *controller,
                                               rt_game3d_world *world,
                                               const char *api_name);
/// @brief Trap when a spawned entity belongs to a different world than @p world.
int game3d_entity_validate_controller_world(rt_game3d_entity *entity,
                                            rt_game3d_world *world,
                                            const char *api_name);
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
void game3d_set_node_world_position(void *node, double world_pos[3]);
void game3d_set_node_world_rotation(void *node, void *world_quat);
/// @brief Shared planar character drive: integrate jump/gravity and move the wrapped
///   Character3D along an explicit XZ basis (already normalized), then sync the entity.
/// @details Used by CharacterController3D.update (camera-derived basis) and the
///   third-person controller (yaw-derived basis) so vertical-velocity state lives in
///   exactly one place.
void game3d_character_controller_drive(rt_game3d_character_controller *controller,
                                       void *input_obj,
                                       double fx,
                                       double fz,
                                       double rx,
                                       double rz,
                                       double dt);
/// @brief Restore all faded occluder materials and drop the fade bookkeeping array.
void game3d_thirdperson_reset_fades(rt_game3d_thirdperson_controller *controller);
/// @brief Combat pass (rt_game3d_combat.c): clears one-shot health flags and event
///   buffers, ticks i-frames, then overlaps live hit volumes against hurt volumes
///   and emits HitEvent3D records. Runs after animation + scene sync each step.
void game3d_world_update_combat(rt_game3d_world *world, double dt);
/// @brief Release the world's buffered hit/damage event records (world teardown).
void game3d_world_clear_combat_events(rt_game3d_world *world);
/// @brief Release an entity's hitbox array and health slot, clearing backrefs
///   first so surviving handles fail closed (entity despawn/teardown path).
void game3d_entity_release_combat_slots(rt_game3d_entity *entity);
/// @brief Timeline pre-physics tick: advance the playhead and fire point tracks
///   (anim/audio/markers). Returns 1 while camera tracks suspend the controller.
int game3d_world_timeline_pre(rt_game3d_world *world, double dt);
/// @brief Timeline camera application (the suspended controller's late slot).
void game3d_world_timeline_camera(rt_game3d_world *world);
/// @brief Timeline overlay pass: letterbox bars, fade quad, active subtitle.
void game3d_world_timeline_overlay(rt_game3d_world *world);
/// @brief Dialogue tick: typewriter reveal + auto-advance (scaled dt).
void game3d_world_dialogue_tick(rt_game3d_world *world, double dt);
/// @brief Dialogue overlay pass: panel/bubble, speaker name, choices.
void game3d_world_dialogue_overlay(rt_game3d_world *world);
/// @brief Facial tick (lip sync envelope + blink + gaze), after ragdoll sync.
void game3d_world_facial_tick(rt_game3d_world *world, double dt);
void game3d_footsteps_tick(rt_game3d_world *world, rt_game3d_entity *entity, double dt);
void game3d_interactor_tick(rt_game3d_world *world, rt_game3d_entity *owner, double dt);
void game3d_ai_tick(rt_game3d_world *world, rt_game3d_entity *entity, double dt);
/// @brief Append a damage event record to the world buffer (Health3D.damage).
void game3d_world_push_damage_event(rt_game3d_world *world,
                                    rt_game3d_entity *victim,
                                    rt_game3d_entity *source,
                                    double amount,
                                    int64_t tag,
                                    int8_t was_lethal);
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
void game3d_audio_rebase_origin(rt_game3d_audio *audio, const double delta[3]);
void game3d_audio_immersion_tick(struct rt_game3d_world *world, double dt);
void game3d_cloth_tick(struct rt_game3d_world *world, double dt);
void game3d_persistence_tick(struct rt_game3d_world *world);
void game3d_persistence_on_despawn(struct rt_game3d_world *world, struct rt_game3d_entity *entity);
void game3d_persistence_release(struct rt_game3d_world *world);
void game3d_stream_push_loaded_event(struct rt_game3d_world_stream *stream, rt_string cell_name);
void game3d_stream_persistence_release(struct rt_game3d_world_stream *stream);
void game3d_world_note_hitches(struct rt_game3d_world *world, double step_wall_ms);
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
