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

#include "rt_audio3d.h"
#include "rt_audiolistener3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_input.h"
#include "rt_mat4.h"
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
    int8_t destroyed;
} rt_game3d_world;

typedef void (*rt_game3d_update_fn)(double dt);
typedef void (*rt_game3d_overlay_fn)(void);

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

void *rt_game3d_entity_attach_body(void *obj, void *body) {
    rt_game3d_entity *entity = game3d_entity_checked(obj, "Game3D.Entity3D.attachBody: invalid entity");
    if (body && !rt_g3d_has_class(body, RT_G3D_BODY3D_CLASS_ID))
        rt_trap("Game3D.Entity3D.attachBody: expected Physics3DBody");
    if (entity) {
        game3d_assign_ref(&entity->body, body);
        if (body) {
            rt_body3d_set_collision_layer(body, entity->layer);
            rt_body3d_set_collision_mask(body, entity->collision_mask_bits);
            if (entity->node)
                rt_scene_node3d_bind_body(entity->node, body);
            if (entity->spawned && entity->world)
                rt_world3d_add(((rt_game3d_world *)entity->world)->physics, body);
        } else if (entity->node) {
            rt_scene_node3d_clear_body_binding(entity->node);
        }
    }
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
        rt_canvas3d_set_fog(world->canvas, r, g, b, near_plane, far_plane);
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
        return rt_world3d_get_collision_event_count(world->physics);
    }
}

void *rt_game3d_world_collision_event(void *obj, int64_t phase, int64_t index) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.collisionEvent: invalid world");
    if (!world || !world->physics)
        return NULL;
    switch (phase) {
    case RT_GAME3D_COLLISION_ENTER:
        return rt_world3d_get_enter_event(world->physics, index);
    case RT_GAME3D_COLLISION_STAY:
        return rt_world3d_get_stay_event(world->physics, index);
    case RT_GAME3D_COLLISION_EXIT:
        return rt_world3d_get_exit_event(world->physics, index);
    case RT_GAME3D_COLLISION_ANY:
    default:
        return rt_world3d_get_collision_event(world->physics, index);
    }
}

void rt_game3d_world_clear_collision_events(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.clearCollisionEvents: invalid world");
    if (world && world->physics)
        rt_world3d_clear_collision_events(world->physics);
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
    if (world->physics)
        rt_world3d_step(world->physics, dt);
    if (world->scene)
        rt_scene3d_sync_bindings(world->scene, dt);
    rt_audio3d_sync_bindings(dt);
}

void rt_game3d_world_begin_frame(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.beginFrame: invalid world");
    if (!world || !world->canvas || !world->camera)
        return;
    rt_canvas3d_clear(world->canvas, 0.04, 0.055, 0.065);
    rt_canvas3d_begin(world->canvas, world->camera);
}

void rt_game3d_world_draw_scene(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.drawScene: invalid world");
    if (world && world->scene && world->canvas && world->camera)
        rt_scene3d_draw(world->scene, world->canvas, world->camera);
}

void rt_game3d_world_draw_effects(void *obj) {
    (void)game3d_world_checked(obj, "Game3D.World3D.drawEffects: invalid world");
}

void rt_game3d_world_end_scene(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.endScene: invalid world");
    if (world && world->canvas)
        rt_canvas3d_end(world->canvas);
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
