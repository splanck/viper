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
#include "rt_gltf.h"
#include "rt_pixels.h"
#include "rt_string.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
extern rt_string rt_const_cstr(const char *s);
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

template <typename T> static void append_bytes(std::vector<uint8_t> &buf, const T &value) {
    size_t offset = buf.size();
    buf.resize(offset + sizeof(T));
    std::memcpy(buf.data() + offset, &value, sizeof(T));
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
        "      \"baseColorFactor\": [1.0, 1.0, 1.0, 1.0],\n"
        "      \"baseColorTexture\": {\"index\": 0},\n"
        "      \"metallicRoughnessTexture\": {\"index\": 0}\n"
        "    },\n"
        "    \"normalTexture\": {\"index\": 0},\n"
        "    \"emissiveTexture\": {\"index\": 0},\n"
        "    \"emissiveFactor\": [1.0, 1.0, 1.0]\n"
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
    EXPECT_TRUE(material->texture != nullptr &&
                    rt_pixels_get(material->texture, 0, 0) == 0x336699FFll,
                "GLTF.Load wires base color textures into Material3D");
    EXPECT_TRUE(material->normal_map != nullptr &&
                    rt_pixels_get(material->normal_map, 0, 0) == 0x336699FFll,
                "GLTF.Load wires normal textures into Material3D");
    EXPECT_TRUE(material->specular_map != nullptr &&
                    rt_pixels_get(material->specular_map, 0, 0) == 0x336699FFll,
                "GLTF.Load wires metallic-roughness textures into Material3D");
    EXPECT_TRUE(material->emissive_map != nullptr &&
                    rt_pixels_get(material->emissive_map, 0, 0) == 0x336699FFll,
                "GLTF.Load wires emissive textures into Material3D");
}

int main() {
    test_gltf_loads_data_uri_buffers_and_embedded_textures();
    std::printf("GLTF tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
