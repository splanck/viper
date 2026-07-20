//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/tests/runtime/RTSceneEditorTests.cpp
// Purpose: Tests for scene-owned editor primitives, Tiled JSON/TMX import, and
//   scaled tile hit testing.
//
// Key invariants:
//   - Canonical SceneDocument editing and compatibility loads remain stable.
//   - Tiled imports preserve supported layers, objects, properties, collision,
//     animation, and render-ready tileset binding, while rejecting lossy flags.
//
// Ownership/Lifetime:
//   - Test-owned runtime handles live for the process-length test invocation.
//   - Temporary external-scene fixtures are removed before the test exits.
//
// Links: src/runtime/game/rt_scene_editor.cpp,
//   src/runtime/game/rt_tiled_import.cpp, docs/adr/0140-tiled-map-and-scene-import.md
//
//===----------------------------------------------------------------------===//

#include "rt_asset.h"
#include "rt_crc32.h"
#include "rt_graphics2d.h"
#include "rt_pixels.h"
#include "rt_scene_editor.h"
#include "rt_tilemap.h"

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_result.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ZpakWriter.hpp"

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static std::string to_std(rt_string value) {
    std::string out(rt_string_cstr(value), (size_t)rt_str_len(value));
    rt_string_unref(value);
    return out;
}

static std::string read_text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    assert(in && "fixture should exist");
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void write_text(const std::filesystem::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out && "fixture output should open");
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    assert(out.good() && "fixture output should be complete");
}

static std::vector<uint8_t> read_bytes(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    assert(in && "binary fixture should exist");
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

static std::vector<uint8_t> tiled_gid_bytes(const std::vector<uint32_t> &gids) {
    std::vector<uint8_t> out;
    out.reserve(gids.size() * 4);
    for (uint32_t gid : gids) {
        out.push_back(static_cast<uint8_t>(gid));
        out.push_back(static_cast<uint8_t>(gid >> 8));
        out.push_back(static_cast<uint8_t>(gid >> 16));
        out.push_back(static_cast<uint8_t>(gid >> 24));
    }
    return out;
}

static uint32_t adler32(const std::vector<uint8_t> &bytes) {
    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t byte : bytes) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static std::vector<uint8_t> stored_deflate(const std::vector<uint8_t> &bytes) {
    assert(bytes.size() <= 65535u);
    uint16_t length = static_cast<uint16_t>(bytes.size());
    uint16_t inverse = static_cast<uint16_t>(~length);
    std::vector<uint8_t> out = {0x01,
                                static_cast<uint8_t>(length),
                                static_cast<uint8_t>(length >> 8),
                                static_cast<uint8_t>(inverse),
                                static_cast<uint8_t>(inverse >> 8)};
    out.insert(out.end(), bytes.begin(), bytes.end());
    return out;
}

static std::vector<uint8_t> zlib_stored(const std::vector<uint8_t> &bytes) {
    std::vector<uint8_t> out = {0x78, 0x01};
    std::vector<uint8_t> deflate = stored_deflate(bytes);
    out.insert(out.end(), deflate.begin(), deflate.end());
    uint32_t checksum = adler32(bytes);
    out.push_back(static_cast<uint8_t>(checksum >> 24));
    out.push_back(static_cast<uint8_t>(checksum >> 16));
    out.push_back(static_cast<uint8_t>(checksum >> 8));
    out.push_back(static_cast<uint8_t>(checksum));
    return out;
}

static std::vector<uint8_t> gzip_stored(const std::vector<uint8_t> &bytes) {
    std::vector<uint8_t> out = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff};
    std::vector<uint8_t> deflate = stored_deflate(bytes);
    out.insert(out.end(), deflate.begin(), deflate.end());
    uint32_t checksum = rt_crc32_compute(bytes.data(), bytes.size());
    uint32_t length = static_cast<uint32_t>(bytes.size());
    for (int shift = 0; shift <= 24; shift += 8)
        out.push_back(static_cast<uint8_t>(checksum >> shift));
    for (int shift = 0; shift <= 24; shift += 8)
        out.push_back(static_cast<uint8_t>(length >> shift));
    return out;
}

static std::vector<uint8_t> zstd_raw_frame(const std::vector<uint8_t> &bytes) {
    assert(bytes.size() <= 255u);
    std::vector<uint8_t> out = {0x28, 0xb5, 0x2f, 0xfd, 0x20, static_cast<uint8_t>(bytes.size())};
    uint32_t blockHeader = (static_cast<uint32_t>(bytes.size()) << 3u) | 1u;
    out.push_back(static_cast<uint8_t>(blockHeader));
    out.push_back(static_cast<uint8_t>(blockHeader >> 8));
    out.push_back(static_cast<uint8_t>(blockHeader >> 16));
    out.insert(out.end(), bytes.begin(), bytes.end());
    return out;
}

static std::string base64(const std::vector<uint8_t> &bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t index = 0; index < bytes.size(); index += 3u) {
        uint32_t value = static_cast<uint32_t>(bytes[index]) << 16u;
        bool hasSecond = index + 1u < bytes.size();
        bool hasThird = index + 2u < bytes.size();
        if (hasSecond)
            value |= static_cast<uint32_t>(bytes[index + 1u]) << 8u;
        if (hasThird)
            value |= bytes[index + 2u];
        out.push_back(alphabet[(value >> 18u) & 63u]);
        out.push_back(alphabet[(value >> 12u) & 63u]);
        out.push_back(hasSecond ? alphabet[(value >> 6u) & 63u] : '=');
        out.push_back(hasThird ? alphabet[value & 63u] : '=');
    }
    return out;
}

static void *load_text(const std::string &text) {
    rt_string source = rt_string_from_bytes(text.data(), text.size());
    void *scene = rt_game_scene_load_json(source);
    rt_string_unref(source);
    return scene;
}

static void *load_fixture(const std::filesystem::path &fixture_dir, const char *name) {
    return load_text(read_text(fixture_dir / name));
}

static std::string scene_json(void *scene) {
    return to_std(rt_game_scene_to_json(scene));
}

static std::string map_str(void *map, const char *key) {
    rt_string key_s = rt_const_cstr(key);
    rt_string value = rt_map_get_str(map, key_s);
    rt_string_unref(key_s);
    return to_std(value);
}

static int64_t map_int(void *map, const char *key) {
    rt_string key_s = rt_const_cstr(key);
    int64_t value = rt_map_get_int(map, key_s);
    rt_string_unref(key_s);
    return value;
}

static std::string diagnostic_code(void *scene, int64_t index) {
    void *records = rt_game_scene_diagnostic_records(scene);
    assert(rt_seq_len(records) > index);
    return map_str(rt_seq_get(records, index), "code");
}

static std::string diagnostic_severity(void *scene, int64_t index) {
    void *records = rt_game_scene_diagnostic_records(scene);
    assert(rt_seq_len(records) > index);
    return map_str(rt_seq_get(records, index), "severity");
}

static std::string last_error(void *scene) {
    return to_std(rt_game_scene_last_error(scene));
}

static bool has_descriptor(void *descriptors,
                           const char *path,
                           const char *kind,
                           const char *owner,
                           const char *section,
                           const char *key) {
    int64_t count = rt_seq_len(descriptors);
    for (int64_t i = 0; i < count; ++i) {
        void *desc = rt_seq_get(descriptors, i);
        if (map_str(desc, "path") == path && map_str(desc, "kind") == kind &&
            map_str(desc, "owner") == owner && map_str(desc, "section") == section &&
            map_str(desc, "key") == key)
            return true;
    }
    return false;
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

    int64_t obj =
        rt_game_scene_add_object(scene, rt_const_cstr("Player"), rt_const_cstr("player"), 10, 20);
    rt_game_scene_set_object_property(
        scene, obj, rt_const_cstr("sprite"), rt_const_cstr("hero.png"));
    assert(to_std(rt_game_scene_get_object_property(scene, obj, rt_const_cstr("sprite"))) ==
           "hero.png");
    rt_game_scene_set_property(scene, rt_const_cstr("tileset"), rt_const_cstr("tiles/base.png"));
    void *assets = rt_game_scene_asset_paths(scene);
    assert(rt_seq_len(assets) == 3);

    rt_string json = rt_game_scene_to_json(scene);
    std::string json_text = to_std(rt_string_ref(json));
    assert(json_text.find("\"version\": 1") != std::string::npos);
    assert(json_text.find("\"tiles\"") != std::string::npos);
    assert(json_text.find("\"data\"") == std::string::npos);
    void *loaded = rt_game_scene_load_json(json);
    rt_string_unref(json);
    assert(rt_game_scene_get_width(loaded) == 4);
    assert(rt_game_scene_layer_count(loaded) == 2);
    assert(rt_game_scene_get_tile(loaded, 1, 1, 1) == 7);
    assert(rt_game_scene_object_count(loaded) == 1);
    assert(to_std(rt_game_scene_object_id(loaded, 0)) == "player");
    assert(to_std(rt_game_scene_get_object_property(loaded, 0, rt_const_cstr("sprite"))) ==
           "hero.png");

    rt_game_scene_set_int(scene, rt_const_cstr("playerStartX"), 96);
    rt_game_scene_set_float(scene, rt_const_cstr("gravity"), 0.75);
    rt_game_scene_set_bool(scene, rt_const_cstr("dark"), 1);
    rt_game_scene_set_str(scene, rt_const_cstr("theme"), rt_const_cstr("descent"));
    assert(rt_game_scene_get_int(scene, rt_const_cstr("playerStartX"), -1) == 96);
    assert(rt_game_scene_get_int(scene, rt_const_cstr("theme"), -1) == -1);
    assert(rt_game_scene_get_bool(scene, rt_const_cstr("dark"), 0) == 1);
    assert(rt_game_scene_get_float(scene, rt_const_cstr("gravity"), -1.0) > 0.7);
    assert(to_std(rt_game_scene_get_str(scene, rt_const_cstr("theme"), rt_const_cstr(""))) ==
           "descent");
    rt_game_scene_object_set_int(scene, obj, rt_const_cstr("hp"), 3);
    rt_game_scene_object_set_bool(scene, obj, rt_const_cstr("elite"), 1);
    assert(rt_game_scene_object_get_int(scene, obj, rt_const_cstr("hp"), -1) == 3);
    assert(rt_game_scene_object_get_bool(scene, obj, rt_const_cstr("elite"), 0) == 1);
    assert(rt_game_scene_find_object(scene, rt_const_cstr("player")) == obj);
    assert(rt_game_scene_count_of_type(scene, rt_const_cstr("Player")) == 1);
    assert(rt_game_scene_object_of_type(scene, rt_const_cstr("Player"), 0) == obj);
    void *keys = rt_game_scene_object_keys(scene, obj);
    assert(rt_seq_len(keys) >= 3);

    void *descriptors = rt_game_scene_asset_descriptors(scene);
    assert(rt_seq_len(descriptors) == 3);
    void *first_desc = rt_seq_get(descriptors, 0);
    assert(to_std(rt_map_get_str(first_desc, rt_const_cstr("path"))).size() > 0);

    void *render = rt_game_scene_build_tilemap(scene);
    assert(rt_tilemap_get_width(render) == 4);
    assert(rt_tilemap_get_layer_count(render) == 2);
    assert(rt_tilemap_get_tile_layer(render, 1, 1, 1) == 7);
    rt_tilemap_set_tile_layer(render, 1, 1, 1, 99);
    assert(rt_game_scene_get_tile(scene, 1, 1, 1) == 7);

    rt_string bad_json = rt_const_cstr("{\"version\":1,");
    void *bad = rt_game_scene_load_json(bad_json);
    rt_string_unref(bad_json);
    assert(rt_game_scene_has_errors(bad) == 1);
    void *diag_records = rt_game_scene_diagnostic_records(bad);
    assert(rt_seq_len(diag_records) >= 1);
    void *diag0 = rt_seq_get(diag_records, 0);
    assert(to_std(rt_map_get_str(diag0, rt_const_cstr("code"))) == "scene.parse.malformed_json");

    rt_string non_object_json = rt_const_cstr("[1,2,3]");
    void *non_object = rt_game_scene_load_json(non_object_json);
    rt_string_unref(non_object_json);
    assert(rt_game_scene_has_errors(non_object) == 1);

    void *too_big = rt_game_scene_new(5000, 5000, 16, 16);
    assert(rt_game_scene_has_errors(too_big) == 1);
    int64_t long_layer =
        rt_game_scene_add_layer(scene, rt_const_cstr("this-layer-name-is-longer-than-thirty-one"));
    assert(long_layer == -1);
    assert(rt_game_scene_has_errors(scene) == 0);
    assert(diagnostic_code(scene, 0) == "scene.edit.rejected");
    assert(diagnostic_severity(scene, 0) == "warning");
    rt_game_scene_clear_diagnostics(scene);
    assert(rt_game_scene_has_errors(scene) == 0);

    // VDOC-254: clearing diagnostics on a structurally invalid (error-severity)
    // scene must NOT flip it to valid — validity state is separate from the
    // diagnostic queue, so HasErrors stays true after the messages are cleared.
    rt_string invalid_json = rt_const_cstr("{\"version\":1,");
    void *invalid_scene = rt_game_scene_load_json(invalid_json);
    rt_string_unref(invalid_json);
    assert(rt_game_scene_has_errors(invalid_scene) == 1);
    rt_game_scene_clear_diagnostics(invalid_scene);
    assert(rt_game_scene_has_errors(invalid_scene) == 1);

    // VDOC-255: the Result Err message is the first error diagnostic, not lastError
    // (the newest of any severity), so a trailing warning cannot mask the real error.
    {
        const char *mixedText =
            "{\"version\":1,\"width\":-5,\"height\":2,\"tileWidth\":16,\"tileHeight\":16,"
            "\"layers\":[],\"bogusUnknownField\":123}";
        void *doc = rt_game_scene_load_json(rt_const_cstr(mixedText));
        void *records = rt_game_scene_diagnostic_records(doc);
        const int64_t n = rt_seq_len(records);
        std::string firstError;
        std::string lastMsg;
        for (int64_t i = 0; i < n; ++i) {
            void *rec = rt_seq_get(records, i);
            std::string sev = map_str(rec, "severity");
            std::string msg = map_str(rec, "message");
            if (firstError.empty() && sev == "error")
                firstError = msg;
            lastMsg = msg;
        }
        assert(!firstError.empty());
        // lastError holds the newest diagnostic (the trailing warning), which differs
        // from the first error — that is exactly the masking hazard.
        assert(last_error(doc) == lastMsg);
        assert(lastMsg != firstError);

        // The Result Err carries the first error message, not the trailing warning.
        void *res = rt_game_scene_load_json_result(rt_const_cstr(mixedText));
        assert(rt_result_is_err(res) == 1);
        assert(to_std(rt_result_unwrap_err_str(res)) == firstError);
    }

    rt_string legacy_short =
        rt_const_cstr("{\"width\":2,\"height\":2,\"tileWidth\":16,\"tileHeight\":16,"
                      "\"layers\":[{\"name\":\"base\",\"visible\":1,\"data\":[5]}]}");
    void *legacy = rt_game_scene_load_json(legacy_short);
    rt_string_unref(legacy_short);
    assert(rt_game_scene_has_errors(legacy) == 0);
    assert(rt_game_scene_get_tile(legacy, 0, 0, 0) == 5);
    assert(rt_game_scene_get_tile(legacy, 0, 1, 1) == 0);
    assert(rt_seq_len(rt_game_scene_diagnostic_records(legacy)) == 1);

#ifdef ZANNA_SOURCE_DIR
    std::filesystem::path fixture_dir =
        std::filesystem::path(ZANNA_SOURCE_DIR) / "src/tests/data/game/scenes";
    std::string minimal_text = read_text(fixture_dir / "v1_minimal.scene");
    rt_string minimal_s = rt_string_from_bytes(minimal_text.data(), minimal_text.size());
    void *minimal = rt_game_scene_load_json(minimal_s);
    rt_string_unref(minimal_s);
    assert(rt_game_scene_has_errors(minimal) == 0);
    assert(rt_game_scene_get_tile(minimal, 0, 1, 0) == 1);
    assert(to_std(rt_game_scene_to_json(minimal)) ==
           read_text(fixture_dir / "v1_minimal.golden.scene"));

    void *full = load_fixture(fixture_dir, "v1_full.scene");
    assert(rt_game_scene_has_errors(full) == 0);
    assert(rt_game_scene_get_width(full) == 3);
    assert(rt_game_scene_layer_count(full) == 2);
    assert(rt_game_scene_get_tile(full, 0, 1, 0) == 1);
    assert(rt_game_scene_get_tile(full, 1, 2, 1) == 4);
    assert(rt_game_scene_layer_visible(full, 1) == 0);
    assert(to_std(rt_game_scene_get_str(full, rt_const_cstr("theme"), rt_const_cstr(""))) ==
           "cavern");
    assert(to_std(rt_game_scene_object_get_str(
               full, 0, rt_const_cstr("sprite"), rt_const_cstr(""))) == "sprites/slime.png");
    std::string full_once = scene_json(full);
    void *full_reloaded = load_text(full_once);
    assert(rt_game_scene_has_errors(full_reloaded) == 0);
    assert(scene_json(full_reloaded) == full_once);

    void *full_tilemap = rt_game_scene_build_tilemap(full);
    assert(rt_tilemap_get_layer_count(full_tilemap) == 2);
    assert(rt_tilemap_get_layer_visible(full_tilemap, 1) == 0);
    assert(rt_tilemap_get_collision_layer(full_tilemap) == 0);
    assert(rt_tilemap_get_collision(full_tilemap, 1) == RT_TILE_COLLISION_SOLID);
    assert(rt_tilemap_get_collision(full_tilemap, 2) == RT_TILE_COLLISION_SOLID);
    assert(rt_tilemap_get_collision(full_tilemap, 3) == RT_TILE_COLLISION_ONE_WAY_UP);
    assert(rt_tilemap_get_tile_property(full_tilemap, 1, rt_const_cstr("solid"), 0) == 1);
    assert(rt_tilemap_resolve_anim_tile(full_tilemap, 4) == 4);
    rt_tilemap_update_anims(full_tilemap, 120);
    assert(rt_tilemap_resolve_anim_tile(full_tilemap, 4) == 5);
    rt_tilemap_set_tile(full_tilemap, 1, 0, 8);
    rt_tilemap_set_tile(full_tilemap, 2, 0, 8);
    rt_tilemap_apply_autotile(full_tilemap);
    assert(rt_tilemap_get_tile(full_tilemap, 1, 0) == 10);
    assert(rt_tilemap_get_tile(full_tilemap, 2, 0) == 16);
    void *full_descriptors = rt_game_scene_asset_descriptors(full);
    assert(has_descriptor(full_descriptors,
                          "textures/light-mask.png",
                          "image",
                          "section",
                          "lighting",
                          "textureAsset"));

    void *leveldata = load_fixture(fixture_dir, "legacy_leveldata.json");
    assert(rt_game_scene_has_errors(leveldata) == 0);
    assert(rt_game_scene_get_int(leveldata, rt_const_cstr("playerStartX"), -1) == 16);
    assert(rt_game_scene_object_count(leveldata) == 1);

    void *legacy_current = load_fixture(fixture_dir, "legacy_current_generated.json");
    assert(rt_game_scene_has_errors(legacy_current) == 0);
    assert(rt_game_scene_get_tile(legacy_current, 0, 1, 0) == 7);
    assert(to_std(rt_game_scene_get_str(
               legacy_current, rt_const_cstr("tileset"), rt_const_cstr(""))) == "tiles/base.png");

    void *legacy_nested = load_fixture(fixture_dir, "legacy_nested_tilemap.json");
    assert(rt_game_scene_has_errors(legacy_nested) == 0);
    assert(rt_game_scene_get_width(legacy_nested) == 2);
    assert(rt_game_scene_get_tile(legacy_nested, 0, 1, 0) == 4);
    assert(rt_game_scene_object_count(legacy_nested) == 1);
    assert(has_descriptor(rt_game_scene_asset_descriptors(legacy_nested),
                          "tiles/nested.png",
                          "tileset",
                          "scene",
                          "",
                          "tilesetAsset"));

    void *legacy_flat = load_fixture(fixture_dir, "legacy_flat_objects.json");
    assert(rt_game_scene_has_errors(legacy_flat) == 0);
    assert(rt_game_scene_object_get_int(legacy_flat, 0, rt_const_cstr("hp"), -1) == 5);
    assert(rt_game_scene_object_get_bool(legacy_flat, 0, rt_const_cstr("elite"), 0) == 1);
    assert(to_std(rt_game_scene_object_get_str(
               legacy_flat, 0, rt_const_cstr("sprite"), rt_const_cstr(""))) ==
           "sprites/player.png");

    std::string long_key(129, 'k');
    std::string long_value(64 * 1024 + 1, 'v');
    std::string legacy_flat_limits =
        "{\"width\":1,\"height\":1,\"tileWidth\":16,\"tileHeight\":16,"
        "\"layers\":[{\"name\":\"base\",\"data\":[0]}],\"objects\":[{\"type\":\"Thing\","
        "\"id\":\"bad\",\"x\":0,\"y\":0,\"" +
        long_key + "\":1,\"sprite\":\"" + long_value + "\"}]}";
    void *legacy_flat_rejected = load_text(legacy_flat_limits);
    assert(rt_game_scene_has_errors(legacy_flat_rejected) == 1);
    rt_string long_key_s = rt_string_from_bytes(long_key.data(), long_key.size());
    assert(rt_game_scene_object_has(legacy_flat_rejected, 0, long_key_s) == 0);
    rt_string_unref(long_key_s);
    assert(to_std(rt_game_scene_object_get_str(
               legacy_flat_rejected, 0, rt_const_cstr("sprite"), rt_const_cstr(""))) == "");

    std::string warning_flood =
        "{\"version\":1,\"name\":\"flood\",\"width\":1,\"height\":1,"
        "\"tileWidth\":16,\"tileHeight\":16,\"tilesetAsset\":\"\","
        "\"properties\":{},\"layers\":[{\"name\":\"base\",\"tiles\":[0]}],\"objects\":[]";
    for (int i = 0; i < 260; ++i)
        warning_flood += ",\"unknown" + std::to_string(i) + "\":0";
    warning_flood += "}";
    void *warning_scene = load_text(warning_flood);
    void *warning_records = rt_game_scene_diagnostic_records(warning_scene);
    assert(rt_game_scene_has_errors(warning_scene) == 0);
    assert(rt_seq_len(warning_records) == 256);
    assert(diagnostic_code(warning_scene, 255) == "scene.diagnostics.truncated");
    assert(last_error(warning_scene) ==
           "too many scene diagnostics; later diagnostics were dropped");

    void *invalid_version = load_fixture(fixture_dir, "invalid_version.scene");
    assert(rt_game_scene_has_errors(invalid_version) == 1);
    assert(diagnostic_code(invalid_version, 0) == "scene.schema.unsupported_version");

    void *invalid_malformed = load_fixture(fixture_dir, "invalid_malformed.scene");
    assert(rt_game_scene_has_errors(invalid_malformed) == 1);
    assert(diagnostic_code(invalid_malformed, 0) == "scene.parse.malformed_json");

    void *invalid_count = load_fixture(fixture_dir, "invalid_tile_count.scene");
    assert(rt_game_scene_has_errors(invalid_count) == 1);
#endif

    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "zanna_scene_editor_test.json";
    std::string path_text = path.string();
    rt_string path_s = rt_string_from_bytes(path_text.data(), path_text.size());
    assert(rt_game_scene_save_file(scene, path_s) == 1);
    void *from_file = rt_game_scene_load_file(path_s);
    rt_string_unref(path_s);
    assert(rt_game_scene_get_tile(from_file, 1, 2, 1) == 7);
    std::filesystem::remove(path);

    std::filesystem::path blocked_path =
        std::filesystem::temp_directory_path() / "zanna_scene_editor_save_target_dir";
    std::filesystem::remove_all(blocked_path);
    std::filesystem::create_directory(blocked_path);
    std::string blocked_text = blocked_path.string();
    rt_string blocked_s = rt_string_from_bytes(blocked_text.data(), blocked_text.size());
    assert(rt_game_scene_save_file(scene, blocked_s) == 0);
    rt_string_unref(blocked_s);
    assert(std::filesystem::is_directory(blocked_path));
    assert(rt_game_scene_has_errors(scene) == 1);
    rt_game_scene_clear_diagnostics(scene);
    std::filesystem::remove_all(blocked_path);

    // ADR 0140: external Tiled JSON/TMX imports preserve tile layers, objects,
    // typed properties, tileset metadata, animation, and collision. The loader
    // convenience returns a render-ready Tilemap using the same normalized data.
    std::filesystem::path tiled_dir =
        std::filesystem::temp_directory_path() / "zanna_tiled_import_test";
    std::filesystem::remove_all(tiled_dir);
    std::filesystem::create_directories(tiled_dir);
    std::filesystem::path tiled_png = tiled_dir / "tiles.png";
    void *tiled_pixels = rt_pixels_new(7, 7);
    assert(tiled_pixels != nullptr);
    rt_pixels_fill(tiled_pixels, 0xff8040ff);
    assert(rt_pixels_save_png(tiled_pixels, rt_const_cstr(tiled_png.string().c_str())) == 1);

    write_text(tiled_dir / "terrain.tsj",
               R"json({
  "type":"tileset","name":"terrain","tilewidth":2,"tileheight":2,
  "tilecount":4,"columns":2,"margin":1,"spacing":1,
  "image":"tiles.png","imagewidth":7,"imageheight":7,
  "tiles":[{
    "id":1,
    "properties":[{"name":"damage","type":"int","value":3}],
    "objectgroup":{"objects":[{"id":1,"x":0,"y":0,"width":2,"height":2}]},
    "animation":[{"tileid":1,"duration":100},{"tileid":2,"duration":200}]
  }]
})json");
    write_text(tiled_dir / "spawn.tj",
               R"json({
  "type":"template",
  "object":{"class":"Enemy","width":2,"height":2,
    "properties":[{"name":"team","type":"string","value":"red"},
                  {"name":"hp","type":"int","value":2}]}
})json");
    write_text(tiled_dir / "level.tmj",
               R"json({
  "type":"map","tiledversion":"1.11.2","orientation":"orthogonal","infinite":false,
  "width":3,"height":2,"tilewidth":2,"tileheight":2,
  "properties":[{"name":"theme","type":"string","value":"cavern"},
                {"name":"config","type":"file","value":"config/game.json"}],
  "tilesets":[{"firstgid":1,"source":"terrain.tsj"}],
  "layers":[
    {"id":1,"type":"group","name":"World","visible":true,
     "properties":[{"name":"category","type":"string","value":"terrain"}],
     "layers":[
       {"id":2,"type":"tilelayer","name":"Ground","width":2,"height":2,"x":1,
        "visible":true,"properties":[{"name":"kind","type":"string","value":"floor"}],
        "data":[2,1,3,4]}
     ]},
    {"id":3,"type":"objectgroup","name":"Objects","objects":[
      {"id":7,"name":"spawn-a","class":"Spawn","x":2,"y":4,"width":2,"height":2,
       "properties":[{"name":"team","type":"string","value":"blue"},
                     {"name":"hp","type":"int","value":5}]},
      {"id":8,"name":"spawn-a","template":"spawn.tj","x":2.5,"y":1,
       "properties":[{"name":"hp","type":"int","value":9}]}
    ]},
    {"id":4,"type":"imagelayer","name":"Backdrop","image":"tiles.png","visible":false}
  ]
})json");

    void *tiled_scene_result = rt_game_scene_import_tiled_result(
        rt_const_cstr((tiled_dir / "level.tmj").string().c_str()));
    assert(rt_result_is_ok(tiled_scene_result) == 1);
    void *tiled_scene = rt_result_unwrap(tiled_scene_result);
    assert(rt_game_scene_get_width(tiled_scene) == 3);
    assert(rt_game_scene_get_height(tiled_scene) == 2);
    assert(rt_game_scene_get_tile_width(tiled_scene) == 2);
    assert(rt_game_scene_layer_count(tiled_scene) == 1);
    assert(rt_game_scene_get_tile(tiled_scene, 0, 1, 0) == 2);
    assert(rt_game_scene_object_count(tiled_scene) == 3);
    assert(to_std(rt_game_scene_object_type(tiled_scene, 0)) == "Spawn");
    assert(to_std(rt_game_scene_object_id(tiled_scene, 0)) == "spawn-a");
    assert(rt_game_scene_object_get_int(tiled_scene, 0, rt_const_cstr("hp"), -1) == 5);
    assert(to_std(rt_game_scene_object_get_str(
               tiled_scene, 0, rt_const_cstr("team"), rt_const_cstr(""))) == "blue");
    assert(to_std(rt_game_scene_get_str(tiled_scene, rt_const_cstr("theme"), rt_const_cstr(""))) ==
           "cavern");
    assert(to_std(rt_game_scene_get_str(tiled_scene,
                                        rt_const_cstr("tiled.layer.World.category"),
                                        rt_const_cstr(""))) == "terrain");
    assert(to_std(rt_game_scene_get_str(tiled_scene,
                                        rt_const_cstr("tiled.layer.World/Ground.kind"),
                                        rt_const_cstr(""))) == "floor");
    assert(to_std(rt_game_scene_get_str(tiled_scene, rt_const_cstr("config"), rt_const_cstr(""))) ==
           (tiled_dir / "config/game.json").lexically_normal().string());
    assert(to_std(rt_game_scene_object_type(tiled_scene, 1)) == "Enemy");
    assert(to_std(rt_game_scene_object_id(tiled_scene, 1)) == "8");
    assert(rt_game_scene_object_get_int(tiled_scene, 1, rt_const_cstr("hp"), -1) == 9);
    assert(to_std(rt_game_scene_object_get_str(
               tiled_scene, 1, rt_const_cstr("team"), rt_const_cstr(""))) == "red");
    assert(rt_game_scene_object_get_float(tiled_scene, 1, rt_const_cstr("tiled.sourceX"), -1.0) ==
           2.5);
    assert(to_std(rt_game_scene_object_get_str(
               tiled_scene, 1, rt_const_cstr("tiled.template"), rt_const_cstr(""))) ==
           (tiled_dir / "spawn.tj").string());
    assert(to_std(rt_game_scene_object_type(tiled_scene, 2)) == "tiled.image-layer");
    void *tiled_render = rt_game_scene_build_tilemap(tiled_scene);
    assert(rt_tilemap_get_collision(tiled_render, 2) == RT_TILE_COLLISION_SOLID);
    assert(rt_tilemap_get_tile_property(tiled_render, 2, rt_const_cstr("damage"), -1) == 3);
    assert(rt_tilemap_resolve_anim_tile(tiled_render, 2) == 2);
    rt_tilemap_update_anims(tiled_render, 100);
    assert(rt_tilemap_resolve_anim_tile(tiled_render, 2) == 3);
    rt_tilemap_update_anims(tiled_render, 100);
    assert(rt_tilemap_resolve_anim_tile(tiled_render, 2) == 3);
    rt_tilemap_update_anims(tiled_render, 100);
    assert(rt_tilemap_resolve_anim_tile(tiled_render, 2) == 2);

    void *tiled_loader = rt_tiledmaploader_new();
    void *tiled_map_result = rt_tiledmaploader_load_result(
        tiled_loader, rt_const_cstr((tiled_dir / "level.tmj").string().c_str()));
    assert(rt_result_is_ok(tiled_map_result) == 1);
    void *loaded_tiled_map = rt_result_unwrap(tiled_map_result);
    assert(rt_tilemap_get_width(loaded_tiled_map) == 3);
    assert(rt_tilemap_get_tile_count(loaded_tiled_map) == 4);
    assert(rt_tilemap_get_tile(loaded_tiled_map, 2, 1) == 4);

    write_text(tiled_dir / "level.tmx",
               R"xml(<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" tiledversion="1.11.2" orientation="orthogonal"
     width="2" height="2" tilewidth="2" tileheight="2" infinite="0">
  <properties><property name="theme" value="tmx-cave"/></properties>
  <tileset firstgid="1" source="terrain.tsx"/>
  <layer id="1" name="Ground" width="2" height="2">
    <properties><property name="kind" value="tmx-floor"/></properties>
    <data encoding="csv">1,0,2,4</data>
  </layer>
  <objectgroup id="2" name="Objects">
    <object id="9" name="door-a" type="Door" x="2" y="2" width="2" height="2">
      <properties><property name="locked" type="bool" value="true"/></properties>
    </object>
    <object id="10" name="switch-a" template="door.tx" x="1.5" y="3">
      <properties><property name="channel" type="int" value="7"/></properties>
    </object>
  </objectgroup>
</map>)xml");
    write_text(tiled_dir / "terrain.tsx",
               R"xml(<?xml version="1.0" encoding="UTF-8"?>
<tileset version="1.10" name="terrain" tilewidth="2" tileheight="2"
         tilecount="4" columns="2" margin="1" spacing="1">
  <image source="tiles.png" width="7" height="7"/>
</tileset>)xml");
    write_text(tiled_dir / "door.tx",
               R"xml(<?xml version="1.0" encoding="UTF-8"?>
<template>
  <object type="Switch" width="2" height="2">
    <properties><property name="channel" type="int" value="1"/></properties>
  </object>
</template>)xml");
    void *tmx_result = rt_game_scene_import_tiled_result(
        rt_const_cstr((tiled_dir / "level.tmx").string().c_str()));
    if (rt_result_is_err(tmx_result))
        std::fprintf(stderr,
                     "TMX import failed: %s\n",
                     to_std(rt_result_unwrap_err_str(tmx_result)).c_str());
    assert(rt_result_is_ok(tmx_result) == 1);
    void *tmx_scene = rt_result_unwrap(tmx_result);
    assert(rt_game_scene_get_tile(tmx_scene, 0, 1, 1) == 4);
    assert(to_std(rt_game_scene_object_type(tmx_scene, 0)) == "Door");
    assert(rt_game_scene_object_get_bool(tmx_scene, 0, rt_const_cstr("locked"), 0) == 1);
    assert(to_std(rt_game_scene_get_str(tmx_scene,
                                        rt_const_cstr("tiled.layer.Ground.kind"),
                                        rt_const_cstr(""))) == "tmx-floor");
    assert(to_std(rt_game_scene_object_type(tmx_scene, 1)) == "Switch");
    assert(rt_game_scene_object_get_int(tmx_scene, 1, rt_const_cstr("channel"), -1) == 7);
    assert(rt_game_scene_object_get_float(tmx_scene, 1, rt_const_cstr("tiled.sourceX"), -1.0) ==
           1.5);

    // Every encoded Tiled payload path is decoded against the exact cell-byte
    // bound. These tiny stored frames keep the fixture dependency-free.
    std::vector<uint8_t> gid_bytes = tiled_gid_bytes({1, 2, 3, 4});

    struct EncodedCase {
        const char *name;
        const char *compression;
        std::vector<uint8_t> bytes;
    };

    std::vector<EncodedCase> encoded_cases = {
        {"raw", "", gid_bytes},
        {"zlib", "zlib", zlib_stored(gid_bytes)},
        {"gzip", "gzip", gzip_stored(gid_bytes)},
        {"zstd", "zstd", zstd_raw_frame(gid_bytes)},
    };
    for (const EncodedCase &encoded_case : encoded_cases) {
        std::string compression_field =
            encoded_case.compression[0]
                ? std::string(",\"compression\":\"") + encoded_case.compression + "\""
                : std::string();
        std::string encoded_json =
            "{\"type\":\"map\",\"orientation\":\"orthogonal\",\"infinite\":false,"
            "\"width\":2,\"height\":2,\"tilewidth\":2,\"tileheight\":2,"
            "\"tilesets\":[{\"firstgid\":1,\"source\":\"terrain.tsj\"}],"
            "\"layers\":[{\"type\":\"tilelayer\",\"name\":\"encoded\","
            "\"width\":2,\"height\":2,\"encoding\":\"base64\"" +
            compression_field + ",\"data\":\"" + base64(encoded_case.bytes) + "\"}]}";
        std::filesystem::path encoded_path =
            tiled_dir / (std::string("encoded-") + encoded_case.name + ".tmj");
        write_text(encoded_path, encoded_json);
        void *encoded_result =
            rt_game_scene_import_tiled_result(rt_const_cstr(encoded_path.string().c_str()));
        if (rt_result_is_err(encoded_result))
            std::fprintf(stderr,
                         "%s import failed: %s\n",
                         encoded_case.name,
                         to_std(rt_result_unwrap_err_str(encoded_result)).c_str());
        assert(rt_result_is_ok(encoded_result) == 1);
        void *encoded_scene = rt_result_unwrap(encoded_result);
        assert(rt_game_scene_get_tile(encoded_scene, 0, 1, 1) == 4);
    }

    // The asset-mode entry points must resolve the entire map/tileset/template/
    // image graph from a mounted package after loose-file paths are irrelevant.
    std::filesystem::path tiled_pack = tiled_dir.parent_path() / "zanna_tiled_import_test.zpak";
    zanna::asset::ZpakWriter tiled_writer;
    auto add_text_entry = [&](const char *name, const std::filesystem::path &path) {
        std::string value = read_text(path);
        tiled_writer.addEntry(
            name, reinterpret_cast<const uint8_t *>(value.data()), value.size(), false);
    };
    add_text_entry("maps/level.tmj", tiled_dir / "level.tmj");
    add_text_entry("maps/terrain.tsj", tiled_dir / "terrain.tsj");
    add_text_entry("maps/spawn.tj", tiled_dir / "spawn.tj");
    const std::string unsafe_asset_map =
        R"json({"type":"map","orientation":"orthogonal","infinite":false,
"width":1,"height":1,"tilewidth":2,"tileheight":2,
"tilesets":[{"firstgid":1,"source":"../../terrain.tsj"}],
"layers":[{"type":"tilelayer","name":"bad","width":1,"height":1,"data":[1]}]})json";
    tiled_writer.addEntry("maps/unsafe.tmj",
                          reinterpret_cast<const uint8_t *>(unsafe_asset_map.data()),
                          unsafe_asset_map.size(),
                          false);
    std::vector<uint8_t> packed_png = read_bytes(tiled_png);
    tiled_writer.addEntry("maps/tiles.png", packed_png.data(), packed_png.size(), false);
    std::string pack_error;
    assert(tiled_writer.writeToFile(tiled_pack.string(), pack_error));
    assert(rt_asset_mount(rt_const_cstr(tiled_pack.string().c_str())) == 1);
    void *asset_scene_result =
        rt_game_scene_import_tiled_asset_result(rt_const_cstr("maps/level.tmj"));
    assert(rt_result_is_ok(asset_scene_result) == 1);
    void *asset_scene = rt_result_unwrap(asset_scene_result);
    assert(rt_game_scene_get_tile(asset_scene, 0, 2, 1) == 4);
    assert(to_std(rt_game_scene_object_type(asset_scene, 1)) == "Enemy");
    void *asset_map_result =
        rt_tiledmaploader_load_asset_result(tiled_loader, rt_const_cstr("maps/level.tmj"));
    assert(rt_result_is_ok(asset_map_result) == 1);
    assert(rt_tilemap_get_tile_count(rt_result_unwrap(asset_map_result)) == 4);
    void *unsafe_asset_result =
        rt_game_scene_import_tiled_asset_result(rt_const_cstr("maps/unsafe.tmj"));
    assert(rt_result_is_err(unsafe_asset_result) == 1);
    assert(to_std(rt_result_unwrap_err_str(unsafe_asset_result)).find("escapes") !=
           std::string::npos);
    assert(rt_asset_unmount(rt_const_cstr(tiled_pack.string().c_str())) == 1);
    std::filesystem::remove(tiled_pack);

    void *wrong_loader_result = rt_tiledmaploader_load_result(
        tiled_pixels, rt_const_cstr((tiled_dir / "level.tmj").string().c_str()));
    assert(rt_result_is_err(wrong_loader_result) == 1);

    write_text(tiled_dir / "flipped.tmj",
               R"json({"type":"map","orientation":"orthogonal","infinite":false,
"width":1,"height":1,"tilewidth":2,"tileheight":2,
"tilesets":[{"firstgid":1,"source":"terrain.tsj"}],
"layers":[{"type":"tilelayer","name":"bad","width":1,"height":1,
"data":[2147483649]}]})json");
    void *flipped_result = rt_game_scene_import_tiled_result(
        rt_const_cstr((tiled_dir / "flipped.tmj").string().c_str()));
    assert(rt_result_is_err(flipped_result) == 1);
    assert(to_std(rt_result_unwrap_err_str(flipped_result)).find("transform flags") !=
           std::string::npos);

    write_text(tiled_dir / "mixed.tmj",
               R"json({"type":"map","orientation":"orthogonal","infinite":false,
"width":2,"height":1,"tilewidth":2,"tileheight":2,
"tilesets":[{"firstgid":1,"source":"terrain.tsj"},
            {"firstgid":5,"source":"terrain.tsj"}],
"layers":[{"type":"tilelayer","name":"mixed","width":2,"height":1,"data":[1,5]}]})json");
    void *mixed_result = rt_game_scene_import_tiled_result(
        rt_const_cstr((tiled_dir / "mixed.tmj").string().c_str()));
    assert(rt_result_is_err(mixed_result) == 1);
    assert(to_std(rt_result_unwrap_err_str(mixed_result)).find("more than one tileset") !=
           std::string::npos);

    write_text(tiled_dir / "bad-base64.tmj",
               R"json({"type":"map","orientation":"orthogonal","infinite":false,
"width":1,"height":1,"tilewidth":2,"tileheight":2,
"tilesets":[{"firstgid":1,"source":"terrain.tsj"}],
"layers":[{"type":"tilelayer","name":"bad","width":1,"height":1,
"encoding":"base64","data":"AA=="}]})json");
    void *bad_base64_result = rt_game_scene_import_tiled_result(
        rt_const_cstr((tiled_dir / "bad-base64.tmj").string().c_str()));
    assert(rt_result_is_err(bad_base64_result) == 1);
    assert(to_std(rt_result_unwrap_err_str(bad_base64_result)).find("wrong byte length") !=
           std::string::npos);
    assert(rt_game_scene_import_tiled(
               rt_const_cstr((tiled_dir / "does-not-exist.tmj").string().c_str())) == nullptr);
    std::filesystem::remove_all(tiled_dir);

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
