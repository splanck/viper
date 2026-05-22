//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/tests/runtime/RTSceneEditorTests.cpp
// Purpose: Tests for scene-owned editor primitives and scaled tile hit testing.
//
//===----------------------------------------------------------------------===//

#include "rt_scene_editor.h"
#include "rt_tilemap.h"

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static std::string to_std(rt_string value) {
    std::string out(rt_string_cstr(value), (size_t)rt_str_len(value));
    rt_string_unref(value);
    return out;
}

int main() {
    void *scene = rt_game_scene_new(4, 3, 16, 16);
    assert(rt_game_scene_get_width(scene) == 4);
    assert(rt_game_scene_layer_count(scene) == 1);

    int64_t fg = rt_game_scene_add_layer(scene, rt_const_cstr("foreground"));
    rt_game_scene_set_layer_asset(scene, fg, rt_const_cstr("tiles/terrain.png"));
    rt_game_scene_fill_tiles(scene, fg, 1, 1, 2, 1, 7);
    assert(rt_game_scene_get_tile(scene, fg, 1, 1) == 7);
    assert(rt_game_scene_get_tile(scene, fg, 2, 1) == 7);
    rt_game_scene_set_layer_visible(scene, fg, 0);
    assert(rt_game_scene_layer_visible(scene, fg) == 0);

    int64_t obj = rt_game_scene_add_object(
        scene, rt_const_cstr("Player"), rt_const_cstr("player"), 10, 20);
    rt_game_scene_set_object_property(scene, obj, rt_const_cstr("sprite"), rt_const_cstr("hero.png"));
    assert(to_std(rt_game_scene_get_object_property(scene, obj, rt_const_cstr("sprite"))) ==
           "hero.png");
    rt_game_scene_set_property(scene, rt_const_cstr("tileset"), rt_const_cstr("tiles/base.png"));
    void *assets = rt_game_scene_asset_paths(scene);
    assert(rt_seq_len(assets) == 3);

    rt_string json = rt_game_scene_to_json(scene);
    void *loaded = rt_game_scene_load_json(json);
    rt_string_unref(json);
    assert(rt_game_scene_get_width(loaded) == 4);
    assert(rt_game_scene_layer_count(loaded) == 2);
    assert(rt_game_scene_get_tile(loaded, 1, 1, 1) == 7);
    assert(rt_game_scene_object_count(loaded) == 1);
    assert(to_std(rt_game_scene_object_id(loaded, 0)) == "player");

    std::filesystem::path path = std::filesystem::temp_directory_path() / "viper_scene_editor_test.json";
    std::string path_text = path.string();
    rt_string path_s = rt_string_from_bytes(path_text.data(), path_text.size());
    assert(rt_game_scene_save_file(scene, path_s) == 1);
    void *from_file = rt_game_scene_load_file(path_s);
    rt_string_unref(path_s);
    assert(rt_game_scene_get_tile(from_file, 1, 2, 1) == 7);
    std::filesystem::remove(path);

    void *tilemap = rt_tilemap_new(10, 10, 16, 16);
    rt_tilemap_set_tile(tilemap, 4, 3, 9);
    void *hit = rt_tilemap_hit_test_scaled(tilemap, 40, 30, -24, -18, 100);
    assert(rt_map_get_bool(hit, rt_const_cstr("inBounds")) == 1);
    assert(rt_map_get_int(hit, rt_const_cstr("tileX")) == 4);
    assert(rt_map_get_int(hit, rt_const_cstr("tileY")) == 3);
    assert(rt_map_get_int(hit, rt_const_cstr("tile")) == 9);
    hit = rt_tilemap_hit_test_scaled(tilemap, 16, 16, 0, 0, 200);
    assert(rt_map_get_int(hit, rt_const_cstr("tileX")) == 0);
    assert(rt_map_get_int(hit, rt_const_cstr("tileY")) == 0);
    return 0;
}
