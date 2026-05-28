//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_asset.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_gltf.h"
#include "rt_morphtarget3d.h"
#include "rt_pixels.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "VpaWriter.hpp"

extern "C" {
extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern rt_string rt_const_cstr(const char *s);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
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

static bool read_file_bytes(const char *path, std::vector<uint8_t> &out) {
    out.clear();
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    long size = std::ftell(f);
    if (size < 0 || std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    out.resize((size_t)size);
    bool ok = size == 0 || std::fread(out.data(), 1, (size_t)size, f) == (size_t)size;
    std::fclose(f);
    return ok;
}

static bool write_text_file(const char *path, const std::string &text) {
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

static void test_gltf_accessors_reject_wrong_handles() {
    void *fake = rt_obj_new_i64(0, 8);
    EXPECT_TRUE(rt_gltf_mesh_count(fake) == 0, "GLTF mesh count rejects wrong handles");
    EXPECT_TRUE(rt_gltf_material_count(fake) == 0, "GLTF material count rejects wrong handles");
    EXPECT_TRUE(rt_gltf_skeleton_count(fake) == 0, "GLTF skeleton count rejects wrong handles");
    EXPECT_TRUE(rt_gltf_animation_count(fake) == 0, "GLTF animation count rejects wrong handles");
    EXPECT_TRUE(rt_gltf_node_animation_count(fake) == 0,
                "GLTF node-animation count rejects wrong handles");
    EXPECT_TRUE(rt_gltf_node_count(fake) == 0, "GLTF node count rejects wrong handles");
    EXPECT_TRUE(rt_gltf_get_mesh(fake, 0) == nullptr, "GLTF mesh getter rejects wrong handles");
    EXPECT_TRUE(rt_gltf_get_material(fake, 0) == nullptr,
                "GLTF material getter rejects wrong handles");
    EXPECT_TRUE(rt_gltf_get_skeleton(fake, 0) == nullptr,
                "GLTF skeleton getter rejects wrong handles");
    EXPECT_TRUE(rt_gltf_get_animation(fake, 0) == nullptr,
                "GLTF animation getter rejects wrong handles");
    EXPECT_TRUE(rt_gltf_get_node_animation(fake, 0) == nullptr,
                "GLTF node-animation getter rejects wrong handles");
    EXPECT_TRUE(rt_gltf_get_scene_root(fake) == nullptr,
                "GLTF scene-root getter rejects wrong handles");
}

template <typename T> static void append_bytes(std::vector<uint8_t> &buf, const T &value) {
    size_t offset = buf.size();
    buf.resize(offset + sizeof(T));
    std::memcpy(buf.data() + offset, &value, sizeof(T));
}

static void append_u32_le(std::vector<uint8_t> &buf, uint32_t value) {
    buf.push_back((uint8_t)(value & 0xFFu));
    buf.push_back((uint8_t)((value >> 8) & 0xFFu));
    buf.push_back((uint8_t)((value >> 16) & 0xFFu));
    buf.push_back((uint8_t)((value >> 24) & 0xFFu));
}

static void test_gltf_loads_data_uri_buffers_and_embedded_textures() {
    const char *png_path = "/tmp/viper_gltf_embedded.png";
    const char *gltf_path = "/tmp/viper_gltf_embedded.gltf";

    void *pixels = rt_pixels_new(1, 1);
    rt_pixels_set(pixels, 0, 0, 0x336699FFll);
    EXPECT_TRUE(rt_pixels_save_png(pixels, rt_const_cstr(png_path)) == 1,
                "Pixels.SavePng creates a temporary embedded texture");

    std::vector<uint8_t> png_bytes;
    EXPECT_TRUE(read_file_bytes(png_path, png_bytes), "Embedded texture PNG can be read back");
    if (png_bytes.empty())
        return;

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
    std::string image_b64 = base64_encode(png_bytes.data(), png_bytes.size());

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
        "  \"images\": [{\"uri\": \"data:image/png;base64," +
        image_b64 +
        "\"}],\n"
        "  \"textures\": [{\"source\": 0}],\n"
        "  \"materials\": [{\n"
        "    \"pbrMetallicRoughness\": {\n"
        "      \"baseColorFactor\": [1.0, 1.0, 1.0, 0.7],\n"
        "      \"baseColorTexture\": {\"index\": 0},\n"
        "      \"metallicFactor\": 0.75,\n"
        "      \"roughnessFactor\": 0.25,\n"
        "      \"metallicRoughnessTexture\": {\"index\": 0}\n"
        "    },\n"
        "    \"normalTexture\": {\"index\": 0, \"scale\": 0.6},\n"
        "    \"occlusionTexture\": {\"index\": 0, \"strength\": 0.5},\n"
        "    \"emissiveTexture\": {\"index\": 0},\n"
        "    \"emissiveFactor\": [1.0, 1.0, 1.0],\n"
        "    \"doubleSided\": true,\n"
        "    \"alphaMode\": \"BLEND\",\n"
        "    \"extensions\": {\"KHR_materials_emissive_strength\": {\"emissiveStrength\": 1.8}}\n"
        "  }],\n"
        "  \"meshes\": [{\"primitives\": [{\n"
        "    \"attributes\": {\"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2},\n"
        "    \"indices\": 3,\n"
        "    \"material\": 0\n"
        "  }]}]\n"
        "}\n";

    FILE *gltf = std::fopen(gltf_path, "wb");
    EXPECT_TRUE(gltf != nullptr, "Embedded glTF file can be created");
    if (!gltf)
        return;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses data-uri assets");
    if (!asset)
        return;

    EXPECT_TRUE(rt_gltf_mesh_count(asset) == 1, "GLTF.Load extracts one mesh primitive");
    EXPECT_TRUE(rt_gltf_material_count(asset) == 1, "GLTF.Load extracts one material");

    rt_mesh3d *mesh = (rt_mesh3d *)rt_gltf_get_mesh(asset, 0);
    rt_material3d *material = (rt_material3d *)rt_gltf_get_material(asset, 0);
    EXPECT_TRUE(mesh != nullptr && mesh->vertex_count == 3 && mesh->index_count == 3,
                "GLTF.Load decodes mesh buffers from data URIs");
    EXPECT_TRUE(material != nullptr, "GLTF.Load returns decoded materials");
    if (!mesh || !material)
        return;

    EXPECT_TRUE(mesh->vertices[1].pos[0] == 1.0f && mesh->vertices[2].uv[1] == 1.0f,
                "GLTF.Load preserves vertex attributes");
    EXPECT_TRUE(std::fabs(mesh->vertices[0].tangent[0]) > 0.9f &&
                    std::fabs(mesh->vertices[0].tangent[3]) > 0.9f,
                "GLTF.Load computes tangents when a normal map is present but tangents are absent");
    EXPECT_TRUE(material->workflow == RT_MATERIAL3D_WORKFLOW_PBR,
                "GLTF.Load preserves the PBR workflow");
    EXPECT_TRUE(material->texture != nullptr &&
                    rt_pixels_get(material->texture, 0, 0) == 0x336699FFll,
                "GLTF.Load wires base color textures into Material3D");
    EXPECT_TRUE(material->normal_map != nullptr &&
                    rt_pixels_get(material->normal_map, 0, 0) == 0x336699FFll,
                "GLTF.Load wires normal textures into Material3D");
    EXPECT_TRUE(material->metallic_roughness_map != nullptr &&
                    rt_pixels_get(material->metallic_roughness_map, 0, 0) == 0x336699FFll,
                "GLTF.Load wires metallic-roughness textures into Material3D");
    EXPECT_TRUE(material->ao_map != nullptr &&
                    rt_pixels_get(material->ao_map, 0, 0) == 0x336699FFll,
                "GLTF.Load wires occlusion textures into Material3D");
    EXPECT_TRUE(material->emissive_map != nullptr &&
                    rt_pixels_get(material->emissive_map, 0, 0) == 0x336699FFll,
                "GLTF.Load wires emissive textures into Material3D");
    EXPECT_NEAR(material->metallic, 0.75, 0.001, "GLTF.Load preserves metallicFactor");
    EXPECT_NEAR(material->roughness, 0.25, 0.001, "GLTF.Load preserves roughnessFactor");
    EXPECT_NEAR(material->ao, 0.5, 0.001, "GLTF.Load preserves occlusion strength");
    EXPECT_NEAR(material->normal_scale, 0.6, 0.001, "GLTF.Load preserves normal-map scale");
    EXPECT_NEAR(material->alpha, 0.7, 0.001, "GLTF.Load preserves base-color alpha");
    EXPECT_NEAR(material->emissive_intensity,
                1.8,
                0.001,
                "GLTF.Load preserves emissive strength extension");
    EXPECT_TRUE(material->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_BLEND,
                "GLTF.Load preserves alphaMode");
    EXPECT_TRUE(material->double_sided == 1, "GLTF.Load preserves doubleSided");
}

static void test_gltf_resolves_percent_encoded_external_buffers() {
    const char *bin_path = "/tmp/viper gltf encoded buffer.bin";
    const char *gltf_path = "/tmp/viper_gltf_encoded_external_buffer.gltf";
    std::vector<uint8_t> gltf_buffer;
    const float positions[9] = {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f};
    const uint16_t indices[3] = {0, 1, 2};

    for (float v : positions)
        append_bytes(gltf_buffer, v);
    for (uint16_t v : indices)
        append_bytes(gltf_buffer, v);

    FILE *bin = std::fopen(bin_path, "wb");
    EXPECT_TRUE(bin != nullptr, "External glTF buffer file can be created");
    if (!bin)
        return;
    std::fwrite(gltf_buffer.data(), 1, gltf_buffer.size(), bin);
    std::fclose(bin);

    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"viper%20gltf%20encoded%20buffer.bin\",\"byteLength\":" +
        std::to_string(gltf_buffer.size()) +
        "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}"
        "],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
        "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]"
        "}";

    FILE *gltf = std::fopen(gltf_path, "wb");
    EXPECT_TRUE(gltf != nullptr, "External-buffer glTF fixture can be created");
    if (!gltf)
        return;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load resolves percent-encoded external buffer URIs");
    if (!asset)
        return;
    auto *mesh = static_cast<rt_mesh3d *>(rt_gltf_get_mesh(asset, 0));
    EXPECT_TRUE(mesh != nullptr && mesh->vertex_count == 3 && mesh->index_count == 3,
                "GLTF.Load imports mesh data from an encoded external buffer path");
    if (!mesh)
        return;
    EXPECT_NEAR(mesh->vertices[1].pos[0], 2.0, 0.001, "External buffer vertex X is loaded");
    EXPECT_NEAR(mesh->vertices[2].pos[1], 3.0, 0.001, "External buffer vertex Y is loaded");
}

static void test_gltf_load_asset_resolves_mounted_external_buffers() {
    const char *pack_path = "/tmp/viper_gltf_asset_pack.vpa";
    const char *png_path = "/tmp/viper_gltf_asset_texture.png";
    void *pixels = rt_pixels_new(1, 1);
    rt_pixels_set(pixels, 0, 0, 0x224466FFll);
    EXPECT_TRUE(rt_pixels_save_png(pixels, rt_const_cstr(png_path)) == 1,
                "Package texture PNG can be written");
    std::vector<uint8_t> png_bytes;
    EXPECT_TRUE(read_file_bytes(png_path, png_bytes), "Package texture PNG can be read");
    if (png_bytes.empty())
        return;

    std::vector<uint8_t> gltf_buffer;
    const float positions[9] = {0.0f, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f};
    const uint16_t indices[3] = {0, 1, 2};

    for (float v : positions)
        append_bytes(gltf_buffer, v);
    for (uint16_t v : indices)
        append_bytes(gltf_buffer, v);

    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"buffers/tri.bin\",\"byteLength\":" +
        std::to_string(gltf_buffer.size()) +
        "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}"
        "],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
        "],"
        "\"images\":[{\"uri\":\"textures/albedo.png\"}],"
        "\"textures\":[{\"source\":0}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,"
        "\"material\":0}]}]"
        "}";

    viper::asset::VpaWriter writer;
    writer.addEntry("assets/models/tri.gltf",
                    reinterpret_cast<const uint8_t *>(gltf_json.data()),
                    gltf_json.size(),
                    false);
    writer.addEntry("assets/models/buffers/tri.bin", gltf_buffer.data(), gltf_buffer.size(), false);
    writer.addEntry("assets/models/textures/albedo.png", png_bytes.data(), png_bytes.size(), false);
    std::string err;
    bool wrote_pack = writer.writeToFile(pack_path, err);
    EXPECT_TRUE(wrote_pack, "Mounted glTF asset pack can be written");
    if (!err.empty())
        std::fprintf(stderr, "VPA write detail: %s\n", err.c_str());
    if (!wrote_pack)
        return;

    bool mounted = rt_asset_mount(rt_const_cstr(pack_path)) == 1;
    EXPECT_TRUE(mounted, "Mounted glTF asset pack can mount");
    if (!mounted)
        return;
    void *asset = rt_gltf_load_asset(rt_const_cstr("asset://assets/models/tri.gltf"));
    EXPECT_TRUE(asset != nullptr, "GLTF.LoadAsset loads a root model from a mounted asset URI");
    if (asset) {
        auto *mesh = static_cast<rt_mesh3d *>(rt_gltf_get_mesh(asset, 0));
        auto *material = static_cast<rt_material3d *>(rt_gltf_get_material(asset, 0));
        EXPECT_TRUE(mesh != nullptr && mesh->vertex_count == 3 && mesh->index_count == 3,
                    "GLTF.LoadAsset imports geometry from a mounted external buffer");
        EXPECT_TRUE(material != nullptr && material->texture != nullptr &&
                        rt_pixels_get(material->texture, 0, 0) == 0x224466FFll,
                    "GLTF.LoadAsset imports package-relative external textures");
        if (mesh) {
            EXPECT_NEAR(
                mesh->vertices[1].pos[0], 4.0, 0.001, "Mounted external buffer vertex X is loaded");
            EXPECT_NEAR(
                mesh->vertices[2].pos[1], 5.0, 0.001, "Mounted external buffer vertex Y is loaded");
        }
    }

    rt_asset_unmount(rt_const_cstr(pack_path));
    std::remove(png_path);
    std::remove(pack_path);
}

static std::vector<uint8_t> make_triangle_glb(float x1, float y2) {
    std::vector<uint8_t> bin;
    const float positions[9] = {0.0f, 0.0f, 0.0f, x1, 0.0f, 0.0f, 0.0f, y2, 0.0f};
    const uint16_t indices[3] = {0, 1, 2};
    for (float v : positions)
        append_bytes(bin, v);
    for (uint16_t v : indices)
        append_bytes(bin, v);
    size_t gltf_byte_length = bin.size();
    while ((bin.size() & 3u) != 0)
        bin.push_back(0);

    std::string json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":" +
        std::to_string(gltf_byte_length) +
        "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}"
        "],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
        "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]"
        "}";
    while ((json.size() & 3u) != 0)
        json.push_back(' ');

    std::vector<uint8_t> glb;
    glb.insert(glb.end(), {'g', 'l', 'T', 'F'});
    append_u32_le(glb, 2);
    append_u32_le(glb, (uint32_t)(12 + 8 + json.size() + 8 + bin.size()));
    append_u32_le(glb, (uint32_t)json.size());
    append_u32_le(glb, 0x4E4F534Au);
    glb.insert(glb.end(), json.begin(), json.end());
    append_u32_le(glb, (uint32_t)bin.size());
    append_u32_le(glb, 0x004E4942u);
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

static void test_gltf_load_asset_handles_glb_filesystem_and_mounted_package() {
    const char *glb_path = "/tmp/viper_gltf_asset_triangle.glb";
    const char *pack_path = "/tmp/viper_gltf_asset_glb_pack.vpa";
    std::vector<uint8_t> glb = make_triangle_glb(8.0f, 9.0f);

    FILE *f = std::fopen(glb_path, "wb");
    EXPECT_TRUE(f != nullptr, "GLB fixture can be written");
    if (!f)
        return;
    std::fwrite(glb.data(), 1, glb.size(), f);
    std::fclose(f);

    void *fs_asset = rt_gltf_load_asset(rt_const_cstr(glb_path));
    EXPECT_TRUE(fs_asset != nullptr, "GLTF.LoadAsset loads GLB through filesystem fallback");
    if (fs_asset) {
        auto *mesh = static_cast<rt_mesh3d *>(rt_gltf_get_mesh(fs_asset, 0));
        EXPECT_TRUE(mesh != nullptr && mesh->vertex_count == 3 && mesh->index_count == 3,
                    "Filesystem GLB imports embedded BIN geometry");
        if (mesh) {
            EXPECT_NEAR(mesh->vertices[1].pos[0], 8.0, 0.001, "Filesystem GLB vertex X loads");
            EXPECT_NEAR(mesh->vertices[2].pos[1], 9.0, 0.001, "Filesystem GLB vertex Y loads");
        }
    }

    viper::asset::VpaWriter writer;
    writer.addEntry("assets/models/tri.glb", glb.data(), glb.size(), false);
    std::string err;
    bool wrote_pack = writer.writeToFile(pack_path, err);
    EXPECT_TRUE(wrote_pack, "GLB asset pack can be written");
    if (!wrote_pack)
        return;
    bool mounted = rt_asset_mount(rt_const_cstr(pack_path)) == 1;
    EXPECT_TRUE(mounted, "GLB asset pack can mount");
    if (!mounted)
        return;

    void *pack_asset = rt_gltf_load_asset(rt_const_cstr("asset://assets/models/tri.glb"));
    EXPECT_TRUE(pack_asset != nullptr, "GLTF.LoadAsset loads GLB from a mounted package");
    if (pack_asset) {
        auto *mesh = static_cast<rt_mesh3d *>(rt_gltf_get_mesh(pack_asset, 0));
        EXPECT_TRUE(mesh != nullptr && mesh->vertex_count == 3 && mesh->index_count == 3,
                    "Mounted GLB imports embedded BIN geometry");
    }

    rt_asset_unmount(rt_const_cstr(pack_path));
    std::remove(pack_path);
    std::remove(glb_path);
}

static void test_gltf_rejects_out_of_range_indices() {
    const char *gltf_path = "/tmp/viper_gltf_bad_indices.gltf";
    std::vector<uint8_t> gltf_buffer;
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const uint16_t indices[3] = {0, 1, 99};

    for (float v : positions)
        append_bytes(gltf_buffer, v);
    for (uint16_t v : indices)
        append_bytes(gltf_buffer, v);

    std::string buffer_b64 = base64_encode(gltf_buffer.data(), gltf_buffer.size());
    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," +
        buffer_b64 + "\",\"byteLength\":" + std::to_string(gltf_buffer.size()) +
        "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}"
        "],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
        "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]"
        "}";

    FILE *gltf = std::fopen(gltf_path, "wb");
    EXPECT_TRUE(gltf != nullptr, "Invalid-index glTF fixture can be created");
    if (!gltf)
        return;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load tolerates malformed primitive indices");
    if (!asset)
        return;
    EXPECT_TRUE(rt_gltf_mesh_count(asset) == 0,
                "GLTF.Load skips primitives whose indices reference missing vertices");
}

static void test_gltf_builds_scene_hierarchy_for_active_scene() {
    const char *gltf_path = "/tmp/viper_gltf_scene_graph.gltf";
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
        "      \"baseColorFactor\": [0.2, 0.6, 0.8, 1.0]\n"
        "    }\n"
        "  }],\n"
        "  \"meshes\": [{\"primitives\": [{\n"
        "    \"attributes\": {\"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2},\n"
        "    \"indices\": 3,\n"
        "    \"material\": 0\n"
        "  }]}],\n"
        "  \"nodes\": [\n"
        "    {\"name\": \"RootNode\", \"translation\": [1.0, 2.0, 3.0], \"mesh\": 0, "
        "\"children\": [1]},\n"
        "    {\"name\": \"ChildNode\", \"scale\": [2.0, 3.0, 4.0]}\n"
        "  ],\n"
        "  \"scenes\": [{\"nodes\": [0]}],\n"
        "  \"scene\": 0\n"
        "}\n";

    FILE *gltf = std::fopen(gltf_path, "wb");
    EXPECT_TRUE(gltf != nullptr, "Scene graph glTF file can be created");
    if (!gltf)
        return;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses scene graph assets");
    if (!asset)
        return;

    EXPECT_TRUE(rt_gltf_node_count(asset) == 2, "GLTF.Load counts logical scene nodes");
    void *scene_root = rt_gltf_get_scene_root(asset);
    EXPECT_TRUE(scene_root != nullptr, "GLTF.Load exposes a reusable scene root");
    if (!scene_root)
        return;

    EXPECT_TRUE(rt_scene_node3d_child_count(scene_root) == 1,
                "GLTF scene root contains one active-scene child");

    void *root_node = rt_scene_node3d_find(scene_root, rt_const_cstr("RootNode"));
    void *child_node = rt_scene_node3d_find(scene_root, rt_const_cstr("ChildNode"));
    EXPECT_TRUE(root_node != nullptr, "GLTF scene graph preserves named root nodes");
    EXPECT_TRUE(child_node != nullptr, "GLTF scene graph preserves named child nodes");
    if (!root_node || !child_node)
        return;

    EXPECT_TRUE(rt_scene_node3d_get_parent(root_node) == scene_root,
                "Top-level glTF nodes attach below the synthetic scene root");
    EXPECT_TRUE(rt_scene_node3d_get_parent(child_node) == root_node,
                "glTF child nodes preserve hierarchy");
    EXPECT_NEAR(rt_vec3_x(rt_scene_node3d_get_position(root_node)),
                1.0,
                0.001,
                "GLTF root node preserves translation.x");
    EXPECT_NEAR(rt_vec3_y(rt_scene_node3d_get_position(root_node)),
                2.0,
                0.001,
                "GLTF root node preserves translation.y");
    EXPECT_NEAR(rt_vec3_z(rt_scene_node3d_get_position(root_node)),
                3.0,
                0.001,
                "GLTF root node preserves translation.z");
    EXPECT_NEAR(rt_vec3_x(rt_scene_node3d_get_scale(child_node)),
                2.0,
                0.001,
                "GLTF child node preserves scale.x");
    EXPECT_NEAR(rt_vec3_y(rt_scene_node3d_get_scale(child_node)),
                3.0,
                0.001,
                "GLTF child node preserves scale.y");
    EXPECT_NEAR(rt_vec3_z(rt_scene_node3d_get_scale(child_node)),
                4.0,
                0.001,
                "GLTF child node preserves scale.z");
    EXPECT_TRUE(rt_scene_node3d_get_mesh(root_node) == rt_gltf_get_mesh(asset, 0),
                "GLTF root node reuses extracted mesh objects");
    EXPECT_TRUE(rt_scene_node3d_get_material(root_node) == rt_gltf_get_material(asset, 0),
                "GLTF root node reuses extracted material objects");
}

static void test_gltf_imports_extended_vertex_attributes_and_triangle_strips() {
    const char *gltf_path = "/tmp/viper_gltf_extended_attrs.gltf";
    std::vector<uint8_t> gltf_buffer;
    const float positions[12] = {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        1.0f,
        1.0f,
        0.0f,
    };
    const float normals[12] = {
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    const float uvs[8] = {
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
    };
    const uint8_t colors[16] = {
        255,
        0,
        0,
        255,
        0,
        255,
        0,
        255,
        0,
        0,
        255,
        255,
        255,
        255,
        255,
        128,
    };
    const float tangents[16] = {
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        0.0f,
        0.0f,
        -1.0f,
    };
    const uint8_t joints[16] = {
        1,
        2,
        3,
        4,
        4,
        3,
        2,
        1,
        5,
        6,
        7,
        8,
        8,
        7,
        6,
        5,
    };
    const uint8_t weights[16] = {
        255,
        0,
        0,
        0,
        128,
        127,
        0,
        0,
        64,
        64,
        64,
        63,
        0,
        0,
        255,
        0,
    };
    const uint16_t indices[4] = {0, 1, 2, 3};

    for (float v : positions)
        append_bytes(gltf_buffer, v);
    for (float v : normals)
        append_bytes(gltf_buffer, v);
    for (float v : uvs)
        append_bytes(gltf_buffer, v);
    for (uint8_t v : colors)
        append_bytes(gltf_buffer, v);
    for (float v : tangents)
        append_bytes(gltf_buffer, v);
    for (uint8_t v : joints)
        append_bytes(gltf_buffer, v);
    for (uint8_t v : weights)
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
        "    {\"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 48},\n"
        "    {\"buffer\": 0, \"byteOffset\": 48, \"byteLength\": 48},\n"
        "    {\"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 32},\n"
        "    {\"buffer\": 0, \"byteOffset\": 128, \"byteLength\": 16},\n"
        "    {\"buffer\": 0, \"byteOffset\": 144, \"byteLength\": 64},\n"
        "    {\"buffer\": 0, \"byteOffset\": 208, \"byteLength\": 16},\n"
        "    {\"buffer\": 0, \"byteOffset\": 224, \"byteLength\": 16},\n"
        "    {\"buffer\": 0, \"byteOffset\": 240, \"byteLength\": 8}\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    {\"bufferView\": 0, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\"},\n"
        "    {\"bufferView\": 1, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\"},\n"
        "    {\"bufferView\": 2, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC2\"},\n"
        "    {\"bufferView\": 3, \"componentType\": 5121, \"normalized\": true, \"count\": 4, "
        "\"type\": \"VEC4\"},\n"
        "    {\"bufferView\": 4, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC4\"},\n"
        "    {\"bufferView\": 5, \"componentType\": 5121, \"count\": 4, \"type\": \"VEC4\"},\n"
        "    {\"bufferView\": 6, \"componentType\": 5121, \"normalized\": true, \"count\": 4, "
        "\"type\": \"VEC4\"},\n"
        "    {\"bufferView\": 7, \"componentType\": 5123, \"count\": 4, \"type\": \"SCALAR\"}\n"
        "  ],\n"
        "  \"materials\": [{\"pbrMetallicRoughness\": {\"baseColorFactor\": [1.0, 1.0, 1.0, "
        "1.0]}}],\n"
        "  \"meshes\": [{\"primitives\": [{\n"
        "    \"attributes\": {\n"
        "      \"POSITION\": 0,\n"
        "      \"NORMAL\": 1,\n"
        "      \"TEXCOORD_0\": 2,\n"
        "      \"COLOR_0\": 3,\n"
        "      \"TANGENT\": 4,\n"
        "      \"JOINTS_0\": 5,\n"
        "      \"WEIGHTS_0\": 6\n"
        "    },\n"
        "    \"indices\": 7,\n"
        "    \"mode\": 5,\n"
        "    \"material\": 0\n"
        "  }]}]\n"
        "}\n";

    FILE *gltf = std::fopen(gltf_path, "wb");
    EXPECT_TRUE(gltf != nullptr, "Extended-attribute glTF file can be created");
    if (!gltf)
        return;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr,
                "GLTF.Load parses triangle-strip meshes with extended attributes");
    if (!asset)
        return;

    rt_mesh3d *mesh = (rt_mesh3d *)rt_gltf_get_mesh(asset, 0);
    EXPECT_TRUE(mesh != nullptr, "GLTF.Load returns the extended-attribute mesh");
    if (!mesh)
        return;

    EXPECT_TRUE(mesh->vertex_count == 4 && mesh->index_count == 6,
                "GLTF.Load triangulates triangle strips into indexed triangles");
    EXPECT_NEAR(mesh->vertices[0].color[0], 1.0, 0.001, "GLTF.Load normalizes COLOR_0 red");
    EXPECT_NEAR(
        mesh->vertices[3].color[3], 128.0 / 255.0, 0.001, "GLTF.Load normalizes COLOR_0 alpha");
    EXPECT_NEAR(
        mesh->vertices[3].tangent[3], -1.0, 0.001, "GLTF.Load preserves tangent handedness");
    EXPECT_TRUE(mesh->vertices[0].bone_indices[0] == 1 && mesh->vertices[0].bone_indices[3] == 4,
                "GLTF.Load preserves JOINTS_0 indices");
    EXPECT_NEAR(
        mesh->vertices[1].bone_weights[0], 128.0 / 255.0, 0.001, "GLTF.Load normalizes WEIGHTS_0");
    EXPECT_TRUE(mesh->bone_count == 9, "GLTF.Load grows the mesh bone palette from JOINTS_0 data");
    EXPECT_TRUE(mesh->indices[0] == 0 && mesh->indices[1] == 1 && mesh->indices[2] == 2 &&
                    mesh->indices[3] == 2 && mesh->indices[4] == 1 && mesh->indices[5] == 3,
                "GLTF.Load preserves triangle-strip winding during triangulation");
}

static void test_gltf_reduces_secondary_joint_sets_to_top_four_influences() {
    const char *gltf_path = "/tmp/viper_gltf_joints1.gltf";
    std::vector<uint8_t> gltf_buffer;
    auto align4 = [&]() {
        while ((gltf_buffer.size() & 3u) != 0)
            gltf_buffer.push_back(0);
    };
    auto append_float_array = [&](const float *values, size_t count) -> size_t {
        align4();
        size_t offset = gltf_buffer.size();
        for (size_t i = 0; i < count; i++)
            append_bytes(gltf_buffer, values[i]);
        return offset;
    };
    auto append_u8_array = [&](const uint8_t *values, size_t count) -> size_t {
        align4();
        size_t offset = gltf_buffer.size();
        for (size_t i = 0; i < count; i++)
            append_bytes(gltf_buffer, values[i]);
        return offset;
    };
    auto append_u16_array = [&](const uint16_t *values, size_t count) -> size_t {
        align4();
        size_t offset = gltf_buffer.size();
        for (size_t i = 0; i < count; i++)
            append_bytes(gltf_buffer, values[i]);
        return offset;
    };

    static const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    static const uint8_t joints0[12] = {1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4};
    static const float weights0[12] = {
        0.10f, 0.20f, 0.05f, 0.05f, 0.10f, 0.20f, 0.05f, 0.05f, 0.10f, 0.20f, 0.05f, 0.05f};
    static const uint8_t joints1[12] = {5, 6, 7, 8, 5, 6, 7, 8, 5, 6, 7, 8};
    static const float weights1[12] = {
        0.30f, 0.15f, 0.10f, 0.05f, 0.30f, 0.15f, 0.10f, 0.05f, 0.30f, 0.15f, 0.10f, 0.05f};
    static const uint16_t indices[3] = {0, 1, 2};

    size_t pos_off = append_float_array(positions, 9);
    size_t joints0_off = append_u8_array(joints0, 12);
    size_t weights0_off = append_float_array(weights0, 12);
    size_t joints1_off = append_u8_array(joints1, 12);
    size_t weights1_off = append_float_array(weights1, 12);
    size_t idx_off = append_u16_array(indices, 3);
    std::string buffer_b64 = base64_encode(gltf_buffer.data(), gltf_buffer.size());

    std::string gltf_json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," +
        buffer_b64 + "\",\"byteLength\":" + std::to_string(gltf_buffer.size()) +
        "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(pos_off) +
        ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(joints0_off) +
        ",\"byteLength\":12},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(weights0_off) +
        ",\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(joints1_off) +
        ",\"byteLength\":12},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(weights1_off) +
        ",\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(idx_off) +
        ",\"byteLength\":6}"
        "],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":3,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":5,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
        "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,"
        "\"WEIGHTS_0\":2,\"JOINTS_1\":3,\"WEIGHTS_1\":4},\"indices\":5}]}]"
        "}";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json), "JOINTS_1 glTF fixture can be created");
    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses secondary joint sets");
    if (!asset)
        return;
    auto *mesh = static_cast<rt_mesh3d *>(rt_gltf_get_mesh(asset, 0));
    EXPECT_TRUE(mesh != nullptr, "GLTF.Load exposes the JOINTS_1 mesh");
    if (!mesh)
        return;
    EXPECT_TRUE(mesh->vertices[0].bone_indices[0] == 1 && mesh->vertices[0].bone_indices[1] == 2 &&
                    mesh->vertices[0].bone_indices[2] == 5 &&
                    mesh->vertices[0].bone_indices[3] == 6,
                "GLTF.Load keeps the four strongest influences across JOINTS_0 and JOINTS_1");
    EXPECT_NEAR(mesh->vertices[0].bone_weights[0],
                0.10 / 0.75,
                0.001,
                "GLTF.Load renormalizes reduced JOINTS_1 influence weights");
    EXPECT_NEAR(mesh->vertices[0].bone_weights[2],
                0.30 / 0.75,
                0.001,
                "GLTF.Load keeps the strongest secondary influence");
    EXPECT_TRUE(mesh->bone_count == 7, "GLTF.Load includes retained JOINTS_1 palette entries");
}

static void test_gltf_applies_matrix_nodes_in_column_major_order() {
    const char *gltf_path = "/tmp/viper_gltf_matrix_node.gltf";
    std::vector<uint8_t> gltf_buffer;
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const uint16_t indices[3] = {0, 1, 2};

    for (float v : positions)
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
        "    {\"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 6}\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    {\"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\"},\n"
        "    {\"bufferView\": 1, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\"}\n"
        "  ],\n"
        "  \"materials\": [{\"pbrMetallicRoughness\": {\"baseColorFactor\": [1.0, 1.0, 1.0, "
        "1.0]}}],\n"
        "  \"meshes\": [{\"primitives\": [{\"attributes\": {\"POSITION\": 0}, \"indices\": 1, "
        "\"material\": 0}]}],\n"
        "  \"nodes\": [{\n"
        "    \"name\": \"MatrixNode\",\n"
        "    \"mesh\": 0,\n"
        "    \"matrix\": [2.0, 0.0, 0.0, 0.0, 0.0, 3.0, 0.0, 0.0, 0.0, 0.0, 4.0, 0.0, 5.0, 6.0, "
        "7.0, 1.0]\n"
        "  }],\n"
        "  \"scenes\": [{\"nodes\": [0]}],\n"
        "  \"scene\": 0\n"
        "}\n";

    FILE *gltf = std::fopen(gltf_path, "wb");
    EXPECT_TRUE(gltf != nullptr, "Matrix-node glTF file can be created");
    if (!gltf)
        return;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses matrix-node assets");
    if (!asset)
        return;

    void *scene_root = rt_gltf_get_scene_root(asset);
    void *node = rt_scene_node3d_find(scene_root, rt_const_cstr("MatrixNode"));
    EXPECT_TRUE(node != nullptr, "GLTF.Load exposes nodes authored with matrix transforms");
    if (!node)
        return;

    EXPECT_NEAR(rt_vec3_x(rt_scene_node3d_get_position(node)),
                5.0,
                0.001,
                "GLTF.Load decodes column-major matrix translation.x correctly");
    EXPECT_NEAR(rt_vec3_y(rt_scene_node3d_get_position(node)),
                6.0,
                0.001,
                "GLTF.Load decodes column-major matrix translation.y correctly");
    EXPECT_NEAR(rt_vec3_z(rt_scene_node3d_get_position(node)),
                7.0,
                0.001,
                "GLTF.Load decodes column-major matrix translation.z correctly");
    EXPECT_NEAR(rt_vec3_x(rt_scene_node3d_get_scale(node)),
                2.0,
                0.001,
                "GLTF.Load decodes matrix scale.x correctly");
    EXPECT_NEAR(rt_vec3_y(rt_scene_node3d_get_scale(node)),
                3.0,
                0.001,
                "GLTF.Load decodes matrix scale.y correctly");
    EXPECT_NEAR(rt_vec3_z(rt_scene_node3d_get_scale(node)),
                4.0,
                0.001,
                "GLTF.Load decodes matrix scale.z correctly");
}

static void test_gltf_imports_skins_and_animation_clips() {
    const char *gltf_path = "/tmp/viper_gltf_skinned_anim.gltf";
    std::vector<uint8_t> gltf_buffer;
    auto align4 = [&]() {
        while ((gltf_buffer.size() & 3u) != 0)
            gltf_buffer.push_back(0);
    };
    auto append_float_array = [&](const float *values, size_t count) -> size_t {
        align4();
        size_t offset = gltf_buffer.size();
        for (size_t i = 0; i < count; i++)
            append_bytes(gltf_buffer, values[i]);
        return offset;
    };
    auto append_u16_array = [&](const uint16_t *values, size_t count) -> size_t {
        align4();
        size_t offset = gltf_buffer.size();
        for (size_t i = 0; i < count; i++)
            append_bytes(gltf_buffer, values[i]);
        return offset;
    };

    static const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    static const uint16_t joints[12] = {0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0};
    static const float weights[12] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.75f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    static const uint16_t indices[3] = {0, 1, 2};
    static const float inverse_binds[32] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0,  0, 1,
                                            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1};
    static const float anim_times[2] = {0.0f, 1.0f};
    static const float anim_translations[18] = {0.0f,
                                                0.0f,
                                                0.0f,
                                                0.0f,
                                                0.0f,
                                                0.0f,
                                                2.0f,
                                                0.0f,
                                                0.0f,
                                                0.0f,
                                                0.0f,
                                                0.0f,
                                                2.0f,
                                                3.0f,
                                                0.0f,
                                                0.0f,
                                                0.0f,
                                                0.0f};
    static const float anim_mid_times[1] = {0.5f};
    static const float anim_scales[3] = {1.0f, 1.0f, 1.0f};

    size_t pos_off = append_float_array(positions, 9);
    size_t joints_off = append_u16_array(joints, 12);
    size_t weights_off = append_float_array(weights, 12);
    size_t indices_off = append_u16_array(indices, 3);
    size_t inverse_off = append_float_array(inverse_binds, 32);
    size_t times_off = append_float_array(anim_times, 2);
    size_t trans_off = append_float_array(anim_translations, 18);
    size_t mid_times_off = append_float_array(anim_mid_times, 1);
    size_t scale_off = append_float_array(anim_scales, 3);
    std::string buffer_b64 = base64_encode(gltf_buffer.data(), gltf_buffer.size());

    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," +
        buffer_b64 + "\",\"byteLength\":" + std::to_string(gltf_buffer.size()) +
        "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(pos_off) +
        ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(joints_off) +
        ",\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(weights_off) +
        ",\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(indices_off) +
        ",\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(inverse_off) +
        ",\"byteLength\":128},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(times_off) +
        ",\"byteLength\":8},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(trans_off) +
        ",\"byteLength\":72},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(mid_times_off) +
        ",\"byteLength\":4},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(scale_off) +
        ",\"byteLength\":12}"
        "],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},"
        "{\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":6,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"},"
        "{\"bufferView\":7,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"},"
        "{\"bufferView\":8,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"}"
        "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,"
        "\"WEIGHTS_0\":2},\"indices\":3}]}],"
        "\"skins\":[{\"joints\":[1,2],\"inverseBindMatrices\":4}],"
        "\"animations\":[{\"name\":\"MoveRoot\",\"samplers\":[{\"input\":5,\"output\":6,"
        "\"interpolation\":\"CUBICSPLINE\"},{\"input\":7,\"output\":8}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}},"
        "{\"sampler\":1,\"target\":{\"node\":1,\"path\":\"scale\"}}]}],"
        "\"nodes\":[{\"name\":\"MeshNode\",\"mesh\":0,\"skin\":0},{\"name\":\"RootJoint\","
        "\"children\":[2]},{\"name\":\"ChildJoint\",\"translation\":[0,1,0]}],"
        "\"scenes\":[{\"nodes\":[0,1]}],\"scene\":0"
        "}";

    FILE *gltf = std::fopen(gltf_path, "wb");
    EXPECT_TRUE(gltf != nullptr, "Skinned glTF fixture can be created");
    if (!gltf)
        return;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses skinned animation assets");
    if (!asset)
        return;

    EXPECT_TRUE(rt_gltf_skeleton_count(asset) == 1, "GLTF.Load extracts one skeleton");
    EXPECT_TRUE(rt_gltf_animation_count(asset) == 1, "GLTF.Load extracts one animation");
    auto *mesh = static_cast<rt_mesh3d *>(rt_gltf_get_mesh(asset, 0));
    EXPECT_TRUE(mesh != nullptr, "GLTF.Load exposes the skinned mesh");
    if (mesh) {
        EXPECT_TRUE(mesh->bone_count == 2, "GLTF.Load remaps joint attributes to runtime bones");
        EXPECT_TRUE(mesh->vertices[1].bone_indices[0] == 0 &&
                        mesh->vertices[1].bone_indices[1] == 1,
                    "GLTF.Load preserves mixed joint influences");
        EXPECT_NEAR(mesh->vertices[1].bone_weights[0], 0.25, 0.001, "GLTF.Load keeps root weight");
        EXPECT_NEAR(mesh->vertices[1].bone_weights[1], 0.75, 0.001, "GLTF.Load keeps child weight");
    }
    auto *anim = static_cast<rt_animation3d *>(rt_gltf_get_animation(asset, 0));
    EXPECT_TRUE(anim != nullptr, "GLTF.Load exposes imported Animation3D clips");
    if (anim) {
        EXPECT_NEAR(anim->duration, 1.0, 0.001, "GLTF animation duration follows sampler input");
        EXPECT_TRUE(anim->channel_count == 1, "GLTF animation targets the imported root bone");
        EXPECT_TRUE(anim->channels[0].keyframe_count == 3,
                    "GLTF animation merges CUBICSPLINE and scale sample times");
        EXPECT_NEAR(anim->channels[0].keyframes[1].position[0],
                    1.25,
                    0.001,
                    "GLTF CUBICSPLINE translation samples Hermite tangents");
        EXPECT_NEAR(anim->channels[0].keyframes[2].position[0],
                    2.0,
                    0.001,
                    "GLTF animation imports translation X");
        EXPECT_NEAR(anim->channels[0].keyframes[2].position[1],
                    3.0,
                    0.001,
                    "GLTF animation imports translation Y");
        EXPECT_NEAR(anim->channels[0].keyframes[1].scale_xyz[0],
                    1.0,
                    0.001,
                    "GLTF animation preserves default scale");
    }
}

static void test_gltf_splits_animation_clips_per_skin() {
    const char *gltf_path = "/tmp/viper_gltf_multi_skin_animation.gltf";
    std::vector<uint8_t> gltf_buffer;
    auto align4 = [&]() {
        while ((gltf_buffer.size() & 3u) != 0)
            gltf_buffer.push_back(0);
    };
    auto append_float_array = [&](const float *values, size_t count) -> size_t {
        align4();
        size_t offset = gltf_buffer.size();
        for (size_t i = 0; i < count; i++)
            append_bytes(gltf_buffer, values[i]);
        return offset;
    };

    static const float times[2] = {0.0f, 1.0f};
    static const float skin_a_trans[6] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    static const float skin_b_trans[6] = {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
    size_t times_off = append_float_array(times, 2);
    size_t skin_a_off = append_float_array(skin_a_trans, 6);
    size_t skin_b_off = append_float_array(skin_b_trans, 6);
    std::string buffer_b64 = base64_encode(gltf_buffer.data(), gltf_buffer.size());

    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," +
        buffer_b64 + "\",\"byteLength\":" + std::to_string(gltf_buffer.size()) +
        "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(times_off) +
        ",\"byteLength\":8},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(skin_a_off) +
        ",\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(skin_b_off) +
        ",\"byteLength\":24}"
        "],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}"
        "],"
        "\"skins\":[{\"joints\":[0]},{\"joints\":[1]}],"
        "\"animations\":[{\"name\":\"Split\",\"samplers\":[{\"input\":0,\"output\":1},"
        "{\"input\":0,\"output\":2}],\"channels\":["
        "{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}},"
        "{\"sampler\":1,\"target\":{\"node\":1,\"path\":\"translation\"}}]}],"
        "\"nodes\":[{\"name\":\"JointA\"},{\"name\":\"JointB\"}],"
        "\"scenes\":[{\"nodes\":[0,1]}],\"scene\":0"
        "}";

    FILE *gltf = std::fopen(gltf_path, "wb");
    EXPECT_TRUE(gltf != nullptr, "Multi-skin glTF fixture can be created");
    if (!gltf)
        return;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses multi-skin animation assets");
    if (!asset)
        return;
    EXPECT_TRUE(rt_gltf_skeleton_count(asset) == 2, "GLTF.Load extracts both skeletons");
    EXPECT_TRUE(rt_gltf_animation_count(asset) == 2,
                "GLTF.Load creates one runtime clip per animated skin");

    auto *anim_a = static_cast<rt_animation3d *>(rt_gltf_get_animation(asset, 0));
    auto *anim_b = static_cast<rt_animation3d *>(rt_gltf_get_animation(asset, 1));
    EXPECT_TRUE(anim_a != nullptr && anim_b != nullptr, "GLTF.Load exposes both split clips");
    if (!anim_a || !anim_b)
        return;
    EXPECT_TRUE(anim_a->channel_count == 1 && anim_b->channel_count == 1,
                "Split skin clips each target one local bone");
    EXPECT_NEAR(anim_a->channels[0].keyframes[1].position[0],
                1.0,
                0.001,
                "First split skin clip keeps its own node curve");
    EXPECT_NEAR(anim_b->channels[0].keyframes[1].position[0],
                2.0,
                0.001,
                "Second split skin clip keeps its own node curve");
}

static void test_gltf_applies_sparse_accessors() {
    const char *gltf_path = "/tmp/viper_gltf_sparse_accessor.gltf";
    std::vector<uint8_t> gltf_buffer;
    auto align4 = [&]() {
        while ((gltf_buffer.size() & 3u) != 0)
            gltf_buffer.push_back(0);
    };
    auto append_u16_array = [&](const uint16_t *values, size_t count) -> size_t {
        align4();
        size_t offset = gltf_buffer.size();
        for (size_t i = 0; i < count; i++)
            append_bytes(gltf_buffer, values[i]);
        return offset;
    };
    auto append_float_array = [&](const float *values, size_t count) -> size_t {
        align4();
        size_t offset = gltf_buffer.size();
        for (size_t i = 0; i < count; i++)
            append_bytes(gltf_buffer, values[i]);
        return offset;
    };

    static const uint16_t sparse_indices[2] = {1, 2};
    static const float sparse_values[6] = {1.0f, 2.0f, 3.0f, 0.0f, 1.0f, 0.0f};
    static const uint16_t triangle_indices[3] = {0, 1, 2};
    size_t sparse_idx_off = append_u16_array(sparse_indices, 2);
    size_t sparse_value_off = append_float_array(sparse_values, 6);
    size_t index_off = append_u16_array(triangle_indices, 3);
    std::string buffer_b64 = base64_encode(gltf_buffer.data(), gltf_buffer.size());

    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," +
        buffer_b64 + "\",\"byteLength\":" + std::to_string(gltf_buffer.size()) +
        "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(sparse_idx_off) +
        ",\"byteLength\":4},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(sparse_value_off) +
        ",\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(index_off) +
        ",\"byteLength\":6}"
        "],"
        "\"accessors\":["
        "{\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
        "\"sparse\":{\"count\":2,\"indices\":{\"bufferView\":0,\"componentType\":5123},"
        "\"values\":{\"bufferView\":1}}},"
        "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
        "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]"
        "}";

    FILE *gltf = std::fopen(gltf_path, "wb");
    EXPECT_TRUE(gltf != nullptr, "Sparse glTF fixture can be created");
    if (!gltf)
        return;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses sparse accessor assets");
    if (!asset)
        return;
    auto *mesh = static_cast<rt_mesh3d *>(rt_gltf_get_mesh(asset, 0));
    EXPECT_TRUE(mesh != nullptr, "GLTF.Load exposes sparse accessor mesh");
    if (!mesh)
        return;
    EXPECT_NEAR(mesh->vertices[0].pos[0], 0.0, 0.001, "Sparse accessor defaults missing values");
    EXPECT_NEAR(mesh->vertices[1].pos[0], 1.0, 0.001, "Sparse accessor overrides X");
    EXPECT_NEAR(mesh->vertices[1].pos[1], 2.0, 0.001, "Sparse accessor overrides Y");
    EXPECT_NEAR(mesh->vertices[1].pos[2], 3.0, 0.001, "Sparse accessor overrides Z");
    EXPECT_NEAR(
        mesh->vertices[2].pos[1], 1.0, 0.001, "Sparse accessor can author a valid triangle");
}

static void test_gltf_imports_morph_targets() {
    const char *gltf_path = "/tmp/viper_gltf_morph_targets.gltf";
    std::vector<uint8_t> gltf_buffer;
    auto align4 = [&]() {
        while ((gltf_buffer.size() & 3u) != 0)
            gltf_buffer.push_back(0);
    };
    auto append_float_array = [&](const float *values, size_t count) -> size_t {
        align4();
        size_t offset = gltf_buffer.size();
        for (size_t i = 0; i < count; i++)
            append_bytes(gltf_buffer, values[i]);
        return offset;
    };
    auto append_u16_array = [&](const uint16_t *values, size_t count) -> size_t {
        align4();
        size_t offset = gltf_buffer.size();
        for (size_t i = 0; i < count; i++)
            append_bytes(gltf_buffer, values[i]);
        return offset;
    };

    static const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    static const float normals[9] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    static const float morph_positions[9] = {
        0.0f, 0.0f, 0.0f, 0.25f, 0.5f, 0.0f, 0.0f, -0.25f, 0.0f};
    static const float morph_normals[9] = {0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 0.0f, -0.1f};
    static const float morph_tangents[9] = {0.0f, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, -0.2f};
    static const uint16_t indices[3] = {0, 1, 2};

    size_t pos_off = append_float_array(positions, 9);
    size_t norm_off = append_float_array(normals, 9);
    size_t morph_pos_off = append_float_array(morph_positions, 9);
    size_t morph_norm_off = append_float_array(morph_normals, 9);
    size_t morph_tangent_off = append_float_array(morph_tangents, 9);
    size_t idx_off = append_u16_array(indices, 3);
    std::string buffer_b64 = base64_encode(gltf_buffer.data(), gltf_buffer.size());

    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," +
        buffer_b64 + "\",\"byteLength\":" + std::to_string(gltf_buffer.size()) +
        "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(pos_off) +
        ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(norm_off) +
        ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(morph_pos_off) +
        ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(morph_norm_off) +
        ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(morph_tangent_off) +
        ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(idx_off) +
        ",\"byteLength\":6}"
        "],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":5,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
        "],"
        "\"meshes\":[{\"weights\":[0.25],\"extras\":{\"targetNames\":[\"Smile\"]},"
        "\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":5,"
        "\"targets\":[{\"POSITION\":2,\"NORMAL\":3,\"TANGENT\":4}]}]}],"
        "\"nodes\":[{\"name\":\"MorphNodeA\",\"mesh\":0,\"weights\":[0.75]},"
        "{\"name\":\"MorphNodeB\",\"mesh\":0,\"weights\":[0.10]}],"
        "\"scenes\":[{\"nodes\":[0,1]}],\"scene\":0"
        "}";

    FILE *gltf = std::fopen(gltf_path, "wb");
    EXPECT_TRUE(gltf != nullptr, "Morph-target glTF fixture can be created");
    if (!gltf)
        return;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses morph-target assets");
    if (!asset)
        return;

    auto *mesh = static_cast<rt_mesh3d *>(rt_gltf_get_mesh(asset, 0));
    EXPECT_TRUE(mesh != nullptr, "GLTF.Load exposes the morphed mesh");
    if (!mesh)
        return;
    EXPECT_TRUE(mesh->morph_targets_ref != nullptr, "GLTF.Load binds morph targets to the mesh");
    if (!mesh->morph_targets_ref)
        return;

    EXPECT_TRUE(rt_morphtarget3d_get_shape_count(mesh->morph_targets_ref) == 1,
                "GLTF.Load imports one blend shape");
    EXPECT_TRUE(rt_morphtarget3d_has_tangent_deltas(mesh->morph_targets_ref) == 1,
                "GLTF.Load imports morph tangent deltas");
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mesh->morph_targets_ref, 0),
                0.25,
                0.001,
                "GLTF.Load preserves mesh default morph weights on the shared asset mesh");
    void *scene_root = rt_gltf_get_scene_root(asset);
    void *scene_node = scene_root ? rt_scene_node3d_get_child(scene_root, 0) : nullptr;
    void *scene_node_b = scene_root ? rt_scene_node3d_get_child(scene_root, 1) : nullptr;
    auto *scene_mesh =
        scene_node ? static_cast<rt_mesh3d *>(rt_scene_node3d_get_mesh(scene_node)) : nullptr;
    auto *scene_mesh_b =
        scene_node_b ? static_cast<rt_mesh3d *>(rt_scene_node3d_get_mesh(scene_node_b)) : nullptr;
    EXPECT_TRUE(scene_mesh != nullptr && scene_mesh != mesh,
                "GLTF.Load creates a per-node morph variant when node weights override defaults");
    if (scene_mesh && scene_mesh->morph_targets_ref) {
        EXPECT_NEAR(rt_morphtarget3d_get_weight(scene_mesh->morph_targets_ref, 0),
                    0.75,
                    0.001,
                    "GLTF.Load applies node morph weights to the scene-node mesh variant");
    }
    EXPECT_TRUE(scene_mesh_b != nullptr && scene_mesh_b != mesh && scene_mesh_b != scene_mesh,
                "GLTF.Load creates independent variants for repeated mesh nodes");
    if (scene_mesh_b && scene_mesh_b->morph_targets_ref) {
        EXPECT_NEAR(rt_morphtarget3d_get_weight(scene_mesh_b->morph_targets_ref, 0),
                    0.10,
                    0.001,
                    "GLTF.Load keeps repeated-node morph weights independent");
    }
    const float *pos_deltas = rt_morphtarget3d_get_packed_deltas(mesh->morph_targets_ref);
    const float *normal_deltas = rt_morphtarget3d_get_packed_normal_deltas(mesh->morph_targets_ref);
    EXPECT_TRUE(pos_deltas != nullptr, "GLTF.Load packs morph position deltas");
    EXPECT_TRUE(normal_deltas != nullptr, "GLTF.Load packs morph normal deltas");
    if (pos_deltas) {
        EXPECT_NEAR(pos_deltas[3], 0.25, 0.001, "GLTF morph keeps vertex 1 position delta X");
        EXPECT_NEAR(pos_deltas[4], 0.5, 0.001, "GLTF morph keeps vertex 1 position delta Y");
        EXPECT_NEAR(pos_deltas[7], -0.25, 0.001, "GLTF morph keeps vertex 2 position delta Y");
    }
    if (normal_deltas) {
        EXPECT_NEAR(normal_deltas[4], 0.1, 0.001, "GLTF morph keeps vertex 1 normal delta Y");
        EXPECT_NEAR(normal_deltas[8], -0.1, 0.001, "GLTF morph keeps vertex 2 normal delta Z");
    }
}

static void test_gltf_rejects_malformed_glb_headers() {
    const char *glb_path = "/tmp/viper_gltf_bad_header.glb";
    std::vector<uint8_t> glb;
    std::string json = "{\"asset\":{\"version\":\"2.0\"}}";
    while ((json.size() & 3u) != 0)
        json.push_back(' ');

    glb.insert(glb.end(), {'g', 'l', 'T', 'F'});
    append_u32_le(glb, 1); /* invalid: only GLB 2 is supported */
    append_u32_le(glb, (uint32_t)(12 + 8 + json.size()));
    append_u32_le(glb, (uint32_t)json.size());
    append_u32_le(glb, 0x4E4F534Au);
    glb.insert(glb.end(), json.begin(), json.end());

    FILE *f = std::fopen(glb_path, "wb");
    EXPECT_TRUE(f != nullptr, "Malformed GLB fixture can be created");
    if (!f)
        return;
    std::fwrite(glb.data(), 1, glb.size(), f);
    std::fclose(f);

    EXPECT_TRUE(rt_gltf_load(rt_const_cstr(glb_path)) == nullptr,
                "GLTF.Load rejects unsupported GLB versions");
}

static void test_gltf_rejects_unsafe_external_buffer_paths() {
    const char *gltf_path = "/tmp/viper_gltf_unsafe_uri.gltf";
    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"../outside.bin\",\"byteLength\":4}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":4}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"SCALAR\"}]"
        "}";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json), "Unsafe-URI glTF fixture can be created");
    EXPECT_TRUE(rt_gltf_load(rt_const_cstr(gltf_path)) == nullptr,
                "GLTF.Load rejects external resource paths outside the asset directory");
}

static void test_gltf_assigns_default_material_to_materialless_primitives() {
    const char *gltf_path = "/tmp/viper_gltf_default_material.gltf";
    std::vector<uint8_t> gltf_buffer;
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const uint16_t indices[3] = {0, 1, 2};
    for (float v : positions)
        append_bytes(gltf_buffer, v);
    for (uint16_t v : indices)
        append_bytes(gltf_buffer, v);
    std::string buffer_b64 = base64_encode(gltf_buffer.data(), gltf_buffer.size());
    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," +
        buffer_b64 + "\",\"byteLength\":" + std::to_string(gltf_buffer.size()) +
        "}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"nodes\":[{\"name\":\"Matless\",\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0"
        "}";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json), "Materialless glTF fixture can be created");
    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses materialless primitives");
    if (!asset)
        return;
    EXPECT_TRUE(rt_gltf_material_count(asset) == 1,
                "GLTF.Load creates a shared default material for materialless primitives");
    void *node = rt_scene_node3d_find(rt_gltf_get_scene_root(asset), rt_const_cstr("Matless"));
    EXPECT_TRUE(node != nullptr && rt_scene_node3d_get_material(node) != nullptr,
                "GLTF scene nodes bind the default material so geometry renders");
}

static void test_gltf_uses_texture_texcoord_and_transform() {
    const char *gltf_path = "/tmp/viper_gltf_texcoord_transform.gltf";
    std::vector<uint8_t> gltf_buffer;
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const float uv0[6] = {0.0f, 0.0f, 0.1f, 0.1f, 0.2f, 0.2f};
    const float uv1[6] = {0.25f, 0.5f, 0.5f, 0.25f, 1.0f, 0.75f};
    const uint16_t indices[3] = {0, 1, 2};
    for (float v : positions)
        append_bytes(gltf_buffer, v);
    for (float v : uv0)
        append_bytes(gltf_buffer, v);
    for (float v : uv1)
        append_bytes(gltf_buffer, v);
    for (uint16_t v : indices)
        append_bytes(gltf_buffer, v);
    std::string buffer_b64 = base64_encode(gltf_buffer.data(), gltf_buffer.size());
    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," +
        buffer_b64 + "\",\"byteLength\":" + std::to_string(gltf_buffer.size()) +
        "}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":60,\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":84,\"byteLength\":6}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
        "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"textures\":[{}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0,"
        "\"texCoord\":1,\"extensions\":{\"KHR_texture_transform\":{\"offset\":[0.1,0.2],"
        "\"scale\":[2.0,3.0]}}}}}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1,"
        "\"TEXCOORD_1\":2},\"indices\":3,\"material\":0}]}]"
        "}";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json), "UV-transform glTF fixture can be created");
    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses texture-info UV transforms");
    if (!asset)
        return;
    auto *mesh = static_cast<rt_mesh3d *>(rt_gltf_get_mesh(asset, 0));
    auto *mat = static_cast<rt_material3d *>(rt_gltf_get_material(asset, 0));
    EXPECT_TRUE(mesh != nullptr, "GLTF.Load exposes UV-transform mesh");
    EXPECT_TRUE(mat != nullptr, "GLTF.Load exposes UV-transform material");
    if (!mesh || !mat)
        return;
    EXPECT_NEAR(mesh->vertices[0].uv[0], 0.0, 0.001, "GLTF.Load preserves TEXCOORD_0 on vertex");
    EXPECT_NEAR(mesh->vertices[0].uv1[0], 0.25, 0.001, "GLTF.Load preserves TEXCOORD_1 on vertex");
    EXPECT_TRUE(mat->texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] == 1,
                "GLTF.Load records baseColorTexture texCoord=1 on the material slot");
    EXPECT_NEAR(mat->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR][0],
                2.0,
                0.001,
                "GLTF.Load records texture transform scale.x");
    EXPECT_NEAR(mat->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR][3],
                3.0,
                0.001,
                "GLTF.Load records texture transform scale.y");
    EXPECT_NEAR(mat->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR][4],
                0.1,
                0.001,
                "GLTF.Load records texture transform offset.x");
    EXPECT_NEAR(mat->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR][5],
                0.2,
                0.001,
                "GLTF.Load records texture transform offset.y");
}

static void test_gltf_imports_material_extensions_supported_by_material3d() {
    const char *gltf_path = "/tmp/viper_gltf_material_extensions.gltf";
    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"textures\":[{}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1]},"
        "\"extensions\":{\"KHR_materials_unlit\":{},"
        "\"KHR_materials_specular\":{\"specularFactor\":0.25,"
        "\"specularColorFactor\":[0.2,0.3,0.4],\"specularTexture\":{\"index\":0}},"
        "\"KHR_materials_clearcoat\":{\"clearcoatFactor\":0.6}}}]"
        "}";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json),
                "Material-extension glTF fixture can be created");
    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses material extension assets");
    if (!asset)
        return;
    auto *mat = static_cast<rt_material3d *>(rt_gltf_get_material(asset, 0));
    EXPECT_TRUE(mat != nullptr, "GLTF.Load exposes extension material");
    if (!mat)
        return;
    EXPECT_TRUE(mat->unlit == 1 && mat->shading_model == 3,
                "GLTF.Load maps KHR_materials_unlit to Material3D unlit shading");
    EXPECT_NEAR(mat->specular[0], 0.2, 0.001, "GLTF.Load imports specularColorFactor R");
    EXPECT_NEAR(mat->specular[1], 0.3, 0.001, "GLTF.Load imports specularColorFactor G");
    EXPECT_NEAR(mat->specular[2], 0.4, 0.001, "GLTF.Load imports specularColorFactor B");
    EXPECT_NEAR(mat->reflectivity, 0.6, 0.001, "GLTF.Load approximates clearcoat reflectivity");
}

static void test_gltf_preserves_negative_matrix_scale_sign() {
    const char *gltf_path = "/tmp/viper_gltf_negative_scale.gltf";
    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"nodes\":[{\"name\":\"Mirror\",\"matrix\":[-2,0,0,0,0,3,0,0,0,0,4,0,0,0,0,1]}],"
        "\"scenes\":[{\"nodes\":[0]}],\"scene\":0"
        "}";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json),
                "Negative-scale glTF fixture can be created");
    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses negative-scale matrix nodes");
    if (!asset)
        return;
    void *node = rt_scene_node3d_find(rt_gltf_get_scene_root(asset), rt_const_cstr("Mirror"));
    EXPECT_TRUE(node != nullptr, "GLTF.Load exposes negative-scale node");
    if (!node)
        return;
    EXPECT_NEAR(rt_vec3_x(rt_scene_node3d_get_scale(node)),
                -2.0,
                0.001,
                "GLTF.Load preserves negative X scale from matrix transforms");
}

static void test_gltf_rejects_skins_over_runtime_bone_limit() {
    const char *gltf_path = "/tmp/viper_gltf_oversized_skin.gltf";
    std::string nodes;
    std::string joints;
    for (int i = 0; i < 257; i++) {
        if (i > 0) {
            nodes += ",";
            joints += ",";
        }
        nodes += "{\"name\":\"J" + std::to_string(i) + "\"}";
        joints += std::to_string(i);
    }
    std::string gltf_json = "{\"asset\":{\"version\":\"2.0\"},\"skins\":[{\"joints\":[" + joints +
                            "]}],\"nodes\":[" + nodes +
                            "],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json),
                "Oversized-skin glTF fixture can be created");
    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset == nullptr,
                "GLTF.Load rejects skins above the runtime 256-bone palette limit");
}

static void test_gltf_rejects_unsupported_required_extensions() {
    const char *gltf_path = "/tmp/viper_gltf_required_extension.gltf";
    std::string gltf_json = "{\"asset\":{\"version\":\"2.0\"},"
                            "\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],"
                            "\"extensionsUsed\":[\"KHR_draco_mesh_compression\"]}";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json),
                "Required-extension glTF fixture can be created");
    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(
        asset == nullptr,
        "GLTF.Load rejects unsupported required extensions instead of rendering fallback data");
}

static void test_gltf_imports_required_punctual_lights() {
    const char *gltf_path = "/tmp/viper_gltf_punctual_light.gltf";
    std::string gltf_json = "{\n"
                            "  \"asset\": {\"version\": \"2.0\"},\n"
                            "  \"extensionsUsed\": [\"KHR_lights_punctual\"],\n"
                            "  \"extensionsRequired\": [\"KHR_lights_punctual\"],\n"
                            "  \"extensions\": {\"KHR_lights_punctual\": {\"lights\": [{\n"
                            "    \"type\": \"spot\",\n"
                            "    \"color\": [0.2, 0.4, 0.6],\n"
                            "    \"intensity\": 7.5,\n"
                            "    \"range\": 5.0,\n"
                            "    \"spot\": {\"innerConeAngle\": 0.1, \"outerConeAngle\": 0.5}\n"
                            "  }]}},\n"
                            "  \"nodes\": [{\"name\": \"lamp\", \"translation\": [1.0, 2.0, 3.0], "
                            "\"extensions\": {\"KHR_lights_punctual\": {\"light\": 0}}}],\n"
                            "  \"scenes\": [{\"nodes\": [0]}],\n"
                            "  \"scene\": 0\n"
                            "}\n";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json),
                "Punctual-light glTF fixture can be created");

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load accepts required KHR_lights_punctual");
    if (!asset)
        return;
    void *root = rt_gltf_get_scene_root(asset);
    EXPECT_TRUE(root != nullptr && rt_scene_node3d_child_count(root) == 1,
                "GLTF.Load builds scene nodes for punctual-light fixtures");
    if (!root || rt_scene_node3d_child_count(root) != 1)
        return;
    void *node = rt_scene_node3d_get_child(root, 0);
    rt_light3d *light = (rt_light3d *)rt_scene_node3d_get_light(node);
    EXPECT_TRUE(light != nullptr, "GLTF.Load attaches punctual lights to their nodes");
    if (!light)
        return;
    EXPECT_TRUE(light->type == 3, "GLTF.Load preserves spot-light type");
    EXPECT_NEAR(light->color[1], 0.4, 0.001, "GLTF.Load preserves light color");
    EXPECT_NEAR(light->intensity, 7.5, 0.001, "GLTF.Load preserves light intensity");
    EXPECT_NEAR(light->attenuation, 0.04, 0.001, "GLTF.Load maps range to attenuation");
    EXPECT_TRUE(light->inner_cos > light->outer_cos,
                "GLTF.Load stores valid spot inner/outer cone cosines");
}

static void test_gltf_preserves_primary_texture_sampler_state() {
    const char *gltf_path = "/tmp/viper_gltf_sampler_state.gltf";
    std::string gltf_json =
        "{\n"
        "  \"asset\": {\"version\": \"2.0\"},\n"
        "  \"samplers\": [{\"magFilter\": 9728, \"minFilter\": 9728, \"wrapS\": 33071, "
        "\"wrapT\": 33648}],\n"
        "  \"textures\": [{\"sampler\": 0, \"source\": 0}],\n"
        "  \"materials\": [{\"pbrMetallicRoughness\": {\"baseColorTexture\": {\"index\": 0}}}]\n"
        "}\n";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json), "Sampler-state glTF fixture can be created");

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load accepts material sampler fixtures");
    if (!asset)
        return;

    auto *mat = static_cast<rt_material3d *>(rt_gltf_get_material(asset, 0));
    EXPECT_TRUE(mat != nullptr, "Sampler-state fixture imports a material");
    if (!mat)
        return;
    EXPECT_TRUE(mat->texture_wrap_s == RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE,
                "GLTF.Load preserves sampler wrapS=CLAMP_TO_EDGE");
    EXPECT_TRUE(mat->texture_wrap_t == RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT,
                "GLTF.Load preserves sampler wrapT=MIRRORED_REPEAT");
    EXPECT_TRUE(mat->texture_filter == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST,
                "GLTF.Load preserves nearest sampler filtering");
    EXPECT_TRUE(mat->texture_slot_wrap_s[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] ==
                    RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE,
                "GLTF.Load mirrors base slot sampler wrapS");
    EXPECT_TRUE(mat->texture_slot_wrap_t[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] ==
                    RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT,
                "GLTF.Load mirrors base slot sampler wrapT");
    EXPECT_TRUE(mat->texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] ==
                    RT_MATERIAL3D_TEXTURE_FILTER_NEAREST,
                "GLTF.Load mirrors base slot sampler filtering");
}

static void test_gltf_preserves_independent_texture_slot_metadata() {
    const char *gltf_path = "/tmp/viper_gltf_texture_slot_metadata.gltf";
    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"samplers\":[{\"wrapS\":33071,\"wrapT\":33071,\"minFilter\":9728,\"magFilter\":9728},"
        "{\"wrapS\":33648,\"wrapT\":10497,\"minFilter\":9729,\"magFilter\":9729}],"
        "\"textures\":[{\"sampler\":0},{\"sampler\":1}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0,"
        "\"texCoord\":0},\"metallicRoughnessTexture\":{\"index\":1,\"texCoord\":1,"
        "\"extensions\":{\"KHR_texture_transform\":{\"offset\":[0.25,0.5],"
        "\"scale\":[4,5]}}}},\"normalTexture\":{\"index\":1,\"texCoord\":1}}]"
        "}";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json),
                "Independent texture-slot glTF fixture can be created");
    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load parses independent texture-slot metadata");
    if (!asset)
        return;
    auto *mat = static_cast<rt_material3d *>(rt_gltf_get_material(asset, 0));
    EXPECT_TRUE(mat != nullptr, "Independent slot fixture imports a material");
    if (!mat)
        return;
    EXPECT_TRUE(mat->texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] ==
                    RT_MATERIAL3D_TEXTURE_FILTER_NEAREST,
                "GLTF.Load keeps base-color nearest sampling independent");
    EXPECT_TRUE(mat->texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS] ==
                    RT_MATERIAL3D_TEXTURE_FILTER_LINEAR,
                "GLTF.Load keeps metallic-roughness linear sampling independent");
    EXPECT_TRUE(mat->texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS] == 1,
                "GLTF.Load keeps metallic-roughness texCoord independent");
    EXPECT_NEAR(mat->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS][0],
                4.0,
                0.001,
                "GLTF.Load keeps metallic-roughness transform scale.x independent");
    EXPECT_NEAR(mat->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS][5],
                0.5,
                0.001,
                "GLTF.Load keeps metallic-roughness transform offset.y independent");
    EXPECT_TRUE(mat->texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL] == 1,
                "GLTF.Load keeps normal-map texCoord independent");
}

static void test_gltf_rejects_invalid_scene_graph_links() {
    const char *gltf_path = "/tmp/viper_gltf_invalid_scene_graph.gltf";
    std::string gltf_json = "{\n"
                            "  \"asset\": {\"version\": \"2.0\"},\n"
                            "  \"nodes\": [\n"
                            "    {\"name\": \"A\", \"children\": [1]},\n"
                            "    {\"name\": \"B\", \"children\": [0]}\n"
                            "  ],\n"
                            "  \"scenes\": [{\"nodes\": [0]}],\n"
                            "  \"scene\": 0\n"
                            "}\n";
    EXPECT_TRUE(write_text_file(gltf_path, gltf_json),
                "Invalid scene-graph glTF fixture can be created");

    void *asset = rt_gltf_load(rt_const_cstr(gltf_path));
    EXPECT_TRUE(asset != nullptr, "GLTF.Load keeps resources while rejecting cyclic node graphs");
    if (!asset)
        return;
    EXPECT_TRUE(rt_gltf_get_scene_root(asset) == nullptr,
                "GLTF.Load does not build a scene root for cyclic node graphs");
    EXPECT_TRUE(rt_gltf_node_count(asset) == 0,
                "GLTF.Load reports zero imported nodes for invalid scene graphs");
}

int main() {
    test_gltf_accessors_reject_wrong_handles();
    test_gltf_loads_data_uri_buffers_and_embedded_textures();
    test_gltf_resolves_percent_encoded_external_buffers();
    test_gltf_load_asset_resolves_mounted_external_buffers();
    test_gltf_load_asset_handles_glb_filesystem_and_mounted_package();
    test_gltf_rejects_out_of_range_indices();
    test_gltf_builds_scene_hierarchy_for_active_scene();
    test_gltf_imports_extended_vertex_attributes_and_triangle_strips();
    test_gltf_reduces_secondary_joint_sets_to_top_four_influences();
    test_gltf_applies_matrix_nodes_in_column_major_order();
    test_gltf_imports_skins_and_animation_clips();
    test_gltf_splits_animation_clips_per_skin();
    test_gltf_applies_sparse_accessors();
    test_gltf_imports_morph_targets();
    test_gltf_rejects_malformed_glb_headers();
    test_gltf_rejects_unsafe_external_buffer_paths();
    test_gltf_assigns_default_material_to_materialless_primitives();
    test_gltf_uses_texture_texcoord_and_transform();
    test_gltf_imports_material_extensions_supported_by_material3d();
    test_gltf_preserves_negative_matrix_scale_sign();
    test_gltf_rejects_skins_over_runtime_bone_limit();
    test_gltf_rejects_unsupported_required_extensions();
    test_gltf_imports_required_punctual_lights();
    test_gltf_preserves_primary_texture_sampler_state();
    test_gltf_preserves_independent_texture_slot_metadata();
    test_gltf_rejects_invalid_scene_graph_links();
    std::printf("GLTF tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
