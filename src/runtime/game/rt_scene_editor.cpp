//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_scene_editor.cpp
// Purpose: Scene-owned editable level document primitives for IDE scene tools.
//
//===----------------------------------------------------------------------===//

#include "rt_scene_editor.h"

#include "rt_box.h"
#include "rt_json.h"
#include "rt_jsonpath.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string toStd(rt_string s) {
    if (!s)
        return {};
    const char *data = rt_string_cstr(s);
    int64_t len = rt_str_len(s);
    if (!data || len <= 0)
        return {};
    return std::string(data, static_cast<size_t>(len));
}

rt_string makeString(const std::string &value) {
    return rt_string_from_bytes(value.data(), value.size());
}

void releaseObject(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

std::string jsonEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char ch : s) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20)
                out += "?";
            else
                out.push_back(static_cast<char>(ch));
            break;
        }
    }
    return out;
}

std::string jsonStr(void *obj, const char *key) {
    if (!obj)
        return {};
    rt_string s = rt_jsonpath_get_str(obj, rt_const_cstr(key));
    return toStd(s);
}

int64_t boxedToI64(void *value) {
    if (!value)
        return 0;
    int64_t tag = rt_box_type(value);
    if (tag == RT_BOX_I64)
        return rt_unbox_i64(value);
    if (tag == RT_BOX_F64)
        return static_cast<int64_t>(rt_unbox_f64(value));
    if (tag == RT_BOX_I1)
        return rt_unbox_i1(value) ? 1 : 0;
    return 0;
}

struct Layer {
    std::string name;
    std::string asset;
    bool visible{true};
    std::vector<int64_t> tiles;
};

struct Object {
    std::string type;
    std::string id;
    int64_t x{0};
    int64_t y{0};
    std::map<std::string, std::string> properties;
};

struct SceneState {
    int64_t width{1};
    int64_t height{1};
    int64_t tileWidth{16};
    int64_t tileHeight{16};
    std::vector<Layer> layers;
    std::vector<Object> objects;
    std::map<std::string, std::string> properties;
    std::string lastError;
    std::vector<std::string> diagnostics;
};

struct SceneHandle {
    SceneState *state{nullptr};
};

SceneHandle *requireScene(void *scene) {
    if (!scene || rt_obj_class_id(scene) != RT_GAME_SCENE_CLASS_ID)
        rt_trap("Game.Scene: invalid handle");
    auto *h = static_cast<SceneHandle *>(scene);
    if (!h->state)
        rt_trap("Game.Scene: destroyed handle");
    return h;
}

void sceneFinalizer(void *obj) {
    auto *h = static_cast<SceneHandle *>(obj);
    delete h->state;
    h->state = nullptr;
}

Layer makeLayer(SceneState &s, const std::string &name) {
    Layer layer;
    layer.name = name.empty() ? "Layer" + std::to_string(s.layers.size()) : name;
    layer.tiles.assign(static_cast<size_t>(s.width * s.height), 0);
    return layer;
}

bool validLayer(SceneState &s, int64_t layer) {
    return layer >= 0 && layer < static_cast<int64_t>(s.layers.size());
}

bool validTile(SceneState &s, int64_t x, int64_t y) {
    return x >= 0 && y >= 0 && x < s.width && y < s.height;
}

void addDiagnostic(SceneState &s, const std::string &message) {
    s.lastError = message;
    s.diagnostics.push_back(message);
}

void *newSceneHandle(int64_t width, int64_t height, int64_t tileWidth, int64_t tileHeight) {
    auto *h = static_cast<SceneHandle *>(rt_obj_new_i64(RT_GAME_SCENE_CLASS_ID, sizeof(SceneHandle)));
    h->state = new SceneState();
    h->state->width = std::max<int64_t>(1, width);
    h->state->height = std::max<int64_t>(1, height);
    h->state->tileWidth = std::max<int64_t>(1, tileWidth);
    h->state->tileHeight = std::max<int64_t>(1, tileHeight);
    h->state->layers.push_back(makeLayer(*h->state, "base"));
    rt_obj_set_finalizer(h, sceneFinalizer);
    return h;
}

} // namespace

extern "C" {

void *rt_game_scene_new(int64_t width, int64_t height, int64_t tile_width, int64_t tile_height) {
    return newSceneHandle(width, height, tile_width, tile_height);
}

void *rt_game_scene_load_json(rt_string text) {
    void *root = rt_json_parse(text);
    if (!root) {
        void *scene = newSceneHandle(1, 1, 16, 16);
        addDiagnostic(*requireScene(scene)->state, "invalid scene JSON");
        return scene;
    }

    int64_t width = std::max<int64_t>(1, rt_jsonpath_get_int(root, rt_const_cstr("width")));
    int64_t height = std::max<int64_t>(1, rt_jsonpath_get_int(root, rt_const_cstr("height")));
    int64_t tileWidth = rt_jsonpath_get_int(root, rt_const_cstr("tileWidth"));
    int64_t tileHeight = rt_jsonpath_get_int(root, rt_const_cstr("tileHeight"));
    void *scene = newSceneHandle(width, height, tileWidth <= 0 ? 16 : tileWidth,
                                 tileHeight <= 0 ? 16 : tileHeight);
    SceneState &s = *requireScene(scene)->state;
    s.layers.clear();

    void *layers = rt_jsonpath_get(root, rt_const_cstr("layers"));
    if (layers && rt_obj_class_id(layers) == RT_SEQ_CLASS_ID) {
        int64_t count = rt_seq_len(layers);
        for (int64_t li = 0; li < count; li++) {
            void *lm = rt_seq_get(layers, li);
            Layer layer = makeLayer(s, jsonStr(lm, "name"));
            layer.asset = jsonStr(lm, "asset");
            layer.visible = rt_jsonpath_get_int(lm, rt_const_cstr("visible")) != 0;
            void *data = rt_jsonpath_get(lm, rt_const_cstr("data"));
            if (data && rt_obj_class_id(data) == RT_SEQ_CLASS_ID) {
                int64_t n = std::min<int64_t>(rt_seq_len(data), s.width * s.height);
                for (int64_t i = 0; i < n; i++)
                    layer.tiles[static_cast<size_t>(i)] = boxedToI64(rt_seq_get(data, i));
            }
            s.layers.push_back(std::move(layer));
        }
    }
    if (s.layers.empty())
        s.layers.push_back(makeLayer(s, "base"));

    void *objects = rt_jsonpath_get(root, rt_const_cstr("objects"));
    if (objects && rt_obj_class_id(objects) == RT_SEQ_CLASS_ID) {
        int64_t count = rt_seq_len(objects);
        for (int64_t i = 0; i < count; i++) {
            void *om = rt_seq_get(objects, i);
            Object obj;
            obj.type = jsonStr(om, "type");
            obj.id = jsonStr(om, "id");
            obj.x = rt_jsonpath_get_int(om, rt_const_cstr("x"));
            obj.y = rt_jsonpath_get_int(om, rt_const_cstr("y"));
            s.objects.push_back(std::move(obj));
        }
    }

    void *props = rt_jsonpath_get(root, rt_const_cstr("properties"));
    if (props && rt_obj_class_id(props) == RT_MAP_CLASS_ID) {
        void *keys = rt_map_keys(props);
        int64_t count = rt_seq_len(keys);
        for (int64_t i = 0; i < count; i++) {
            rt_string key = rt_seq_get_str(keys, i);
            rt_string value = rt_map_get_str(props, key);
            s.properties[toStd(key)] = toStd(value);
            rt_string_unref(key);
            rt_string_unref(value);
        }
        releaseObject(keys);
    }

    releaseObject(root);
    return scene;
}

void *rt_game_scene_load_file(rt_string path_s) {
    std::ifstream in(toStd(path_s), std::ios::binary);
    if (!in) {
        void *scene = newSceneHandle(1, 1, 16, 16);
        addDiagnostic(*requireScene(scene)->state, "cannot open scene file");
        return scene;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    rt_string text = makeString(buffer.str());
    void *scene = rt_game_scene_load_json(text);
    rt_string_unref(text);
    return scene;
}

rt_string rt_game_scene_to_json(void *scene) {
    SceneState &s = *requireScene(scene)->state;
    std::ostringstream out;
    out << "{\n";
    out << "  \"width\": " << s.width << ",\n";
    out << "  \"height\": " << s.height << ",\n";
    out << "  \"tileWidth\": " << s.tileWidth << ",\n";
    out << "  \"tileHeight\": " << s.tileHeight << ",\n";
    out << "  \"layers\": [\n";
    for (size_t li = 0; li < s.layers.size(); li++) {
        const Layer &layer = s.layers[li];
        out << "    {\"name\":\"" << jsonEscape(layer.name) << "\",\"visible\":"
            << (layer.visible ? 1 : 0) << ",\"asset\":\"" << jsonEscape(layer.asset)
            << "\",\"data\":[";
        for (size_t i = 0; i < layer.tiles.size(); i++) {
            if (i)
                out << ",";
            out << layer.tiles[i];
        }
        out << "]}";
        if (li + 1 < s.layers.size())
            out << ",";
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"objects\": [\n";
    for (size_t oi = 0; oi < s.objects.size(); oi++) {
        const Object &obj = s.objects[oi];
        out << "    {\"type\":\"" << jsonEscape(obj.type) << "\",\"id\":\""
            << jsonEscape(obj.id) << "\",\"x\":" << obj.x << ",\"y\":" << obj.y << "}";
        if (oi + 1 < s.objects.size())
            out << ",";
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"properties\": {";
    bool first = true;
    for (const auto &[key, value] : s.properties) {
        if (!first)
            out << ",";
        out << "\"" << jsonEscape(key) << "\":\"" << jsonEscape(value) << "\"";
        first = false;
    }
    out << "}\n";
    out << "}\n";
    return makeString(out.str());
}

int8_t rt_game_scene_save_file(void *scene, rt_string path_s) {
    std::ofstream out(toStd(path_s), std::ios::binary | std::ios::trunc);
    if (!out) {
        addDiagnostic(*requireScene(scene)->state, "cannot save scene file");
        return 0;
    }
    rt_string json = rt_game_scene_to_json(scene);
    out.write(rt_string_cstr(json), rt_str_len(json));
    rt_string_unref(json);
    return out.good() ? 1 : 0;
}

rt_string rt_game_scene_last_error(void *scene) {
    return makeString(requireScene(scene)->state->lastError);
}

void *rt_game_scene_diagnostics(void *scene) {
    SceneState &s = *requireScene(scene)->state;
    void *seq = rt_seq_new_owned();
    for (const auto &diag : s.diagnostics) {
        rt_string item = makeString(diag);
        rt_seq_push(seq, item);
        rt_string_unref(item);
    }
    return seq;
}

int64_t rt_game_scene_get_width(void *scene) {
    return requireScene(scene)->state->width;
}
int64_t rt_game_scene_get_height(void *scene) {
    return requireScene(scene)->state->height;
}
int64_t rt_game_scene_get_tile_width(void *scene) {
    return requireScene(scene)->state->tileWidth;
}
int64_t rt_game_scene_get_tile_height(void *scene) {
    return requireScene(scene)->state->tileHeight;
}

int64_t rt_game_scene_add_layer(void *scene, rt_string name) {
    SceneState &s = *requireScene(scene)->state;
    s.layers.push_back(makeLayer(s, toStd(name)));
    return static_cast<int64_t>(s.layers.size()) - 1;
}

int64_t rt_game_scene_layer_count(void *scene) {
    return static_cast<int64_t>(requireScene(scene)->state->layers.size());
}

rt_string rt_game_scene_layer_name(void *scene, int64_t layer) {
    SceneState &s = *requireScene(scene)->state;
    return makeString(validLayer(s, layer) ? s.layers[static_cast<size_t>(layer)].name : "");
}

void rt_game_scene_set_layer_name(void *scene, int64_t layer, rt_string name) {
    SceneState &s = *requireScene(scene)->state;
    if (validLayer(s, layer))
        s.layers[static_cast<size_t>(layer)].name = toStd(name);
}

int8_t rt_game_scene_layer_visible(void *scene, int64_t layer) {
    SceneState &s = *requireScene(scene)->state;
    return validLayer(s, layer) && s.layers[static_cast<size_t>(layer)].visible ? 1 : 0;
}

void rt_game_scene_set_layer_visible(void *scene, int64_t layer, int8_t visible) {
    SceneState &s = *requireScene(scene)->state;
    if (validLayer(s, layer))
        s.layers[static_cast<size_t>(layer)].visible = visible != 0;
}

void rt_game_scene_move_layer(void *scene, int64_t from, int64_t to) {
    SceneState &s = *requireScene(scene)->state;
    if (!validLayer(s, from) || !validLayer(s, to) || from == to)
        return;
    Layer layer = std::move(s.layers[static_cast<size_t>(from)]);
    s.layers.erase(s.layers.begin() + from);
    s.layers.insert(s.layers.begin() + to, std::move(layer));
}

void rt_game_scene_remove_layer(void *scene, int64_t layer) {
    SceneState &s = *requireScene(scene)->state;
    if (s.layers.size() <= 1 || !validLayer(s, layer))
        return;
    s.layers.erase(s.layers.begin() + layer);
}

int64_t rt_game_scene_get_tile(void *scene, int64_t layer, int64_t x, int64_t y) {
    SceneState &s = *requireScene(scene)->state;
    if (!validLayer(s, layer) || !validTile(s, x, y))
        return 0;
    return s.layers[static_cast<size_t>(layer)].tiles[static_cast<size_t>(y * s.width + x)];
}

void rt_game_scene_set_tile(void *scene, int64_t layer, int64_t x, int64_t y, int64_t tile) {
    SceneState &s = *requireScene(scene)->state;
    if (!validLayer(s, layer) || !validTile(s, x, y))
        return;
    s.layers[static_cast<size_t>(layer)].tiles[static_cast<size_t>(y * s.width + x)] = tile;
}

void rt_game_scene_fill_tiles(void *scene,
                              int64_t layer,
                              int64_t x,
                              int64_t y,
                              int64_t w,
                              int64_t h,
                              int64_t tile) {
    for (int64_t yy = y; yy < y + h; yy++)
        for (int64_t xx = x; xx < x + w; xx++)
            rt_game_scene_set_tile(scene, layer, xx, yy, tile);
}

void rt_game_scene_set_layer_asset(void *scene, int64_t layer, rt_string asset_path) {
    SceneState &s = *requireScene(scene)->state;
    if (validLayer(s, layer))
        s.layers[static_cast<size_t>(layer)].asset = toStd(asset_path);
}

rt_string rt_game_scene_layer_asset(void *scene, int64_t layer) {
    SceneState &s = *requireScene(scene)->state;
    return makeString(validLayer(s, layer) ? s.layers[static_cast<size_t>(layer)].asset : "");
}

int64_t rt_game_scene_add_object(void *scene,
                                 rt_string type,
                                 rt_string id,
                                 int64_t x,
                                 int64_t y) {
    SceneState &s = *requireScene(scene)->state;
    s.objects.push_back(Object{toStd(type), toStd(id), x, y, {}});
    return static_cast<int64_t>(s.objects.size()) - 1;
}

int64_t rt_game_scene_object_count(void *scene) {
    return static_cast<int64_t>(requireScene(scene)->state->objects.size());
}

void rt_game_scene_remove_object(void *scene, int64_t index) {
    SceneState &s = *requireScene(scene)->state;
    if (index >= 0 && index < static_cast<int64_t>(s.objects.size()))
        s.objects.erase(s.objects.begin() + index);
}

rt_string rt_game_scene_object_type(void *scene, int64_t index) {
    SceneState &s = *requireScene(scene)->state;
    return makeString(index >= 0 && index < static_cast<int64_t>(s.objects.size())
                          ? s.objects[static_cast<size_t>(index)].type
                          : "");
}

rt_string rt_game_scene_object_id(void *scene, int64_t index) {
    SceneState &s = *requireScene(scene)->state;
    return makeString(index >= 0 && index < static_cast<int64_t>(s.objects.size())
                          ? s.objects[static_cast<size_t>(index)].id
                          : "");
}

int64_t rt_game_scene_object_x(void *scene, int64_t index) {
    SceneState &s = *requireScene(scene)->state;
    return index >= 0 && index < static_cast<int64_t>(s.objects.size())
               ? s.objects[static_cast<size_t>(index)].x
               : 0;
}

int64_t rt_game_scene_object_y(void *scene, int64_t index) {
    SceneState &s = *requireScene(scene)->state;
    return index >= 0 && index < static_cast<int64_t>(s.objects.size())
               ? s.objects[static_cast<size_t>(index)].y
               : 0;
}

void rt_game_scene_set_object_position(void *scene, int64_t index, int64_t x, int64_t y) {
    SceneState &s = *requireScene(scene)->state;
    if (index >= 0 && index < static_cast<int64_t>(s.objects.size())) {
        s.objects[static_cast<size_t>(index)].x = x;
        s.objects[static_cast<size_t>(index)].y = y;
    }
}

void rt_game_scene_set_object_property(void *scene,
                                       int64_t index,
                                       rt_string key,
                                       rt_string value) {
    SceneState &s = *requireScene(scene)->state;
    if (index >= 0 && index < static_cast<int64_t>(s.objects.size()))
        s.objects[static_cast<size_t>(index)].properties[toStd(key)] = toStd(value);
}

rt_string rt_game_scene_get_object_property(void *scene, int64_t index, rt_string key) {
    SceneState &s = *requireScene(scene)->state;
    if (index < 0 || index >= static_cast<int64_t>(s.objects.size()))
        return makeString("");
    auto it = s.objects[static_cast<size_t>(index)].properties.find(toStd(key));
    return makeString(it == s.objects[static_cast<size_t>(index)].properties.end() ? "" : it->second);
}

void rt_game_scene_delete_object_property(void *scene, int64_t index, rt_string key) {
    SceneState &s = *requireScene(scene)->state;
    if (index >= 0 && index < static_cast<int64_t>(s.objects.size()))
        s.objects[static_cast<size_t>(index)].properties.erase(toStd(key));
}

void rt_game_scene_set_property(void *scene, rt_string key, rt_string value) {
    requireScene(scene)->state->properties[toStd(key)] = toStd(value);
}

rt_string rt_game_scene_get_property(void *scene, rt_string key) {
    SceneState &s = *requireScene(scene)->state;
    auto it = s.properties.find(toStd(key));
    return makeString(it == s.properties.end() ? "" : it->second);
}

void rt_game_scene_delete_property(void *scene, rt_string key) {
    requireScene(scene)->state->properties.erase(toStd(key));
}

void *rt_game_scene_asset_paths(void *scene) {
    SceneState &s = *requireScene(scene)->state;
    std::set<std::string> paths;
    for (const auto &layer : s.layers) {
        if (!layer.asset.empty())
            paths.insert(layer.asset);
    }
    for (const auto &[key, value] : s.properties) {
        if (key.find("asset") != std::string::npos || key.find("tileset") != std::string::npos)
            paths.insert(value);
    }
    for (const auto &obj : s.objects) {
        for (const auto &[key, value] : obj.properties) {
            if (key.find("asset") != std::string::npos || key.find("sprite") != std::string::npos)
                paths.insert(value);
        }
    }
    void *seq = rt_seq_new_owned();
    for (const auto &path : paths) {
        rt_string item = makeString(path);
        rt_seq_push(seq, item);
        rt_string_unref(item);
    }
    return seq;
}

} // extern "C"
