//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_animcontroller3d.h"
#include "rt_asset.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_model3d.h"
#include "rt_morphtarget3d.h"
#include "rt_scene3d.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"

#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "VpaWriter.hpp"

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
static std::jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_expect_trap = false;

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_expect_trap)
        std::longjmp(g_trap_jmp, 1);
    std::fprintf(stderr, "Unexpected trap: %s\n", msg ? msg : "(null)");
    std::abort();
}

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

static void skip_test(const char *msg) {
    tests_run++;
    tests_passed++;
    std::printf("SKIP: %s\n", msg);
}

template <typename T> static void append_bytes(std::vector<uint8_t> &buf, const T &value) {
    size_t offset = buf.size();
    buf.resize(offset + sizeof(T));
    std::memcpy(buf.data() + offset, &value, sizeof(T));
}

static bool write_text_file(const char *path, const std::string &text) {
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
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

struct FbxPropFixture {
    char type;
    std::vector<uint8_t> payload;
};

struct FbxNodeFixture {
    std::string name;
    std::vector<FbxPropFixture> props;
    std::vector<FbxNodeFixture> children;
};

static void patch_u32(std::vector<uint8_t> &buf, size_t offset, uint32_t value) {
    if (offset + sizeof(value) <= buf.size())
        std::memcpy(buf.data() + offset, &value, sizeof(value));
}

static FbxPropFixture fbx_prop_string_fixture(const std::string &value) {
    FbxPropFixture prop;
    uint32_t len = (uint32_t)value.size();
    prop.type = 'S';
    append_bytes(prop.payload, len);
    prop.payload.insert(prop.payload.end(), value.begin(), value.end());
    return prop;
}

static FbxPropFixture fbx_prop_i64_fixture(int64_t value) {
    FbxPropFixture prop;
    prop.type = 'L';
    append_bytes(prop.payload, value);
    return prop;
}

static FbxPropFixture fbx_prop_f64_fixture(double value) {
    FbxPropFixture prop;
    prop.type = 'D';
    append_bytes(prop.payload, value);
    return prop;
}

template <typename T>
static FbxPropFixture fbx_prop_array_fixture(char type, const T *data, size_t count) {
    FbxPropFixture prop;
    uint32_t element_count = (uint32_t)count;
    uint32_t byte_length = (uint32_t)(count * sizeof(T));
    prop.type = type;
    append_bytes(prop.payload, element_count);
    append_bytes(prop.payload, (uint32_t)0);
    append_bytes(prop.payload, byte_length);
    if (data && count > 0) {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
        prop.payload.insert(prop.payload.end(), bytes, bytes + byte_length);
    }
    return prop;
}

static void write_fbx_node_fixture(const FbxNodeFixture &node, std::vector<uint8_t> &out) {
    size_t start = out.size();
    size_t prop_len_offset;
    size_t prop_start;

    append_bytes(out, (uint32_t)0);
    append_bytes(out, (uint32_t)node.props.size());
    prop_len_offset = out.size();
    append_bytes(out, (uint32_t)0);
    append_bytes(out, (uint8_t)node.name.size());
    out.insert(out.end(), node.name.begin(), node.name.end());

    prop_start = out.size();
    for (const FbxPropFixture &prop : node.props) {
        append_bytes(out, (uint8_t)prop.type);
        out.insert(out.end(), prop.payload.begin(), prop.payload.end());
    }
    patch_u32(out, prop_len_offset, (uint32_t)(out.size() - prop_start));

    for (const FbxNodeFixture &child : node.children)
        write_fbx_node_fixture(child, out);
    if (!node.children.empty())
        out.resize(out.size() + 13, 0);

    patch_u32(out, start, (uint32_t)out.size());
}

static std::string make_fbx_object_name(const char *name, const char *kind) {
    std::string out = name ? name : "";
    out.push_back('\0');
    out.push_back('\x01');
    out += kind ? kind : "";
    return out;
}

static FbxNodeFixture make_fbx_property_vec3(const char *name, double x, double y, double z) {
    FbxNodeFixture node;
    node.name = "P";
    node.props.push_back(fbx_prop_string_fixture(name ? name : ""));
    node.props.push_back(fbx_prop_string_fixture("Vector3D"));
    node.props.push_back(fbx_prop_string_fixture(""));
    node.props.push_back(fbx_prop_string_fixture("A"));
    node.props.push_back(fbx_prop_f64_fixture(x));
    node.props.push_back(fbx_prop_f64_fixture(y));
    node.props.push_back(fbx_prop_f64_fixture(z));
    return node;
}

static FbxNodeFixture make_fbx_property_scalar(const char *name, double value) {
    FbxNodeFixture node;
    node.name = "P";
    node.props.push_back(fbx_prop_string_fixture(name ? name : ""));
    node.props.push_back(fbx_prop_string_fixture("Number"));
    node.props.push_back(fbx_prop_string_fixture(""));
    node.props.push_back(fbx_prop_string_fixture("A"));
    node.props.push_back(fbx_prop_f64_fixture(value));
    node.props.push_back(fbx_prop_f64_fixture(0.0));
    node.props.push_back(fbx_prop_f64_fixture(0.0));
    return node;
}

static FbxNodeFixture make_fbx_model_fixture(
    int64_t id, const char *name, const char *type, double tx, double ty, double tz) {
    FbxNodeFixture node;
    FbxNodeFixture props70;
    node.name = "Model";
    node.props.push_back(fbx_prop_i64_fixture(id));
    node.props.push_back(fbx_prop_string_fixture(make_fbx_object_name(name, "Model")));
    node.props.push_back(fbx_prop_string_fixture(type ? type : "Null"));
    props70.name = "Properties70";
    props70.children.push_back(make_fbx_property_vec3("Lcl Translation", tx, ty, tz));
    props70.children.push_back(make_fbx_property_vec3("Lcl Rotation", 0.0, 0.0, 0.0));
    props70.children.push_back(make_fbx_property_vec3("Lcl Scaling", 1.0, 1.0, 1.0));
    node.children.push_back(props70);
    return node;
}

static FbxNodeFixture make_fbx_connection_fixture(int64_t child,
                                                  int64_t parent,
                                                  const char *prop = nullptr) {
    FbxNodeFixture node;
    node.name = "C";
    node.props.push_back(fbx_prop_string_fixture(prop ? "OP" : "OO"));
    node.props.push_back(fbx_prop_i64_fixture(child));
    node.props.push_back(fbx_prop_i64_fixture(parent));
    if (prop)
        node.props.push_back(fbx_prop_string_fixture(prop));
    return node;
}

static FbxNodeFixture make_fbx_deformer_fixture(int64_t id, const char *name, const char *type) {
    FbxNodeFixture node;
    node.name = "Deformer";
    node.props.push_back(fbx_prop_i64_fixture(id));
    node.props.push_back(fbx_prop_string_fixture(make_fbx_object_name(name, "Deformer")));
    node.props.push_back(fbx_prop_string_fixture(type ? type : ""));
    return node;
}

static FbxNodeFixture make_fbx_cluster_fixture(int64_t id,
                                               const char *name,
                                               const int32_t *indices,
                                               size_t index_count,
                                               const double *weights,
                                               size_t weight_count) {
    FbxNodeFixture node = make_fbx_deformer_fixture(id, name, "Cluster");
    node.children.push_back(
        FbxNodeFixture{"Indexes", {fbx_prop_array_fixture('i', indices, index_count)}, {}});
    node.children.push_back(
        FbxNodeFixture{"Weights", {fbx_prop_array_fixture('d', weights, weight_count)}, {}});
    return node;
}

static FbxNodeFixture make_fbx_animation_curve_fixture(int64_t id,
                                                       const int64_t *times,
                                                       const double *values,
                                                       size_t count) {
    FbxNodeFixture node;
    node.name = "AnimationCurve";
    node.props.push_back(fbx_prop_i64_fixture(id));
    node.props.push_back(fbx_prop_string_fixture(make_fbx_object_name("Curve", "AnimCurve")));
    node.props.push_back(fbx_prop_string_fixture(""));
    node.children.push_back(
        FbxNodeFixture{"KeyTime", {fbx_prop_array_fixture('l', times, count)}, {}});
    node.children.push_back(
        FbxNodeFixture{"KeyValueFloat", {fbx_prop_array_fixture('d', values, count)}, {}});
    return node;
}

static bool write_fbx_fixture(const char *path) {
    static const int64_t kGeometryId = 100;
    static const int64_t kMaterialId = 200;
    static const int64_t kParentModelId = 300;
    static const int64_t kChildModelId = 301;
    static const double kPositions[9] = {0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
    static const int32_t kIndices[3] = {0, 1, -3};

    FbxNodeFixture geometry;
    FbxNodeFixture material;
    FbxNodeFixture material_props70;
    FbxNodeFixture objects;
    FbxNodeFixture connections;
    std::vector<uint8_t> bytes;

    geometry.name = "Geometry";
    geometry.props.push_back(fbx_prop_i64_fixture(kGeometryId));
    geometry.props.push_back(
        fbx_prop_string_fixture(make_fbx_object_name("FixtureMesh", "Geometry")));
    geometry.props.push_back(fbx_prop_string_fixture("Mesh"));
    geometry.children.push_back(FbxNodeFixture{
        "Vertices",
        {fbx_prop_array_fixture('d', kPositions, sizeof(kPositions) / sizeof(kPositions[0]))},
        {}});
    geometry.children.push_back(FbxNodeFixture{
        "PolygonVertexIndex",
        {fbx_prop_array_fixture('i', kIndices, sizeof(kIndices) / sizeof(kIndices[0]))},
        {}});

    material.name = "Material";
    material.props.push_back(fbx_prop_i64_fixture(kMaterialId));
    material.props.push_back(
        fbx_prop_string_fixture(make_fbx_object_name("FixtureMaterial", "Material")));
    material.props.push_back(fbx_prop_string_fixture(""));
    material_props70.name = "Properties70";
    material_props70.children.push_back(make_fbx_property_vec3("DiffuseColor", 0.2, 0.4, 0.8));
    material_props70.children.push_back(make_fbx_property_scalar("Shininess", 16.0));
    material.children.push_back(material_props70);

    objects.name = "Objects";
    objects.children.push_back(geometry);
    objects.children.push_back(material);
    objects.children.push_back(
        make_fbx_model_fixture(kParentModelId, "Parent", "Null", 1.0, 2.0, 3.0));
    objects.children.push_back(
        make_fbx_model_fixture(kChildModelId, "Child", "Mesh", 0.0, 5.0, 0.0));

    connections.name = "Connections";
    connections.children.push_back(FbxNodeFixture{"C",
                                                  {fbx_prop_string_fixture("OO"),
                                                   fbx_prop_i64_fixture(kParentModelId),
                                                   fbx_prop_i64_fixture(0)},
                                                  {}});
    connections.children.push_back(FbxNodeFixture{"C",
                                                  {fbx_prop_string_fixture("OO"),
                                                   fbx_prop_i64_fixture(kChildModelId),
                                                   fbx_prop_i64_fixture(kParentModelId)},
                                                  {}});
    connections.children.push_back(FbxNodeFixture{"C",
                                                  {fbx_prop_string_fixture("OO"),
                                                   fbx_prop_i64_fixture(kGeometryId),
                                                   fbx_prop_i64_fixture(kChildModelId)},
                                                  {}});
    connections.children.push_back(FbxNodeFixture{"C",
                                                  {fbx_prop_string_fixture("OO"),
                                                   fbx_prop_i64_fixture(kMaterialId),
                                                   fbx_prop_i64_fixture(kChildModelId)},
                                                  {}});

    bytes.insert(bytes.end(), {'K', 'a', 'y', 'd', 'a', 'r', 'a', ' ', 'F',  'B',    'X', ' ',
                               'B', 'i', 'n', 'a', 'r', 'y', ' ', ' ', '\0', '\x1A', '\0'});
    append_bytes(bytes, (uint32_t)7400);
    write_fbx_node_fixture(objects, bytes);
    write_fbx_node_fixture(connections, bytes);
    bytes.resize(bytes.size() + 13, 0);

    FILE *f = std::fopen(path, "wb");
    if (!f)
        return false;
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    return ok;
}

static bool write_fbx_skinned_animation_fixture(const char *path) {
    static const int64_t kGeometryId = 1100;
    static const int64_t kMeshModelId = 1200;
    static const int64_t kRootBoneId = 1300;
    static const int64_t kChildBoneId = 1301;
    static const int64_t kSkinId = 1400;
    static const int64_t kRootClusterId = 1401;
    static const int64_t kChildClusterId = 1402;
    static const int64_t kStackId = 1500;
    static const int64_t kLayerId = 1501;
    static const int64_t kTranslateNodeId = 1510;
    static const int64_t kCurveXId = 1511;
    static const int64_t kCurveYId = 1512;
    static const int64_t kFbxSecond = 46186158000LL;
    static const double kPositions[9] = {0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
    static const int32_t kIndices[3] = {0, 1, -3};
    static const int32_t kRootIndices[2] = {0, 1};
    static const double kRootWeights[2] = {1.0, 0.25};
    static const int32_t kChildIndices[2] = {1, 2};
    static const double kChildWeights[2] = {0.75, 1.0};
    static const int64_t kKeyTimes[2] = {0, kFbxSecond};
    static const double kCurveXValues[2] = {0.0, 10.0};
    static const double kCurveYValues[2] = {0.0, 20.0};

    FbxNodeFixture geometry;
    FbxNodeFixture objects;
    FbxNodeFixture connections;
    FbxNodeFixture skin = make_fbx_deformer_fixture(kSkinId, "Skin", "Skin");
    FbxNodeFixture anim_stack;
    FbxNodeFixture anim_layer;
    FbxNodeFixture translate_node;
    std::vector<uint8_t> bytes;

    geometry.name = "Geometry";
    geometry.props.push_back(fbx_prop_i64_fixture(kGeometryId));
    geometry.props.push_back(
        fbx_prop_string_fixture(make_fbx_object_name("SkinnedMesh", "Geometry")));
    geometry.props.push_back(fbx_prop_string_fixture("Mesh"));
    geometry.children.push_back(FbxNodeFixture{
        "Vertices",
        {fbx_prop_array_fixture('d', kPositions, sizeof(kPositions) / sizeof(kPositions[0]))},
        {}});
    geometry.children.push_back(FbxNodeFixture{
        "PolygonVertexIndex",
        {fbx_prop_array_fixture('i', kIndices, sizeof(kIndices) / sizeof(kIndices[0]))},
        {}});

    anim_stack.name = "AnimationStack";
    anim_stack.props.push_back(fbx_prop_i64_fixture(kStackId));
    anim_stack.props.push_back(fbx_prop_string_fixture(make_fbx_object_name("Walk", "AnimStack")));
    anim_stack.props.push_back(fbx_prop_string_fixture(""));

    anim_layer.name = "AnimationLayer";
    anim_layer.props.push_back(fbx_prop_i64_fixture(kLayerId));
    anim_layer.props.push_back(
        fbx_prop_string_fixture(make_fbx_object_name("BaseLayer", "AnimLayer")));
    anim_layer.props.push_back(fbx_prop_string_fixture(""));

    translate_node.name = "AnimationCurveNode";
    translate_node.props.push_back(fbx_prop_i64_fixture(kTranslateNodeId));
    translate_node.props.push_back(
        fbx_prop_string_fixture(make_fbx_object_name("RootTranslate", "AnimCurveNode")));
    translate_node.props.push_back(fbx_prop_string_fixture(""));

    objects.name = "Objects";
    objects.children.push_back(geometry);
    objects.children.push_back(
        make_fbx_model_fixture(kMeshModelId, "MeshNode", "Mesh", 0.0, 0.0, 0.0));
    objects.children.push_back(
        make_fbx_model_fixture(kRootBoneId, "RootBone", "Root", 0.0, 0.0, 0.0));
    objects.children.push_back(
        make_fbx_model_fixture(kChildBoneId, "ChildBone", "LimbNode", 0.0, 1.0, 0.0));
    objects.children.push_back(skin);
    objects.children.push_back(
        make_fbx_cluster_fixture(kRootClusterId,
                                 "RootCluster",
                                 kRootIndices,
                                 sizeof(kRootIndices) / sizeof(kRootIndices[0]),
                                 kRootWeights,
                                 sizeof(kRootWeights) / sizeof(kRootWeights[0])));
    objects.children.push_back(
        make_fbx_cluster_fixture(kChildClusterId,
                                 "ChildCluster",
                                 kChildIndices,
                                 sizeof(kChildIndices) / sizeof(kChildIndices[0]),
                                 kChildWeights,
                                 sizeof(kChildWeights) / sizeof(kChildWeights[0])));
    objects.children.push_back(anim_stack);
    objects.children.push_back(anim_layer);
    objects.children.push_back(translate_node);
    objects.children.push_back(make_fbx_animation_curve_fixture(
        kCurveXId, kKeyTimes, kCurveXValues, sizeof(kKeyTimes) / sizeof(kKeyTimes[0])));
    objects.children.push_back(make_fbx_animation_curve_fixture(
        kCurveYId, kKeyTimes, kCurveYValues, sizeof(kKeyTimes) / sizeof(kKeyTimes[0])));

    connections.name = "Connections";
    connections.children.push_back(make_fbx_connection_fixture(kMeshModelId, 0));
    connections.children.push_back(make_fbx_connection_fixture(kRootBoneId, 0));
    connections.children.push_back(make_fbx_connection_fixture(kChildBoneId, kRootBoneId));
    connections.children.push_back(make_fbx_connection_fixture(kGeometryId, kMeshModelId));
    connections.children.push_back(make_fbx_connection_fixture(kSkinId, kGeometryId));
    connections.children.push_back(make_fbx_connection_fixture(kRootClusterId, kSkinId));
    connections.children.push_back(make_fbx_connection_fixture(kRootClusterId, kRootBoneId));
    connections.children.push_back(make_fbx_connection_fixture(kChildClusterId, kSkinId));
    connections.children.push_back(make_fbx_connection_fixture(kChildClusterId, kChildBoneId));
    connections.children.push_back(make_fbx_connection_fixture(kLayerId, kStackId));
    connections.children.push_back(make_fbx_connection_fixture(kTranslateNodeId, kLayerId));
    connections.children.push_back(
        make_fbx_connection_fixture(kTranslateNodeId, kRootBoneId, "Lcl Translation"));
    connections.children.push_back(make_fbx_connection_fixture(kCurveXId, kTranslateNodeId, "d|X"));
    connections.children.push_back(make_fbx_connection_fixture(kCurveYId, kTranslateNodeId, "d|Y"));

    bytes.insert(bytes.end(), {'K', 'a', 'y', 'd', 'a', 'r', 'a', ' ', 'F',  'B',    'X', ' ',
                               'B', 'i', 'n', 'a', 'r', 'y', ' ', ' ', '\0', '\x1A', '\0'});
    append_bytes(bytes, (uint32_t)7400);
    write_fbx_node_fixture(objects, bytes);
    write_fbx_node_fixture(connections, bytes);
    bytes.resize(bytes.size() + 13, 0);

    FILE *f = std::fopen(path, "wb");
    if (!f)
        return false;
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    return ok;
}

static bool write_truncated_fbx_fixture(const char *path) {
    const char *valid_path = "/tmp/viper_model3d_valid_for_truncate.fbx";
    if (!write_fbx_fixture(valid_path))
        return false;

    FILE *f = std::fopen(valid_path, "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 40) {
        std::fclose(f);
        return false;
    }
    std::vector<uint8_t> bytes((size_t)size);
    bool read_ok = std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    if (!read_ok)
        return false;

    FILE *out = std::fopen(path, "wb");
    if (!out)
        return false;
    size_t truncated_size = bytes.size() - 7;
    bool write_ok = std::fwrite(bytes.data(), 1, truncated_size, out) == truncated_size;
    std::fclose(out);
    return write_ok;
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
        "    {\"name\": \"GltfChild\", \"scale\": [2.0, 2.0, 2.0], \"camera\": 0},\n"
        "    {\"name\": \"GltfOrthoCamera\", \"translation\": [0.0, 2.0, 0.0], "
        "\"camera\": 1},\n"
        "    {\"name\": \"GltfSecondary\", \"translation\": [-3.0, 0.0, 0.0], \"mesh\": 0}\n"
        "  ],\n"
        "  \"cameras\": [\n"
        "    {\"type\": \"perspective\", \"perspective\": {\"yfov\": 1.0471975512, "
        "\"aspectRatio\": 1.7777777778, \"znear\": 0.2, \"zfar\": 250.0}},\n"
        "    {\"type\": \"orthographic\", \"orthographic\": {\"xmag\": 8.0, \"ymag\": 4.0, "
        "\"znear\": 0.5, \"zfar\": 80.0}}\n"
        "  ],\n"
        "  \"scenes\": [{\"nodes\": [0, 2]}, {\"name\": \"SecondaryScene\", \"nodes\": [3]}],\n"
        "  \"scene\": 0\n"
        "}\n";

    FILE *gltf = std::fopen(path, "wb");
    if (!gltf)
        return false;
    std::fwrite(gltf_json.data(), 1, gltf_json.size(), gltf);
    std::fclose(gltf);
    return true;
}

static const char *find_existing_path(std::initializer_list<const char *> candidates) {
    for (const char *candidate : candidates) {
        FILE *f = std::fopen(candidate, "rb");
        if (f) {
            std::fclose(f);
            return candidate;
        }
    }
    return nullptr;
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
    EXPECT_TRUE(rt_model3d_get_material_count(model) == 1, "Model3D deduplicates shared materials");
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

    void *inst_root_min = rt_scene_node3d_get_aabb_min(inst_root);
    void *inst_root_max = rt_scene_node3d_get_aabb_max(inst_root);
    EXPECT_NEAR(rt_vec3_x(inst_root_min),
                0.5,
                0.001,
                "Model3D synthetic instance roots expose subtree AABB min X");
    EXPECT_NEAR(rt_vec3_y(inst_root_min),
                1.0,
                0.001,
                "Model3D synthetic instance roots expose subtree AABB min Y");
    EXPECT_NEAR(rt_vec3_z(inst_root_min),
                1.5,
                0.001,
                "Model3D synthetic instance roots expose subtree AABB min Z");
    EXPECT_NEAR(rt_vec3_x(inst_root_max),
                1.5,
                0.001,
                "Model3D synthetic instance roots expose subtree AABB max X");
    EXPECT_NEAR(rt_vec3_y(inst_root_max),
                8.0,
                0.001,
                "Model3D synthetic instance roots expose subtree AABB max Y");
    EXPECT_NEAR(rt_vec3_z(inst_root_max),
                4.5,
                0.001,
                "Model3D synthetic instance roots expose subtree AABB max Z");

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
    EXPECT_TRUE(rt_model3d_get_node_count(model) == 3,
                "Model3D preserves logical glTF scene-node counts");
    EXPECT_TRUE(rt_model3d_get_scene_count(model) == 2,
                "Model3D exposes glTF active and secondary immutable scenes");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_model3d_get_scene_name(model, 0)), "default") == 0,
                "Model3D.GetSceneName names the default scene");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_model3d_get_scene_name(model, 1)), "SecondaryScene") ==
                    0,
                "Model3D.GetSceneName preserves secondary glTF scene names");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_model3d_get_scene_name(model, 2)), "") == 0,
                "Model3D.GetSceneName returns empty for invalid scene indices");
    EXPECT_TRUE(rt_model3d_get_camera_count(model, 0) == 2,
                "Model3D.GetCameraCount reports active-scene glTF cameras");
    EXPECT_TRUE(rt_model3d_get_camera_count(model, 1) == 0,
                "Model3D.GetCameraCount reports no cameras for the secondary scene");
    EXPECT_TRUE(rt_model3d_get_camera_count(model, 2) == 0,
                "Model3D.GetCameraCount returns zero for invalid scene indices");
    void *perspective_camera = rt_model3d_get_camera(model, 0, 0);
    void *ortho_camera = rt_model3d_get_camera(model, 0, 1);
    EXPECT_TRUE(perspective_camera != nullptr, "Model3D.GetCamera returns glTF perspective cameras");
    EXPECT_TRUE(ortho_camera != nullptr, "Model3D.GetCamera returns glTF orthographic cameras");
    EXPECT_TRUE(rt_model3d_get_camera(model, 0, 2) == nullptr,
                "Model3D.GetCamera rejects out-of-range camera indices");
    EXPECT_TRUE(rt_model3d_get_camera(model, 1, 0) == nullptr,
                "Model3D.GetCamera returns null for secondary scenes without cameras");
    EXPECT_TRUE(rt_model3d_get_camera(model, 2, 0) == nullptr,
                "Model3D.GetCamera rejects invalid scene indices");
    EXPECT_TRUE(rt_camera3d_is_ortho(perspective_camera) == 0,
                "glTF perspective cameras import as perspective Camera3D handles");
    EXPECT_TRUE(rt_camera3d_is_ortho(ortho_camera) == 1,
                "glTF orthographic cameras import as orthographic Camera3D handles");
    EXPECT_NEAR(rt_camera3d_get_fov(perspective_camera),
                60.0,
                0.001,
                "glTF perspective yfov is converted from radians to degrees");
    void *perspective_pos = rt_camera3d_get_position(perspective_camera);
    void *perspective_forward = rt_camera3d_get_forward(perspective_camera);
    void *ortho_pos = rt_camera3d_get_position(ortho_camera);
    EXPECT_NEAR(rt_vec3_x(perspective_pos),
                1.0,
                0.001,
                "glTF camera inherits parent world translation");
    EXPECT_NEAR(rt_vec3_z(perspective_forward),
                -1.0,
                0.001,
                "glTF camera uses local -Z as world forward");
    EXPECT_NEAR(rt_vec3_y(ortho_pos),
                2.0,
                0.001,
                "glTF orthographic camera preserves node translation");

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

    EXPECT_TRUE(rt_scene3d_get_node_count(inst_scene) == 4,
                "glTF-backed Model3D instances attach below a new scene root");
    EXPECT_TRUE(rt_scene3d_find(inst_scene, rt_const_cstr("GltfChild")) != nullptr,
                "glTF-backed Model3D instances preserve child names");

    void *indexed_scene = rt_model3d_instantiate_scene_at(model, 0);
    EXPECT_TRUE(indexed_scene != nullptr, "Model3D.InstantiateSceneAt clones the default scene");
    void *secondary_scene = rt_model3d_instantiate_scene_at(model, 1);
    EXPECT_TRUE(secondary_scene != nullptr, "Model3D.InstantiateSceneAt clones secondary glTF scenes");
    EXPECT_TRUE(rt_model3d_instantiate_scene_at(model, 2) == nullptr,
                "Model3D.InstantiateSceneAt rejects invalid scene indices");
    if (!indexed_scene)
        return;
    EXPECT_TRUE(rt_scene3d_get_node_count(indexed_scene) == 4,
                "Model3D.InstantiateSceneAt preserves default-scene nodes");
    EXPECT_TRUE(rt_scene3d_find(indexed_scene, rt_const_cstr("GltfParent")) != nullptr,
                "Model3D.InstantiateSceneAt preserves indexed-scene searchability");
    EXPECT_TRUE(rt_scene3d_find(indexed_scene, rt_const_cstr("GltfSecondary")) == nullptr,
                "Model3D.InstantiateSceneAt keeps secondary roots out of the default scene");
    if (!secondary_scene)
        return;
    EXPECT_TRUE(rt_scene3d_get_node_count(secondary_scene) == 2,
                "Model3D.InstantiateSceneAt builds secondary scene roots");
    EXPECT_TRUE(rt_scene3d_find(secondary_scene, rt_const_cstr("GltfSecondary")) != nullptr,
                "Model3D.InstantiateSceneAt preserves secondary scene searchability");
    EXPECT_TRUE(rt_scene3d_find(secondary_scene, rt_const_cstr("GltfParent")) == nullptr,
                "Model3D.InstantiateSceneAt keeps default roots out of the secondary scene");
}

static void test_model3d_load_asset_resolves_mounted_gltf_dependencies() {
    const char *pack_path = "/tmp/viper_model3d_asset_pack.vpa";
    std::vector<uint8_t> gltf_buffer;
    const float positions[9] = {0.0f, 0.0f, 0.0f, 6.0f, 0.0f, 0.0f, 0.0f, 7.0f, 0.0f};
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
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]"
        "}";

    viper::asset::VpaWriter writer;
    writer.addEntry("assets/models/model.gltf",
                    reinterpret_cast<const uint8_t *>(gltf_json.data()),
                    gltf_json.size(),
                    false);
    writer.addEntry("assets/models/buffers/tri.bin", gltf_buffer.data(), gltf_buffer.size(), false);
    std::string err;
    bool wrote_pack = writer.writeToFile(pack_path, err);
    EXPECT_TRUE(wrote_pack, "Model3D asset pack can be written");
    if (!wrote_pack)
        return;
    bool mounted = rt_asset_mount(rt_const_cstr(pack_path)) == 1;
    EXPECT_TRUE(mounted, "Model3D asset pack can mount");
    if (!mounted)
        return;

    void *model = rt_model3d_load_asset(rt_const_cstr("assets/models/model.gltf"));
    EXPECT_TRUE(model != nullptr, "Model3D.LoadAsset loads a mounted glTF model path");
    if (model) {
        EXPECT_TRUE(rt_model3d_get_mesh_count(model) == 1,
                    "Model3D.LoadAsset exposes meshes loaded from mounted dependencies");
        auto *mesh = static_cast<rt_mesh3d *>(rt_model3d_get_mesh(model, 0));
        EXPECT_TRUE(mesh != nullptr && mesh->vertex_count == 3 && mesh->index_count == 3,
                    "Model3D.LoadAsset imports geometry from a package-relative buffer");
        if (mesh) {
            EXPECT_NEAR(mesh->vertices[1].pos[0],
                        6.0,
                        0.001,
                        "Model3D.LoadAsset keeps mounted buffer vertex X");
            EXPECT_NEAR(mesh->vertices[2].pos[1],
                        7.0,
                        0.001,
                        "Model3D.LoadAsset keeps mounted buffer vertex Y");
        }
    }

    rt_asset_unmount(rt_const_cstr(pack_path));
    std::remove(pack_path);
}

static void test_model3d_load_asset_diagnostics_name_missing_dependency() {
    const char *pack_path = "/tmp/viper_model3d_missing_dep_pack.vpa";
    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"missing.bin\",\"byteLength\":12}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}]"
        "}";

    viper::asset::VpaWriter writer;
    writer.addEntry("assets/models/missing_dep.gltf",
                    reinterpret_cast<const uint8_t *>(gltf_json.data()),
                    gltf_json.size(),
                    false);
    std::string err;
    bool wrote_pack = writer.writeToFile(pack_path, err);
    EXPECT_TRUE(wrote_pack, "Missing-dependency asset pack can be written");
    if (!wrote_pack)
        return;
    bool mounted = rt_asset_mount(rt_const_cstr(pack_path)) == 1;
    EXPECT_TRUE(mounted, "Missing-dependency asset pack can mount");
    if (!mounted)
        return;

    g_last_trap = nullptr;
    g_expect_trap = true;
    if (setjmp(g_trap_jmp) == 0) {
        (void)rt_model3d_load_asset(rt_const_cstr("asset://assets/models/missing_dep.gltf"));
        g_expect_trap = false;
        EXPECT_TRUE(false, "Model3D.LoadAsset traps on missing external glTF dependencies");
    } else {
        g_expect_trap = false;
        EXPECT_TRUE(g_last_trap != nullptr &&
                        std::strstr(g_last_trap, "assets/models/missing_dep.gltf") != nullptr &&
                        std::strstr(g_last_trap, "assets/models/missing.bin") != nullptr,
                    "Model3D.LoadAsset diagnostics name the model and missing dependency");
    }

    rt_asset_unmount(rt_const_cstr(pack_path));
    std::remove(pack_path);
}

static void test_model3d_adapts_fbx_scene_graphs() {
    const char *path = "/tmp/viper_model3d_fixture.fbx";
    bool wrote_fixture = write_fbx_fixture(path);
    EXPECT_TRUE(wrote_fixture, "FBX fixture can be written");
    if (!wrote_fixture)
        return;

    void *model = rt_model3d_load(rt_const_cstr(path));
    EXPECT_TRUE(model != nullptr, "Model3D.Load parses generated FBX assets");
    if (!model)
        return;

    EXPECT_TRUE(rt_model3d_get_mesh_count(model) == 1, "Model3D exposes FBX meshes");
    EXPECT_TRUE(rt_model3d_get_material_count(model) == 1, "Model3D exposes FBX materials");
    EXPECT_TRUE(rt_model3d_get_node_count(model) == 2,
                "Model3D preserves logical FBX scene-node counts");

    void *parent = rt_model3d_find_node(model, rt_const_cstr("Parent"));
    void *child = rt_model3d_find_node(model, rt_const_cstr("Child"));
    EXPECT_TRUE(parent != nullptr, "Model3D.FindNode finds FBX parent nodes");
    EXPECT_TRUE(child != nullptr, "Model3D.FindNode finds FBX child nodes");
    if (!parent || !child)
        return;

    EXPECT_NEAR(rt_vec3_x(rt_scene_node3d_get_position(parent)),
                1.0,
                0.001,
                "Model3D preserves FBX parent translations");
    EXPECT_NEAR(rt_vec3_y(rt_scene_node3d_get_position(parent)),
                2.0,
                0.001,
                "Model3D preserves FBX parent Y translations");
    EXPECT_NEAR(rt_vec3_y(rt_scene_node3d_get_position(child)),
                5.0,
                0.001,
                "Model3D preserves FBX child translations");
    EXPECT_TRUE(rt_scene_node3d_get_mesh(child) == rt_model3d_get_mesh(model, 0),
                "FBX scene nodes reuse the extracted mesh object");
    EXPECT_TRUE(rt_scene_node3d_get_material(child) == rt_model3d_get_material(model, 0),
                "FBX scene nodes reuse the extracted material object");

    void *inst_scene = rt_model3d_instantiate_scene(model);
    EXPECT_TRUE(inst_scene != nullptr, "Model3D.InstantiateScene works for FBX assets");
    if (!inst_scene)
        return;

    EXPECT_TRUE(rt_scene3d_get_node_count(inst_scene) == 3,
                "FBX-backed Model3D instances attach below a new scene root");
    EXPECT_TRUE(rt_scene_node3d_child_count(rt_scene3d_get_root(inst_scene)) == 1,
                "FBX-backed Model3D instances preserve top-level grouping");
    EXPECT_TRUE(rt_scene3d_find(inst_scene, rt_const_cstr("Child")) != nullptr,
                "FBX-backed Model3D instances preserve child names");
}

static void test_model3d_loads_obj_as_template_asset() {
    const char *path = "/tmp/viper_model3d_fixture.obj";
    const char *obj = "# simple indexed triangle\n"
                      "v 0 0 0\n"
                      "v 1 0 0\n"
                      "v 0 1 0\n"
                      "vt 0 0\n"
                      "vt 1 0\n"
                      "vt 0 1\n"
                      "vn 0 0 1\n"
                      "f 1/1/1 2/2/1 3/3/1\n";
    FILE *f = std::fopen(path, "wb");
    bool wrote_fixture = f && std::fwrite(obj, 1, std::strlen(obj), f) == std::strlen(obj);
    if (f)
        std::fclose(f);
    EXPECT_TRUE(wrote_fixture, "OBJ fixture can be written");
    if (!wrote_fixture)
        return;

    void *model = rt_model3d_load(rt_const_cstr(path));
    EXPECT_TRUE(model != nullptr, "Model3D.Load parses OBJ assets");
    if (!model)
        return;

    EXPECT_TRUE(rt_model3d_get_mesh_count(model) == 1, "OBJ-backed Model3D exposes one mesh");
    EXPECT_TRUE(rt_model3d_get_material_count(model) == 1,
                "OBJ-backed Model3D creates a default material");
    EXPECT_TRUE(rt_model3d_get_node_count(model) == 1,
                "OBJ-backed Model3D synthesizes one template node");

    auto *mesh = static_cast<rt_mesh3d *>(rt_model3d_get_mesh(model, 0));
    EXPECT_TRUE(mesh != nullptr && mesh->vertex_count == 3 && mesh->index_count == 3,
                "OBJ-backed Model3D preserves imported mesh geometry");

    void *node = rt_model3d_find_node(model, rt_const_cstr("mesh_0"));
    EXPECT_TRUE(node != nullptr, "OBJ-backed Model3D names the synthesized mesh node");
    if (node) {
        EXPECT_TRUE(rt_scene_node3d_get_mesh(node) == rt_model3d_get_mesh(model, 0),
                    "OBJ synthesized node reuses the imported mesh");
        EXPECT_TRUE(rt_scene_node3d_get_material(node) == rt_model3d_get_material(model, 0),
                    "OBJ synthesized node uses the generated material");
    }
}

static void test_model3d_imports_fbx_skinning_and_grouped_animation() {
    const char *path = "/tmp/viper_model3d_skinned_anim_fixture.fbx";
    bool wrote_fixture = write_fbx_skinned_animation_fixture(path);
    EXPECT_TRUE(wrote_fixture, "Skinned FBX fixture can be written");
    if (!wrote_fixture)
        return;

    void *model = rt_model3d_load(rt_const_cstr(path));
    EXPECT_TRUE(model != nullptr, "Model3D.Load parses skinned FBX assets");
    if (!model)
        return;

    EXPECT_TRUE(rt_model3d_get_mesh_count(model) == 1, "Skinned FBX exposes one mesh");
    EXPECT_TRUE(rt_model3d_get_skeleton_count(model) == 1, "Skinned FBX exposes one skeleton");
    EXPECT_TRUE(rt_model3d_get_animation_count(model) == 1, "Skinned FBX exposes one animation");

    auto *mesh = static_cast<rt_mesh3d *>(rt_model3d_get_mesh(model, 0));
    EXPECT_TRUE(mesh != nullptr, "Skinned FBX mesh is available");
    if (!mesh)
        return;
    EXPECT_TRUE(mesh->bone_count == 2, "FBX skin clusters set mesh bone_count");
    EXPECT_NEAR(
        mesh->vertices[0].bone_weights[0], 1.0, 0.001, "Root-only vertex keeps full root weight");
    EXPECT_TRUE(mesh->vertices[0].bone_indices[0] == 0, "Root-only vertex references root bone");
    EXPECT_NEAR(mesh->vertices[1].bone_weights[0], 0.25, 0.001, "Shared vertex keeps root weight");
    EXPECT_NEAR(mesh->vertices[1].bone_weights[1], 0.75, 0.001, "Shared vertex keeps child weight");
    EXPECT_TRUE(mesh->vertices[1].bone_indices[0] == 0 && mesh->vertices[1].bone_indices[1] == 1,
                "Shared vertex references both imported bones");
    EXPECT_NEAR(
        mesh->vertices[2].bone_weights[0], 1.0, 0.001, "Child-only vertex keeps full child weight");
    EXPECT_TRUE(mesh->vertices[2].bone_indices[0] == 1, "Child-only vertex references child bone");

    auto *anim = static_cast<rt_animation3d *>(rt_model3d_get_animation(model, 0));
    EXPECT_TRUE(anim != nullptr, "Skinned FBX animation is available");
    if (!anim)
        return;
    EXPECT_NEAR(anim->duration, 1.0, 0.001, "FBX animation duration follows imported key time");

    const vgfx3d_anim_channel_t *root_channel = nullptr;
    for (int32_t i = 0; i < anim->channel_count; i++) {
        if (anim->channels[i].bone_index == 0) {
            root_channel = &anim->channels[i];
            break;
        }
    }
    EXPECT_TRUE(root_channel != nullptr, "FBX animation creates a root-bone channel");
    if (!root_channel)
        return;
    EXPECT_TRUE(root_channel->keyframe_count == 2,
                "FBX animation groups X/Y component curves into shared keyframes");
    EXPECT_NEAR(root_channel->keyframes[1].position[0],
                10.0,
                0.001,
                "Grouped FBX animation preserves X translation");
    EXPECT_NEAR(root_channel->keyframes[1].position[1],
                20.0,
                0.001,
                "Grouped FBX animation preserves Y translation");
    EXPECT_NEAR(root_channel->keyframes[1].position[2],
                0.0,
                0.001,
                "Grouped FBX animation keeps untouched Z translation at bind value");
    EXPECT_NEAR(root_channel->keyframes[1].scale_xyz[0],
                1.0,
                0.001,
                "Grouped FBX animation preserves default scale");

    void *instance = rt_model3d_instantiate(model);
    EXPECT_TRUE(instance != nullptr, "Model3D.Instantiate creates an animated instance root");
    void *controller = instance ? rt_scene_node3d_get_animator(instance) : nullptr;
    EXPECT_TRUE(controller != nullptr,
                "Model3D.Instantiate auto-binds a controller for imported skeleton animations");
    if (controller) {
        EXPECT_TRUE(rt_anim_controller3d_get_state_count(controller) == 1,
                    "Auto-bound Model3D controller registers imported clips as states");
        rt_string state = rt_anim_controller3d_get_current_state(controller);
        const char *state_name = state ? rt_string_cstr(state) : "";
        EXPECT_TRUE(std::strcmp(state_name, "Walk") == 0,
                    "Auto-bound Model3D controller starts the first imported animation");
    }
}

static void test_model3d_rejects_truncated_fbx() {
    const char *path = "/tmp/viper_model3d_truncated_fixture.fbx";
    bool wrote_fixture = write_truncated_fbx_fixture(path);
    EXPECT_TRUE(wrote_fixture, "Truncated FBX fixture can be written");
    if (!wrote_fixture)
        return;

    g_last_trap = nullptr;
    g_expect_trap = true;
    if (setjmp(g_trap_jmp) == 0) {
        (void)rt_model3d_load(rt_const_cstr(path));
        g_expect_trap = false;
        EXPECT_TRUE(false, "Model3D.Load traps on truncated FBX input");
        return;
    }
    g_expect_trap = false;
    EXPECT_TRUE(g_last_trap != nullptr &&
                    std::strstr(g_last_trap, "malformed or truncated binary FBX") != nullptr,
                "Model3D.Load reports truncated FBX as a hard parse error");
}

static void test_model3d_loads_demo_fbx_textures() {
    const char *path = find_existing_path({
#ifdef VIPER_SOURCE_DIR
        VIPER_SOURCE_DIR "/examples/games/3dbaseball/model.fbx",
#endif
        "examples/games/3dbaseball/model.fbx",
        "../examples/games/3dbaseball/model.fbx"});
    if (!path) {
        skip_test("3dbaseball FBX fixture is not present in this checkout");
        return;
    }

    void *model = rt_model3d_load(rt_const_cstr(path));
    EXPECT_TRUE(model != nullptr, "Model3D.Load parses the 3dbaseball FBX asset");
    if (!model)
        return;

    bool saw_textured_material = false;
    int64_t material_count = rt_model3d_get_material_count(model);
    for (int64_t i = 0; i < material_count; i++) {
        auto *mat = static_cast<rt_material3d *>(rt_model3d_get_material(model, i));
        if (mat && mat->texture) {
            saw_textured_material = true;
            break;
        }
    }

    EXPECT_TRUE(
        saw_textured_material,
        "FBX imports preserve demo diffuse textures when texture files sit beside the asset");
}

static void test_model3d_autoplays_gltf_node_and_morph_animation() {
    const char *path = "/tmp/viper_model3d_node_animation.gltf";
    std::vector<uint8_t> buffer;
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const uint16_t indices[3] = {0, 1, 2};
    const float morph_positions[9] = {0.0f, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.3f, 0.0f};
    const float times[2] = {0.0f, 1.0f};
    const float translations[6] = {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
    const float weights[2] = {0.0f, 1.0f};

    size_t pos_off = buffer.size();
    for (float v : positions)
        append_bytes(buffer, v);
    size_t idx_off = buffer.size();
    for (uint16_t v : indices)
        append_bytes(buffer, v);
    while (buffer.size() % 4 != 0)
        buffer.push_back(0);
    size_t morph_off = buffer.size();
    for (float v : morph_positions)
        append_bytes(buffer, v);
    size_t times_off = buffer.size();
    for (float v : times)
        append_bytes(buffer, v);
    size_t translation_off = buffer.size();
    for (float v : translations)
        append_bytes(buffer, v);
    size_t weights_off = buffer.size();
    for (float v : weights)
        append_bytes(buffer, v);

    std::string buffer_b64 = base64_encode(buffer.data(), buffer.size());
    std::string gltf_json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," +
        buffer_b64 + "\",\"byteLength\":" + std::to_string(buffer.size()) +
        "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(pos_off) +
        ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(idx_off) +
        ",\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(morph_off) +
        ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(times_off) +
        ",\"byteLength\":8},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(translation_off) +
        ",\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(weights_off) +
        ",\"byteLength\":8}"
        "],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
        "{\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"}"
        "],"
        "\"meshes\":[{\"weights\":[0.0],\"primitives\":[{\"attributes\":{\"POSITION\":0},"
        "\"indices\":1,\"targets\":[{\"POSITION\":2}]}]}],"
        "\"nodes\":[{\"name\":\"Mover\",\"mesh\":0,\"weights\":[0.0]}],"
        "\"scenes\":[{\"nodes\":[0]}],\"scene\":0,"
        "\"animations\":[{\"name\":\"MoveAndSmile\",\"samplers\":[{\"input\":3,\"output\":4},"
        "{\"input\":3,\"output\":5}],\"channels\":["
        "{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}},"
        "{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"weights\"}}]}]"
        "}";
    EXPECT_TRUE(write_text_file(path, gltf_json), "Node-animation glTF fixture can be written");
    void *model = rt_model3d_load(rt_const_cstr(path));
    EXPECT_TRUE(model != nullptr, "Model3D.Load imports glTF node animation clips");
    if (!model)
        return;
    void *scene = rt_model3d_instantiate_scene(model);
    EXPECT_TRUE(scene != nullptr, "Model3D.InstantiateScene creates a scene for node animation");
    if (!scene)
        return;
    rt_scene3d_sync_bindings(scene, 0.5);
    void *node = rt_scene3d_find(scene, rt_const_cstr("Mover"));
    EXPECT_TRUE(node != nullptr, "Instantiated scene preserves animated node name");
    if (!node)
        return;
    EXPECT_NEAR(rt_vec3_x(rt_scene_node3d_get_position(node)),
                1.0,
                0.001,
                "Scene3D.SyncBindings advances glTF node translation animation");
    auto *mesh = static_cast<rt_mesh3d *>(rt_scene_node3d_get_mesh(node));
    EXPECT_TRUE(mesh != nullptr && mesh->morph_targets_ref != nullptr,
                "Animated glTF node keeps an instance-local morph target");
    if (mesh && mesh->morph_targets_ref) {
        EXPECT_NEAR(rt_morphtarget3d_get_weight(mesh->morph_targets_ref, 0),
                    0.5,
                    0.001,
                    "Scene3D.SyncBindings advances glTF morph weight animation");
    }
}

static void test_model3d_rejects_wrong_handle_types() {
    void *node = rt_scene_node3d_new();
    EXPECT_TRUE(rt_model3d_get_mesh_count(node) == 0,
                "Model3D.GetMeshCount rejects non-Model3D handles");
    EXPECT_TRUE(rt_model3d_get_scene_count(node) == 0,
                "Model3D.SceneCount rejects non-Model3D handles");
    EXPECT_TRUE(rt_model3d_get_camera_count(node, 0) == 0,
                "Model3D.GetCameraCount rejects non-Model3D handles");
    EXPECT_TRUE(rt_model3d_get_camera(node, 0, 0) == nullptr,
                "Model3D.GetCamera rejects non-Model3D handles");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_model3d_get_scene_name(node, 0)), "") == 0,
                "Model3D.GetSceneName rejects non-Model3D handles");
    EXPECT_TRUE(rt_model3d_get_mesh(node, 0) == nullptr,
                "Model3D.GetMesh rejects non-Model3D handles");
    EXPECT_TRUE(rt_model3d_instantiate(node) == nullptr,
                "Model3D.Instantiate rejects non-Model3D handles");
    EXPECT_TRUE(rt_model3d_instantiate_scene(node) == nullptr,
                "Model3D.InstantiateScene rejects non-Model3D handles");
    EXPECT_TRUE(rt_model3d_instantiate_scene_at(node, 0) == nullptr,
                "Model3D.InstantiateSceneAt rejects non-Model3D handles");
}

int main() {
    test_model3d_rejects_wrong_handle_types();
    test_model3d_roundtrips_vscn_assets();
    test_model3d_adapts_gltf_scene_graphs();
    test_model3d_load_asset_resolves_mounted_gltf_dependencies();
    test_model3d_load_asset_diagnostics_name_missing_dependency();
    test_model3d_adapts_fbx_scene_graphs();
    test_model3d_loads_obj_as_template_asset();
    test_model3d_imports_fbx_skinning_and_grouped_animation();
    test_model3d_rejects_truncated_fbx();
    test_model3d_loads_demo_fbx_textures();
    test_model3d_autoplays_gltf_node_and_morph_animation();
    std::printf("Model3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
