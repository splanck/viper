//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_canvas3d_internal.h"
#include "rt_model3d.h"
#include "rt_scene3d.h"
#include "rt_string.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
extern rt_string rt_const_cstr(const char *s);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_mesh3d_new_box(double w, double h, double d);
extern void *rt_material3d_new_color(double r, double g, double b);
}

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
            std::fprintf(stderr, "FAIL: %s\n", msg);                                               \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (std::fabs((a) - (b)) > (eps))                                                          \
            std::fprintf(stderr, "FAIL: %s\n", msg);                                               \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

template <typename T> static void append_bytes(std::vector<uint8_t> &buf, const T &value) {
    size_t offset = buf.size();
    buf.resize(offset + sizeof(T));
    std::memcpy(buf.data() + offset, &value, sizeof(T));
}

static std::string base64_encode(const uint8_t *data, size_t len) {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out(((len + 2) / 3) * 4, '=');
    size_t i = 0;
    size_t j = 0;
    while (i < len) {
        uint32_t a = data[i++];
        uint32_t b = (i < len) ? data[i++] : 0;
        uint32_t c = (i < len) ? data[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = chars[(triple >> 18) & 0x3F];
        out[j++] = chars[(triple >> 12) & 0x3F];
        out[j++] = chars[(triple >> 6) & 0x3F];
        out[j++] = chars[triple & 0x3F];
    }
    size_t padding = (3 - (len % 3)) % 3;
    for (size_t p = 0; p < padding; p++)
        out[out.size() - 1 - p] = '=';
    return out;
}

static bool write_scene_fixture(const char *path) {
    void *scene = rt_scene3d_new();
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new_box(1.0, 2.0, 3.0);
    void *material = rt_material3d_new_color(0.2, 0.4, 0.8);

    if (!scene || !parent || !child || !mesh || !material)
        return false;

    rt_scene_node3d_set_name(parent, rt_const_cstr("parent"));
    rt_scene_node3d_set_name(child, rt_const_cstr("child"));
    rt_scene_node3d_set_position(parent, 1.0, 2.0, 3.0);
    rt_scene_node3d_set_position(child, 0.0, 5.0, 0.0);
    rt_scene_node3d_set_mesh(parent, mesh);
    rt_scene_node3d_set_material(parent, material);
    rt_scene_node3d_set_mesh(child, mesh);
    rt_scene_node3d_set_material(child, material);
    rt_scene_node3d_add_child(parent, child);
    rt_scene3d_add(scene, parent);

    return rt_scene3d_save(scene, rt_const_cstr(path)) == 1;
}

static bool write_gltf_fixture(const char *path) {
    std::vector<uint8_t> gltf_buffer;
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const float normals[9] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    const float uvs[6] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    const uint16_t indices[3] = {0, 1, 2};

    for (float v : positions)
        append_bytes(gltf_buffer, v);
    for (float v : normals)
        append_bytes(gltf_buffer, v);
    for (float v : uvs)
        append_bytes(gltf_buffer, v);
    for (uint16_t v : indices)
        append_bytes(gltf_buffer, v);

    std::string buffer_b64 = base64_encode(gltf_buffer.data(), gltf_buffer.size());
    std::string gltf_json =
        "{\n"
        "  \"asset\": {\"version\": \"2.0\"},\n"
        "  \"buffers\": [{\"uri\": \"data:application/octet-stream;base64," +
        buffer_b64 + "\", \"byteLength\": " + std::to_string(gltf_buffer.size()) +
        "}],\n"
        "  \"bufferViews\": [\n"
        "    {\"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36},\n"
        "    {\"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36},\n"
        "    {\"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 24},\n"
        "    {\"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 6}\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    {\"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\"},\n"
        "    {\"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\"},\n"
        "    {\"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\"},\n"
        "    {\"bufferView\": 3, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\"}\n"
        "  ],\n"
        "  \"materials\": [{\n"
        "    \"pbrMetallicRoughness\": {\n"
        "      \"baseColorFactor\": [0.7, 0.3, 0.2, 1.0]\n"
        "    }\n"
        "  }],\n"
        "  \"meshes\": [{\"primitives\": [{\n"
        "    \"attributes\": {\"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2},\n"
        "    \"indices\": 3,\n"
        "    \"material\": 0\n"
        "  }]}],\n"
        "  \"nodes\": [\n"
        "    {\"name\": \"GltfParent\", \"translation\": [1.0, 0.0, 0.0], \"mesh\": 0, "
        "\"children\": [1]},\n"
        "    {\"name\": \"GltfChild\", \"scale\": [2.0, 2.0, 2.0]}\n"
        "  ],\n"
        "  \"scenes\": [{\"nodes\": [0]}],\n"
        "  \"scene\": 0\n"
        "}\n";

    FILE *gltf = std::fopen(path, "wb");
    if (!gltf)
        return false;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);
    return true;
}

static void test_model3d_roundtrips_vscn_assets() {
    const char *path = "/tmp/viper_model3d_fixture.vscn";
    bool wrote_fixture = write_scene_fixture(path);
    EXPECT_TRUE(wrote_fixture, "Scene fixture can be written to .vscn");
    if (!wrote_fixture)
        return;

    void *model = rt_model3d_load(rt_const_cstr(path));
    EXPECT_TRUE(model != nullptr, "Model3D.Load parses .vscn assets");
    if (!model)
        return;

    EXPECT_TRUE(rt_model3d_get_mesh_count(model) == 1, "Model3D deduplicates shared meshes");
    EXPECT_TRUE(rt_model3d_get_material_count(model) == 1,
                "Model3D deduplicates shared materials");
    EXPECT_TRUE(rt_model3d_get_skeleton_count(model) == 0,
                "Model3D .vscn fixtures start without skeletons");
    EXPECT_TRUE(rt_model3d_get_animation_count(model) == 0,
                "Model3D .vscn fixtures start without animations");
    EXPECT_TRUE(rt_model3d_get_node_count(model) == 2, "Model3D counts imported scene nodes");

    void *template_parent = rt_model3d_find_node(model, rt_const_cstr("parent"));
    void *template_child = rt_model3d_find_node(model, rt_const_cstr("child"));
    EXPECT_TRUE(template_parent != nullptr, "Model3D.FindNode finds template parent nodes");
    EXPECT_TRUE(template_child != nullptr, "Model3D.FindNode finds template child nodes");
    if (!template_parent || !template_child)
        return;

    EXPECT_NEAR(rt_vec3_x(rt_scene_node3d_get_position(template_parent)),
                1.0,
                0.001,
                "Model3D preserves parent node translation");
    EXPECT_NEAR(rt_vec3_y(rt_scene_node3d_get_position(template_child)),
                5.0,
                0.001,
                "Model3D preserves child node translation");

    void *inst_root = rt_model3d_instantiate(model);
    EXPECT_TRUE(inst_root != nullptr, "Model3D.Instantiate clones a scene-node subtree");
    if (!inst_root)
        return;

    EXPECT_TRUE(rt_scene_node3d_child_count(inst_root) == 1,
                "Model3D.Instantiate returns a synthetic root with top-level children");
    void *inst_parent = rt_scene_node3d_find(inst_root, rt_const_cstr("parent"));
    void *inst_child = rt_scene_node3d_find(inst_root, rt_const_cstr("child"));
    EXPECT_TRUE(inst_parent != nullptr, "Model3D.Instantiate preserves named parent nodes");
    EXPECT_TRUE(inst_child != nullptr, "Model3D.Instantiate preserves named child nodes");
    if (!inst_parent || !inst_child)
        return;

    rt_scene_node3d_set_position(inst_child, 9.0, 9.0, 9.0);
    EXPECT_NEAR(rt_vec3_y(rt_scene_node3d_get_position(inst_child)),
                9.0,
                0.001,
                "Instantiated nodes can be mutated independently");
    EXPECT_NEAR(rt_vec3_y(rt_scene_node3d_get_position(template_child)),
                5.0,
                0.001,
                "Template nodes remain unchanged after instance mutation");
    EXPECT_TRUE(rt_scene_node3d_get_mesh(inst_parent) == rt_model3d_get_mesh(model, 0),
                "Instantiated nodes reuse shared mesh objects");
    EXPECT_TRUE(rt_scene_node3d_get_material(inst_parent) == rt_model3d_get_material(model, 0),
                "Instantiated nodes reuse shared material objects");

    void *inst_scene = rt_model3d_instantiate_scene(model);
    EXPECT_TRUE(inst_scene != nullptr, "Model3D.InstantiateScene creates a live Scene3D");
    if (!inst_scene)
        return;

    EXPECT_TRUE(rt_scene3d_get_node_count(inst_scene) == 3,
                "Model3D.InstantiateScene attaches imported nodes below the scene root");
    EXPECT_TRUE(rt_scene_node3d_child_count(rt_scene3d_get_root(inst_scene)) == 1,
                "InstantiateScene preserves top-level node grouping");
    EXPECT_TRUE(rt_scene3d_find(inst_scene, rt_const_cstr("child")) != nullptr,
                "InstantiateScene preserves node searchability");
}

static void test_model3d_adapts_gltf_scene_graphs() {
    const char *path = "/tmp/viper_model3d_fixture.gltf";
    bool wrote_fixture = write_gltf_fixture(path);
    EXPECT_TRUE(wrote_fixture, "glTF fixture can be written");
    if (!wrote_fixture)
        return;

    void *model = rt_model3d_load(rt_const_cstr(path));
    EXPECT_TRUE(model != nullptr, "Model3D.Load parses glTF assets");
    if (!model)
        return;

    EXPECT_TRUE(rt_model3d_get_mesh_count(model) == 1, "Model3D exposes glTF meshes");
    EXPECT_TRUE(rt_model3d_get_material_count(model) == 1, "Model3D exposes glTF materials");
    EXPECT_TRUE(rt_model3d_get_node_count(model) == 2,
                "Model3D preserves logical glTF scene-node counts");

    void *parent = rt_model3d_find_node(model, rt_const_cstr("GltfParent"));
    void *child = rt_model3d_find_node(model, rt_const_cstr("GltfChild"));
    EXPECT_TRUE(parent != nullptr, "Model3D.FindNode finds glTF parent nodes");
    EXPECT_TRUE(child != nullptr, "Model3D.FindNode finds glTF child nodes");
    if (!parent || !child)
        return;

    EXPECT_NEAR(rt_vec3_x(rt_scene_node3d_get_position(parent)),
                1.0,
                0.001,
                "Model3D preserves glTF node translations");
    EXPECT_NEAR(rt_vec3_x(rt_scene_node3d_get_scale(child)),
                2.0,
                0.001,
                "Model3D preserves glTF node scales");

    void *inst_scene = rt_model3d_instantiate_scene(model);
    EXPECT_TRUE(inst_scene != nullptr, "Model3D.InstantiateScene works for glTF assets");
    if (!inst_scene)
        return;

    EXPECT_TRUE(rt_scene3d_get_node_count(inst_scene) == 3,
                "glTF-backed Model3D instances attach below a new scene root");
    EXPECT_TRUE(rt_scene3d_find(inst_scene, rt_const_cstr("GltfChild")) != nullptr,
                "glTF-backed Model3D instances preserve child names");
}

int main() {
    test_model3d_roundtrips_vscn_assets();
    test_model3d_adapts_gltf_scene_graphs();
    std::printf("Model3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
