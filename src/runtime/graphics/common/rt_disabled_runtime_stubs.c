//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/common/rt_disabled_runtime_stubs.c
// Purpose: Supplemental exported runtime stubs for graphics-disabled builds.
//
//===----------------------------------------------------------------------===//

#include "rt_string.h"

#include <stdint.h>
#include <stddef.h>

typedef struct rt_gltf_preload_bundle rt_gltf_preload_bundle;

static rt_string disabled_empty_string(void) {
    return rt_string_from_bytes("", 0);
}

void rt_widget_set_preferred_size(void *widget, double width, double height) {
    (void)widget;
    (void)width;
    (void)height;
}

void rt_widget_set_max_size(void *widget, double width, double height) {
    (void)widget;
    (void)width;
    (void)height;
}

void rt_progressbar_set_style(void *progress, int64_t style) {
    (void)progress;
    (void)style;
}

void rt_progressbar_show_percentage(void *progress, int64_t show) {
    (void)progress;
    (void)show;
}

void *rt_gui_test_harness_new(void) { return NULL; }
void rt_gui_test_harness_clear(void *harness) { (void)harness; }
int64_t rt_gui_test_harness_tick(void *harness, int64_t ms) {
    (void)harness;
    (void)ms;
    return 0;
}
void rt_gui_test_harness_register_widget(void *harness,
                                         rt_string id,
                                         rt_string name,
                                         rt_string type,
                                         int64_t x,
                                         int64_t y,
                                         int64_t w,
                                         int64_t h) {
    (void)harness;
    (void)id;
    (void)name;
    (void)type;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}
void *rt_gui_test_harness_find_by_id(void *harness, rt_string id) {
    (void)harness;
    (void)id;
    return NULL;
}
void *rt_gui_test_harness_find_by_name(void *harness, rt_string name) {
    (void)harness;
    (void)name;
    return NULL;
}
void *rt_gui_test_harness_find_by_type(void *harness, rt_string type) {
    (void)harness;
    (void)type;
    return NULL;
}
void rt_gui_test_harness_send_key(void *harness, rt_string id, int64_t key) {
    (void)harness;
    (void)id;
    (void)key;
}
void rt_gui_test_harness_send_mouse(void *harness,
                                    rt_string id,
                                    int64_t x,
                                    int64_t y,
                                    int64_t buttons) {
    (void)harness;
    (void)id;
    (void)x;
    (void)y;
    (void)buttons;
}
rt_string rt_gui_test_harness_get_focus(void *harness) {
    (void)harness;
    return disabled_empty_string();
}
void *rt_gui_test_harness_focus_order(void *harness) {
    (void)harness;
    return NULL;
}
void *rt_gui_test_harness_capture_region(
    void *harness, int64_t x, int64_t y, int64_t w, int64_t h) {
    (void)harness;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return NULL;
}
int8_t rt_gui_test_harness_assert_nonblank(void *harness) {
    (void)harness;
    return 0;
}

void *rt_virtual_list_new(int64_t row_count, int64_t row_height, int64_t viewport_height) {
    (void)row_count;
    (void)row_height;
    (void)viewport_height;
    return NULL;
}
void rt_virtual_list_set_count(void *list, int64_t count) {
    (void)list;
    (void)count;
}
void rt_virtual_list_set_row_id(void *list, int64_t row, rt_string id) {
    (void)list;
    (void)row;
    (void)id;
}
void *rt_virtual_list_visible_range(void *list, int64_t scroll_y) {
    (void)list;
    (void)scroll_y;
    return NULL;
}
void rt_virtual_list_select_id(void *list, rt_string id) {
    (void)list;
    (void)id;
}
rt_string rt_virtual_list_get_selected_id(void *list) {
    (void)list;
    return disabled_empty_string();
}
int64_t rt_virtual_list_get_selected_index(void *list) {
    (void)list;
    return -1;
}

void *rt_virtual_tree_new(void) { return NULL; }
void rt_virtual_tree_add_node(void *tree, rt_string id, rt_string parent, rt_string label) {
    (void)tree;
    (void)id;
    (void)parent;
    (void)label;
}
void *rt_virtual_tree_expand(void *tree, rt_string id) {
    (void)tree;
    (void)id;
    return NULL;
}
void rt_virtual_tree_collapse(void *tree, rt_string id) {
    (void)tree;
    (void)id;
}
void rt_virtual_tree_select_id(void *tree, rt_string id) {
    (void)tree;
    (void)id;
}
rt_string rt_virtual_tree_get_selected_id(void *tree) {
    (void)tree;
    return disabled_empty_string();
}
void *rt_virtual_tree_visible_rows(void *tree) {
    (void)tree;
    return NULL;
}
void rt_virtual_tree_refresh_subtree(void *tree, rt_string id) {
    (void)tree;
    (void)id;
}

void *rt_command_state_new(rt_string id, rt_string label) {
    (void)id;
    (void)label;
    return NULL;
}
void rt_command_state_set_enabled(void *state, int8_t enabled) {
    (void)state;
    (void)enabled;
}
int8_t rt_command_state_get_enabled(void *state) {
    (void)state;
    return 0;
}
void rt_command_state_set_checked(void *state, int8_t checked) {
    (void)state;
    (void)checked;
}
int8_t rt_command_state_get_checked(void *state) {
    (void)state;
    return 0;
}
void rt_command_state_set_accessible(void *state, rt_string label, rt_string description) {
    (void)state;
    (void)label;
    (void)description;
}
void *rt_command_state_snapshot(void *state) {
    (void)state;
    return NULL;
}

void *rt_command_new(rt_string id, rt_string title) {
    (void)id;
    (void)title;
    return NULL;
}
rt_string rt_command_get_id(void *command) {
    (void)command;
    return disabled_empty_string();
}
rt_string rt_command_get_title(void *command) {
    (void)command;
    return disabled_empty_string();
}
void rt_command_set_shortcut(void *command, rt_string keys) {
    (void)command;
    (void)keys;
}
rt_string rt_command_get_shortcut(void *command) {
    (void)command;
    return disabled_empty_string();
}
void rt_command_set_enabled(void *command, int8_t enabled) {
    (void)command;
    (void)enabled;
}
int8_t rt_command_is_enabled(void *command) {
    (void)command;
    return 0;
}
void rt_command_set_checkable(void *command, int8_t checkable) {
    (void)command;
    (void)checkable;
}
int8_t rt_command_is_checkable(void *command) {
    (void)command;
    return 0;
}
void rt_command_set_checked(void *command, int8_t checked) {
    (void)command;
    (void)checked;
}
int8_t rt_command_is_checked(void *command) {
    (void)command;
    return 0;
}
void rt_command_bind_menu_item(void *command, void *item) {
    (void)command;
    (void)item;
}
void rt_command_bind_toolbar_item(void *command, void *item) {
    (void)command;
    (void)item;
}
int8_t rt_command_poll(void *command) {
    (void)command;
    return 0;
}
int8_t rt_command_was_invoked(void *command) {
    (void)command;
    return 0;
}
void *rt_command_snapshot(void *command) {
    (void)command;
    return NULL;
}

void *rt_command_registry_new(void) { return NULL; }
void rt_command_registry_add(void *registry, void *command) {
    (void)registry;
    (void)command;
}
int64_t rt_command_registry_count(void *registry) {
    (void)registry;
    return 0;
}
void *rt_command_registry_find(void *registry, rt_string id) {
    (void)registry;
    (void)id;
    return NULL;
}
void rt_command_registry_bind_palette(void *registry, void *palette) {
    (void)registry;
    (void)palette;
}
rt_string rt_command_registry_poll(void *registry) {
    (void)registry;
    return disabled_empty_string();
}
void rt_command_registry_clear(void *registry) { (void)registry; }

double rt_accessibility_contrast_ratio(int64_t fg_rgb, int64_t bg_rgb) {
    (void)fg_rgb;
    (void)bg_rgb;
    return 1.0;
}
int8_t rt_accessibility_meets_contrast(int64_t fg_rgb, int64_t bg_rgb, double min_ratio) {
    (void)fg_rgb;
    (void)bg_rgb;
    return min_ratio <= 1.0 ? 1 : 0;
}
void *rt_accessibility_high_contrast_tokens(void) { return NULL; }

void rt_canvas3d_begin_overlay(void *obj) { (void)obj; }
void rt_canvas3d_end_overlay(void *obj) { (void)obj; }
void rt_canvas3d_clear_overlay(void *obj) { (void)obj; }
void rt_canvas3d_finalize_frame(void *obj) { (void)obj; }
int64_t rt_canvas3d_get_frame_gpu_time_us(void *obj) {
    (void)obj;
    return 0;
}
int64_t rt_canvas3d_get_draws_submitted(void *obj) {
    (void)obj;
    return 0;
}
int64_t rt_canvas3d_get_aabb_transforms(void *obj) {
    (void)obj;
    return 0;
}
int64_t rt_canvas3d_get_sort_passes(void *obj) {
    (void)obj;
    return 0;
}
int64_t rt_canvas3d_get_backend_state_changes(void *obj) {
    (void)obj;
    return 0;
}
void *rt_canvas3d_screenshot_final(void *obj) {
    (void)obj;
    return NULL;
}
int8_t rt_canvas3d_get_frame_finalized(void *obj) {
    (void)obj;
    return 0;
}

void rt_mesh3d_reserve(void *obj, int64_t vertex_count, int64_t triangle_count) {
    (void)obj;
    (void)vertex_count;
    (void)triangle_count;
}
void rt_material3d_set_anisotropy(void *obj, int64_t anisotropy) {
    (void)obj;
    (void)anisotropy;
}
int64_t rt_material3d_get_anisotropy(void *obj) {
    (void)obj;
    return 1;
}
void rt_scene3d_rebase_origin(void *scene, double dx, double dy, double dz) {
    (void)scene;
    (void)dx;
    (void)dy;
    (void)dz;
}
int8_t rt_scene_node3d_try_add_child(void *node, void *child) {
    (void)node;
    (void)child;
    return 0;
}
int8_t rt_scene_node3d_get_world_position_components(void *node,
                                                     double *x,
                                                     double *y,
                                                     double *z) {
    (void)node;
    if (x)
        *x = 0.0;
    if (y)
        *y = 0.0;
    if (z)
        *z = 0.0;
    return 0;
}

int64_t rt_model3d_get_scene_count(void *model) {
    (void)model;
    return 0;
}
int64_t rt_model3d_get_camera_count(void *model) {
    (void)model;
    return 0;
}
void *rt_model3d_get_camera(void *model, int64_t index) {
    (void)model;
    (void)index;
    return NULL;
}
rt_string rt_model3d_get_scene_name(void *model, int64_t index) {
    (void)model;
    (void)index;
    return disabled_empty_string();
}
void *rt_model3d_instantiate_scene_at(void *model, int64_t index) {
    (void)model;
    (void)index;
    return NULL;
}
void *rt_model3d_load_preloaded_gltf_bundle(rt_string path,
                                            rt_gltf_preload_bundle *bundle,
                                            int8_t asset_path) {
    (void)path;
    (void)bundle;
    (void)asset_path;
    return NULL;
}
void *rt_model3d_load_preloaded_fbx(rt_string path,
                                    const uint8_t *data,
                                    size_t size,
                                    int8_t asset_path) {
    (void)path;
    (void)data;
    (void)size;
    (void)asset_path;
    return NULL;
}

int8_t rt_world3d_contains_body(void *world, void *body) {
    (void)world;
    (void)body;
    return 0;
}
int64_t rt_world3d_get_last_ccd_clamped_body_count(void *world) {
    (void)world;
    return 0;
}
int64_t rt_world3d_get_ccd_substep_clamped_body_count(void *world) {
    (void)world;
    return 0;
}
int64_t rt_world3d_get_broadphase_fallback_count(void *world) {
    (void)world;
    return 0;
}
int64_t rt_world3d_get_query_broadphase_rebuild_count(void *world) {
    (void)world;
    return 0;
}
void rt_world3d_rebase_origin(void *world, double dx, double dy, double dz) {
    (void)world;
    (void)dx;
    (void)dy;
    (void)dz;
}

int8_t rt_navmesh3d_export(void *navmesh, rt_string path) {
    (void)navmesh;
    (void)path;
    return 0;
}
void *rt_navmesh3d_import(rt_string path) {
    (void)path;
    return NULL;
}
void rt_ik_solver3d_set_ground_normal(void *solver, void *normal) {
    (void)solver;
    (void)normal;
}
double rt_anim_controller3d_get_state_time(void *controller) {
    (void)controller;
    return 0.0;
}
int8_t rt_anim_controller3d_is_state_playing(void *controller) {
    (void)controller;
    return 0;
}
void rt_anim_controller3d_set_bone_lod(void *controller, int64_t lod) {
    (void)controller;
    (void)lod;
}

void rt_particles3d_rebase_origin(void *particles, double dx, double dy, double dz) {
    (void)particles;
    (void)dx;
    (void)dy;
    (void)dz;
}
void rt_decal3d_rebase_origin(void *decal, double dx, double dy, double dz) {
    (void)decal;
    (void)dx;
    (void)dy;
    (void)dz;
}
void rt_sprite3d_rebase_origin(void *sprite, double dx, double dy, double dz) {
    (void)sprite;
    (void)dx;
    (void)dy;
    (void)dz;
}
void *rt_terrain3d_build_heightmap_pixels(void *terrain) {
    (void)terrain;
    return NULL;
}
int64_t rt_terrain3d_stitch_edge(void *terrain,
                                 int64_t edge,
                                 void *neighbor,
                                 int64_t neighbor_edge) {
    (void)terrain;
    (void)edge;
    (void)neighbor;
    (void)neighbor_edge;
    return 0;
}
void *rt_terrain3d_build_nav_mesh(void *terrain, int64_t step) {
    (void)terrain;
    (void)step;
    return NULL;
}
void rt_canvas3d_draw_terrain_at(void *canvas, void *terrain, double x, double y, double z) {
    (void)canvas;
    (void)terrain;
    (void)x;
    (void)y;
    (void)z;
}

rt_gltf_preload_bundle *rt_gltf_preload_bundle_create_cstr(const char *path,
                                                           int8_t asset_path,
                                                           const uint8_t *data,
                                                           size_t size) {
    (void)path;
    (void)asset_path;
    (void)data;
    (void)size;
    return NULL;
}
void rt_gltf_preload_bundle_free(rt_gltf_preload_bundle *bundle) {
    (void)bundle;
}
size_t rt_gltf_preload_bundle_decoded_image_bytes(const rt_gltf_preload_bundle *bundle) {
    (void)bundle;
    return 0;
}
size_t rt_gltf_preload_bundle_next_decoded_image_slice_bytes(
    const rt_gltf_preload_bundle *bundle, size_t max_bytes) {
    (void)bundle;
    (void)max_bytes;
    return 0;
}
size_t rt_gltf_preload_bundle_prepare_decoded_image_slice(rt_gltf_preload_bundle *bundle,
                                                          size_t max_bytes) {
    (void)bundle;
    (void)max_bytes;
    return 0;
}

int8_t rt_camera3d_get_position_components(void *camera, double *x, double *y, double *z) {
    (void)camera;
    if (x)
        *x = 0.0;
    if (y)
        *y = 0.0;
    if (z)
        *z = 0.0;
    return 0;
}
void rt_camera3d_look_at_components(void *camera,
                                    double x,
                                    double y,
                                    double z,
                                    double up_x,
                                    double up_y,
                                    double up_z) {
    (void)camera;
    (void)x;
    (void)y;
    (void)z;
    (void)up_x;
    (void)up_y;
    (void)up_z;
}
void rt_camera3d_orbit_components(void *camera,
                                  double target_x,
                                  double target_y,
                                  double target_z,
                                  double yaw,
                                  double pitch,
                                  double distance) {
    (void)camera;
    (void)target_x;
    (void)target_y;
    (void)target_z;
    (void)yaw;
    (void)pitch;
    (void)distance;
}

void vgfx_show_cursor(void) {}
void vgfx_hide_cursor(void) {}
