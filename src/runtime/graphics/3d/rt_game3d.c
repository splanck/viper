//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
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
#include "rt_audio3d.h"
#include "rt_audiolistener3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_collider3d.h"
#include "rt_input.h"
#include "rt_mat4.h"
#include "rt_model3d.h"
#include "rt_object.h"
#include "rt_physics3d.h"
#include "rt_postfx3d.h"
#include "rt_quat.h"
#include "rt_scene3d.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec2.h"
#include "rt_vec3.h"
#include "rt_graphics3d_ids.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#include <dlfcn.h>
#endif

#define RT_GAME3D_DEFAULT_FOV_DEG 60.0
#define RT_GAME3D_DEFAULT_NEAR 0.1
#define RT_GAME3D_DEFAULT_FAR 1000.0
#define RT_GAME3D_DEFAULT_DT (1.0 / 60.0)
#define RT_GAME3D_MAX_DT 0.25
#define RT_GAME3D_DEFAULT_MOVE_SPEED 6.0
#define RT_GAME3D_DEFAULT_LOOK_SENSITIVITY 0.01
#define RT_GAME3D_DEFAULT_JUMP_SPEED 5.5
#define RT_GAME3D_DEFAULT_GRAVITY -20.0
#define RT_GAME3D_DEFAULT_FOLLOW_DAMPING 12.0
#define RT_GAME3D_PI 3.14159265358979323846
#define RT_GAME3D_ANIM_EVENT_MAX 64

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
void *rt_camera3d_get_forward(void *obj);
void *rt_camera3d_get_right(void *obj);
void rt_camera3d_set_position(void *obj, void *pos);

typedef struct rt_game3d_layermask {
    int64_t bits;
} rt_game3d_layermask;

typedef struct rt_game3d_input {
    double look_sensitivity;
} rt_game3d_input;

typedef struct rt_game3d_entity {
    int64_t id;
    void *node;
    void *mesh;
    void *material;
    void *body;
    void *anim;
    int64_t layer;
    int64_t collision_mask_bits;
    rt_string name;
    void *world;
    struct rt_game3d_entity **children;
    int32_t child_count;
    int32_t child_capacity;
    int8_t group;
    int8_t spawned;
    int8_t destroyed;
} rt_game3d_entity;

typedef struct rt_game3d_audio {
    void *listener;
} rt_game3d_audio;

typedef struct rt_game3d_effects {
    void *postfx;
} rt_game3d_effects;

typedef struct rt_game3d_env_handle {
    void *world;
    void *terrain_entity;
    void *water_entity;
} rt_game3d_env_handle;

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

typedef struct rt_game3d_collision_event {
    int64_t phase;
    void *a;
    void *b;
    void *raw;
} rt_game3d_collision_event;

typedef struct rt_game3d_animator {
    void *controller;
    rt_string events[RT_GAME3D_ANIM_EVENT_MAX];
    int32_t event_count;
} rt_game3d_animator;

typedef struct rt_game3d_model_template {
    rt_string path;
    int8_t asset_path;
    void *model;
} rt_game3d_model_template;

typedef struct rt_game3d_model_cache_entry {
    rt_string path;
    int8_t asset_path;
    void *model_template;
} rt_game3d_model_cache_entry;

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

typedef struct rt_game3d_first_person_controller {
    void *world;
    void *character_controller;
    double speed;
    double look_sensitivity;
    int8_t capture_mouse;
} rt_game3d_first_person_controller;

typedef struct rt_game3d_free_fly_controller {
    void *world;
    double speed;
    double look_sensitivity;
    int8_t capture_mouse;
} rt_game3d_free_fly_controller;

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

typedef struct rt_game3d_follow_controller {
    void *world;
    void *target_entity;
    void *offset;
    double damping;
} rt_game3d_follow_controller;

typedef struct rt_game3d_world {
    void *canvas;
    void *camera;
    void *scene;
    void *physics;
    void *input;
    void *audio;
    void *effects;
    void *camera_controller;
    rt_game3d_entity **entities;
    int32_t entity_count;
    int32_t entity_capacity;
    int64_t next_entity_id;
    double dt;
    double elapsed;
    int64_t frame;
    int64_t width;
    int64_t height;
    double clear_r;
    double clear_g;
    double clear_b;
    void *debug_axis_origin;
    double debug_axis_size;
    int8_t debug_overlay_enabled;
    int8_t debug_axes_enabled;
    int8_t debug_physics_enabled;
    int8_t debug_camera_enabled;
    int8_t debug_caps_enabled;
    int8_t destroyed;
} rt_game3d_world;

typedef void (*rt_game3d_update_fn)(double dt);
typedef void (*rt_game3d_overlay_fn)(void);

static rt_game3d_model_cache_entry *g_game3d_model_cache = NULL;
static int32_t g_game3d_model_cache_count = 0;
static int32_t g_game3d_model_cache_capacity = 0;

static int game3d_callback_pointer_is_native(void *callback) {
    if (!callback)
        return 1;
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(callback, &info, sizeof(info)) == 0)
        return 0;
    DWORD protect = info.Protect & 0xffu;
    return protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    Dl_info info;
    memset(&info, 0, sizeof(info));
    return dladdr(callback, &info) != 0 && info.dli_fbase != NULL;
#else
    return 1;
#endif
}

static rt_game3d_update_fn game3d_update_callback_checked(void *callback, const char *method) {
    if (!callback)
        return NULL;
    if (!game3d_callback_pointer_is_native(callback))
        rt_trap(method);
    return (rt_game3d_update_fn)callback;
}

static rt_game3d_overlay_fn game3d_overlay_callback_checked(void *callback, const char *method) {
    if (!callback)
        return NULL;
    if (!game3d_callback_pointer_is_native(callback))
        rt_trap(method);
    return (rt_game3d_overlay_fn)callback;
}

static void game3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    void *obj = *slot;
    *slot = NULL;
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void game3d_assign_ref(void **slot, void *value) {
    if (!slot || *slot == value)
        return;
    rt_obj_retain_maybe(value);
    game3d_release_ref(slot);
    *slot = value;
}

static int8_t game3d_valid_layer(int64_t layer) {
    return layer > 0 && (layer & (layer - 1)) == 0 ? 1 : 0;
}

static int64_t game3d_sanitize_mask_bits(int64_t bits) {
    return bits < 0 ? INT64_MAX : bits;
}

static double game3d_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

static double game3d_clamp_dt(double dt) {
    dt = game3d_finite_or(dt, RT_GAME3D_DEFAULT_DT);
    if (dt <= 0.0)
        return RT_GAME3D_DEFAULT_DT;
    if (dt > RT_GAME3D_MAX_DT)
        return RT_GAME3D_MAX_DT;
    return dt;
}

static double game3d_nonnegative_or(double value, double fallback) {
    value = game3d_finite_or(value, fallback);
    return value < 0.0 ? fallback : value;
}

static double game3d_clamp(double value, double lo, double hi) {
    value = game3d_finite_or(value, lo);
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

static double game3d_positive_or(double value, double fallback) {
    value = game3d_finite_or(value, fallback);
    return value > 0.0 ? value : fallback;
}

static void game3d_normalize_xz(double *x, double *z, double fallback_x, double fallback_z) {
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

static rt_game3d_layermask *game3d_layermask_checked(void *obj, const char *method) {
    rt_game3d_layermask *mask =
        (rt_game3d_layermask *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_LAYERMASK_CLASS_ID);
    if (!mask)
        rt_trap(method);
    return mask;
}

static rt_game3d_input *game3d_input_checked(void *obj, const char *method) {
    rt_game3d_input *input =
        (rt_game3d_input *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_INPUT_CLASS_ID);
    if (!input)
        rt_trap(method);
    return input;
}

static rt_game3d_entity *game3d_entity_checked_allow_destroyed(void *obj, const char *method) {
    rt_game3d_entity *entity =
        (rt_game3d_entity *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    if (!entity)
        rt_trap(method);
    return entity;
}

static rt_game3d_entity *game3d_entity_checked(void *obj, const char *method) {
    rt_game3d_entity *entity = game3d_entity_checked_allow_destroyed(obj, method);
    if (entity && entity->destroyed)
        rt_trap("Game3D.Entity3D: entity is destroyed");
    return entity;
}

static rt_game3d_audio *game3d_audio_checked(void *obj, const char *method) {
    rt_game3d_audio *audio =
        (rt_game3d_audio *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_AUDIO_CLASS_ID);
    if (!audio)
        rt_trap(method);
    return audio;
}

static rt_game3d_effects *game3d_effects_checked(void *obj, const char *method) {
    rt_game3d_effects *effects =
        (rt_game3d_effects *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_EFFECTS_CLASS_ID);
    if (!effects)
        rt_trap(method);
    return effects;
}

static rt_game3d_env_handle *game3d_env_handle_checked(void *obj, const char *method) {
    rt_game3d_env_handle *env =
        (rt_game3d_env_handle *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_ENV_HANDLE_CLASS_ID);
    if (!env)
        rt_trap(method);
    return env;
}

static rt_game3d_body_def *game3d_body_def_checked(void *obj, const char *method) {
    rt_game3d_body_def *def =
        (rt_game3d_body_def *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_BODYDEF_CLASS_ID);
    if (!def)
        rt_trap(method);
    return def;
}

static rt_game3d_collision_event *game3d_collision_event_checked(void *obj, const char *method) {
    rt_game3d_collision_event *event =
        (rt_game3d_collision_event *)rt_g3d_checked_or_null(
            obj, RT_G3D_GAME3D_COLLISION_EVENT_CLASS_ID);
    if (!event)
        rt_trap(method);
    return event;
}

static rt_game3d_animator *game3d_animator_checked(void *obj, const char *method) {
    rt_game3d_animator *animator =
        (rt_game3d_animator *)rt_g3d_checked_or_null(
            obj, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID);
    if (!animator)
        rt_trap(method);
    return animator;
}

static rt_game3d_model_template *game3d_model_template_checked(void *obj, const char *method) {
    rt_game3d_model_template *model_template =
        (rt_game3d_model_template *)rt_g3d_checked_or_null(
            obj, RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID);
    if (!model_template)
        rt_trap(method);
    return model_template;
}

static rt_game3d_world *game3d_world_checked_allow_destroyed(void *obj, const char *method) {
    rt_game3d_world *world =
        (rt_game3d_world *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_WORLD_CLASS_ID);
    if (!world)
        rt_trap(method);
    return world;
}

static rt_game3d_world *game3d_world_checked(void *obj, const char *method) {
    rt_game3d_world *world = game3d_world_checked_allow_destroyed(obj, method);
    if (world && world->destroyed)
        rt_trap("Game3D.World3D: world is destroyed");
    return world;
}

static rt_game3d_character_controller *game3d_character_controller_checked(
    void *obj, const char *method) {
    rt_game3d_character_controller *controller =
        (rt_game3d_character_controller *)rt_g3d_checked_or_null(
            obj, RT_G3D_GAME3D_CHARACTER_CONTROLLER_CLASS_ID);
    if (!controller)
        rt_trap(method);
    return controller;
}

static rt_game3d_first_person_controller *game3d_first_person_controller_checked(
    void *obj, const char *method) {
    rt_game3d_first_person_controller *controller =
        (rt_game3d_first_person_controller *)rt_g3d_checked_or_null(
            obj, RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID);
    if (!controller)
        rt_trap(method);
    return controller;
}

static rt_game3d_free_fly_controller *game3d_free_fly_controller_checked(
    void *obj, const char *method) {
    rt_game3d_free_fly_controller *controller =
        (rt_game3d_free_fly_controller *)rt_g3d_checked_or_null(
            obj, RT_G3D_GAME3D_FREEFLY_CLASS_ID);
    if (!controller)
        rt_trap(method);
    return controller;
}

static rt_game3d_orbit_controller *game3d_orbit_controller_checked(
    void *obj, const char *method) {
    rt_game3d_orbit_controller *controller =
        (rt_game3d_orbit_controller *)rt_g3d_checked_or_null(
            obj, RT_G3D_GAME3D_ORBIT_CLASS_ID);
    if (!controller)
        rt_trap(method);
    return controller;
}

static rt_game3d_follow_controller *game3d_follow_controller_checked(
    void *obj, const char *method) {
    rt_game3d_follow_controller *controller =
        (rt_game3d_follow_controller *)rt_g3d_checked_or_null(
            obj, RT_G3D_GAME3D_FOLLOW_CLASS_ID);
    if (!controller)
        rt_trap(method);
    return controller;
}

static void *game3d_layermask_new_bits(int64_t bits) {
    rt_game3d_layermask *mask =
        (rt_game3d_layermask *)rt_obj_new_i64(RT_G3D_GAME3D_LAYERMASK_CLASS_ID, (int64_t)sizeof(*mask));
    if (!mask) {
        rt_trap("Game3D.LayerMask.New: allocation failed");
        return NULL;
    }
    mask->bits = game3d_sanitize_mask_bits(bits);
    return mask;
}

int64_t rt_game3d_layers_world(void) { return RT_GAME3D_LAYER_WORLD; }
int64_t rt_game3d_layers_dynamic(void) { return RT_GAME3D_LAYER_DYNAMIC; }
int64_t rt_game3d_layers_player(void) { return RT_GAME3D_LAYER_PLAYER; }
int64_t rt_game3d_layers_trigger(void) { return RT_GAME3D_LAYER_TRIGGER; }
int64_t rt_game3d_layers_debris(void) { return RT_GAME3D_LAYER_DEBRIS; }

int64_t rt_game3d_body_shape_box(void) { return RT_GAME3D_BODY_SHAPE_BOX; }
int64_t rt_game3d_body_shape_sphere(void) { return RT_GAME3D_BODY_SHAPE_SPHERE; }
int64_t rt_game3d_body_shape_capsule(void) { return RT_GAME3D_BODY_SHAPE_CAPSULE; }

int64_t rt_game3d_sync_mode_node_from_body(void) { return RT_GAME3D_SYNC_NODE_FROM_BODY; }
int64_t rt_game3d_sync_mode_body_from_node(void) { return RT_GAME3D_SYNC_BODY_FROM_NODE; }
int64_t rt_game3d_sync_mode_node_from_anim_root_motion(void) { return RT_GAME3D_SYNC_NODE_FROM_ANIM_ROOT_MOTION; }
int64_t rt_game3d_sync_mode_two_way_kinematic(void) { return RT_GAME3D_SYNC_TWO_WAY_KINEMATIC; }

int64_t rt_game3d_alpha_mode_opaque(void) { return RT_GAME3D_ALPHA_OPAQUE; }
int64_t rt_game3d_alpha_mode_mask(void) { return RT_GAME3D_ALPHA_MASK; }
int64_t rt_game3d_alpha_mode_blend(void) { return RT_GAME3D_ALPHA_BLEND; }

int64_t rt_game3d_shading_model_phong(void) { return RT_GAME3D_SHADING_PHONG; }
int64_t rt_game3d_shading_model_toon(void) { return RT_GAME3D_SHADING_TOON; }
int64_t rt_game3d_shading_model_pbr(void) { return RT_GAME3D_SHADING_PBR; }
int64_t rt_game3d_shading_model_fresnel(void) { return RT_GAME3D_SHADING_FRESNEL; }
int64_t rt_game3d_shading_model_emissive(void) { return RT_GAME3D_SHADING_EMISSIVE; }
int64_t rt_game3d_shading_model_unlit(void) { return RT_GAME3D_SHADING_UNLIT; }

int64_t rt_game3d_quality_performance(void) { return RT_GAME3D_QUALITY_PERFORMANCE; }
int64_t rt_game3d_quality_balanced(void) { return RT_GAME3D_QUALITY_BALANCED; }
int64_t rt_game3d_quality_cinematic(void) { return RT_GAME3D_QUALITY_CINEMATIC; }

int64_t rt_game3d_collision_enter(void) { return RT_GAME3D_COLLISION_ENTER; }
int64_t rt_game3d_collision_stay(void) { return RT_GAME3D_COLLISION_STAY; }
int64_t rt_game3d_collision_exit(void) { return RT_GAME3D_COLLISION_EXIT; }
int64_t rt_game3d_collision_any(void) { return RT_GAME3D_COLLISION_ANY; }

int64_t rt_game3d_key_w(void) { return rt_keyboard_key_w(); }
int64_t rt_game3d_key_a(void) { return rt_keyboard_key_a(); }
int64_t rt_game3d_key_s(void) { return rt_keyboard_key_s(); }
int64_t rt_game3d_key_d(void) { return rt_keyboard_key_d(); }
int64_t rt_game3d_key_space(void) { return rt_keyboard_key_space(); }
int64_t rt_game3d_key_escape(void) { return rt_keyboard_key_escape(); }
int64_t rt_game3d_key_shift(void) { return rt_keyboard_key_shift(); }
int64_t rt_game3d_key_ctrl(void) { return rt_keyboard_key_ctrl(); }
int64_t rt_game3d_key_up(void) { return rt_keyboard_key_up(); }
int64_t rt_game3d_key_down(void) { return rt_keyboard_key_down(); }
int64_t rt_game3d_key_left(void) { return rt_keyboard_key_left(); }
int64_t rt_game3d_key_right(void) { return rt_keyboard_key_right(); }

int64_t rt_game3d_mouse_left(void) { return rt_mouse_button_left(); }
int64_t rt_game3d_mouse_right(void) { return rt_mouse_button_right(); }
int64_t rt_game3d_mouse_middle(void) { return rt_mouse_button_middle(); }
int64_t rt_game3d_mouse_x1(void) { return rt_mouse_button_x1(); }
int64_t rt_game3d_mouse_x2(void) { return rt_mouse_button_x2(); }

void *rt_game3d_layermask_none(void) { return game3d_layermask_new_bits(0); }
void *rt_game3d_layermask_all(void) { return game3d_layermask_new_bits(INT64_MAX); }

void *rt_game3d_layermask_of(int64_t layer) {
    if (!game3d_valid_layer(layer))
        rt_trap("Game3D.LayerMask.Of: layer must be a single positive bit");
    return game3d_layermask_new_bits(layer);
}

int64_t rt_game3d_layermask_get_bits(void *obj) {
    rt_game3d_layermask *mask =
        game3d_layermask_checked(obj, "Game3D.LayerMask.get_Bits: invalid mask");
    return mask ? mask->bits : 0;
}

void rt_game3d_layermask_set_bits(void *obj, int64_t bits) {
    rt_game3d_layermask *mask =
        game3d_layermask_checked(obj, "Game3D.LayerMask.set_Bits: invalid mask");
    if (mask)
        mask->bits = game3d_sanitize_mask_bits(bits);
}

void *rt_game3d_layermask_include(void *obj, int64_t layer) {
    rt_game3d_layermask *mask =
        game3d_layermask_checked(obj, "Game3D.LayerMask.include: invalid mask");
    if (!game3d_valid_layer(layer))
        rt_trap("Game3D.LayerMask.include: layer must be a single positive bit");
    if (mask)
        mask->bits |= layer;
    return obj;
}

int8_t rt_game3d_layermask_includes(void *obj, int64_t layer) {
    rt_game3d_layermask *mask =
        game3d_layermask_checked(obj, "Game3D.LayerMask.includes: invalid mask");
    if (!game3d_valid_layer(layer))
        return 0;
    return mask && (mask->bits & layer) != 0 ? 1 : 0;
}

static void game3d_body_def_defaults(rt_game3d_body_def *def) {
    if (!def)
        return;
    memset(def, 0, sizeof(*def));
    def->shape = RT_GAME3D_BODY_SHAPE_BOX;
    def->half_extents[0] = 0.5;
    def->half_extents[1] = 0.5;
    def->half_extents[2] = 0.5;
    def->radius = 0.5;
    def->height = 1.0;
    def->mass = 1.0;
    def->friction = 0.5;
    def->restitution = 0.3;
    def->layer = RT_GAME3D_LAYER_DYNAMIC;
    def->mask_bits = INT64_MAX;
    def->sync_mode = RT_GAME3D_SYNC_NODE_FROM_BODY;
}

static void *game3d_body_def_alloc(const char *method) {
    rt_game3d_body_def *def =
        (rt_game3d_body_def *)rt_obj_new_i64(RT_G3D_GAME3D_BODYDEF_CLASS_ID, (int64_t)sizeof(*def));
    if (!def) {
        rt_trap(method ? method : "Game3D.BodyDef: allocation failed");
        return NULL;
    }
    game3d_body_def_defaults(def);
    return def;
}

static int64_t game3d_valid_sync_or_default(int64_t sync_mode) {
    switch (sync_mode) {
    case RT_GAME3D_SYNC_NODE_FROM_BODY:
    case RT_GAME3D_SYNC_BODY_FROM_NODE:
    case RT_GAME3D_SYNC_NODE_FROM_ANIM_ROOT_MOTION:
    case RT_GAME3D_SYNC_TWO_WAY_KINEMATIC:
        return sync_mode;
    default:
        return RT_GAME3D_SYNC_NODE_FROM_BODY;
    }
}

static double game3d_bodydef_extent_or(double value, double fallback) {
    value = game3d_finite_or(value, fallback);
    return value > 0.0 ? value : fallback;
}

void *rt_game3d_body_def_box(double half_x, double half_y, double half_z, double mass) {
    rt_game3d_body_def *def =
        (rt_game3d_body_def *)game3d_body_def_alloc("Game3D.BodyDef.Box: allocation failed");
    if (!def)
        return NULL;
    def->shape = RT_GAME3D_BODY_SHAPE_BOX;
    def->half_extents[0] = game3d_bodydef_extent_or(half_x, 0.5);
    def->half_extents[1] = game3d_bodydef_extent_or(half_y, 0.5);
    def->half_extents[2] = game3d_bodydef_extent_or(half_z, 0.5);
    def->mass = game3d_nonnegative_or(mass, 1.0);
    def->is_static = def->mass <= 1e-12 ? 1 : 0;
    return def;
}

void *rt_game3d_body_def_sphere(double radius, double mass) {
    rt_game3d_body_def *def =
        (rt_game3d_body_def *)game3d_body_def_alloc("Game3D.BodyDef.Sphere: allocation failed");
    if (!def)
        return NULL;
    def->shape = RT_GAME3D_BODY_SHAPE_SPHERE;
    def->radius = game3d_bodydef_extent_or(radius, 0.5);
    def->mass = game3d_nonnegative_or(mass, 1.0);
    def->is_static = def->mass <= 1e-12 ? 1 : 0;
    return def;
}

void *rt_game3d_body_def_capsule(double radius, double height, double mass) {
    rt_game3d_body_def *def =
        (rt_game3d_body_def *)game3d_body_def_alloc("Game3D.BodyDef.Capsule: allocation failed");
    if (!def)
        return NULL;
    def->shape = RT_GAME3D_BODY_SHAPE_CAPSULE;
    def->radius = game3d_bodydef_extent_or(radius, 0.25);
    def->height = game3d_bodydef_extent_or(height, def->radius * 2.0);
    if (def->height < def->radius * 2.0)
        def->height = def->radius * 2.0;
    def->mass = game3d_nonnegative_or(mass, 1.0);
    def->is_static = def->mass <= 1e-12 ? 1 : 0;
    return def;
}

void *rt_game3d_body_def_static_box(double half_x, double half_y, double half_z) {
    rt_game3d_body_def *def =
        (rt_game3d_body_def *)rt_game3d_body_def_box(half_x, half_y, half_z, 0.0);
    if (!def)
        return NULL;
    def->is_static = 1;
    def->layer = RT_GAME3D_LAYER_WORLD;
    def->has_layer = 1;
    return def;
}

void *rt_game3d_body_def_static_plane(double size) {
    double half = game3d_bodydef_extent_or(size, 1.0) * 0.5;
    rt_game3d_body_def *def =
        (rt_game3d_body_def *)rt_game3d_body_def_static_box(half, 0.05, half);
    return def;
}

int64_t rt_game3d_body_def_get_shape(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.get_shape: invalid BodyDef");
    return def ? def->shape : RT_GAME3D_BODY_SHAPE_BOX;
}

void rt_game3d_body_def_set_shape(void *obj, int64_t shape) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.set_shape: invalid BodyDef");
    if (!def)
        return;
    switch (shape) {
    case RT_GAME3D_BODY_SHAPE_BOX:
    case RT_GAME3D_BODY_SHAPE_SPHERE:
    case RT_GAME3D_BODY_SHAPE_CAPSULE:
        def->shape = shape;
        break;
    default:
        rt_trap("Game3D.BodyDef.set_shape: invalid BodyShape");
        break;
    }
}

double rt_game3d_body_def_get_mass(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.get_mass: invalid BodyDef");
    return def ? def->mass : 0.0;
}

void rt_game3d_body_def_set_mass(void *obj, double mass) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.set_mass: invalid BodyDef");
    if (def) {
        def->mass = game3d_nonnegative_or(mass, def->mass);
        if (def->mass > 1e-12)
            def->is_static = 0;
    }
}

double rt_game3d_body_def_get_friction(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.get_friction: invalid BodyDef");
    return def ? def->friction : 0.0;
}

void rt_game3d_body_def_set_friction(void *obj, double friction) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.set_friction: invalid BodyDef");
    if (def)
        def->friction = game3d_nonnegative_or(friction, def->friction);
}

double rt_game3d_body_def_get_restitution(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.get_restitution: invalid BodyDef");
    return def ? def->restitution : 0.0;
}

void rt_game3d_body_def_set_restitution(void *obj, double restitution) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.set_restitution: invalid BodyDef");
    if (def)
        def->restitution = game3d_clamp(restitution, 0.0, 1.0);
}

int8_t rt_game3d_body_def_get_static(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.get_isStatic: invalid BodyDef");
    return def ? def->is_static : 0;
}

void rt_game3d_body_def_set_static(void *obj, int8_t is_static) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.set_isStatic: invalid BodyDef");
    if (def) {
        def->is_static = is_static ? 1 : 0;
        if (def->is_static)
            def->mass = 0.0;
    }
}

int8_t rt_game3d_body_def_get_kinematic(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.get_isKinematic: invalid BodyDef");
    return def ? def->is_kinematic : 0;
}

void rt_game3d_body_def_set_kinematic(void *obj, int8_t is_kinematic) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.set_isKinematic: invalid BodyDef");
    if (def) {
        def->is_kinematic = is_kinematic ? 1 : 0;
        if (def->is_kinematic)
            def->is_static = 0;
    }
}

int8_t rt_game3d_body_def_get_trigger(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.get_isTrigger: invalid BodyDef");
    return def ? def->is_trigger : 0;
}

void rt_game3d_body_def_set_trigger(void *obj, int8_t is_trigger) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.set_isTrigger: invalid BodyDef");
    if (def)
        def->is_trigger = is_trigger ? 1 : 0;
}

int8_t rt_game3d_body_def_get_use_ccd(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.get_useCCD: invalid BodyDef");
    return def ? def->use_ccd : 0;
}

void rt_game3d_body_def_set_use_ccd(void *obj, int8_t use_ccd) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.set_useCCD: invalid BodyDef");
    if (def)
        def->use_ccd = use_ccd ? 1 : 0;
}

int64_t rt_game3d_body_def_get_layer(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.get_layer: invalid BodyDef");
    return def ? def->layer : RT_GAME3D_LAYER_DYNAMIC;
}

void rt_game3d_body_def_set_layer_prop(void *obj, int64_t layer) {
    (void)rt_game3d_body_def_with_layer(obj, layer);
}

void *rt_game3d_body_def_get_mask(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.get_mask: invalid BodyDef");
    return def ? game3d_layermask_new_bits(def->mask_bits) : NULL;
}

void rt_game3d_body_def_set_mask_prop(void *obj, void *mask) {
    (void)rt_game3d_body_def_with_mask(obj, mask);
}

int64_t rt_game3d_body_def_get_sync_mode(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.get_syncMode: invalid BodyDef");
    return def ? def->sync_mode : RT_GAME3D_SYNC_NODE_FROM_BODY;
}

void rt_game3d_body_def_set_sync_mode_prop(void *obj, int64_t sync_mode) {
    (void)rt_game3d_body_def_with_sync(obj, sync_mode);
}

void *rt_game3d_body_def_with_layer(void *obj, int64_t layer) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.withLayer: invalid BodyDef");
    if (!game3d_valid_layer(layer))
        rt_trap("Game3D.BodyDef.withLayer: layer must be a single positive bit");
    if (def) {
        def->layer = layer;
        def->has_layer = 1;
    }
    return obj;
}

void *rt_game3d_body_def_with_mask(void *obj, void *mask_obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.withMask: invalid BodyDef");
    rt_game3d_layermask *mask =
        game3d_layermask_checked(mask_obj, "Game3D.BodyDef.withMask: invalid mask");
    if (def && mask) {
        def->mask_bits = mask->bits;
        def->has_mask = 1;
    }
    return obj;
}

void *rt_game3d_body_def_as_trigger(void *obj) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.asTrigger: invalid BodyDef");
    if (def)
        def->is_trigger = 1;
    return obj;
}

void *rt_game3d_body_def_with_sync(void *obj, int64_t sync_mode) {
    rt_game3d_body_def *def =
        game3d_body_def_checked(obj, "Game3D.BodyDef.withSync: invalid BodyDef");
    if (def)
        def->sync_mode = game3d_valid_sync_or_default(sync_mode);
    return obj;
}

static void *game3d_body_def_create_body(rt_game3d_body_def *def) {
    void *body = NULL;
    if (!def)
        return NULL;
    switch (def->shape) {
    case RT_GAME3D_BODY_SHAPE_SPHERE:
        body = rt_body3d_new_sphere(def->radius, def->is_static ? 0.0 : def->mass);
        break;
    case RT_GAME3D_BODY_SHAPE_CAPSULE:
        body = rt_body3d_new_capsule(def->radius, def->height, def->is_static ? 0.0 : def->mass);
        break;
    case RT_GAME3D_BODY_SHAPE_BOX:
    default:
        body = rt_body3d_new_aabb(
            def->half_extents[0], def->half_extents[1], def->half_extents[2],
            def->is_static ? 0.0 : def->mass);
        break;
    }
    if (!body)
        return NULL;
    rt_body3d_set_friction(body, def->friction);
    rt_body3d_set_restitution(body, def->restitution);
    if (def->is_static)
        rt_body3d_set_static(body, 1);
    else if (def->is_kinematic)
        rt_body3d_set_kinematic(body, 1);
    rt_body3d_set_trigger(body, def->is_trigger);
    rt_body3d_set_use_ccd(body, def->use_ccd);
    rt_body3d_set_collision_layer(body, def->layer);
    rt_body3d_set_collision_mask(body, def->mask_bits);
    return body;
}

void *rt_game3d_input_new(void) {
    rt_game3d_input *input =
        (rt_game3d_input *)rt_obj_new_i64(RT_G3D_GAME3D_INPUT_CLASS_ID, (int64_t)sizeof(*input));
    if (!input) {
        rt_trap("Game3D.Input3D.New: allocation failed");
        return NULL;
    }
    input->look_sensitivity = 0.01;
    return input;
}

double rt_game3d_input_get_look_sensitivity(void *obj) {
    rt_game3d_input *input =
        game3d_input_checked(obj, "Game3D.Input3D.get_LookSensitivity: invalid input");
    return input ? input->look_sensitivity : 0.0;
}

void rt_game3d_input_set_look_sensitivity(void *obj, double sensitivity) {
    rt_game3d_input *input =
        game3d_input_checked(obj, "Game3D.Input3D.set_LookSensitivity: invalid input");
    if (!input)
        return;
    input->look_sensitivity = game3d_finite_or(sensitivity, 0.01);
}

void rt_game3d_input_update(void *obj) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.update: invalid input");
}

int8_t rt_game3d_input_is_down(void *obj, int64_t key) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.isDown: invalid input");
    return rt_keyboard_is_down(key);
}

int8_t rt_game3d_input_pressed(void *obj, int64_t key) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.pressed: invalid input");
    return rt_keyboard_was_pressed(key);
}

int8_t rt_game3d_input_released(void *obj, int64_t key) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.released: invalid input");
    return rt_keyboard_was_released(key);
}

void *rt_game3d_input_mouse_delta(void *obj) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.mouseDelta: invalid input");
    return rt_vec2_new((double)rt_mouse_delta_x(), (double)rt_mouse_delta_y());
}

int8_t rt_game3d_input_mouse_button(void *obj, int64_t button) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.mouseButton: invalid input");
    return rt_mouse_is_down(button);
}

int8_t rt_game3d_input_mouse_pressed(void *obj, int64_t button) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.mousePressed: invalid input");
    return rt_mouse_was_pressed(button);
}

double rt_game3d_input_wheel_y(void *obj) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.wheelY: invalid input");
    return rt_mouse_wheel_yf();
}

void *rt_game3d_input_move_axis(void *obj) {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    (void)game3d_input_checked(obj, "Game3D.Input3D.moveAxis: invalid input");
    if (rt_keyboard_is_down(rt_keyboard_key_d()) || rt_keyboard_is_down(rt_keyboard_key_right()))
        x += 1.0;
    if (rt_keyboard_is_down(rt_keyboard_key_a()) || rt_keyboard_is_down(rt_keyboard_key_left()))
        x -= 1.0;
    if (rt_keyboard_is_down(rt_keyboard_key_w()) || rt_keyboard_is_down(rt_keyboard_key_up()))
        z += 1.0;
    if (rt_keyboard_is_down(rt_keyboard_key_s()) || rt_keyboard_is_down(rt_keyboard_key_down()))
        z -= 1.0;
    if (rt_keyboard_is_down(rt_keyboard_key_space()))
        y += 1.0;
    if (rt_keyboard_is_down(rt_keyboard_key_shift()) || rt_keyboard_is_down(rt_keyboard_key_ctrl()))
        y -= 1.0;
    return rt_vec3_new(x, y, z);
}

void *rt_game3d_input_look_axis(void *obj) {
    rt_game3d_input *input =
        game3d_input_checked(obj, "Game3D.Input3D.lookAxis: invalid input");
    double s = input ? input->look_sensitivity : 0.01;
    return rt_vec2_new((double)rt_mouse_delta_x() * s, (double)rt_mouse_delta_y() * s);
}

void rt_game3d_input_capture_mouse(void *obj) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.captureMouse: invalid input");
    rt_mouse_capture();
}

void rt_game3d_input_release_mouse(void *obj) {
    (void)game3d_input_checked(obj, "Game3D.Input3D.releaseMouse: invalid input");
    rt_mouse_release();
}

static int game3d_entity_grow_children(rt_game3d_entity *entity, int32_t need) {
    if (entity->child_capacity >= need)
        return 1;
    int32_t new_cap = entity->child_capacity > 0 ? entity->child_capacity * 2 : 4;
    if (new_cap < need)
        new_cap = need;
    rt_game3d_entity **grown =
        (rt_game3d_entity **)realloc(entity->children, (size_t)new_cap * sizeof(*grown));
    if (!grown)
        return 0;
    entity->children = grown;
    entity->child_capacity = new_cap;
    return 1;
}

static void game3d_entity_finalize(void *obj) {
    rt_game3d_entity *entity = (rt_game3d_entity *)obj;
    if (!entity)
        return;
    for (int32_t i = 0; i < entity->child_count; ++i)
        game3d_release_ref((void **)&entity->children[i]);
    free(entity->children);
    entity->children = NULL;
    entity->child_count = 0;
    entity->child_capacity = 0;
    game3d_release_ref(&entity->node);
    game3d_release_ref(&entity->mesh);
    game3d_release_ref(&entity->material);
    game3d_release_ref(&entity->body);
    game3d_release_ref(&entity->anim);
    game3d_release_ref((void **)&entity->name);
}

void *rt_game3d_entity_new(void) {
    rt_game3d_entity *entity =
        (rt_game3d_entity *)rt_obj_new_i64(RT_G3D_GAME3D_ENTITY_CLASS_ID, (int64_t)sizeof(*entity));
    if (!entity) {
        rt_trap("Game3D.Entity3D.New: allocation failed");
        return NULL;
    }
    memset(entity, 0, sizeof(*entity));
    rt_obj_set_finalizer(entity, game3d_entity_finalize);
    entity->node = rt_scene_node3d_new();
    if (!entity->node) {
        if (rt_obj_release_check0(entity))
            rt_obj_free(entity);
        rt_trap("Game3D.Entity3D.New: node allocation failed");
        return NULL;
    }
    entity->layer = RT_GAME3D_LAYER_DYNAMIC;
    entity->collision_mask_bits = INT64_MAX;
    entity->name = rt_const_cstr("");
    rt_obj_retain_maybe(entity->name);
    return entity;
}

void *rt_game3d_entity_of(void *mesh, void *material) {
    rt_game3d_entity *entity = (rt_game3d_entity *)rt_game3d_entity_new();
    if (!entity)
        return NULL;
    rt_game3d_entity_set_mesh(entity, mesh);
    rt_game3d_entity_set_material(entity, material);
    return entity;
}

void *rt_game3d_entity_from_node(void *root) {
    if (!rt_g3d_has_class(root, RT_G3D_SCENENODE3D_CLASS_ID))
        rt_trap("Game3D.Entity3D.FromNode: root must be a SceneNode3D");
    rt_game3d_entity *entity = (rt_game3d_entity *)rt_game3d_entity_new();
    if (!entity)
        return NULL;
    game3d_assign_ref(&entity->node, root);
    entity->group = 1;
    return entity;
}

int64_t rt_game3d_entity_get_id(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked_allow_destroyed(obj, "Game3D.Entity3D.get_Id: invalid entity");
    return entity ? entity->id : 0;
}

void *rt_game3d_entity_get_node(void *obj) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.get_Node: invalid entity");
    return entity ? entity->node : NULL;
}

void *rt_game3d_entity_get_mesh(void *obj) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.get_Mesh: invalid entity");
    return entity ? entity->mesh : NULL;
}

void rt_game3d_entity_set_mesh_prop(void *obj, void *mesh) {
    (void)rt_game3d_entity_set_mesh(obj, mesh);
}

void *rt_game3d_entity_get_material(void *obj) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.get_Material: invalid entity");
    return entity ? entity->material : NULL;
}

void rt_game3d_entity_set_material_prop(void *obj, void *material) {
    (void)rt_game3d_entity_set_material(obj, material);
}

void *rt_game3d_entity_get_body(void *obj) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.get_Body: invalid entity");
    return entity ? entity->body : NULL;
}

void *rt_game3d_entity_get_anim(void *obj) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.get_Anim: invalid entity");
    return entity ? entity->anim : NULL;
}

int64_t rt_game3d_entity_get_layer(void *obj) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.get_Layer: invalid entity");
    return entity ? entity->layer : 0;
}

void rt_game3d_entity_set_layer_prop(void *obj, int64_t layer) {
    (void)rt_game3d_entity_set_layer(obj, layer);
}

void *rt_game3d_entity_get_collision_mask(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.get_CollisionMask: invalid entity");
    return entity ? game3d_layermask_new_bits(entity->collision_mask_bits) : NULL;
}

void rt_game3d_entity_set_collision_mask_prop(void *obj, void *mask) {
    (void)rt_game3d_entity_set_collision_mask(obj, mask);
}

rt_string rt_game3d_entity_get_name(void *obj) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.get_Name: invalid entity");
    if (!entity || !entity->name)
        return rt_const_cstr("");
    return entity->name;
}

void rt_game3d_entity_set_name_prop(void *obj, rt_string name) {
    (void)rt_game3d_entity_set_name(obj, name);
}

void *rt_game3d_entity_set_position(void *obj, double x, double y, double z) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.setPosition: invalid entity");
    if (entity && entity->node)
        rt_scene_node3d_set_position(entity->node, x, y, z);
    if (entity && entity->body)
        rt_body3d_set_position(entity->body, x, y, z);
    return obj;
}

void *rt_game3d_entity_set_position_v(void *obj, void *position) {
    if (!rt_g3d_is_vec3(position))
        rt_trap("Game3D.Entity3D.setPositionV: position must be Vec3");
    return rt_game3d_entity_set_position(
        obj, rt_vec3_x(position), rt_vec3_y(position), rt_vec3_z(position));
}

void *rt_game3d_entity_set_scale(void *obj, double scale) {
    return rt_game3d_entity_set_scale_xyz(obj, scale, scale, scale);
}

void *rt_game3d_entity_set_scale_xyz(void *obj, double x, double y, double z) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.setScaleXYZ: invalid entity");
    if (entity && entity->node)
        rt_scene_node3d_set_scale(entity->node, x, y, z);
    return obj;
}

void *rt_game3d_entity_set_rotation_euler(void *obj, double x_deg, double y_deg, double z_deg) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setRotationEuler: invalid entity");
    if (entity && entity->node) {
        const double deg = 3.14159265358979323846 / 180.0;
        void *quat = rt_quat_from_euler(x_deg * deg, y_deg * deg, z_deg * deg);
        rt_scene_node3d_set_rotation(entity->node, quat);
        game3d_release_ref(&quat);
    }
    return obj;
}

void *rt_game3d_entity_set_mesh(void *obj, void *mesh) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.setMesh: invalid entity");
    if (mesh && !rt_g3d_has_class(mesh, RT_G3D_MESH3D_CLASS_ID))
        rt_trap("Game3D.Entity3D.setMesh: mesh must be Mesh3D");
    if (entity) {
        game3d_assign_ref(&entity->mesh, mesh);
        if (entity->node)
            rt_scene_node3d_set_mesh(entity->node, mesh);
    }
    return obj;
}

void *rt_game3d_entity_set_material(void *obj, void *material) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.setMaterial: invalid entity");
    if (material && !rt_g3d_has_class(material, RT_G3D_MATERIAL3D_CLASS_ID))
        rt_trap("Game3D.Entity3D.setMaterial: material must be Material3D");
    if (entity) {
        game3d_assign_ref(&entity->material, material);
        if (entity->node)
            rt_scene_node3d_set_material(entity->node, material);
    }
    return obj;
}

void *rt_game3d_entity_add_child(void *obj, void *child_obj) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.addChild: invalid entity");
    rt_game3d_entity *child =
        game3d_entity_checked(child_obj, "Game3D.Entity3D.addChild: child must be Entity3D");
    if (!entity || !child)
        return obj;
    if (!game3d_entity_grow_children(entity, entity->child_count + 1))
        rt_trap("Game3D.Entity3D.addChild: allocation failed");
    rt_obj_retain_maybe(child);
    entity->children[entity->child_count++] = child;
    if (entity->node && child->node)
        rt_scene_node3d_add_child(entity->node, child->node);
    entity->group = 1;
    return obj;
}

int8_t rt_game3d_entity_is_group(void *obj) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.isGroup: invalid entity");
    return entity && (entity->group || entity->child_count > 0) ? 1 : 0;
}

void *rt_game3d_entity_set_name(void *obj, rt_string name) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.setName: invalid entity");
    if (!name)
        name = rt_const_cstr("");
    if (entity) {
        game3d_assign_ref((void **)&entity->name, name);
        if (entity->node)
            rt_scene_node3d_set_name(entity->node, name);
    }
    return obj;
}

void *rt_game3d_entity_set_layer(void *obj, int64_t layer) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.setLayer: invalid entity");
    if (!game3d_valid_layer(layer))
        rt_trap("Game3D.Entity3D.setLayer: layer must be a single positive bit");
    if (entity) {
        entity->layer = layer;
        if (entity->body)
            rt_body3d_set_collision_layer(entity->body, layer);
    }
    return obj;
}

void *rt_game3d_entity_set_collision_mask(void *obj, void *mask_obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setCollisionMask: invalid entity");
    rt_game3d_layermask *mask =
        game3d_layermask_checked(mask_obj, "Game3D.Entity3D.setCollisionMask: invalid mask");
    if (entity && mask) {
        entity->collision_mask_bits = mask->bits;
        if (entity->body)
            rt_body3d_set_collision_mask(entity->body, entity->collision_mask_bits);
    }
    return obj;
}

void *rt_game3d_entity_attach_body(void *obj, void *body_or_def) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.attachBody: invalid entity");
    void *body = body_or_def;
    void *created_body = NULL;
    rt_game3d_body_def *def =
        (rt_game3d_body_def *)rt_g3d_checked_or_null(body_or_def, RT_G3D_GAME3D_BODYDEF_CLASS_ID);
    if (def) {
        created_body = game3d_body_def_create_body(def);
        body = created_body;
    } else if (body && !rt_g3d_has_class(body, RT_G3D_BODY3D_CLASS_ID)) {
        rt_trap("Game3D.Entity3D.attachBody: expected Physics3DBody or BodyDef");
    }
    if (entity) {
        rt_game3d_world *world = (rt_game3d_world *)entity->world;
        if (entity->body && entity->body != body && entity->spawned && world && world->physics)
            rt_world3d_remove(world->physics, entity->body);
        if (def && def->has_layer)
            entity->layer = def->layer;
        if (def && def->has_mask)
            entity->collision_mask_bits = def->mask_bits;
        game3d_assign_ref(&entity->body, body);
        if (body) {
            rt_body3d_set_collision_layer(body, entity->layer);
            rt_body3d_set_collision_mask(body, entity->collision_mask_bits);
            if (entity->node) {
                void *pos = rt_scene_node3d_get_world_position(entity->node);
                rt_body3d_set_position(
                    body,
                    pos ? rt_vec3_x(pos) : 0.0,
                    pos ? rt_vec3_y(pos) : 0.0,
                    pos ? rt_vec3_z(pos) : 0.0);
                game3d_release_ref(&pos);
                if (def)
                    rt_scene_node3d_set_sync_mode(entity->node, def->sync_mode);
                rt_scene_node3d_bind_body(entity->node, body);
            }
            if (entity->spawned && world && world->physics)
                rt_world3d_add(world->physics, body);
        } else if (entity->node) {
            rt_scene_node3d_clear_body_binding(entity->node);
        }
    }
    game3d_release_ref(&created_body);
    return obj;
}

void rt_game3d_entity_apply_impulse(void *obj, double x, double y, double z) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.applyImpulse: invalid entity");
    if (!entity || !entity->body)
        rt_trap("Game3D.Entity3D.applyImpulse: entity has no body");
    rt_body3d_apply_impulse(entity->body, x, y, z);
}

void rt_game3d_entity_set_velocity(void *obj, double x, double y, double z) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.setVelocity: invalid entity");
    if (!entity || !entity->body)
        rt_trap("Game3D.Entity3D.setVelocity: entity has no body");
    rt_body3d_set_velocity(entity->body, x, y, z);
}

void *rt_game3d_entity_position(void *obj) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.position: invalid entity");
    return entity && entity->node ? rt_scene_node3d_get_position(entity->node) : rt_vec3_new(0, 0, 0);
}

void *rt_game3d_entity_world_position(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.worldPosition: invalid entity");
    return entity && entity->node ? rt_scene_node3d_get_world_position(entity->node)
                                  : rt_vec3_new(0, 0, 0);
}

int8_t rt_game3d_entity_is_spawned(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked_allow_destroyed(obj, "Game3D.Entity3D.isSpawned: invalid entity");
    return entity && entity->spawned ? 1 : 0;
}

int8_t rt_game3d_entity_is_destroyed(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked_allow_destroyed(obj, "Game3D.Entity3D.isDestroyed: invalid entity");
    return entity && entity->destroyed ? 1 : 0;
}

static void game3d_animator_clear_events(rt_game3d_animator *animator) {
    if (!animator)
        return;
    for (int32_t i = 0; i < animator->event_count; ++i)
        game3d_release_ref((void **)&animator->events[i]);
    animator->event_count = 0;
}

static void game3d_animator_drain_events(rt_game3d_animator *animator) {
    if (!animator || !animator->controller)
        return;
    while (animator->event_count < RT_GAME3D_ANIM_EVENT_MAX) {
        rt_string event_name = rt_anim_controller3d_poll_event(animator->controller);
        const char *name = event_name ? rt_string_cstr(event_name) : "";
        if (!name || name[0] == '\0') {
            game3d_release_ref((void **)&event_name);
            break;
        }
        game3d_assign_ref((void **)&animator->events[animator->event_count++], event_name);
        game3d_release_ref((void **)&event_name);
    }
}

static void game3d_animator_finalize(void *obj) {
    rt_game3d_animator *animator = (rt_game3d_animator *)obj;
    if (!animator)
        return;
    game3d_animator_clear_events(animator);
    game3d_release_ref(&animator->controller);
}

void *rt_game3d_animator_new(void *controller) {
    rt_game3d_animator *animator;
    if (!rt_g3d_has_class(controller, RT_G3D_ANIMCONTROLLER3D_CLASS_ID)) {
        rt_trap("Game3D.Animator3D.New: controller must be AnimController3D");
        return NULL;
    }
    animator = (rt_game3d_animator *)rt_obj_new_i64(
        RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID, (int64_t)sizeof(*animator));
    if (!animator) {
        rt_trap("Game3D.Animator3D.New: allocation failed");
        return NULL;
    }
    memset(animator, 0, sizeof(*animator));
    rt_obj_set_finalizer(animator, game3d_animator_finalize);
    game3d_assign_ref(&animator->controller, controller);
    return animator;
}

void *rt_game3d_animator_get_controller(void *obj) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.get_controller: invalid animator");
    return animator ? animator->controller : NULL;
}

int8_t rt_game3d_animator_play(void *obj, rt_string name) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.play: invalid animator");
    int8_t ok;
    if (!animator || !animator->controller)
        return 0;
    game3d_animator_clear_events(animator);
    ok = rt_anim_controller3d_play(animator->controller, name);
    if (ok)
        game3d_animator_drain_events(animator);
    return ok;
}

int8_t rt_game3d_animator_crossfade(void *obj, rt_string name, double seconds) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.crossfade: invalid animator");
    int8_t ok;
    if (!animator || !animator->controller)
        return 0;
    game3d_animator_clear_events(animator);
    ok = rt_anim_controller3d_crossfade(animator->controller, name, seconds);
    if (ok)
        game3d_animator_drain_events(animator);
    return ok;
}

void rt_game3d_animator_set_speed(void *obj, rt_string name, double speed) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.setSpeed: invalid animator");
    if (animator && animator->controller)
        rt_anim_controller3d_set_state_speed(animator->controller, name, speed);
}

int8_t rt_game3d_animator_is_playing(void *obj, rt_string name) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.isPlaying: invalid animator");
    return animator && animator->controller
               ? rt_anim_controller3d_is_state_playing(animator->controller, name)
               : 0;
}

double rt_game3d_animator_state_time(void *obj) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.stateTime: invalid animator");
    return animator && animator->controller
               ? rt_anim_controller3d_get_state_time(animator->controller)
               : 0.0;
}

int64_t rt_game3d_animator_event_count(void *obj) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.eventCount: invalid animator");
    return animator ? animator->event_count : 0;
}

rt_string rt_game3d_animator_event_name(void *obj, int64_t index) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.eventName: invalid animator");
    if (!animator || index < 0 || index >= animator->event_count)
        return rt_const_cstr("");
    return animator->events[index] ? animator->events[index] : rt_const_cstr("");
}

void rt_game3d_animator_update(void *obj, double dt) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.update: invalid animator");
    if (!animator || !animator->controller)
        return;
    if (!isfinite(dt) || dt < 0.0)
        dt = 0.0;
    game3d_animator_clear_events(animator);
    rt_anim_controller3d_update(animator->controller, dt);
    game3d_animator_drain_events(animator);
}

void *rt_game3d_entity_attach_animator(void *obj, void *animator_or_controller) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.attachAnimator: invalid entity");
    void *animator = animator_or_controller;
    void *created_animator = NULL;
    if (animator_or_controller &&
        rt_g3d_has_class(animator_or_controller, RT_G3D_ANIMCONTROLLER3D_CLASS_ID)) {
        created_animator = rt_game3d_animator_new(animator_or_controller);
        animator = created_animator;
    } else if (animator_or_controller &&
               !rt_g3d_has_class(animator_or_controller, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID)) {
        rt_trap("Game3D.Entity3D.attachAnimator: expected Animator3D or AnimController3D");
        return obj;
    }
    if (entity) {
        game3d_assign_ref(&entity->anim, animator);
        if (entity->node) {
            if (animator) {
                rt_game3d_animator *game_animator =
                    game3d_animator_checked(animator, "Game3D.Entity3D.attachAnimator: invalid animator");
                rt_scene_node3d_bind_animator(entity->node,
                                              game_animator ? game_animator->controller : NULL);
            } else {
                rt_scene_node3d_clear_animator_binding(entity->node);
            }
        }
    }
    game3d_release_ref(&created_animator);
    return obj;
}

static void game3d_audio_finalize(void *obj) {
    rt_game3d_audio *audio = (rt_game3d_audio *)obj;
    if (audio)
        game3d_release_ref(&audio->listener);
}

static void *game3d_audio_new(void *camera) {
    rt_game3d_audio *audio =
        (rt_game3d_audio *)rt_obj_new_i64(RT_G3D_GAME3D_AUDIO_CLASS_ID, (int64_t)sizeof(*audio));
    if (!audio) {
        rt_trap("Game3D.Audio3D.New: allocation failed");
        return NULL;
    }
    memset(audio, 0, sizeof(*audio));
    rt_obj_set_finalizer(audio, game3d_audio_finalize);
    audio->listener = rt_audiolistener3d_new();
    if (audio->listener && camera) {
        rt_audiolistener3d_bind_camera(audio->listener, camera);
        rt_audiolistener3d_set_is_active(audio->listener, 1);
    }
    return audio;
}

void *rt_game3d_audio_get_listener(void *obj) {
    rt_game3d_audio *audio = game3d_audio_checked(obj, "Game3D.Audio3D.get_Listener: invalid audio");
    return audio ? audio->listener : NULL;
}

static void game3d_effects_finalize(void *obj) {
    rt_game3d_effects *effects = (rt_game3d_effects *)obj;
    if (effects)
        game3d_release_ref(&effects->postfx);
}

static void *game3d_effects_new(void *canvas, int64_t quality) {
    rt_game3d_effects *effects =
        (rt_game3d_effects *)rt_obj_new_i64(RT_G3D_GAME3D_EFFECTS_CLASS_ID, (int64_t)sizeof(*effects));
    if (!effects) {
        rt_trap("Game3D.EffectRegistry3D.New: allocation failed");
        return NULL;
    }
    memset(effects, 0, sizeof(*effects));
    rt_obj_set_finalizer(effects, game3d_effects_finalize);
    effects->postfx = rt_postfx3d_new_quality(canvas, quality);
    if (effects->postfx && canvas)
        rt_canvas3d_set_post_fx(canvas, effects->postfx);
    return effects;
}

void *rt_game3d_effects_get_postfx(void *obj) {
    rt_game3d_effects *effects =
        game3d_effects_checked(obj, "Game3D.EffectRegistry3D.get_PostFX: invalid effects");
    return effects ? effects->postfx : NULL;
}

static void game3d_world_set_clear_color(
    rt_game3d_world *world, double r, double g, double b) {
    if (!world)
        return;
    world->clear_r = game3d_clamp(r, 0.0, 1.0);
    world->clear_g = game3d_clamp(g, 0.0, 1.0);
    world->clear_b = game3d_clamp(b, 0.0, 1.0);
}

static void game3d_world_assign_postfx(rt_game3d_world *world, void *postfx) {
    if (!world || !world->canvas)
        return;
    rt_game3d_effects *effects = (rt_game3d_effects *)world->effects;
    if (effects)
        game3d_assign_ref(&effects->postfx, postfx);
    rt_canvas3d_set_post_fx(world->canvas, postfx);
}

static void game3d_world_install_light(rt_game3d_world *world, int64_t slot, void *light) {
    if (!world || !world->canvas || !light)
        return;
    rt_canvas3d_set_light(world->canvas, slot, light);
}

void rt_game3d_lighting_clear(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Lighting.Clear: invalid world");
    if (!world || !world->canvas)
        return;
    rt_canvas3d_clear_lights(world->canvas);
    rt_canvas3d_set_ambient(world->canvas, 0.18, 0.18, 0.20);
}

void rt_game3d_lighting_studio(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Lighting.Studio: invalid world");
    if (!world || !world->canvas)
        return;
    rt_game3d_lighting_clear(world);
    rt_canvas3d_set_ambient(world->canvas, 0.30, 0.32, 0.36);
    game3d_world_set_clear_color(world, 0.055, 0.060, 0.070);

    void *key_dir = rt_vec3_new(-0.35, -0.85, -0.30);
    void *fill_dir = rt_vec3_new(0.75, -0.35, 0.40);
    void *key = rt_light3d_new_directional(key_dir, 1.0, 0.96, 0.88);
    void *fill = rt_light3d_new_directional(fill_dir, 0.55, 0.65, 1.0);
    if (key)
        rt_light3d_set_intensity(key, 1.35);
    if (fill)
        rt_light3d_set_intensity(fill, 0.35);
    game3d_world_install_light(world, 0, key);
    game3d_world_install_light(world, 1, fill);
    game3d_release_ref(&key);
    game3d_release_ref(&fill);
    game3d_release_ref(&key_dir);
    game3d_release_ref(&fill_dir);
}

void rt_game3d_lighting_outdoor(void *obj, void *sun_dir) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Lighting.Outdoor: invalid world");
    if (!world || !world->canvas)
        return;
    void *dir = sun_dir;
    int owns_dir = 0;
    if (!dir) {
        dir = rt_vec3_new(-0.45, -1.00, -0.22);
        owns_dir = 1;
    } else if (!rt_g3d_is_vec3(dir)) {
        rt_trap("Game3D.Lighting.Outdoor: sunDir must be Vec3");
    }

    rt_game3d_lighting_clear(world);
    rt_canvas3d_set_ambient(world->canvas, 0.38, 0.42, 0.46);
    game3d_world_set_clear_color(world, 0.50, 0.66, 0.86);
    void *sun = rt_light3d_new_directional(dir, 1.0, 0.94, 0.82);
    if (sun)
        rt_light3d_set_intensity(sun, 1.55);
    game3d_world_install_light(world, 0, sun);
    game3d_release_ref(&sun);
    if (owns_dir)
        game3d_release_ref(&dir);
}

void rt_game3d_lighting_night(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Lighting.Night: invalid world");
    if (!world || !world->canvas)
        return;
    rt_game3d_lighting_clear(world);
    rt_canvas3d_set_ambient(world->canvas, 0.045, 0.055, 0.095);
    game3d_world_set_clear_color(world, 0.015, 0.020, 0.040);
    void *moon_dir = rt_vec3_new(0.25, -1.0, 0.35);
    void *moon = rt_light3d_new_directional(moon_dir, 0.55, 0.68, 1.0);
    void *lamp_pos = rt_vec3_new(0.0, 4.0, 2.0);
    void *lamp = rt_light3d_new_point(lamp_pos, 0.55, 0.64, 1.0, 0.12);
    if (moon)
        rt_light3d_set_intensity(moon, 0.55);
    if (lamp)
        rt_light3d_set_intensity(lamp, 0.80);
    game3d_world_install_light(world, 0, moon);
    game3d_world_install_light(world, 1, lamp);
    game3d_release_ref(&moon);
    game3d_release_ref(&lamp);
    game3d_release_ref(&moon_dir);
    game3d_release_ref(&lamp_pos);
}

void rt_game3d_lighting_interior(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Lighting.Interior: invalid world");
    if (!world || !world->canvas)
        return;
    rt_game3d_lighting_clear(world);
    rt_canvas3d_set_ambient(world->canvas, 0.22, 0.20, 0.18);
    game3d_world_set_clear_color(world, 0.055, 0.052, 0.048);
    void *key_pos = rt_vec3_new(0.0, 4.0, 2.5);
    void *rim_pos = rt_vec3_new(-3.5, 2.0, -2.0);
    void *key = rt_light3d_new_point(key_pos, 1.0, 0.78, 0.52, 0.08);
    void *rim = rt_light3d_new_point(rim_pos, 0.50, 0.62, 1.0, 0.12);
    if (key)
        rt_light3d_set_intensity(key, 1.25);
    if (rim)
        rt_light3d_set_intensity(rim, 0.45);
    game3d_world_install_light(world, 0, key);
    game3d_world_install_light(world, 1, rim);
    game3d_release_ref(&key);
    game3d_release_ref(&rim);
    game3d_release_ref(&key_pos);
    game3d_release_ref(&rim_pos);
}

static void *game3d_material_pbr(double r, double g, double b, double metallic, double roughness) {
    void *mat = rt_material3d_new_pbr(
        game3d_clamp(r, 0.0, 1.0), game3d_clamp(g, 0.0, 1.0), game3d_clamp(b, 0.0, 1.0));
    if (mat) {
        rt_material3d_set_shading_model(mat, RT_GAME3D_SHADING_PBR);
        rt_material3d_set_metallic(mat, game3d_clamp(metallic, 0.0, 1.0));
        rt_material3d_set_roughness(mat, game3d_clamp(roughness, 0.0, 1.0));
        rt_material3d_set_ao(mat, 1.0);
        rt_material3d_set_alpha(mat, 1.0);
        rt_material3d_set_alpha_mode(mat, RT_GAME3D_ALPHA_OPAQUE);
    }
    return mat;
}

void *rt_game3d_materials_plastic(double r, double g, double b) {
    return game3d_material_pbr(r, g, b, 0.0, 0.46);
}

void *rt_game3d_materials_metal(double r, double g, double b) {
    void *mat = game3d_material_pbr(r, g, b, 1.0, 0.22);
    if (mat)
        rt_material3d_set_reflectivity(mat, 0.35);
    return mat;
}

void *rt_game3d_materials_rubber(double r, double g, double b) {
    return game3d_material_pbr(r, g, b, 0.0, 0.88);
}

void *rt_game3d_materials_glass(double r, double g, double b, double alpha) {
    void *mat = game3d_material_pbr(r, g, b, 0.0, 0.08);
    if (mat) {
        rt_material3d_set_alpha(mat, game3d_clamp(alpha, 0.05, 1.0));
        rt_material3d_set_alpha_mode(mat, RT_GAME3D_ALPHA_BLEND);
        rt_material3d_set_double_sided(mat, 1);
        rt_material3d_set_reflectivity(mat, 0.50);
    }
    return mat;
}

void *rt_game3d_materials_emissive(double r, double g, double b, double intensity) {
    void *mat = rt_material3d_new_color(
        game3d_clamp(r, 0.0, 1.0), game3d_clamp(g, 0.0, 1.0), game3d_clamp(b, 0.0, 1.0));
    if (mat) {
        rt_material3d_set_shading_model(mat, RT_GAME3D_SHADING_EMISSIVE);
        rt_material3d_set_emissive_color(mat, r, g, b);
        rt_material3d_set_emissive_intensity(mat, game3d_nonnegative_or(intensity, 1.0));
    }
    return mat;
}

void *rt_game3d_materials_unlit(double r, double g, double b) {
    void *mat = rt_material3d_new_color(
        game3d_clamp(r, 0.0, 1.0), game3d_clamp(g, 0.0, 1.0), game3d_clamp(b, 0.0, 1.0));
    if (mat) {
        rt_material3d_set_unlit(mat, 1);
        rt_material3d_set_shading_model(mat, RT_GAME3D_SHADING_UNLIT);
    }
    return mat;
}

void *rt_game3d_materials_from_albedo_map(void *pixels) {
    void *mat = rt_material3d_new_textured(pixels);
    if (mat) {
        rt_material3d_set_shading_model(mat, RT_GAME3D_SHADING_PBR);
        rt_material3d_set_metallic(mat, 0.0);
        rt_material3d_set_roughness(mat, 0.55);
        rt_material3d_set_ao(mat, 1.0);
    }
    return mat;
}

void rt_game3d_postfx_cinematic(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.PostFX.Cinematic: invalid world");
    if (!world)
        return;
    void *fx = rt_postfx3d_new();
    if (!fx)
        return;
    rt_postfx3d_add_bloom(fx, 0.78, 0.22, 2);
    rt_postfx3d_add_tonemap(fx, 2, 1.10);
    rt_postfx3d_add_fxaa(fx);
    rt_postfx3d_add_color_grade(fx, 0.015, 1.08, 1.06);
    rt_postfx3d_add_vignette(fx, 0.72, 0.16);
    game3d_world_assign_postfx(world, fx);
    game3d_release_ref(&fx);
}

void rt_game3d_postfx_crisp(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.PostFX.Crisp: invalid world");
    if (!world)
        return;
    void *fx = rt_postfx3d_new();
    if (!fx)
        return;
    rt_postfx3d_add_tonemap(fx, 1, 1.02);
    rt_postfx3d_add_fxaa(fx);
    rt_postfx3d_add_color_grade(fx, 0.0, 1.05, 1.02);
    game3d_world_assign_postfx(world, fx);
    game3d_release_ref(&fx);
}

void rt_game3d_postfx_none(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.PostFX.None: invalid world");
    if (!world)
        return;
    void *fx = rt_postfx3d_new();
    if (fx)
        rt_postfx3d_set_enabled(fx, 0);
    game3d_world_assign_postfx(world, fx);
    game3d_release_ref(&fx);
}

void rt_game3d_quality_apply(void *obj, int64_t quality) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Quality.Apply: invalid world");
    if (!world || !world->canvas)
        return;
    if (quality < RT_GAME3D_QUALITY_PERFORMANCE || quality > RT_GAME3D_QUALITY_CINEMATIC)
        quality = RT_GAME3D_QUALITY_BALANCED;

    rt_game3d_world_set_quality(world, quality);
    rt_canvas3d_set_frustum_culling(world->canvas, 1);
    if (quality == RT_GAME3D_QUALITY_PERFORMANCE) {
        rt_canvas3d_disable_shadows(world->canvas);
        return;
    }

    if (rt_canvas3d_backend_supports(world->canvas, rt_const_cstr("shadows"))) {
        rt_canvas3d_enable_shadows(
            world->canvas, quality == RT_GAME3D_QUALITY_CINEMATIC ? 2048 : 1024);
        rt_canvas3d_set_shadow_bias(
            world->canvas, quality == RT_GAME3D_QUALITY_CINEMATIC ? 0.003 : 0.005);
    } else {
        rt_canvas3d_disable_shadows(world->canvas);
    }
}

static int64_t game3d_sanitize_segments(int64_t segments, int64_t fallback) {
    if (segments < 8)
        return fallback < 8 ? 8 : fallback;
    if (segments > 256)
        return 256;
    return segments;
}

static void *game3d_prefab_from_mesh(void *mesh, void *material, const char *name) {
    int owns_material = 0;
    if (!material) {
        material = rt_game3d_materials_plastic(0.72, 0.74, 0.76);
        owns_material = 1;
    }
    void *entity = rt_game3d_entity_of(mesh, material);
    if (entity && name)
        rt_game3d_entity_set_name(entity, rt_const_cstr(name));
    game3d_release_ref(&mesh);
    if (owns_material)
        game3d_release_ref(&material);
    return entity;
}

void *rt_game3d_prefab_box(double size, void *material) {
    double s = game3d_positive_or(size, 1.0);
    return game3d_prefab_from_mesh(rt_mesh3d_new_box(s, s, s), material, "Box");
}

void *rt_game3d_prefab_box_xyz(double width, double height, double depth, void *material) {
    double w = game3d_positive_or(width, 1.0);
    double h = game3d_positive_or(height, 1.0);
    double d = game3d_positive_or(depth, 1.0);
    return game3d_prefab_from_mesh(rt_mesh3d_new_box(w, h, d), material, "BoxXYZ");
}

void *rt_game3d_prefab_sphere(double radius, int64_t segments, void *material) {
    double r = game3d_positive_or(radius, 0.5);
    return game3d_prefab_from_mesh(
        rt_mesh3d_new_sphere(r, game3d_sanitize_segments(segments, 32)), material, "Sphere");
}

void *rt_game3d_prefab_cylinder(double radius, double height, int64_t segments, void *material) {
    double r = game3d_positive_or(radius, 0.5);
    double h = game3d_positive_or(height, 1.0);
    return game3d_prefab_from_mesh(
        rt_mesh3d_new_cylinder(r, h, game3d_sanitize_segments(segments, 24)), material, "Cylinder");
}

void *rt_game3d_prefab_plane(double width, double depth, void *material) {
    double w = game3d_positive_or(width, 1.0);
    double d = game3d_positive_or(depth, 1.0);
    return game3d_prefab_from_mesh(rt_mesh3d_new_plane(w, d), material, "Plane");
}

void *rt_game3d_prefab_ground(double size, void *material) {
    void *entity = rt_game3d_prefab_plane(size, size, material);
    if (entity) {
        rt_game3d_entity_set_name(entity, rt_const_cstr("Ground"));
        rt_game3d_entity_set_layer(entity, RT_GAME3D_LAYER_WORLD);
    }
    return entity;
}

static void game3d_model_template_finalize(void *obj) {
    rt_game3d_model_template *model_template = (rt_game3d_model_template *)obj;
    if (!model_template)
        return;
    game3d_release_ref(&model_template->model);
    game3d_release_ref((void **)&model_template->path);
}

static int game3d_model_cache_grow(int32_t need) {
    if (g_game3d_model_cache_capacity >= need)
        return 1;
    int32_t new_cap = g_game3d_model_cache_capacity > 0 ? g_game3d_model_cache_capacity * 2 : 8;
    if (new_cap < need)
        new_cap = need;
    rt_game3d_model_cache_entry *grown = (rt_game3d_model_cache_entry *)realloc(
        g_game3d_model_cache, (size_t)new_cap * sizeof(*grown));
    if (!grown)
        return 0;
    memset(
        grown + g_game3d_model_cache_capacity,
        0,
        (size_t)(new_cap - g_game3d_model_cache_capacity) * sizeof(*grown));
    g_game3d_model_cache = grown;
    g_game3d_model_cache_capacity = new_cap;
    return 1;
}

static int game3d_string_equals(rt_string a, rt_string b) {
    const char *as = a ? rt_string_cstr(a) : "";
    const char *bs = b ? rt_string_cstr(b) : "";
    return as && bs && strcmp(as, bs) == 0;
}

static rt_game3d_model_template *game3d_model_cache_find(rt_string path, int8_t asset_path) {
    for (int32_t i = 0; i < g_game3d_model_cache_count; ++i) {
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[i];
        if (entry->asset_path == asset_path && game3d_string_equals(entry->path, path))
            return (rt_game3d_model_template *)entry->model_template;
    }
    return NULL;
}

static rt_game3d_model_template *game3d_model_template_new(rt_string path, int8_t asset_path, void *model) {
    rt_game3d_model_template *model_template =
        (rt_game3d_model_template *)rt_obj_new_i64(
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

static rt_game3d_model_template *game3d_assets_load_template_uncached(
    rt_string path, int8_t asset_path, const char *method) {
    void *model = asset_path ? rt_model3d_load_asset(path) : rt_model3d_load(path);
    rt_game3d_model_template *model_template;
    if (!model) {
        rt_trap(method);
        return NULL;
    }
    model_template = game3d_model_template_new(path, asset_path, model);
    game3d_release_ref(&model);
    return model_template;
}

static rt_game3d_model_template *game3d_assets_load_template_cached(
    rt_string path, int8_t asset_path, const char *method) {
    rt_game3d_model_template *cached = game3d_model_cache_find(path, asset_path);
    if (cached) {
        rt_obj_retain_maybe(cached);
        return cached;
    }
    rt_game3d_model_template *model_template =
        game3d_assets_load_template_uncached(path, asset_path, method);
    if (!model_template)
        return NULL;
    if (!game3d_model_cache_grow(g_game3d_model_cache_count + 1)) {
        if (rt_obj_release_check0(model_template))
            rt_obj_free(model_template);
        rt_trap("Game3D.Assets3D: model cache allocation failed");
        return NULL;
    }
    rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[g_game3d_model_cache_count++];
    game3d_assign_ref((void **)&entry->path, path ? path : rt_const_cstr(""));
    entry->asset_path = asset_path ? 1 : 0;
    game3d_assign_ref(&entry->model_template, model_template);
    return model_template;
}

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

void *rt_game3d_model_template_get_model(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.get_model: invalid template");
    return model_template ? model_template->model : NULL;
}

rt_string rt_game3d_model_template_get_path(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.get_path: invalid template");
    return model_template && model_template->path ? model_template->path : rt_const_cstr("");
}

int8_t rt_game3d_model_template_get_is_asset(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.get_isAsset: invalid template");
    return model_template ? model_template->asset_path : 0;
}

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

void *rt_game3d_assets_load_model(rt_string path) {
    rt_game3d_model_template *model_template =
        game3d_assets_load_template_uncached(path, 0, "Game3D.Assets3D.LoadModel: failed to load model");
    if (!model_template)
        return NULL;
    void *entity = rt_game3d_model_template_instantiate(model_template);
    if (rt_obj_release_check0(model_template))
        rt_obj_free(model_template);
    return entity;
}

void *rt_game3d_assets_load_model_asset(rt_string path) {
    rt_game3d_model_template *model_template =
        game3d_assets_load_template_uncached(path, 1, "Game3D.Assets3D.LoadModelAsset: failed to load model asset");
    if (!model_template)
        return NULL;
    void *entity = rt_game3d_model_template_instantiate(model_template);
    if (rt_obj_release_check0(model_template))
        rt_obj_free(model_template);
    return entity;
}

void *rt_game3d_assets_load_model_template(rt_string path) {
    return game3d_assets_load_template_cached(
        path, 0, "Game3D.Assets3D.LoadModelTemplate: failed to load model");
}

void *rt_game3d_assets_load_model_template_asset(rt_string path) {
    return game3d_assets_load_template_cached(
        path, 1, "Game3D.Assets3D.LoadModelTemplateAsset: failed to load model asset");
}

void rt_game3d_assets_preload(rt_string path) {
    void *model_template = rt_game3d_assets_load_model_template(path);
    game3d_release_ref(&model_template);
}

void rt_game3d_assets_clear_cache(void) {
    for (int32_t i = 0; i < g_game3d_model_cache_count; ++i) {
        game3d_release_ref((void **)&g_game3d_model_cache[i].path);
        game3d_release_ref(&g_game3d_model_cache[i].model_template);
    }
    g_game3d_model_cache_count = 0;
}

static void game3d_env_handle_finalize(void *obj) {
    rt_game3d_env_handle *env = (rt_game3d_env_handle *)obj;
    if (!env)
        return;
    game3d_release_ref(&env->water_entity);
    game3d_release_ref(&env->terrain_entity);
    game3d_release_ref(&env->world);
}

static void *game3d_env_handle_new(rt_game3d_world *world) {
    rt_game3d_env_handle *env =
        (rt_game3d_env_handle *)rt_obj_new_i64(RT_G3D_GAME3D_ENV_HANDLE_CLASS_ID, (int64_t)sizeof(*env));
    if (!env) {
        rt_trap("Game3D.Environment: allocation failed");
        return NULL;
    }
    memset(env, 0, sizeof(*env));
    rt_obj_set_finalizer(env, game3d_env_handle_finalize);
    game3d_assign_ref(&env->world, world);
    return env;
}

static rt_game3d_world *game3d_env_world(rt_game3d_env_handle *env, const char *method) {
    if (!env || !env->world)
        rt_trap(method);
    return game3d_world_checked(env->world, method);
}

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
        void *body = rt_body3d_new_aabb(s * 0.5, 0.10, s * 0.5, 0.0);
        rt_game3d_entity_attach_body(terrain, body);
        rt_game3d_world_spawn(world, terrain);
        game3d_assign_ref(&env->terrain_entity, terrain);
        game3d_release_ref(&body);
    }
    game3d_release_ref(&terrain);
    game3d_release_ref(&mat);
    return obj;
}

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
    void *mat = rt_game3d_materials_glass(0.18, 0.42, 0.62, 0.48);
    void *water = rt_game3d_prefab_plane(80.0, 80.0, mat);
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

void *rt_game3d_env_handle_with_fog(void *obj, double near_plane, double far_plane) {
    rt_game3d_env_handle *env =
        game3d_env_handle_checked(obj, "Game3D.EnvHandle.withFog: invalid environment");
    rt_game3d_world *world = game3d_env_world(env, "Game3D.EnvHandle.withFog: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_fog(
            world->canvas,
            game3d_nonnegative_or(near_plane, 18.0),
            game3d_positive_or(far_plane, 120.0),
            world->clear_r,
            world->clear_g,
            world->clear_b);
    return obj;
}

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

void *rt_game3d_environment_overcast(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Environment.Overcast: invalid world");
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

void *rt_game3d_environment_night(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Environment.Night: invalid world");
    void *env = game3d_env_handle_new(world);
    rt_game3d_lighting_night(world);
    rt_game3d_env_handle_with_terrain(env, 96.0, 0.0);
    rt_game3d_env_handle_with_fog(env, 20.0, 95.0);
    return env;
}

void rt_game3d_debug_show_overlay(void *obj, int8_t enabled) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Debug3D.ShowOverlay: invalid world");
    if (world)
        world->debug_overlay_enabled = enabled ? 1 : 0;
}

void rt_game3d_debug_draw_axes(void *obj, void *origin, double size) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Debug3D.DrawAxes: invalid world");
    if (!origin || !rt_g3d_is_vec3(origin))
        rt_trap("Game3D.Debug3D.DrawAxes: origin must be Vec3");
    if (world) {
        game3d_assign_ref(&world->debug_axis_origin, origin);
        world->debug_axis_size = game3d_positive_or(size, 1.0);
        world->debug_axes_enabled = 1;
    }
}

void rt_game3d_debug_draw_physics(void *obj, int8_t enabled) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Debug3D.DrawPhysics: invalid world");
    if (world)
        world->debug_physics_enabled = enabled ? 1 : 0;
}

void rt_game3d_debug_draw_camera_info(void *obj, int8_t enabled) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Debug3D.DrawCameraInfo: invalid world");
    if (world)
        world->debug_camera_enabled = enabled ? 1 : 0;
}

void rt_game3d_debug_draw_capabilities(void *obj, int8_t enabled) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Debug3D.DrawCapabilities: invalid world");
    if (world)
        world->debug_caps_enabled = enabled ? 1 : 0;
}

static void game3d_character_controller_finalize(void *obj) {
    rt_game3d_character_controller *controller = (rt_game3d_character_controller *)obj;
    if (!controller)
        return;
    game3d_release_ref(&controller->entity);
    game3d_release_ref(&controller->character);
}

static void game3d_character_controller_sync_entity(rt_game3d_character_controller *controller) {
    if (!controller || !controller->entity || !controller->character)
        return;
    rt_game3d_entity *entity = (rt_game3d_entity *)controller->entity;
    void *pos = rt_character3d_get_position(controller->character);
    if (entity->node && pos) {
        rt_scene_node3d_set_position(
            entity->node, rt_vec3_x(pos), rt_vec3_y(pos), rt_vec3_z(pos));
    }
    game3d_release_ref(&pos);
}

void *rt_game3d_character_controller_new(
    void *world_obj, void *entity_obj, double radius, double height, double mass) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.CharacterController3D.New: invalid world");
    rt_game3d_entity *entity =
        game3d_entity_checked(entity_obj, "Game3D.CharacterController3D.New: entity must be Entity3D");
    if (!world || !entity)
        return NULL;

    radius = game3d_positive_or(radius, 0.3);
    height = game3d_positive_or(height, 1.8);
    mass = game3d_nonnegative_or(mass, 70.0);

    rt_game3d_character_controller *controller =
        (rt_game3d_character_controller *)rt_obj_new_i64(
            RT_G3D_GAME3D_CHARACTER_CONTROLLER_CLASS_ID, (int64_t)sizeof(*controller));
    if (!controller) {
        rt_trap("Game3D.CharacterController3D.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    rt_obj_set_finalizer(controller, game3d_character_controller_finalize);
    controller->world = world;
    controller->entity = entity;
    rt_obj_retain_maybe(entity);
    controller->character = rt_character3d_new(radius, height, mass);
    if (!controller->character) {
        if (rt_obj_release_check0(controller))
            rt_obj_free(controller);
        rt_trap("Game3D.CharacterController3D.New: Character3D allocation failed");
        return NULL;
    }
    rt_character3d_set_world(controller->character, world->physics);
    controller->speed = RT_GAME3D_DEFAULT_MOVE_SPEED;
    controller->jump_speed = RT_GAME3D_DEFAULT_JUMP_SPEED;
    controller->gravity = RT_GAME3D_DEFAULT_GRAVITY;
    controller->eye_height = height * 0.45;

    void *pos = rt_game3d_entity_position(entity);
    if (pos) {
        rt_character3d_set_position(
            controller->character, rt_vec3_x(pos), rt_vec3_y(pos), rt_vec3_z(pos));
        game3d_release_ref(&pos);
    }
    game3d_character_controller_sync_entity(controller);
    return controller;
}

void *rt_game3d_character_controller_get_character(void *obj) {
    rt_game3d_character_controller *controller =
        game3d_character_controller_checked(obj, "Game3D.CharacterController3D.get_character: invalid controller");
    return controller ? controller->character : NULL;
}

void *rt_game3d_character_controller_get_entity(void *obj) {
    rt_game3d_character_controller *controller =
        game3d_character_controller_checked(obj, "Game3D.CharacterController3D.get_entity: invalid controller");
    return controller ? controller->entity : NULL;
}

double rt_game3d_character_controller_get_speed(void *obj) {
    rt_game3d_character_controller *controller =
        game3d_character_controller_checked(obj, "Game3D.CharacterController3D.get_speed: invalid controller");
    return controller ? controller->speed : 0.0;
}

void rt_game3d_character_controller_set_speed(void *obj, double speed) {
    rt_game3d_character_controller *controller =
        game3d_character_controller_checked(obj, "Game3D.CharacterController3D.set_speed: invalid controller");
    if (controller)
        controller->speed = game3d_nonnegative_or(speed, RT_GAME3D_DEFAULT_MOVE_SPEED);
}

double rt_game3d_character_controller_get_jump_speed(void *obj) {
    rt_game3d_character_controller *controller =
        game3d_character_controller_checked(obj, "Game3D.CharacterController3D.get_jumpSpeed: invalid controller");
    return controller ? controller->jump_speed : 0.0;
}

void rt_game3d_character_controller_set_jump_speed(void *obj, double jump_speed) {
    rt_game3d_character_controller *controller =
        game3d_character_controller_checked(obj, "Game3D.CharacterController3D.set_jumpSpeed: invalid controller");
    if (controller)
        controller->jump_speed = game3d_nonnegative_or(jump_speed, RT_GAME3D_DEFAULT_JUMP_SPEED);
}

double rt_game3d_character_controller_get_gravity(void *obj) {
    rt_game3d_character_controller *controller =
        game3d_character_controller_checked(obj, "Game3D.CharacterController3D.get_gravity: invalid controller");
    return controller ? controller->gravity : 0.0;
}

void rt_game3d_character_controller_set_gravity(void *obj, double gravity) {
    rt_game3d_character_controller *controller =
        game3d_character_controller_checked(obj, "Game3D.CharacterController3D.set_gravity: invalid controller");
    if (controller)
        controller->gravity = game3d_finite_or(gravity, RT_GAME3D_DEFAULT_GRAVITY);
}

void rt_game3d_character_controller_update(void *obj, void *input_obj, void *camera, double dt) {
    rt_game3d_character_controller *controller =
        game3d_character_controller_checked(obj, "Game3D.CharacterController3D.update: invalid controller");
    (void)game3d_input_checked(input_obj, "Game3D.CharacterController3D.update: invalid input");
    if (!rt_g3d_has_class(camera, RT_G3D_CAMERA3D_CLASS_ID))
        rt_trap("Game3D.CharacterController3D.update: camera must be Camera3D");
    if (!controller || !controller->character)
        return;

    dt = game3d_clamp_dt(dt);
    void *move = rt_game3d_input_move_axis(input_obj);
    void *forward = rt_camera3d_get_forward(camera);
    void *right = rt_camera3d_get_right(camera);

    double move_x = move ? game3d_finite_or(rt_vec3_x(move), 0.0) : 0.0;
    double move_y = move ? game3d_finite_or(rt_vec3_y(move), 0.0) : 0.0;
    double move_z = move ? game3d_finite_or(rt_vec3_z(move), 0.0) : 0.0;
    double move_len = sqrt(move_x * move_x + move_z * move_z);
    if (isfinite(move_len) && move_len > 1.0) {
        move_x /= move_len;
        move_z /= move_len;
    }

    double fx = forward ? rt_vec3_x(forward) : 0.0;
    double fz = forward ? rt_vec3_z(forward) : -1.0;
    double rx = right ? rt_vec3_x(right) : 1.0;
    double rz = right ? rt_vec3_z(right) : 0.0;
    game3d_normalize_xz(&fx, &fz, 0.0, -1.0);
    game3d_normalize_xz(&rx, &rz, 1.0, 0.0);

    int8_t grounded = rt_character3d_is_grounded(controller->character);
    if (grounded) {
        if (controller->vertical_velocity < 0.0)
            controller->vertical_velocity = -0.5;
        if (rt_game3d_input_pressed(input_obj, rt_game3d_key_space()) || move_y > 0.5)
            controller->vertical_velocity = controller->jump_speed;
    } else {
        controller->vertical_velocity += controller->gravity * dt;
        controller->vertical_velocity = game3d_clamp(controller->vertical_velocity, -100.0, 100.0);
    }

    double speed = game3d_nonnegative_or(controller->speed, RT_GAME3D_DEFAULT_MOVE_SPEED);
    double vx = (fx * move_z + rx * move_x) * speed;
    double vz = (fz * move_z + rz * move_x) * speed;
    void *velocity = rt_vec3_new(vx, controller->vertical_velocity, vz);
    rt_character3d_move(controller->character, velocity, dt);
    game3d_release_ref(&velocity);
    if (rt_character3d_is_grounded(controller->character) && controller->vertical_velocity < 0.0)
        controller->vertical_velocity = -0.5;
    game3d_character_controller_sync_entity(controller);

    game3d_release_ref(&right);
    game3d_release_ref(&forward);
    game3d_release_ref(&move);
}

void rt_game3d_character_controller_teleport(void *obj, double x, double y, double z) {
    rt_game3d_character_controller *controller =
        game3d_character_controller_checked(obj, "Game3D.CharacterController3D.teleport: invalid controller");
    if (!controller || !controller->character)
        return;
    controller->vertical_velocity = 0.0;
    rt_character3d_set_position(
        controller->character,
        game3d_finite_or(x, 0.0),
        game3d_finite_or(y, 0.0),
        game3d_finite_or(z, 0.0));
    game3d_character_controller_sync_entity(controller);
}

int8_t rt_game3d_character_controller_grounded(void *obj) {
    rt_game3d_character_controller *controller =
        game3d_character_controller_checked(obj, "Game3D.CharacterController3D.grounded: invalid controller");
    return controller && controller->character ? rt_character3d_is_grounded(controller->character) : 0;
}

static int game3d_camera_controller_is_valid(void *controller) {
    if (!controller)
        return 1;
    int64_t cid = rt_obj_class_id(controller);
    return cid == RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID ||
           cid == RT_G3D_GAME3D_FREEFLY_CLASS_ID ||
           cid == RT_G3D_GAME3D_ORBIT_CLASS_ID ||
           cid == RT_G3D_GAME3D_FOLLOW_CLASS_ID;
}

static void game3d_first_person_controller_finalize(void *obj) {
    rt_game3d_first_person_controller *controller = (rt_game3d_first_person_controller *)obj;
    if (controller)
        game3d_release_ref(&controller->character_controller);
}

void *rt_game3d_first_person_controller_new(void *world_obj) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FirstPersonController.New: invalid world");
    if (!world)
        return NULL;
    rt_game3d_first_person_controller *controller =
        (rt_game3d_first_person_controller *)rt_obj_new_i64(
            RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID, (int64_t)sizeof(*controller));
    if (!controller) {
        rt_trap("Game3D.FirstPersonController.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    rt_obj_set_finalizer(controller, game3d_first_person_controller_finalize);
    controller->world = world;
    controller->speed = RT_GAME3D_DEFAULT_MOVE_SPEED;
    controller->look_sensitivity = RT_GAME3D_DEFAULT_LOOK_SENSITIVITY;
    controller->capture_mouse = 1;
    if (world->camera)
        rt_camera3d_fps_init(world->camera);
    return controller;
}

void *rt_game3d_first_person_controller_get_character(void *obj) {
    rt_game3d_first_person_controller *controller =
        game3d_first_person_controller_checked(obj, "Game3D.FirstPersonController.get_character: invalid controller");
    return controller ? controller->character_controller : NULL;
}

void rt_game3d_first_person_controller_set_character(void *obj, void *character_controller) {
    rt_game3d_first_person_controller *controller =
        game3d_first_person_controller_checked(obj, "Game3D.FirstPersonController.set_character: invalid controller");
    if (character_controller &&
        !rt_g3d_has_class(character_controller, RT_G3D_GAME3D_CHARACTER_CONTROLLER_CLASS_ID))
        rt_trap("Game3D.FirstPersonController.set_character: value must be CharacterController3D");
    if (controller)
        game3d_assign_ref(&controller->character_controller, character_controller);
}

double rt_game3d_first_person_controller_get_speed(void *obj) {
    rt_game3d_first_person_controller *controller =
        game3d_first_person_controller_checked(obj, "Game3D.FirstPersonController.get_speed: invalid controller");
    return controller ? controller->speed : 0.0;
}

void rt_game3d_first_person_controller_set_speed(void *obj, double speed) {
    rt_game3d_first_person_controller *controller =
        game3d_first_person_controller_checked(obj, "Game3D.FirstPersonController.set_speed: invalid controller");
    if (controller)
        controller->speed = game3d_nonnegative_or(speed, RT_GAME3D_DEFAULT_MOVE_SPEED);
}

double rt_game3d_first_person_controller_get_look_sensitivity(void *obj) {
    rt_game3d_first_person_controller *controller =
        game3d_first_person_controller_checked(obj, "Game3D.FirstPersonController.get_lookSensitivity: invalid controller");
    return controller ? controller->look_sensitivity : 0.0;
}

void rt_game3d_first_person_controller_set_look_sensitivity(void *obj, double sensitivity) {
    rt_game3d_first_person_controller *controller =
        game3d_first_person_controller_checked(obj, "Game3D.FirstPersonController.set_lookSensitivity: invalid controller");
    if (controller)
        controller->look_sensitivity = game3d_nonnegative_or(sensitivity, RT_GAME3D_DEFAULT_LOOK_SENSITIVITY);
}

void rt_game3d_first_person_controller_capture_mouse(void *obj) {
    rt_game3d_first_person_controller *controller =
        game3d_first_person_controller_checked(obj, "Game3D.FirstPersonController.captureMouse: invalid controller");
    if (controller) {
        controller->capture_mouse = 1;
        rt_mouse_capture();
    }
}

void rt_game3d_first_person_controller_release_mouse(void *obj) {
    rt_game3d_first_person_controller *controller =
        game3d_first_person_controller_checked(obj, "Game3D.FirstPersonController.releaseMouse: invalid controller");
    if (controller) {
        controller->capture_mouse = 0;
        rt_mouse_release();
    }
}

void rt_game3d_first_person_controller_update(void *obj, void *world_obj, double dt) {
    rt_game3d_first_person_controller *controller =
        game3d_first_person_controller_checked(obj, "Game3D.FirstPersonController.update: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FirstPersonController.update: invalid world");
    if (!controller || !world || !world->camera || !world->input)
        return;
    dt = game3d_clamp_dt(dt);
    if (controller->capture_mouse)
        rt_mouse_capture();
    double yaw = (double)rt_mouse_delta_x() * controller->look_sensitivity;
    double pitch = 0.0 - (double)rt_mouse_delta_y() * controller->look_sensitivity;
    if (controller->character_controller) {
        rt_camera3d_fps_update(world->camera, yaw, pitch, 0.0, 0.0, 0.0, 0.0, dt);
        rt_game3d_character_controller_set_speed(controller->character_controller, controller->speed);
        rt_game3d_character_controller_update(
            controller->character_controller, world->input, world->camera, dt);
    } else {
        void *move = rt_game3d_input_move_axis(world->input);
        double move_x = move ? rt_vec3_x(move) : 0.0;
        double move_y = move ? rt_vec3_y(move) : 0.0;
        double move_z = move ? rt_vec3_z(move) : 0.0;
        rt_camera3d_fps_update(
            world->camera, yaw, pitch, move_z, move_x, move_y, controller->speed, dt);
        game3d_release_ref(&move);
    }
}

void rt_game3d_first_person_controller_late_update(void *obj, void *world_obj, double dt) {
    (void)dt;
    rt_game3d_first_person_controller *controller =
        game3d_first_person_controller_checked(obj, "Game3D.FirstPersonController.lateUpdate: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FirstPersonController.lateUpdate: invalid world");
    if (!controller || !world || !world->camera || !controller->character_controller)
        return;
    rt_game3d_character_controller *character =
        game3d_character_controller_checked(controller->character_controller,
                                            "Game3D.FirstPersonController.lateUpdate: invalid character");
    if (!character || !character->character)
        return;
    void *pos = rt_character3d_get_position(character->character);
    if (pos) {
        void *eye = rt_vec3_new(rt_vec3_x(pos), rt_vec3_y(pos) + character->eye_height, rt_vec3_z(pos));
        rt_camera3d_set_position(world->camera, eye);
        game3d_release_ref(&eye);
    }
    game3d_release_ref(&pos);
}

void *rt_game3d_free_fly_controller_new(void *world_obj) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FreeFlyController.New: invalid world");
    if (!world)
        return NULL;
    rt_game3d_free_fly_controller *controller =
        (rt_game3d_free_fly_controller *)rt_obj_new_i64(
            RT_G3D_GAME3D_FREEFLY_CLASS_ID, (int64_t)sizeof(*controller));
    if (!controller) {
        rt_trap("Game3D.FreeFlyController.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    controller->world = world;
    controller->speed = RT_GAME3D_DEFAULT_MOVE_SPEED;
    controller->look_sensitivity = RT_GAME3D_DEFAULT_LOOK_SENSITIVITY;
    controller->capture_mouse = 1;
    if (world->camera)
        rt_camera3d_fps_init(world->camera);
    return controller;
}

double rt_game3d_free_fly_controller_get_speed(void *obj) {
    rt_game3d_free_fly_controller *controller =
        game3d_free_fly_controller_checked(obj, "Game3D.FreeFlyController.get_speed: invalid controller");
    return controller ? controller->speed : 0.0;
}

void rt_game3d_free_fly_controller_set_speed(void *obj, double speed) {
    rt_game3d_free_fly_controller *controller =
        game3d_free_fly_controller_checked(obj, "Game3D.FreeFlyController.set_speed: invalid controller");
    if (controller)
        controller->speed = game3d_nonnegative_or(speed, RT_GAME3D_DEFAULT_MOVE_SPEED);
}

double rt_game3d_free_fly_controller_get_look_sensitivity(void *obj) {
    rt_game3d_free_fly_controller *controller =
        game3d_free_fly_controller_checked(obj, "Game3D.FreeFlyController.get_lookSensitivity: invalid controller");
    return controller ? controller->look_sensitivity : 0.0;
}

void rt_game3d_free_fly_controller_set_look_sensitivity(void *obj, double sensitivity) {
    rt_game3d_free_fly_controller *controller =
        game3d_free_fly_controller_checked(obj, "Game3D.FreeFlyController.set_lookSensitivity: invalid controller");
    if (controller)
        controller->look_sensitivity = game3d_nonnegative_or(sensitivity, RT_GAME3D_DEFAULT_LOOK_SENSITIVITY);
}

void rt_game3d_free_fly_controller_capture_mouse(void *obj) {
    rt_game3d_free_fly_controller *controller =
        game3d_free_fly_controller_checked(obj, "Game3D.FreeFlyController.captureMouse: invalid controller");
    if (controller) {
        controller->capture_mouse = 1;
        rt_mouse_capture();
    }
}

void rt_game3d_free_fly_controller_release_mouse(void *obj) {
    rt_game3d_free_fly_controller *controller =
        game3d_free_fly_controller_checked(obj, "Game3D.FreeFlyController.releaseMouse: invalid controller");
    if (controller) {
        controller->capture_mouse = 0;
        rt_mouse_release();
    }
}

void rt_game3d_free_fly_controller_update(void *obj, void *world_obj, double dt) {
    rt_game3d_free_fly_controller *controller =
        game3d_free_fly_controller_checked(obj, "Game3D.FreeFlyController.update: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FreeFlyController.update: invalid world");
    if (!controller || !world || !world->camera || !world->input)
        return;
    dt = game3d_clamp_dt(dt);
    if (controller->capture_mouse)
        rt_mouse_capture();
    void *move = rt_game3d_input_move_axis(world->input);
    double yaw = (double)rt_mouse_delta_x() * controller->look_sensitivity;
    double pitch = 0.0 - (double)rt_mouse_delta_y() * controller->look_sensitivity;
    rt_camera3d_fps_update(
        world->camera,
        yaw,
        pitch,
        move ? rt_vec3_z(move) : 0.0,
        move ? rt_vec3_x(move) : 0.0,
        move ? rt_vec3_y(move) : 0.0,
        controller->speed,
        dt);
    game3d_release_ref(&move);
}

void rt_game3d_free_fly_controller_late_update(void *obj, void *world_obj, double dt) {
    (void)dt;
    (void)game3d_free_fly_controller_checked(obj, "Game3D.FreeFlyController.lateUpdate: invalid controller");
    (void)game3d_world_checked(world_obj, "Game3D.FreeFlyController.lateUpdate: invalid world");
}

static void game3d_orbit_controller_finalize(void *obj) {
    rt_game3d_orbit_controller *controller = (rt_game3d_orbit_controller *)obj;
    if (controller)
        game3d_release_ref(&controller->target);
}

void *rt_game3d_orbit_controller_new(void *world_obj, void *target) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.OrbitController.New: invalid world");
    if (!rt_g3d_is_vec3(target))
        rt_trap("Game3D.OrbitController.New: target must be Vec3");
    if (!world)
        return NULL;
    rt_game3d_orbit_controller *controller =
        (rt_game3d_orbit_controller *)rt_obj_new_i64(
            RT_G3D_GAME3D_ORBIT_CLASS_ID, (int64_t)sizeof(*controller));
    if (!controller) {
        rt_trap("Game3D.OrbitController.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    rt_obj_set_finalizer(controller, game3d_orbit_controller_finalize);
    controller->world = world;
    game3d_assign_ref(&controller->target, target);
    controller->distance = 6.0;
    controller->min_distance = 1.0;
    controller->max_distance = 100.0;
    controller->pitch = 20.0;
    controller->orbit_sensitivity = 0.25;
    controller->zoom_sensitivity = 1.0;
    return controller;
}

void *rt_game3d_orbit_controller_get_target(void *obj) {
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.get_target: invalid controller");
    return controller ? controller->target : NULL;
}

void rt_game3d_orbit_controller_set_target(void *obj, void *target) {
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.set_target: invalid controller");
    if (!rt_g3d_is_vec3(target))
        rt_trap("Game3D.OrbitController.set_target: target must be Vec3");
    if (controller)
        game3d_assign_ref(&controller->target, target);
}

double rt_game3d_orbit_controller_get_distance(void *obj) {
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.get_distance: invalid controller");
    return controller ? controller->distance : 0.0;
}

void rt_game3d_orbit_controller_set_distance(void *obj, double distance) {
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.set_distance: invalid controller");
    if (controller)
        controller->distance = game3d_clamp(distance, controller->min_distance, controller->max_distance);
}

double rt_game3d_orbit_controller_get_yaw(void *obj) {
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.get_yaw: invalid controller");
    return controller ? controller->yaw : 0.0;
}

void rt_game3d_orbit_controller_set_yaw(void *obj, double yaw) {
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.set_yaw: invalid controller");
    if (controller)
        controller->yaw = game3d_finite_or(yaw, 0.0);
}

double rt_game3d_orbit_controller_get_pitch(void *obj) {
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.get_pitch: invalid controller");
    return controller ? controller->pitch : 0.0;
}

void rt_game3d_orbit_controller_set_pitch(void *obj, double pitch) {
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.set_pitch: invalid controller");
    if (controller)
        controller->pitch = game3d_clamp(pitch, -85.0, 85.0);
}

void rt_game3d_orbit_controller_update(void *obj, void *world_obj, double dt) {
    (void)dt;
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.update: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.OrbitController.update: invalid world");
    if (!controller || !world || !world->input)
        return;
    if (rt_game3d_input_mouse_button(world->input, rt_game3d_mouse_left())) {
        controller->yaw += (double)rt_mouse_delta_x() * controller->orbit_sensitivity;
        controller->pitch -= (double)rt_mouse_delta_y() * controller->orbit_sensitivity;
        controller->pitch = game3d_clamp(controller->pitch, -85.0, 85.0);
    }
    controller->distance -= rt_game3d_input_wheel_y(world->input) * controller->zoom_sensitivity;
    controller->distance =
        game3d_clamp(controller->distance, controller->min_distance, controller->max_distance);
}

void rt_game3d_orbit_controller_late_update(void *obj, void *world_obj, double dt) {
    (void)dt;
    rt_game3d_orbit_controller *controller =
        game3d_orbit_controller_checked(obj, "Game3D.OrbitController.lateUpdate: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.OrbitController.lateUpdate: invalid world");
    if (controller && world && world->camera && controller->target)
        rt_camera3d_orbit(
            world->camera, controller->target, controller->distance, controller->yaw, controller->pitch);
}

static void game3d_follow_controller_finalize(void *obj) {
    rt_game3d_follow_controller *controller = (rt_game3d_follow_controller *)obj;
    if (!controller)
        return;
    game3d_release_ref(&controller->target_entity);
    game3d_release_ref(&controller->offset);
}

void *rt_game3d_follow_controller_new(void *world_obj, void *target_entity, void *offset) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FollowController.New: invalid world");
    (void)game3d_entity_checked(target_entity, "Game3D.FollowController.New: target must be Entity3D");
    if (!rt_g3d_is_vec3(offset))
        rt_trap("Game3D.FollowController.New: offset must be Vec3");
    if (!world)
        return NULL;
    rt_game3d_follow_controller *controller =
        (rt_game3d_follow_controller *)rt_obj_new_i64(
            RT_G3D_GAME3D_FOLLOW_CLASS_ID, (int64_t)sizeof(*controller));
    if (!controller) {
        rt_trap("Game3D.FollowController.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    rt_obj_set_finalizer(controller, game3d_follow_controller_finalize);
    controller->world = world;
    game3d_assign_ref(&controller->target_entity, target_entity);
    game3d_assign_ref(&controller->offset, offset);
    controller->damping = RT_GAME3D_DEFAULT_FOLLOW_DAMPING;
    return controller;
}

void *rt_game3d_follow_controller_get_target(void *obj) {
    rt_game3d_follow_controller *controller =
        game3d_follow_controller_checked(obj, "Game3D.FollowController.get_target: invalid controller");
    return controller ? controller->target_entity : NULL;
}

void rt_game3d_follow_controller_set_target(void *obj, void *target_entity) {
    rt_game3d_follow_controller *controller =
        game3d_follow_controller_checked(obj, "Game3D.FollowController.set_target: invalid controller");
    if (target_entity)
        (void)game3d_entity_checked(target_entity, "Game3D.FollowController.set_target: target must be Entity3D");
    if (controller)
        game3d_assign_ref(&controller->target_entity, target_entity);
}

void *rt_game3d_follow_controller_get_offset(void *obj) {
    rt_game3d_follow_controller *controller =
        game3d_follow_controller_checked(obj, "Game3D.FollowController.get_offset: invalid controller");
    return controller ? controller->offset : NULL;
}

void rt_game3d_follow_controller_set_offset(void *obj, void *offset) {
    rt_game3d_follow_controller *controller =
        game3d_follow_controller_checked(obj, "Game3D.FollowController.set_offset: invalid controller");
    if (!rt_g3d_is_vec3(offset))
        rt_trap("Game3D.FollowController.set_offset: offset must be Vec3");
    if (controller)
        game3d_assign_ref(&controller->offset, offset);
}

double rt_game3d_follow_controller_get_damping(void *obj) {
    rt_game3d_follow_controller *controller =
        game3d_follow_controller_checked(obj, "Game3D.FollowController.get_damping: invalid controller");
    return controller ? controller->damping : 0.0;
}

void rt_game3d_follow_controller_set_damping(void *obj, double damping) {
    rt_game3d_follow_controller *controller =
        game3d_follow_controller_checked(obj, "Game3D.FollowController.set_damping: invalid controller");
    if (controller)
        controller->damping = game3d_nonnegative_or(damping, RT_GAME3D_DEFAULT_FOLLOW_DAMPING);
}

void rt_game3d_follow_controller_update(void *obj, void *world_obj, double dt) {
    (void)dt;
    (void)game3d_follow_controller_checked(obj, "Game3D.FollowController.update: invalid controller");
    (void)game3d_world_checked(world_obj, "Game3D.FollowController.update: invalid world");
}

void rt_game3d_follow_controller_late_update(void *obj, void *world_obj, double dt) {
    rt_game3d_follow_controller *controller =
        game3d_follow_controller_checked(obj, "Game3D.FollowController.lateUpdate: invalid controller");
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.FollowController.lateUpdate: invalid world");
    if (!controller || !world || !world->camera || !controller->target_entity || !controller->offset)
        return;

    dt = game3d_clamp_dt(dt);
    void *target_pos = rt_game3d_entity_world_position(controller->target_entity);
    void *current = rt_camera3d_get_position(world->camera);
    if (!target_pos)
        return;
    double target_x = rt_vec3_x(target_pos) + rt_vec3_x(controller->offset);
    double target_y = rt_vec3_y(target_pos) + rt_vec3_y(controller->offset);
    double target_z = rt_vec3_z(target_pos) + rt_vec3_z(controller->offset);
    double alpha = controller->damping <= 0.0 ? 1.0 : 1.0 - exp(0.0 - controller->damping * dt);
    alpha = game3d_clamp(alpha, 0.0, 1.0);
    double x = current ? rt_vec3_x(current) + (target_x - rt_vec3_x(current)) * alpha : target_x;
    double y = current ? rt_vec3_y(current) + (target_y - rt_vec3_y(current)) * alpha : target_y;
    double z = current ? rt_vec3_z(current) + (target_z - rt_vec3_z(current)) * alpha : target_z;
    void *eye = rt_vec3_new(x, y, z);
    void *look_target = rt_vec3_new(rt_vec3_x(target_pos), rt_vec3_y(target_pos), rt_vec3_z(target_pos));
    void *up = rt_vec3_new(0.0, 1.0, 0.0);
    rt_camera3d_look_at(world->camera, eye, look_target, up);
    game3d_release_ref(&up);
    game3d_release_ref(&look_target);
    game3d_release_ref(&eye);
    game3d_release_ref(&current);
    game3d_release_ref(&target_pos);
}

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
    }
}

static void game3d_world_update_animations(rt_game3d_world *world, double dt) {
    if (!world)
        return;
    for (int32_t i = 0; i < world->entity_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        if (!entity || entity->destroyed || !entity->anim)
            continue;
        if (rt_g3d_has_class(entity->anim, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID))
            rt_game3d_animator_update(entity->anim, dt);
    }
}

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
    }
}

static int game3d_world_grow_entities(rt_game3d_world *world, int32_t need) {
    if (world->entity_capacity >= need)
        return 1;
    int32_t new_cap = world->entity_capacity > 0 ? world->entity_capacity * 2 : 16;
    if (new_cap < need)
        new_cap = need;
    rt_game3d_entity **grown =
        (rt_game3d_entity **)realloc(world->entities, (size_t)new_cap * sizeof(*grown));
    if (!grown)
        return 0;
    world->entities = grown;
    world->entity_capacity = new_cap;
    return 1;
}

static int32_t game3d_world_find_entity_index(rt_game3d_world *world, rt_game3d_entity *entity) {
    if (!world || !entity)
        return -1;
    for (int32_t i = 0; i < world->entity_count; ++i) {
        if (world->entities[i] == entity)
            return i;
    }
    return -1;
}

static rt_game3d_entity *game3d_world_find_entity_by_body(rt_game3d_world *world, void *body) {
    if (!world || !body)
        return NULL;
    for (int32_t i = 0; i < world->entity_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        if (entity && entity->body == body)
            return entity;
    }
    return NULL;
}

static void game3d_world_registry_remove(rt_game3d_world *world, rt_game3d_entity *entity) {
    int32_t index = game3d_world_find_entity_index(world, entity);
    if (index < 0)
        return;
    rt_game3d_entity *owned = world->entities[index];
    for (int32_t i = index; i < world->entity_count - 1; ++i)
        world->entities[i] = world->entities[i + 1];
    world->entities[--world->entity_count] = NULL;
    game3d_release_ref((void **)&owned);
}

static void game3d_entity_detach_node(rt_game3d_world *world, rt_game3d_entity *entity) {
    if (!world || !entity || !entity->node)
        return;
    void *parent = rt_scene_node3d_get_parent(entity->node);
    if (parent)
        rt_scene_node3d_remove_child(parent, entity->node);
    else if (world->scene)
        rt_scene3d_remove(world->scene, entity->node);
}

static void game3d_world_despawn_entity_tree(rt_game3d_world *world, rt_game3d_entity *entity, int detach_root) {
    if (!world || !entity || !entity->spawned)
        return;
    for (int32_t i = 0; i < entity->child_count; ++i)
        game3d_world_despawn_entity_tree(world, entity->children[i], 0);
    if (entity->body && world->physics)
        rt_world3d_remove(world->physics, entity->body);
    if (detach_root)
        game3d_entity_detach_node(world, entity);
    entity->spawned = 0;
    entity->world = NULL;
    game3d_world_registry_remove(world, entity);
}

static void game3d_world_spawn_entity_tree(rt_game3d_world *world,
                                           rt_game3d_entity *entity,
                                           int attach_to_scene,
                                           int64_t *next_id) {
    if (!world || !entity)
        return;
    if (entity->destroyed)
        rt_trap("Game3D.World3D.spawn: entity is destroyed");
    if (entity->spawned && entity->world == world)
        return;
    if (entity->spawned || (entity->world && entity->world != world))
        rt_trap("Game3D.World3D.spawn: entity already belongs to a world");
    if (!game3d_world_grow_entities(world, world->entity_count + 1))
        rt_trap("Game3D.World3D.spawn: registry allocation failed");

    if (entity->id == 0)
        entity->id = (*next_id)++;
    entity->spawned = 1;
    entity->world = world;
    rt_obj_retain_maybe(entity);
    world->entities[world->entity_count++] = entity;

    if (attach_to_scene && world->scene && entity->node)
        rt_scene3d_add(world->scene, entity->node);
    if (entity->body && world->physics) {
        rt_body3d_set_collision_layer(entity->body, entity->layer);
        rt_body3d_set_collision_mask(entity->body, entity->collision_mask_bits);
        if (entity->node)
            rt_scene_node3d_bind_body(entity->node, entity->body);
        rt_world3d_add(world->physics, entity->body);
    }
    for (int32_t i = 0; i < entity->child_count; ++i)
        game3d_world_spawn_entity_tree(world, entity->children[i], 0, next_id);
}

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
    game3d_release_ref(&world->camera_controller);
    game3d_release_ref(&world->debug_axis_origin);
    game3d_release_ref(&world->effects);
    game3d_release_ref(&world->audio);
    game3d_release_ref(&world->input);
    game3d_release_ref(&world->physics);
    game3d_release_ref(&world->scene);
    game3d_release_ref(&world->camera);
    game3d_release_ref(&world->canvas);
}

static void game3d_world_finalize(void *obj) {
    rt_game3d_world *world = (rt_game3d_world *)obj;
    if (!world)
        return;
    game3d_world_release_runtime(world, 1);
    free(world->entities);
    world->entities = NULL;
    world->entity_capacity = 0;
}

static void game3d_world_install_default_camera(rt_game3d_world *world) {
    void *eye = rt_vec3_new(0.0, 2.0, 6.0);
    void *target = rt_vec3_new(0.0, 1.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);
    rt_camera3d_look_at(world->camera, eye, target, up);
    game3d_release_ref(&eye);
    game3d_release_ref(&target);
    game3d_release_ref(&up);
}

void *rt_game3d_world_new(rt_string title, int64_t width, int64_t height) {
    return rt_game3d_world_new_with_camera(
        title, width, height, RT_GAME3D_DEFAULT_FOV_DEG, RT_GAME3D_DEFAULT_NEAR, RT_GAME3D_DEFAULT_FAR);
}

void *rt_game3d_world_new_with_camera(
    rt_string title, int64_t width, int64_t height, double fov_deg, double near_plane, double far_plane) {
    if (width <= 0 || height <= 0)
        rt_trap("Game3D.World3D.New: dimensions must be positive");
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
    world->clear_r = 0.04;
    world->clear_g = 0.055;
    world->clear_b = 0.065;
    world->debug_axis_size = 1.0;

    world->canvas = rt_canvas3d_new(title, width, height);
    world->scene = rt_scene3d_new();
    world->camera =
        rt_camera3d_new(fov_deg, (double)width / (double)height, near_plane, far_plane);
    world->physics = rt_world3d_new(0.0, -9.81, 0.0);
    world->input = rt_game3d_input_new();
    world->audio = game3d_audio_new(world->camera);
    world->effects = game3d_effects_new(world->canvas, RT_GAME3D_QUALITY_BALANCED);

    if (!world->canvas || !world->scene || !world->camera || !world->physics || !world->input)
        rt_trap("Game3D.World3D.New: component allocation failed");
    game3d_world_install_default_camera(world);
    rt_canvas3d_set_default_lighting(world->canvas);
    rt_canvas3d_set_quality(world->canvas, RT_GAME3D_QUALITY_BALANCED);
    rt_canvas3d_set_frustum_culling(world->canvas, 1);
    rt_canvas3d_set_ambient(world->canvas, 0.28, 0.30, 0.34);
    return world;
}

void rt_game3d_world_destroy(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked_allow_destroyed(obj, "Game3D.World3D.destroy: invalid world");
    if (!world || world->destroyed)
        return;
    game3d_world_release_runtime(world, 1);
    world->destroyed = 1;
}

int8_t rt_game3d_world_is_destroyed(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked_allow_destroyed(obj, "Game3D.World3D.isDestroyed: invalid world");
    return world && world->destroyed ? 1 : 0;
}

void *rt_game3d_world_get_canvas(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Canvas: invalid world");
    return world ? world->canvas : NULL;
}

void *rt_game3d_world_get_camera(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Camera: invalid world");
    return world ? world->camera : NULL;
}

void *rt_game3d_world_get_scene(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Scene: invalid world");
    return world ? world->scene : NULL;
}

void *rt_game3d_world_get_physics(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Physics: invalid world");
    return world ? world->physics : NULL;
}

void *rt_game3d_world_get_input(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Input: invalid world");
    return world ? world->input : NULL;
}

void *rt_game3d_world_get_audio(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Audio: invalid world");
    return world ? world->audio : NULL;
}

void *rt_game3d_world_get_effects(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Effects: invalid world");
    return world ? world->effects : NULL;
}

double rt_game3d_world_get_dt(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Dt: invalid world");
    return world ? world->dt : 0.0;
}

double rt_game3d_world_get_elapsed(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Elapsed: invalid world");
    return world ? world->elapsed : 0.0;
}

int64_t rt_game3d_world_get_frame(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Frame: invalid world");
    return world ? world->frame : 0;
}

void *rt_game3d_world_spawn(void *world_obj, void *entity_obj) {
    rt_game3d_world *world = game3d_world_checked(world_obj, "Game3D.World3D.spawn: invalid world");
    rt_game3d_entity *entity =
        game3d_entity_checked(entity_obj, "Game3D.World3D.spawn: entity must be Entity3D");
    int64_t next_id;
    if (!world || !entity)
        return entity_obj;
    next_id = world->next_entity_id;
    game3d_world_spawn_entity_tree(world, entity, 1, &next_id);
    world->next_entity_id = next_id;
    return entity_obj;
}

void rt_game3d_world_despawn(void *world_obj, void *entity_obj) {
    rt_game3d_world *world = game3d_world_checked(world_obj, "Game3D.World3D.despawn: invalid world");
    rt_game3d_entity *entity =
        game3d_entity_checked(entity_obj, "Game3D.World3D.despawn: entity must be Entity3D");
    if (!world || !entity)
        return;
    if (!entity->spawned || entity->world != world)
        rt_trap("Game3D.World3D.despawn: entity is not spawned in this world");
    game3d_world_despawn_entity_tree(world, entity, 1);
}

void *rt_game3d_world_find_node(void *obj, rt_string name) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.findNode: invalid world");
    return world && world->scene ? rt_scene3d_find(world->scene, name) : NULL;
}

void *rt_game3d_world_find_entity(void *obj, rt_string name) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.findEntity: invalid world");
    const char *needle = name ? rt_string_cstr(name) : "";
    if (!world || !needle)
        return NULL;
    for (int32_t i = 0; i < world->entity_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        const char *entity_name = entity && entity->name ? rt_string_cstr(entity->name) : "";
        if (entity_name && strcmp(entity_name, needle) == 0)
            return entity;
    }
    return NULL;
}

void rt_game3d_world_set_camera_controller(void *obj, void *controller) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.setCameraController: invalid world");
    if (!game3d_camera_controller_is_valid(controller))
        rt_trap("Game3D.World3D.setCameraController: controller must be a built-in Game3D camera controller");
    if (world)
        game3d_assign_ref(&world->camera_controller, controller);
}

void rt_game3d_world_look_at(void *obj, void *target) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.lookAt: invalid world");
    if (!rt_g3d_is_vec3(target))
        rt_trap("Game3D.World3D.lookAt: target must be Vec3");
    if (world && world->camera) {
        void *eye = rt_camera3d_get_position(world->camera);
        void *up = rt_vec3_new(0.0, 1.0, 0.0);
        rt_camera3d_look_at(world->camera, eye, target, up);
        game3d_release_ref(&eye);
        game3d_release_ref(&up);
    }
}

void rt_game3d_world_on_resize(void *obj, int64_t width, int64_t height) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.onResize: invalid world");
    if (width <= 0 || height <= 0)
        rt_trap("Game3D.World3D.onResize: dimensions must be positive");
    if (!world)
        return;
    world->width = width;
    world->height = height;
    if (world->camera)
        rt_camera3d_sync_render_aspect(world->camera, (double)width / (double)height);
}

void rt_game3d_world_set_ambient(void *obj, double r, double g, double b) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setAmbient: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_ambient(world->canvas, r, g, b);
}

void rt_game3d_world_add_light(void *obj, int64_t slot, void *light) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.addLight: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_light(world->canvas, slot, light);
}

void rt_game3d_world_clear_lights(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.clearLights: invalid world");
    if (world && world->canvas)
        rt_canvas3d_clear_lights(world->canvas);
}

void rt_game3d_world_set_skybox(void *obj, void *cubemap) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setSkybox: invalid world");
    if (world && world->canvas) {
        if (cubemap)
            rt_canvas3d_set_skybox(world->canvas, cubemap);
        else
            rt_canvas3d_clear_skybox(world->canvas);
    }
}

void rt_game3d_world_set_fog(
    void *obj, double r, double g, double b, double near_plane, double far_plane) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setFog: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_fog(world->canvas, near_plane, far_plane, r, g, b);
}

void rt_game3d_world_set_quality(void *obj, int64_t quality) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setQuality: invalid world");
    if (!world || !world->canvas)
        return;
    rt_canvas3d_set_quality(world->canvas, quality);
    rt_game3d_effects *effects = (rt_game3d_effects *)world->effects;
    if (effects) {
        game3d_release_ref(&effects->postfx);
        effects->postfx = rt_postfx3d_new_quality(world->canvas, quality);
        if (effects->postfx)
            rt_canvas3d_set_post_fx(world->canvas, effects->postfx);
    }
}

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

static void game3d_collision_event_finalize(void *obj) {
    rt_game3d_collision_event *event = (rt_game3d_collision_event *)obj;
    if (!event)
        return;
    game3d_release_ref(&event->raw);
    game3d_release_ref(&event->b);
    game3d_release_ref(&event->a);
}

static void *game3d_collision_event_wrap(rt_game3d_world *world, int64_t phase, void *raw_event) {
    rt_game3d_collision_event *event;
    if (!raw_event)
        return NULL;
    event = (rt_game3d_collision_event *)rt_obj_new_i64(
        RT_G3D_GAME3D_COLLISION_EVENT_CLASS_ID, (int64_t)sizeof(*event));
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

static void *game3d_world_raw_collision_event(
    rt_game3d_world *world, int64_t phase, int64_t index, int64_t *actual_phase) {
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

void *rt_game3d_world_collision_event(void *obj, int64_t phase, int64_t index) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.collisionEvent: invalid world");
    if (!world || !world->physics)
        return NULL;
    int64_t actual_phase = phase;
    void *raw_event = game3d_world_raw_collision_event(world, phase, index, &actual_phase);
    return game3d_collision_event_wrap(world, actual_phase, raw_event);
}

void rt_game3d_world_clear_collision_events(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.clearCollisionEvents: invalid world");
    if (world && world->physics)
        rt_world3d_clear_collision_events(world->physics);
}

int64_t rt_game3d_collision_event_get_phase(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_phase: invalid event");
    return event ? event->phase : RT_GAME3D_COLLISION_ANY;
}

void *rt_game3d_collision_event_get_a(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_a: invalid event");
    return event ? event->a : NULL;
}

void *rt_game3d_collision_event_get_b(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_b: invalid event");
    return event ? event->b : NULL;
}

void *rt_game3d_collision_event_get_raw(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_raw: invalid event");
    return event ? event->raw : NULL;
}

int8_t rt_game3d_collision_event_get_is_trigger(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_isTrigger: invalid event");
    return event && event->raw ? rt_collision_event3d_get_is_trigger(event->raw) : 0;
}

double rt_game3d_collision_event_get_relative_speed(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_relativeSpeed: invalid event");
    return event && event->raw ? rt_collision_event3d_get_relative_speed(event->raw) : 0.0;
}

double rt_game3d_collision_event_get_normal_impulse(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.get_normalImpulse: invalid event");
    return event && event->raw ? rt_collision_event3d_get_normal_impulse(event->raw) : 0.0;
}

void *rt_game3d_collision_event_point(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.point: invalid event");
    return event && event->raw ? rt_collision_event3d_get_contact_point(event->raw, 0)
                               : rt_vec3_new(0.0, 0.0, 0.0);
}

void *rt_game3d_collision_event_normal(void *obj) {
    rt_game3d_collision_event *event =
        game3d_collision_event_checked(obj, "Game3D.Collision3DEvent.normal: invalid event");
    return event && event->raw ? rt_collision_event3d_get_contact_normal(event->raw, 0)
                               : rt_vec3_new(0.0, 1.0, 0.0);
}

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

void rt_game3d_world_set_gravity(void *obj, double x, double y, double z) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setGravity: invalid world");
    if (world && world->physics)
        rt_world3d_set_gravity(world->physics, x, y, z);
}

int8_t rt_game3d_world_tick(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.tick: invalid world");
    if (!world || !world->canvas)
        return 0;
    rt_canvas3d_poll(world->canvas);
    rt_game3d_input_update(world->input);
    world->dt = game3d_clamp_dt(rt_canvas3d_get_delta_time_sec(world->canvas));
    world->elapsed += world->dt;
    world->frame += 1;
    if (world->camera && world->width > 0 && world->height > 0)
        rt_camera3d_sync_render_aspect(world->camera, (double)world->width / (double)world->height);
    return rt_canvas3d_should_close(world->canvas) ? 0 : 1;
}

void rt_game3d_world_step_simulation(void *obj, double step_sec) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.stepSimulation: invalid world");
    double dt = game3d_clamp_dt(step_sec);
    if (!world)
        return;
    game3d_world_update_controller(world, dt);
    game3d_world_update_animations(world, dt);
    if (world->physics)
        rt_world3d_step(world->physics, dt);
    if (world->scene)
        rt_scene3d_sync_bindings(world->scene, dt);
    rt_audio3d_sync_bindings(dt);
    game3d_world_late_update_controller(world, dt);
}

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
        rt_collider3d_get_local_bounds_raw(collider, mn_raw, mx_raw);
        void *pos = rt_body3d_get_position(entity->body);
        double px = pos ? rt_vec3_x(pos) : 0.0;
        double py = pos ? rt_vec3_y(pos) : 0.0;
        double pz = pos ? rt_vec3_z(pos) : 0.0;
        void *mn = rt_vec3_new(px + mn_raw[0], py + mn_raw[1], pz + mn_raw[2]);
        void *mx = rt_vec3_new(px + mx_raw[0], py + mx_raw[1], pz + mx_raw[2]);
        rt_canvas3d_draw_aabb_wire(world->canvas, mn, mx, 0xFFCC33);
        game3d_release_ref(&mx);
        game3d_release_ref(&mn);
        game3d_release_ref(&pos);
    }
}

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
    rt_game3d_world *world,
    int64_t x,
    int64_t y,
    int64_t color,
    const char *fmt,
    ...) RT_GAME3D_PRINTF(5, 6);

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

static void game3d_world_draw_debug_overlay(rt_game3d_world *world) {
    if (!world || !world->canvas || !world->debug_overlay_enabled)
        return;
    rt_canvas3d_begin_overlay(world->canvas);
    rt_canvas3d_draw_rect2d(world->canvas, 8, 8, 250, 106, 0x111820);
    game3d_world_debug_text(world, 14, 14, "Game3D Debug", 0xFFFFFF);
    rt_string backend = rt_canvas3d_get_backend(world->canvas);
    const char *backend_cs = backend ? rt_string_cstr(backend) : "unknown";
    game3d_world_debug_textf(
        world,
        14,
        28,
        0xD7E7FF,
        "backend %s fps %lld",
        backend_cs ? backend_cs : "unknown",
        (long long)rt_canvas3d_get_fps(world->canvas));
    game3d_world_debug_textf(
        world,
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
        game3d_world_debug_textf(
            world,
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

void rt_game3d_world_begin_frame(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.beginFrame: invalid world");
    if (!world || !world->canvas || !world->camera)
        return;
    rt_canvas3d_clear(world->canvas, world->clear_r, world->clear_g, world->clear_b);
    rt_canvas3d_begin(world->canvas, world->camera);
}

void rt_game3d_world_draw_scene(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.drawScene: invalid world");
    if (world && world->scene && world->canvas && world->camera)
        rt_scene3d_draw(world->scene, world->canvas, world->camera);
}

void rt_game3d_world_draw_effects(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.drawEffects: invalid world");
    if (!world || !world->canvas)
        return;
    if (world->debug_axes_enabled && world->debug_axis_origin)
        rt_canvas3d_draw_axis(world->canvas, world->debug_axis_origin, world->debug_axis_size);
    if (world->debug_physics_enabled)
        game3d_world_debug_draw_physics(world);
}

void rt_game3d_world_end_scene(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.endScene: invalid world");
    if (world && world->canvas) {
        rt_canvas3d_end(world->canvas);
        game3d_world_draw_debug_overlay(world);
    }
}

void rt_game3d_world_draw_overlay(void *obj, void *overlay) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.drawOverlay: invalid world");
    rt_game3d_overlay_fn fn = game3d_overlay_callback_checked(
        overlay,
        "Game3D.World3D.drawOverlay: callback must be a native function pointer; use manual overlay calls from interpreted Zia");
    if (!world || !world->canvas)
        return;
    rt_canvas3d_begin_overlay(world->canvas);
    if (fn)
        fn();
    rt_canvas3d_end_overlay(world->canvas);
}

void *rt_game3d_world_capture_final_frame(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.captureFinalFrame: invalid world");
    return world && world->canvas ? rt_canvas3d_screenshot_final(world->canvas) : NULL;
}

void rt_game3d_world_present(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.present: invalid world");
    if (world && world->canvas)
        rt_canvas3d_flip(world->canvas);
}

static void game3d_world_render_once(rt_game3d_world *world, void *overlay) {
    rt_game3d_world_begin_frame(world);
    rt_game3d_world_draw_scene(world);
    rt_game3d_world_draw_effects(world);
    rt_game3d_world_end_scene(world);
    if (overlay)
        rt_game3d_world_draw_overlay(world, overlay);
    rt_game3d_world_present(world);
}

void rt_game3d_world_run(void *obj, void *update) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.run: invalid world");
    rt_game3d_update_fn fn = game3d_update_callback_checked(
        update,
        "Game3D.World3D.run: callback must be a native function pointer; use tick/step/manual frame APIs from interpreted Zia");
    while (world && rt_game3d_world_tick(world)) {
        if (fn)
            fn(world->dt);
        rt_game3d_world_step_simulation(world, world->dt);
        game3d_world_render_once(world, NULL);
    }
}

void rt_game3d_world_run_with_overlay(void *obj, void *update, void *overlay) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.runWithOverlay: invalid world");
    rt_game3d_update_fn fn = game3d_update_callback_checked(
        update,
        "Game3D.World3D.runWithOverlay: update callback must be a native function pointer; use tick/step/manual frame APIs from interpreted Zia");
    (void)game3d_overlay_callback_checked(
        overlay,
        "Game3D.World3D.runWithOverlay: overlay callback must be a native function pointer; use manual overlay calls from interpreted Zia");
    while (world && rt_game3d_world_tick(world)) {
        if (fn)
            fn(world->dt);
        rt_game3d_world_step_simulation(world, world->dt);
        game3d_world_render_once(world, overlay);
    }
}

void rt_game3d_world_run_fixed(void *obj, double step_sec, void *update) {
    rt_game3d_world_run_fixed_with_overlay(obj, step_sec, update, NULL);
}

void rt_game3d_world_run_fixed_with_overlay(void *obj, double step_sec, void *update, void *overlay) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.runFixedWithOverlay: invalid world");
    rt_game3d_update_fn fn = game3d_update_callback_checked(
        update,
        "Game3D.World3D.runFixedWithOverlay: update callback must be a native function pointer; use tick/step/manual frame APIs from interpreted Zia");
    (void)game3d_overlay_callback_checked(
        overlay,
        "Game3D.World3D.runFixedWithOverlay: overlay callback must be a native function pointer; use manual overlay calls from interpreted Zia");
    double fixed = game3d_clamp_dt(step_sec);
    double accumulator = 0.0;
    while (world && rt_game3d_world_tick(world)) {
        accumulator += world->dt;
        while (accumulator >= fixed) {
            if (fn)
                fn(fixed);
            rt_game3d_world_step_simulation(world, fixed);
            accumulator -= fixed;
        }
        game3d_world_render_once(world, overlay);
    }
}

void rt_game3d_world_run_frames(void *obj, int64_t frame_count, double step_sec, void *update) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.runFrames: invalid world");
    rt_game3d_update_fn fn = game3d_update_callback_checked(
        update,
        "Game3D.World3D.runFrames: callback must be a native function pointer; use runFramesOnly/manual frame APIs from interpreted Zia");
    double fixed = game3d_clamp_dt(step_sec);
    if (!world || frame_count < 0)
        return;
    if (world->canvas) {
        rt_canvas3d_set_input_source(world->canvas, 1);
        rt_canvas3d_set_clock_source(world->canvas, 1);
        rt_canvas3d_set_synthetic_delta_time_sec(world->canvas, fixed);
    }
    for (int64_t i = 0; i < frame_count; ++i) {
        if (world->canvas)
            rt_canvas3d_advance_synthetic_frame(world->canvas);
        rt_game3d_input_update(world->input);
        world->dt = fixed;
        world->elapsed += fixed;
        world->frame += 1;
        if (fn)
            fn(fixed);
        rt_game3d_world_step_simulation(world, fixed);
        game3d_world_render_once(world, NULL);
    }
}

void rt_game3d_world_run_frames_only(void *obj, int64_t frame_count, double step_sec) {
    rt_game3d_world_run_frames(obj, frame_count, step_sec, NULL);
}
