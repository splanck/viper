//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gltf.c
// Purpose: glTF 2.0 (.gltf/.glb) loader implementation.
// Key invariants:
//   - Uses existing rt_json parser for JSON content
//   - Supports .glb binary container (magic 0x46546C67)
//   - PBR metallic-roughness → Blinn-Phong material conversion
//   - Mesh primitives with POSITION, NORMAL, TEXCOORD_0 attributes
// Ownership/Lifetime:
//   - All extracted objects are GC-managed
// Links: rt_gltf.h, rt_json.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_gltf.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_pixels.h"
#include "rt_string.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for runtime JSON and collection APIs
extern void *rt_json_parse_object(rt_string text);
extern void *rt_map_get(void *map, rt_string key);
extern int64_t rt_seq_len(void *seq);
extern void *rt_seq_get(void *seq, int64_t index);
extern int64_t rt_box_type(void *box);
extern int64_t rt_unbox_i64(void *boxed);
extern double rt_unbox_f64(void *boxed);
extern int64_t rt_unbox_i1(void *boxed);
extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
#include "rt_trap.h"
extern int64_t rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern void *rt_asset_decode_typed(const char *name, const uint8_t *data, size_t size);
extern void *rt_pixels_load(void *path);
extern void rt_material3d_set_texture(void *obj, void *pixels);
extern void rt_material3d_set_normal_map(void *obj, void *pixels);
extern void rt_material3d_set_specular_map(void *obj, void *pixels);
extern void rt_material3d_set_emissive_map(void *obj, void *pixels);
extern void rt_material3d_set_alpha(void *obj, double alpha);

//===----------------------------------------------------------------------===//
// Asset container
//===----------------------------------------------------------------------===//

typedef struct {
    void *vptr;
    void **meshes;
    int32_t mesh_count;
    void **materials;
    int32_t material_count;
} rt_gltf_asset;

static void gltf_asset_finalize(void *obj) {
    rt_gltf_asset *a = (rt_gltf_asset *)obj;
    if (a->meshes) {
        for (int32_t i = 0; i < a->mesh_count; i++) {
            if (a->meshes[i] && rt_obj_release_check0(a->meshes[i]))
                rt_obj_free(a->meshes[i]);
        }
    }
    free(a->meshes);
    a->meshes = NULL;
    if (a->materials) {
        for (int32_t i = 0; i < a->material_count; i++) {
            if (a->materials[i] && rt_obj_release_check0(a->materials[i]))
                rt_obj_free(a->materials[i]);
        }
    }
    free(a->materials);
    a->materials = NULL;
}

//===----------------------------------------------------------------------===//
// JSON helpers
//===----------------------------------------------------------------------===//

static void *jget(void *obj, const char *key) {
    if (!obj)
        return NULL;
    rt_string k = rt_const_cstr(key);
    return rt_map_get(obj, k);
}

static double jnum(void *obj, const char *key, double def) {
    void *v = jget(obj, key);
    if (!v)
        return def;
    switch (rt_box_type(v)) {
        case 0:
            return (double)rt_unbox_i64(v);
        case 1:
            return rt_unbox_f64(v);
        case 2:
            return (double)rt_unbox_i1(v);
        default:
            return def;
    }
}

static int64_t jint(void *obj, const char *key, int64_t def) {
    void *v = jget(obj, key);
    if (!v)
        return def;
    switch (rt_box_type(v)) {
        case 0:
            return rt_unbox_i64(v);
        case 1:
            return (int64_t)rt_unbox_f64(v);
        case 2:
            return rt_unbox_i1(v);
        default:
            return def;
    }
}

static const char *jstr(void *obj, const char *key) {
    void *v = jget(obj, key);
    rt_string s = (rt_string)v;
    return s ? rt_string_cstr(s) : NULL;
}

static void *jarr(void *obj, const char *key) {
    return jget(obj, key);
}

static int64_t jarr_len(void *arr) {
    return arr ? rt_seq_len(arr) : 0;
}

static double jvalue_num(void *value, double def) {
    if (!value)
        return def;
    switch (rt_box_type(value)) {
        case 0:
            return (double)rt_unbox_i64(value);
        case 1:
            return rt_unbox_f64(value);
        default:
            return def;
    }
}

//===----------------------------------------------------------------------===//
// Buffer management
//===----------------------------------------------------------------------===//

typedef struct {
    uint8_t *data;
    size_t len;
} gltf_buffer_t;

static const char gltf_base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int gltf_base64_digit_value(char c) {
    const char *p = strchr(gltf_base64_chars, c);
    if (p)
        return (int)(p - gltf_base64_chars);
    if (c == '=')
        return -2;
    return -1;
}

static uint8_t *gltf_base64_decode(const char *data, size_t len, size_t *out_len) {
    if (!data)
        return NULL;
    if (len == 0) {
        uint8_t *empty = (uint8_t *)malloc(1);
        if (out_len)
            *out_len = 0;
        return empty;
    }
    if (len % 4 != 0)
        return NULL;

    size_t olen = (len / 4) * 3;
    if (len > 0 && data[len - 1] == '=')
        olen--;
    if (len > 1 && data[len - 2] == '=')
        olen--;

    uint8_t *output = (uint8_t *)malloc(olen > 0 ? olen : 1);
    if (!output)
        return NULL;

    size_t i = 0;
    size_t j = 0;
    while (i < len) {
        int a = gltf_base64_digit_value(data[i++]);
        int b = gltf_base64_digit_value(data[i++]);
        int c = gltf_base64_digit_value(data[i++]);
        int d = gltf_base64_digit_value(data[i++]);
        if (a < 0 || b < 0 || c == -1 || d == -1) {
            free(output);
            return NULL;
        }
        if (c == -2)
            c = 0;
        if (d == -2)
            d = 0;
        {
            uint32_t triple =
                ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
            if (j < olen)
                output[j++] = (uint8_t)((triple >> 16) & 0xFF);
            if (j < olen)
                output[j++] = (uint8_t)((triple >> 8) & 0xFF);
            if (j < olen)
                output[j++] = (uint8_t)(triple & 0xFF);
        }
    }

    if (out_len)
        *out_len = olen;
    return output;
}

static int gltf_parse_data_uri(
    const char *uri, char *mime_buf, size_t mime_buf_cap, uint8_t **out_data, size_t *out_len) {
    const char *comma;
    const char *payload;
    size_t mime_len = 0;
    int is_base64 = 0;
    if (!uri || strncmp(uri, "data:", 5) != 0)
        return 0;
    comma = strchr(uri, ',');
    if (!comma)
        return 0;
    payload = comma + 1;

    if (mime_buf && mime_buf_cap > 0)
        mime_buf[0] = '\0';
    {
        const char *meta = uri + 5;
        const char *semi = memchr(meta, ';', (size_t)(comma - meta));
        if (semi) {
            mime_len = (size_t)(semi - meta);
            if (strstr(semi, ";base64"))
                is_base64 = 1;
        } else {
            mime_len = (size_t)(comma - meta);
        }
        if (mime_buf && mime_buf_cap > 0 && mime_len > 0) {
            if (mime_len >= mime_buf_cap)
                mime_len = mime_buf_cap - 1;
            memcpy(mime_buf, meta, mime_len);
            mime_buf[mime_len] = '\0';
        }
    }

    if (is_base64) {
        *out_data = gltf_base64_decode(payload, strlen(payload), out_len);
        return *out_data != NULL;
    }

    *out_len = strlen(payload);
    *out_data = (uint8_t *)malloc(*out_len > 0 ? *out_len : 1);
    if (!*out_data)
        return 0;
    if (*out_len > 0)
        memcpy(*out_data, payload, *out_len);
    return 1;
}

static const uint8_t *gltf_get_buffer_view_data(
    void *root, int64_t view_idx, gltf_buffer_t *buffers, int buf_count, size_t *out_len) {
    void *views = jarr(root, "bufferViews");
    if (!views || view_idx < 0 || view_idx >= jarr_len(views))
        return NULL;
    void *view = rt_seq_get(views, view_idx);
    if (!view)
        return NULL;
    int buf_idx = (int)jint(view, "buffer", -1);
    size_t byte_offset = (size_t)jint(view, "byteOffset", 0);
    size_t byte_length = (size_t)jint(view, "byteLength", 0);
    if (buf_idx < 0 || buf_idx >= buf_count)
        return NULL;
    if (byte_offset + byte_length > buffers[buf_idx].len)
        return NULL;
    if (out_len)
        *out_len = byte_length;
    return buffers[buf_idx].data + byte_offset;
}

/// @brief Read accessor data: resolve bufferView → buffer → byte range.
static const uint8_t *gltf_get_accessor_data(void *root,
                                             int64_t accessor_idx,
                                             gltf_buffer_t *buffers,
                                             int buf_count,
                                             int *out_count,
                                             int *out_stride) {
    void *accessors = jarr(root, "accessors");
    if (!accessors || accessor_idx < 0 || accessor_idx >= jarr_len(accessors))
        return NULL;
    void *acc = rt_seq_get(accessors, accessor_idx);
    if (!acc)
        return NULL;

    *out_count = (int)jint(acc, "count", 0);
    int bv_idx = (int)jint(acc, "bufferView", -1);
    int byte_offset_acc = (int)jint(acc, "byteOffset", 0);
    int comp_type = (int)jint(acc, "componentType", 5126); // 5126=FLOAT

    void *views = jarr(root, "bufferViews");
    if (!views || bv_idx < 0 || bv_idx >= (int)jarr_len(views))
        return NULL;
    void *bv = rt_seq_get(views, (int64_t)bv_idx);
    if (!bv)
        return NULL;

    int buf_idx = (int)jint(bv, "buffer", 0);
    int byte_offset_bv = (int)jint(bv, "byteOffset", 0);
    int byte_stride = (int)jint(bv, "byteStride", 0);

    if (buf_idx < 0 || buf_idx >= buf_count)
        return NULL;

    // Determine element size from componentType
    int comp_size = 4; // default float
    if (comp_type == 5120 || comp_type == 5121)
        comp_size = 1;
    else if (comp_type == 5122 || comp_type == 5123)
        comp_size = 2;
    else if (comp_type == 5125)
        comp_size = 4; // unsigned int

    // Determine component count from accessor type
    const char *acc_type = jstr(acc, "type");
    int comp_count = 1;
    if (acc_type) {
        if (strcmp(acc_type, "VEC2") == 0)
            comp_count = 2;
        else if (strcmp(acc_type, "VEC3") == 0)
            comp_count = 3;
        else if (strcmp(acc_type, "VEC4") == 0)
            comp_count = 4;
        else if (strcmp(acc_type, "MAT4") == 0)
            comp_count = 16;
    }

    *out_stride = byte_stride > 0 ? byte_stride : comp_size * comp_count;

    size_t offset = (size_t)byte_offset_bv + (size_t)byte_offset_acc;
    if (offset >= buffers[buf_idx].len)
        return NULL;

    return buffers[buf_idx].data + offset;
}

static void gltf_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

static void gltf_resolve_relative_path(const char *base_path,
                                       const char *uri,
                                       char *out,
                                       size_t out_cap) {
    const char *last_sep;
    const char *last_bsep;
    size_t dir_len;
    if (!out || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!uri)
        return;
    last_sep = strrchr(base_path ? base_path : "", '/');
    last_bsep = strrchr(base_path ? base_path : "", '\\');
    if (last_bsep > last_sep)
        last_sep = last_bsep;
    if (last_sep) {
        dir_len = (size_t)(last_sep - base_path + 1);
        if (dir_len >= out_cap)
            dir_len = out_cap - 1;
        memcpy(out, base_path, dir_len);
        out[dir_len] = '\0';
        strncat(out, uri, out_cap - dir_len - 1);
    } else {
        strncpy(out, uri, out_cap - 1);
        out[out_cap - 1] = '\0';
    }
}

static void gltf_build_embedded_name(const char *mime_type,
                                     const char *fallback_ext,
                                     char *out,
                                     size_t out_cap) {
    const char *ext = fallback_ext ? fallback_ext : ".bin";
    if (!out || out_cap == 0)
        return;
    if (mime_type) {
        if (strstr(mime_type, "png"))
            ext = ".png";
        else if (strstr(mime_type, "jpeg") || strstr(mime_type, "jpg"))
            ext = ".jpg";
        else if (strstr(mime_type, "bmp"))
            ext = ".bmp";
        else if (strstr(mime_type, "gif"))
            ext = ".gif";
    }
    snprintf(out, out_cap, "embedded%s", ext);
}

//===----------------------------------------------------------------------===//
// Main loader
//===----------------------------------------------------------------------===//

/// @brief Load a glTF 2.0 binary (.glb) file and extract meshes and materials.
/// @details Parses the GLB container (JSON chunk + binary buffer), resolves
///          accessors → buffer views → byte ranges, and constructs Mesh3D and
///          Material3D objects. Supports POSITION, NORMAL, TEXCOORD_0 attributes
///          and indexed geometry. Materials use baseColorFactor/baseColorTexture.
/// @param path File path to the .glb file (runtime string).
/// @return Opaque GLTF asset handle, or NULL on failure.
void *rt_gltf_load(rt_string path) {
    if (!path)
        return NULL;
    const char *filepath = rt_string_cstr(path);
    if (!filepath)
        return NULL;

    // Read file
    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 256 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    uint8_t *file_data = (uint8_t *)malloc((size_t)fsize);
    if (!file_data) {
        fclose(f);
        return NULL;
    }
    if (fread(file_data, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(file_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    // Detect .glb vs .gltf
    char *json_str = NULL;
    uint8_t *bin_chunk = NULL;
    size_t bin_chunk_len = 0;

    if ((size_t)fsize >= 12 && file_data[0] == 0x67 && file_data[1] == 0x6C &&
        file_data[2] == 0x54 && file_data[3] == 0x46) {
        // GLB binary container
        uint32_t version = file_data[4] | ((uint32_t)file_data[5] << 8) |
                           ((uint32_t)file_data[6] << 16) | ((uint32_t)file_data[7] << 24);
        (void)version;

        // Parse chunks
        size_t pos = 12;
        while (pos + 8 <= (size_t)fsize) {
            uint32_t chunk_len = file_data[pos] | ((uint32_t)file_data[pos + 1] << 8) |
                                 ((uint32_t)file_data[pos + 2] << 16) |
                                 ((uint32_t)file_data[pos + 3] << 24);
            uint32_t chunk_type = file_data[pos + 4] | ((uint32_t)file_data[pos + 5] << 8) |
                                  ((uint32_t)file_data[pos + 6] << 16) |
                                  ((uint32_t)file_data[pos + 7] << 24);
            pos += 8;
            if (pos + chunk_len > (size_t)fsize)
                break;

            if (chunk_type == 0x4E4F534A) {
                // JSON chunk
                json_str = (char *)malloc(chunk_len + 1);
                if (json_str) {
                    memcpy(json_str, file_data + pos, chunk_len);
                    json_str[chunk_len] = '\0';
                }
            } else if (chunk_type == 0x004E4942) {
                // BIN chunk
                bin_chunk = file_data + pos;
                bin_chunk_len = chunk_len;
            }
            pos += chunk_len;
        }
    } else {
        // Text .gltf
        json_str = (char *)malloc((size_t)fsize + 1);
        if (json_str) {
            memcpy(json_str, file_data, (size_t)fsize);
            json_str[fsize] = '\0';
        }
    }

    if (!json_str) {
        free(file_data);
        return NULL;
    }

    // Parse JSON
    rt_string json_rts = rt_const_cstr(json_str);
    void *root = rt_json_parse_object(json_rts);
    free(json_str);
    if (!root) {
        free(file_data);
        return NULL;
    }

    // Load buffers
    void *buffers_arr = jarr(root, "buffers");
    int buf_count = (int)jarr_len(buffers_arr);
    gltf_buffer_t *buffers =
        (gltf_buffer_t *)calloc((size_t)(buf_count + 1), sizeof(gltf_buffer_t));
    if (!buffers) {
        free(file_data);
        return NULL;
    }

    for (int i = 0; i < buf_count; i++) {
        void *buf_obj = rt_seq_get(buffers_arr, (int64_t)i);
        int64_t byte_length = jint(buf_obj, "byteLength", 0);
        const char *uri = jstr(buf_obj, "uri");

        if (i == 0 && bin_chunk && !uri) {
            // GLB: buffer 0 is the BIN chunk
            buffers[i].data = bin_chunk;
            buffers[i].len = bin_chunk_len;
        } else if (uri) {
            if (strncmp(uri, "data:", 5) == 0) {
                char mime_type[64];
                uint8_t *decoded = NULL;
                size_t decoded_len = 0;
                if (gltf_parse_data_uri(
                        uri, mime_type, sizeof(mime_type), &decoded, &decoded_len)) {
                    buffers[i].data = decoded;
                    buffers[i].len = decoded_len;
                }
            } else {
                // External file — resolve relative to .gltf directory
                char buf_path[1024];
                gltf_resolve_relative_path(filepath, uri, buf_path, sizeof(buf_path));
                FILE *bf = fopen(buf_path, "rb");
                if (bf) {
                    fseek(bf, 0, SEEK_END);
                    long blen = ftell(bf);
                    fseek(bf, 0, SEEK_SET);
                    if (blen > 0 && blen <= byte_length + 4) {
                        buffers[i].data = (uint8_t *)malloc((size_t)blen);
                        if (buffers[i].data) {
                            if (fread(buffers[i].data, 1, (size_t)blen, bf) == (size_t)blen)
                                buffers[i].len = (size_t)blen;
                        }
                    }
                    fclose(bf);
                }
            }
        }
    }

    // Create asset
    rt_gltf_asset *asset = (rt_gltf_asset *)rt_obj_new_i64(0, (int64_t)sizeof(rt_gltf_asset));
    if (!asset) {
        for (int i = 0; i < buf_count; i++) {
            if (buffers[i].data != bin_chunk)
                free(buffers[i].data);
        }
        free(buffers);
        free(file_data);
        return NULL;
    }
    asset->vptr = NULL;
    asset->meshes = NULL;
    asset->mesh_count = 0;
    asset->materials = NULL;
    asset->material_count = 0;
    rt_obj_set_finalizer(asset, gltf_asset_finalize);

    void **images = NULL;
    void **texture_images = NULL;
    void *images_arr = jarr(root, "images");
    int image_count = (int)jarr_len(images_arr);
    if (image_count > 0)
        images = (void **)calloc((size_t)image_count, sizeof(void *));

    for (int i = 0; i < image_count && images; i++) {
        void *image_json = rt_seq_get(images_arr, (int64_t)i);
        const char *uri = jstr(image_json, "uri");
        const char *mime_type = jstr(image_json, "mimeType");
        uint8_t *owned_data = NULL;
        const uint8_t *image_data = NULL;
        size_t image_len = 0;
        char image_name[64];

        if (uri && strncmp(uri, "data:", 5) == 0) {
            char parsed_mime[64];
            if (gltf_parse_data_uri(
                    uri, parsed_mime, sizeof(parsed_mime), &owned_data, &image_len)) {
                image_data = owned_data;
                if (!mime_type && parsed_mime[0] != '\0')
                    mime_type = parsed_mime;
            }
        } else if (uri) {
            char image_path[1024];
            gltf_resolve_relative_path(filepath, uri, image_path, sizeof(image_path));
            images[i] = rt_pixels_load(rt_const_cstr(image_path));
        } else {
            int64_t view_idx = jint(image_json, "bufferView", -1);
            image_data = gltf_get_buffer_view_data(root, view_idx, buffers, buf_count, &image_len);
        }

        if (!images[i] && image_data && image_len > 0) {
            gltf_build_embedded_name(mime_type, ".bin", image_name, sizeof(image_name));
            images[i] = rt_asset_decode_typed(image_name, image_data, image_len);
        }
        free(owned_data);
    }

    void *textures_arr = jarr(root, "textures");
    int texture_count = (int)jarr_len(textures_arr);
    if (texture_count > 0)
        texture_images = (void **)calloc((size_t)texture_count, sizeof(void *));
    for (int i = 0; i < texture_count && texture_images; i++) {
        void *texture_json = rt_seq_get(textures_arr, (int64_t)i);
        int64_t source_idx = jint(texture_json, "source", -1);
        if (source_idx >= 0 && source_idx < image_count)
            texture_images[i] = images[source_idx];
    }

    // Extract materials
    void *mats_arr = jarr(root, "materials");
    int mat_count = (int)jarr_len(mats_arr);
    if (mat_count > 0) {
        asset->materials = (void **)calloc((size_t)mat_count, sizeof(void *));
        for (int i = 0; i < mat_count && asset->materials; i++) {
            void *mat_json = rt_seq_get(mats_arr, (int64_t)i);
            void *mat = rt_material3d_new();
            if (!mat)
                continue;

            // PBR metallic-roughness → Blinn-Phong
            void *pbr = jget(mat_json, "pbrMetallicRoughness");
            if (pbr) {
                void *bcf = jarr(pbr, "baseColorFactor");
                if (bcf && jarr_len(bcf) >= 3) {
                    double r = jvalue_num(rt_seq_get(bcf, 0), 0.0);
                    double g = jvalue_num(rt_seq_get(bcf, 1), 0.0);
                    double b = jvalue_num(rt_seq_get(bcf, 2), 0.0);
                    rt_material3d_set_color(mat, r, g, b);
                    if (jarr_len(bcf) >= 4) {
                        double a = jvalue_num(rt_seq_get(bcf, 3), 1.0);
                        if (a < 1.0)
                            rt_material3d_set_alpha(mat, a);
                    }
                }
                {
                    void *base_tex = jget(pbr, "baseColorTexture");
                    int64_t tex_idx = jint(base_tex, "index", -1);
                    if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                        texture_images[tex_idx])
                        rt_material3d_set_texture(mat, texture_images[tex_idx]);
                }
                {
                    void *mr_tex = jget(pbr, "metallicRoughnessTexture");
                    int64_t tex_idx = jint(mr_tex, "index", -1);
                    if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                        texture_images[tex_idx])
                        rt_material3d_set_specular_map(mat, texture_images[tex_idx]);
                }
                double roughness = jnum(pbr, "roughnessFactor", 1.0);
                double shininess = pow(1.0 - roughness, 2.0) * 256.0;
                if (shininess < 1.0)
                    shininess = 1.0;
                rt_material3d_set_shininess(mat, shininess);
            }

            // Emissive
            void *ef = jarr(mat_json, "emissiveFactor");
            if (ef && jarr_len(ef) >= 3) {
                double er = jvalue_num(rt_seq_get(ef, 0), 0.0);
                double eg = jvalue_num(rt_seq_get(ef, 1), 0.0);
                double eb = jvalue_num(rt_seq_get(ef, 2), 0.0);
                rt_material3d_set_emissive_color(mat, er, eg, eb);
            }

            {
                void *normal_tex = jget(mat_json, "normalTexture");
                int64_t tex_idx = jint(normal_tex, "index", -1);
                if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                    texture_images[tex_idx])
                    rt_material3d_set_normal_map(mat, texture_images[tex_idx]);
            }
            {
                void *emissive_tex = jget(mat_json, "emissiveTexture");
                int64_t tex_idx = jint(emissive_tex, "index", -1);
                if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                    texture_images[tex_idx])
                    rt_material3d_set_emissive_map(mat, texture_images[tex_idx]);
            }

            asset->materials[i] = mat;
            asset->material_count = i + 1;
        }
    }

    // Extract meshes
    void *meshes_arr = jarr(root, "meshes");
    int mesh_json_count = (int)jarr_len(meshes_arr);
    if (mesh_json_count > 0) {
        // Count total primitives (each primitive becomes a mesh)
        int total_prims = 0;
        for (int i = 0; i < mesh_json_count; i++) {
            void *mesh_json = rt_seq_get(meshes_arr, (int64_t)i);
            void *prims = jarr(mesh_json, "primitives");
            total_prims += (int)jarr_len(prims);
        }

        if (total_prims > 0) {
            asset->meshes = (void **)calloc((size_t)total_prims, sizeof(void *));
            int mesh_idx = 0;

            for (int mi = 0; mi < mesh_json_count && asset->meshes; mi++) {
                void *mesh_json = rt_seq_get(meshes_arr, (int64_t)mi);
                void *prims = jarr(mesh_json, "primitives");
                int prim_count = (int)jarr_len(prims);

                for (int pi = 0; pi < prim_count; pi++) {
                    void *prim = rt_seq_get(prims, (int64_t)pi);
                    void *attrs = jget(prim, "attributes");
                    if (!attrs)
                        continue;

                    // Get accessor indices
                    int64_t pos_acc = jint(attrs, "POSITION", -1);
                    int64_t norm_acc = jint(attrs, "NORMAL", -1);
                    int64_t uv_acc = jint(attrs, "TEXCOORD_0", -1);
                    int64_t idx_acc = jint(prim, "indices", -1);

                    if (pos_acc < 0)
                        continue;

                    // Read position data
                    int pos_count = 0, pos_stride = 0;
                    const uint8_t *pos_data = gltf_get_accessor_data(
                        root, pos_acc, buffers, buf_count, &pos_count, &pos_stride);
                    if (!pos_data || pos_count == 0)
                        continue;

                    // Read normal data (optional)
                    int norm_count = 0, norm_stride = 0;
                    const uint8_t *norm_data = NULL;
                    if (norm_acc >= 0)
                        norm_data = gltf_get_accessor_data(
                            root, norm_acc, buffers, buf_count, &norm_count, &norm_stride);

                    // Read UV data (optional)
                    int uv_count = 0, uv_stride = 0;
                    const uint8_t *uv_data = NULL;
                    if (uv_acc >= 0)
                        uv_data = gltf_get_accessor_data(
                            root, uv_acc, buffers, buf_count, &uv_count, &uv_stride);

                    // Create mesh and populate vertices
                    void *mesh = rt_mesh3d_new();
                    if (!mesh)
                        continue;

                    for (int vi = 0; vi < pos_count; vi++) {
                        const float *p =
                            (const float *)(pos_data + (size_t)vi * (size_t)pos_stride);
                        float nx = 0, ny = 0, nz = 0;
                        if (norm_data && vi < norm_count) {
                            const float *n =
                                (const float *)(norm_data + (size_t)vi * (size_t)norm_stride);
                            nx = n[0];
                            ny = n[1];
                            nz = n[2];
                        }
                        float u = 0, v = 0;
                        if (uv_data && vi < uv_count) {
                            const float *t =
                                (const float *)(uv_data + (size_t)vi * (size_t)uv_stride);
                            u = t[0];
                            v = t[1];
                        }
                        rt_mesh3d_add_vertex(mesh, p[0], p[1], p[2], nx, ny, nz, u, v);
                    }

                    // Read indices (optional — if absent, use sequential)
                    if (idx_acc >= 0) {
                        int idx_count = 0, idx_stride = 0;
                        const uint8_t *idx_data = gltf_get_accessor_data(
                            root, idx_acc, buffers, buf_count, &idx_count, &idx_stride);
                        // Determine index component type
                        void *acc_obj = rt_seq_get(jarr(root, "accessors"), idx_acc);
                        int comp_type = (int)jint(acc_obj, "componentType", 5123);

                        if (idx_data) {
                            for (int ii = 0; ii + 2 < idx_count; ii += 3) {
                                int64_t i0, i1, i2;
                                if (comp_type == 5121) { // UNSIGNED_BYTE
                                    i0 = idx_data[ii * idx_stride];
                                    i1 = idx_data[(ii + 1) * idx_stride];
                                    i2 = idx_data[(ii + 2) * idx_stride];
                                } else if (comp_type == 5123) { // UNSIGNED_SHORT
                                    i0 = *(const uint16_t *)(idx_data +
                                                             (size_t)ii * (size_t)idx_stride);
                                    i1 = *(const uint16_t *)(idx_data +
                                                             (size_t)(ii + 1) * (size_t)idx_stride);
                                    i2 = *(const uint16_t *)(idx_data +
                                                             (size_t)(ii + 2) * (size_t)idx_stride);
                                } else { // UNSIGNED_INT (5125)
                                    i0 = *(const uint32_t *)(idx_data +
                                                             (size_t)ii * (size_t)idx_stride);
                                    i1 = *(const uint32_t *)(idx_data +
                                                             (size_t)(ii + 1) * (size_t)idx_stride);
                                    i2 = *(const uint32_t *)(idx_data +
                                                             (size_t)(ii + 2) * (size_t)idx_stride);
                                }
                                rt_mesh3d_add_triangle(mesh, i0, i1, i2);
                            }
                        }
                    } else {
                        // No indices — sequential triangles
                        for (int vi = 0; vi + 2 < pos_count; vi += 3)
                            rt_mesh3d_add_triangle(mesh, vi, vi + 1, vi + 2);
                    }

                    // Recalc normals if none provided
                    if (!norm_data && ((rt_mesh3d *)mesh)->vertex_count > 0)
                        rt_mesh3d_recalc_normals(mesh);

                    asset->meshes[mesh_idx++] = mesh;
                    asset->mesh_count = mesh_idx;
                }
            }
        }
    }

    if (images) {
        for (int i = 0; i < image_count; i++)
            gltf_release_ref(&images[i]);
    }
    free(images);
    free(texture_images);

    // Cleanup buffers (except BIN chunk which is part of file_data)
    for (int i = 0; i < buf_count; i++) {
        if (buffers[i].data != bin_chunk)
            free(buffers[i].data);
    }
    free(buffers);
    free(file_data);

    return asset;
}

/// @brief Get the number of meshes extracted from the GLTF file.
int64_t rt_gltf_mesh_count(void *obj) {
    return obj ? ((rt_gltf_asset *)obj)->mesh_count : 0;
}

/// @brief Get a mesh by index from the loaded GLTF asset.
void *rt_gltf_get_mesh(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_gltf_asset *a = (rt_gltf_asset *)obj;
    if (index < 0 || index >= a->mesh_count)
        return NULL;
    return a->meshes[index];
}

/// @brief Get the number of materials extracted from the GLTF file.
int64_t rt_gltf_material_count(void *obj) {
    return obj ? ((rt_gltf_asset *)obj)->material_count : 0;
}

/// @brief Get a material by index from the loaded GLTF asset.
void *rt_gltf_get_material(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_gltf_asset *a = (rt_gltf_asset *)obj;
    if (index < 0 || index >= a->material_count)
        return NULL;
    return a->materials[index];
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
