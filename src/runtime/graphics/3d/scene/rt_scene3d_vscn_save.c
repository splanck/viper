//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_scene3d_vscn_save.c
// Purpose: Scene3D .vscn serialization (save). Walks a Scene3D / SceneNode3D
//   tree and emits a JSON document with base64-encoded binary asset payloads
//   (textures, mesh buffers). A pointer-deduplication table ensures shared
//   assets are emitted once and referenced by index.
//
// Key invariants:
//   - .vscn is a JSON document with base64-encoded binary embeds.
//   - Each unique mesh / material / texture / cubemap pointer appears once.
//
// Ownership/Lifetime:
//   - Operates on Scene3D / SceneNode3D objects defined in rt_scene3d.c;
//     this TU owns no GC objects of its own.
//
// Links: rt_scene3d.h, rt_scene3d_internal.h, rt_scene3d_vscn_internal.h,
//        rt_scene3d_vscn_load.c (inverse: load), rt_json.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_box.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_file_stdio.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_scene3d_vscn_internal.h"
#include "rt_seq.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void **items;
    int32_t count;
    int32_t capacity;
} vscn_ptr_table_t;

typedef struct {
    vscn_ptr_table_t meshes;
    vscn_ptr_table_t materials;
    vscn_ptr_table_t textures;
    vscn_ptr_table_t cubemaps;
} vscn_save_context_t;

/// @brief Base64 alphabet table used by the encoder.
static const char vscn_base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// @brief Encode `len` raw bytes to a freshly-allocated NUL-terminated base64 string.
///
/// Output length is `((len + 2) / 3) * 4` characters. Trailing
/// `=` padding is appended for inputs whose length is not a
/// multiple of 3. Caller owns the buffer (`free`); writes the
/// length sans NUL into `*out_len` if non-NULL.
static char *vscn_base64_encode(const uint8_t *data, size_t len, size_t *out_len) {
    if (!data && len > 0)
        return NULL;
    if (len > SIZE_MAX - 2)
        return NULL;
    size_t groups = (len + 2) / 3;
    if (groups > (SIZE_MAX - 1) / 4)
        return NULL;
    size_t olen = groups * 4;
    char *output = (char *)malloc(olen + 1);
    if (!output)
        return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = data[i++];
        uint32_t octet_b = (i < len) ? data[i++] : 0;
        uint32_t octet_c = (i < len) ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output[j++] = vscn_base64_chars[(triple >> 18) & 0x3F];
        output[j++] = vscn_base64_chars[(triple >> 12) & 0x3F];
        output[j++] = vscn_base64_chars[(triple >> 6) & 0x3F];
        output[j++] = vscn_base64_chars[triple & 0x3F];
    }

    {
        size_t padding = (3 - (len % 3)) % 3;
        for (size_t p = 0; p < padding; p++)
            output[j - 1 - p] = '=';
    }

    output[j] = '\0';
    if (out_len)
        *out_len = j;
    return output;
}

/// @brief Reset a pointer table — free its backing array and zero counts.
static void vscn_free_ptr_table(vscn_ptr_table_t *table) {
    if (!table)
        return;
    free(table->items);
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

/// @brief Return the index of `item` in the table, inserting it if not present.
///
/// Provides O(n) interning of asset pointers so the saver can
/// emit each shared mesh / material / texture exactly once and
/// reference it by index from elsewhere in the JSON.
/// @return Existing or newly-assigned index, or -1 on alloc failure / NULL inputs.
static int vscn_ptr_table_index_or_add(vscn_ptr_table_t *table, void *item) {
    if (!table || !item)
        return -1;
    for (int32_t i = 0; i < table->count; i++) {
        if (table->items[i] == item)
            return i;
    }
    if (table->count >= table->capacity) {
        if (table->capacity > INT32_MAX / 2)
            return -1;
        int32_t new_cap = table->capacity == 0 ? 8 : table->capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(void *))
            return -1;
        void **new_items = (void **)realloc(table->items, (size_t)new_cap * sizeof(void *));
        if (!new_items)
            return -1;
        table->items = new_items;
        table->capacity = new_cap;
    }
    table->items[table->count] = item;
    table->count++;
    return table->count - 1;
}

/// @brief Write `depth` levels of two-space indentation into `indent` (NUL-terminated).
static void vscn_make_indent(char *indent, size_t indent_cap, int depth) {
    size_t count;
    if (!indent || indent_cap == 0)
        return;
    count = (size_t)(depth * 2);
    if (count >= indent_cap)
        count = indent_cap - 1;
    memset(indent, ' ', count);
    indent[count] = '\0';
}

/// @brief Ensure `*buf` has at least `needed` bytes of capacity, doubling (starting at
///   4096) until it fits. @return 1 on success, 0 on alloc failure.
static int vscn_reserve(char **buf, size_t *cap, size_t needed) {
    char *nb;
    size_t new_cap;
    if (!buf || !cap)
        return 0;
    if (*cap >= needed)
        return 1;
    new_cap = *cap == 0 ? 4096u : *cap;
    while (new_cap < needed) {
        size_t doubled;
        if (new_cap > SIZE_MAX / 2) {
            new_cap = needed;
            break;
        }
        doubled = new_cap * 2u;
        new_cap = doubled < needed ? needed : doubled;
    }
    nb = (char *)realloc(*buf, new_cap);
    if (!nb)
        return 0;
    *buf = nb;
    *cap = new_cap;
    return 1;
}

/// @brief Append `src_len` raw bytes to the growing `*buf` (reserving capacity first and
///   keeping the buffer NUL-terminated). @return 1 on success, 0 on overflow/alloc failure.
static int vscn_append_raw(char **buf, size_t *len, size_t *cap, const char *src, size_t src_len) {
    if (!buf || !len || !cap || (!src && src_len > 0))
        return 0;
    if (src_len > SIZE_MAX - *len - 1)
        return 0;
    if (!vscn_reserve(buf, cap, *len + src_len + 1))
        return 0;
    if (src_len > 0)
        memcpy(*buf + *len, src, src_len);
    *len += src_len;
    (*buf)[*len] = '\0';
    return 1;
}

/// @brief Append a formatted string to a growable byte buffer.
/// @details Formats directly into the buffer and retries after growing it if
///   the current capacity is too small. Avoids `vsnprintf(NULL, 0)` because
///   that path corrupts floating-point varargs on MSVC ARM64.
/// @return 1 on success, 0 if `vsnprintf` fails or realloc is denied.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
static int vscn_append(char **buf, size_t *len, size_t *cap, const char *fmt, ...) {
    if (!buf || !len || !cap || !fmt)
        return 0;
    for (;;) {
        va_list ap;
        int written;
        size_t available;

        if (*len > SIZE_MAX - 1)
            return 0;
        if (!vscn_reserve(buf, cap, *len + 1))
            return 0;

        available = *cap - *len;
        va_start(ap, fmt);
        written = vsnprintf(*buf + *len, available, fmt, ap);
        va_end(ap);
        if (written < 0)
            return 0;
        if ((size_t)written < available) {
            *len += (size_t)written;
            return 1;
        }

        if ((size_t)written > SIZE_MAX - *len - 1)
            return 0;
        if (!vscn_reserve(buf, cap, *len + (size_t)written + 1))
            return 0;
    }
}

/// @brief Append `text` to the buffer wrapped in JSON quotes with proper escapes.
///
/// Emits the standard backslash escapes (`\"`, `\\`, `\b`, `\f`,
/// `\n`, `\r`, `\t`) for ASCII control characters and `\u00XX`
/// for any other sub-0x20 byte. Bytes >= 0x20 are passed through
/// verbatim — JSON allows raw UTF-8.
static int vscn_append_json_string(char **buf, size_t *len, size_t *cap, const char *text) {
    if (!vscn_append_raw(buf, len, cap, "\"", 1))
        return 0;
    if (text) {
        for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
            char unicode_escape[7];
            switch (*p) {
                case '\"':
                    if (!vscn_append_raw(buf, len, cap, "\\\"", 2))
                        return 0;
                    break;
                case '\\':
                    if (!vscn_append_raw(buf, len, cap, "\\\\", 2))
                        return 0;
                    break;
                case '\b':
                    if (!vscn_append_raw(buf, len, cap, "\\b", 2))
                        return 0;
                    break;
                case '\f':
                    if (!vscn_append_raw(buf, len, cap, "\\f", 2))
                        return 0;
                    break;
                case '\n':
                    if (!vscn_append_raw(buf, len, cap, "\\n", 2))
                        return 0;
                    break;
                case '\r':
                    if (!vscn_append_raw(buf, len, cap, "\\r", 2))
                        return 0;
                    break;
                case '\t':
                    if (!vscn_append_raw(buf, len, cap, "\\t", 2))
                        return 0;
                    break;
                default:
                    if (*p < 0x20u) {
                        snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04x", (unsigned)*p);
                        if (!vscn_append_raw(buf, len, cap, unicode_escape, 6))
                            return 0;
                    } else if (!vscn_append_raw(buf, len, cap, (const char *)p, 1)) {
                        return 0;
                    }
                    break;
            }
        }
    }
    return vscn_append_raw(buf, len, cap, "\"", 1);
}

/// @brief First pass: register every texture / cubemap referenced by `material` in the dedupe
/// tables.
///
/// The save algorithm runs collection over the whole scene before
/// any serialisation so each shared asset gets a stable index
/// referenced from every material that uses it.
static rt_pixels_impl *vscn_material_texture_pixels(void *texture_ref) {
    void *pixels = rt_material3d_resolve_texture_pixels(texture_ref);
    return rt_pixels_checked_impl_or_null(pixels);
}

/// @brief True if a cubemap has all six faces present and square at its declared face size
///   (i.e. it can be serialized into a .vscn asset).
static int vscn_cubemap_is_serializable(rt_cubemap3d *cubemap) {
    if (!rt_g3d_has_class(cubemap, RT_G3D_CUBEMAP3D_CLASS_ID) || cubemap->face_size <= 0 ||
        cubemap->face_size > INT32_MAX)
        return 0;
    for (int i = 0; i < 6; i++) {
        rt_pixels_impl *face = rt_pixels_checked_impl_or_null(cubemap->faces[i]);
        if (!face || face->width != cubemap->face_size || face->height != cubemap->face_size)
            return 0;
    }
    return 1;
}

/// @brief Return a material's environment cubemap only when present and serializable, else NULL.
static rt_cubemap3d *vscn_material_env_map(rt_material3d *material) {
    rt_cubemap3d *cubemap = material ? (rt_cubemap3d *)material->env_map : NULL;
    return vscn_cubemap_is_serializable(cubemap) ? cubemap : NULL;
}

/// @brief Register a material's referenced textures and environment cubemap into the save
///   context's dedup tables; returns 0 on allocation failure, 1 on success (or nothing to do).
static int vscn_collect_material_assets(rt_material3d *material, vscn_save_context_t *ctx) {
    rt_pixels_impl *texture;
    rt_cubemap3d *cubemap;
    if (!material || !ctx)
        return 1;
    texture = vscn_material_texture_pixels(material->texture);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    texture = vscn_material_texture_pixels(material->normal_map);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    texture = vscn_material_texture_pixels(material->specular_map);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    texture = vscn_material_texture_pixels(material->emissive_map);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    texture = vscn_material_texture_pixels(material->metallic_roughness_map);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    texture = vscn_material_texture_pixels(material->ao_map);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    cubemap = vscn_material_env_map(material);
    if (cubemap) {
        if (vscn_ptr_table_index_or_add(&ctx->cubemaps, cubemap) < 0)
            return 0;
        for (int i = 0; i < 6; i++) {
            if (cubemap->faces[i] &&
                vscn_ptr_table_index_or_add(&ctx->textures, cubemap->faces[i]) < 0)
                return 0;
        }
    }
    return 1;
}

/// @brief Collect mesh / material / texture references from a node subtree without recursion.
static int vscn_collect_node_assets(rt_scene_node3d *node, vscn_save_context_t *ctx) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!node || !ctx)
        return 1;
    for (;;) {
        size_t new_capacity = 64u;
        stack = (rt_scene_node3d **)malloc(new_capacity * sizeof(*stack));
        if (!stack)
            return 0;
        capacity = new_capacity;
        stack[count++] = node;
        break;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        if (!current)
            continue;
        if (rt_g3d_has_class(current->mesh, RT_G3D_MESH3D_CLASS_ID) &&
            vscn_ptr_table_index_or_add(&ctx->meshes, current->mesh) < 0) {
            free(stack);
            return 0;
        }
        if (rt_g3d_has_class(current->material, RT_G3D_MATERIAL3D_CLASS_ID)) {
            if (vscn_ptr_table_index_or_add(&ctx->materials, current->material) < 0 ||
                !vscn_collect_material_assets((rt_material3d *)current->material, ctx)) {
                free(stack);
                return 0;
            }
        }
        for (int32_t i = 0, lod_count = scene3d_node_lod_count(current); i < lod_count; i++) {
            if (rt_g3d_has_class(current->lod_levels[i].mesh, RT_G3D_MESH3D_CLASS_ID) &&
                vscn_ptr_table_index_or_add(&ctx->meshes, current->lod_levels[i].mesh) < 0) {
                free(stack);
                return 0;
            }
        }
        for (int32_t i = 0, child_count = scene3d_node_child_count(current); i < child_count; i++) {
            rt_scene_node3d *child = scene_node3d_checked(current->children[i]);
            if (!child)
                continue;
            if (count >= capacity) {
                size_t new_capacity = capacity * 2u;
                rt_scene_node3d **grown;
                if (new_capacity <= capacity ||
                    new_capacity > SIZE_MAX / sizeof(rt_scene_node3d *)) {
                    free(stack);
                    return 0;
                }
                grown =
                    (rt_scene_node3d **)realloc(stack, new_capacity * sizeof(rt_scene_node3d *));
                if (!grown) {
                    free(stack);
                    return 0;
                }
                stack = grown;
                capacity = new_capacity;
            }
            stack[count++] = child;
        }
    }
    free(stack);
    return 1;
}

/// @brief Emit a Pixels object as a `{"width":…, "height":…, "data":"<base64>"}` JSON entry.
static int vscn_serialize_texture(
    rt_pixels_impl *pixels, char **buf, size_t *len, size_t *cap, int depth) {
    char indent[64];
    size_t pixel_count;
    size_t rgba_len;
    uint8_t *rgba = NULL;
    char *rgba_base64 = NULL;

    if (!pixels)
        return 0;
    if (pixels->width < 0 || pixels->height < 0)
        return 0;
    if ((size_t)pixels->width > SIZE_MAX / (size_t)(pixels->height > 0 ? pixels->height : 1))
        return 0;

    vscn_make_indent(indent, sizeof(indent), depth);
    pixel_count = (size_t)pixels->width * (size_t)pixels->height;
    if (pixel_count > SIZE_MAX / 4)
        return 0;
    if (pixel_count > 0 && !pixels->data)
        return 0;
    rgba_len = pixel_count * 4;
    rgba = (uint8_t *)malloc(rgba_len > 0 ? rgba_len : 1);
    if (!rgba)
        return 0;

    for (size_t i = 0; i < pixel_count; i++) {
        uint32_t px = pixels->data[i];
        rgba[i * 4 + 0] = (uint8_t)((px >> 24) & 0xFF);
        rgba[i * 4 + 1] = (uint8_t)((px >> 16) & 0xFF);
        rgba[i * 4 + 2] = (uint8_t)((px >> 8) & 0xFF);
        rgba[i * 4 + 3] = (uint8_t)(px & 0xFF);
    }

    rgba_base64 = vscn_base64_encode(rgba, rgba_len, NULL);
    free(rgba);
    if (!rgba_base64)
        return 0;

    {
        int ok = vscn_append(buf,
                             len,
                             cap,
                             "%s{\"width\": %lld, \"height\": %lld, \"rgbaBase64\": ",
                             indent,
                             (long long)pixels->width,
                             (long long)pixels->height) &&
                 vscn_append_json_string(buf, len, cap, rgba_base64) &&
                 vscn_append(buf, len, cap, "}");
        free(rgba_base64);
        return ok;
    }
}

/// @brief Emit a cubemap as six texture-index references (px/nx/py/ny/pz/nz).
static int vscn_serialize_cubemap(rt_cubemap3d *cubemap,
                                  vscn_save_context_t *ctx,
                                  char **buf,
                                  size_t *len,
                                  size_t *cap,
                                  int depth) {
    char indent[64];
    if (!cubemap || !ctx)
        return 0;
    if (!vscn_cubemap_is_serializable(cubemap))
        return 0;
    vscn_make_indent(indent, sizeof(indent), depth);
    if (!vscn_append(buf, len, cap, "%s{\"faces\": [", indent))
        return 0;
    for (int i = 0; i < 6; i++) {
        if (!cubemap->faces[i])
            return 0;
        int index = vscn_ptr_table_index_or_add(&ctx->textures, cubemap->faces[i]);
        if (index < 0)
            return 0;
        if (!vscn_append(buf, len, cap, "%s%d", i == 0 ? "" : ", ", index))
            return 0;
    }
    return vscn_append(buf, len, cap, "]}");
}

/// @brief Emit a material as JSON: PBR parameters + texture-index references for each map.
static int vscn_serialize_material(rt_material3d *material,
                                   vscn_save_context_t *ctx,
                                   char **buf,
                                   size_t *len,
                                   size_t *cap,
                                   int depth) {
    char indent[64];
    if (!material || !ctx)
        return 0;
    vscn_make_indent(indent, sizeof(indent), depth);

    const int texture_index = vscn_ptr_table_index_or_add(
        &ctx->textures, vscn_material_texture_pixels(material->texture));
    const int normal_index = vscn_ptr_table_index_or_add(
        &ctx->textures, vscn_material_texture_pixels(material->normal_map));
    const int specular_index = vscn_ptr_table_index_or_add(
        &ctx->textures, vscn_material_texture_pixels(material->specular_map));
    const int emissive_index = vscn_ptr_table_index_or_add(
        &ctx->textures, vscn_material_texture_pixels(material->emissive_map));
    const int metallic_roughness_index = vscn_ptr_table_index_or_add(
        &ctx->textures, vscn_material_texture_pixels(material->metallic_roughness_map));
    const int ao_index =
        vscn_ptr_table_index_or_add(&ctx->textures, vscn_material_texture_pixels(material->ao_map));
    const int env_index =
        vscn_ptr_table_index_or_add(&ctx->cubemaps, vscn_material_env_map(material));

    if (!vscn_append(buf,
                     len,
                     cap,
                     "%s{\"diffuse\": [%.17g, %.17g, %.17g, %.17g], ",
                     indent,
                     vscn_clamp_or(material->diffuse[0], 1.0, 0.0, 1.0),
                     vscn_clamp_or(material->diffuse[1], 1.0, 0.0, 1.0),
                     vscn_clamp_or(material->diffuse[2], 1.0, 0.0, 1.0),
                     vscn_clamp_or(material->diffuse[3], 1.0, 0.0, 1.0)) ||
        !vscn_append(
            buf,
            len,
            cap,
            "\"specular\": [%.17g, %.17g, %.17g], \"shininess\": %.17g, "
            "\"workflow\": %d, ",
            vscn_clamp_or(material->specular[0], 1.0, 0.0, VSCN_ABS_MAX),
            vscn_clamp_or(material->specular[1], 1.0, 0.0, VSCN_ABS_MAX),
            vscn_clamp_or(material->specular[2], 1.0, 0.0, VSCN_ABS_MAX),
            vscn_nonnegative_or(material->shininess, 32.0),
            vscn_material_workflow_or(material->workflow, RT_MATERIAL3D_WORKFLOW_LEGACY)) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "\"emissive\": [%.17g, %.17g, %.17g], \"metallic\": %.17g, "
                     "\"roughness\": %.17g, \"ao\": %.17g, ",
                     vscn_clamp_or(material->emissive[0], 0.0, 0.0, VSCN_ABS_MAX),
                     vscn_clamp_or(material->emissive[1], 0.0, 0.0, VSCN_ABS_MAX),
                     vscn_clamp_or(material->emissive[2], 0.0, 0.0, VSCN_ABS_MAX),
                     vscn_clamp_or(material->metallic, 0.0, 0.0, 1.0),
                     vscn_clamp_or(material->roughness, 0.5, 0.0, 1.0),
                     vscn_clamp_or(material->ao, 1.0, 0.0, 1.0)) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "\"emissiveIntensity\": %.17g, \"normalScale\": %.17g, "
                     "\"alpha\": %.17g, \"alphaMode\": %d, \"alphaCutoff\": %.17g, ",
                     vscn_nonnegative_or(material->emissive_intensity, 1.0),
                     vscn_nonnegative_or(material->normal_scale, 1.0),
                     vscn_clamp_or(material->alpha, 1.0, 0.0, 1.0),
                     vscn_alpha_mode_or(material->alpha_mode, RT_MATERIAL3D_ALPHA_MODE_OPAQUE),
                     vscn_clamp_or(material->alpha_cutoff, 0.5, 0.0, 1.0)) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "\"doubleSided\": %s, \"reflectivity\": %.17g, \"unlit\": %s, "
                     "\"shadingModel\": %d, ",
                     material->double_sided ? "true" : "false",
                     vscn_clamp_or(material->reflectivity, 0.0, 0.0, 1.0),
                     material->unlit ? "true" : "false",
                     (material->shading_model >= 0 && material->shading_model <= 5)
                         ? material->shading_model
                         : 0) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "\"texture\": %d, \"normalMap\": %d, \"specularMap\": %d, "
                     "\"emissiveMap\": %d, \"metallicRoughnessMap\": %d, \"aoMap\": %d, "
                     "\"envMap\": %d, \"customParams\": [",
                     texture_index,
                     normal_index,
                     specular_index,
                     emissive_index,
                     metallic_roughness_index,
                     ao_index,
                     env_index)) {
        return 0;
    }

    for (int i = 0; i < 8; i++) {
        if (!vscn_append(buf,
                         len,
                         cap,
                         "%s%.17g",
                         i == 0 ? "" : ", ",
                         vscn_clamp_abs_or(material->custom_params[i], 0.0))) {
            return 0;
        }
    }
    if (!vscn_append(buf, len, cap, "], \"textureSlots\": ["))
        return 0;
    for (int i = 0; i < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; i++) {
        const double *uvm = material->texture_slot_uv_transform[i];
        if (!vscn_append(
                buf,
                len,
                cap,
                "%s{\"uvSet\": %d, \"wrapS\": %d, \"wrapT\": %d, "
                "\"filter\": %d, \"uvTransform\": [%.17g, %.17g, %.17g, %.17g, %.17g, %.17g]}",
                i == 0 ? "" : ", ",
                material->texture_slot_uv_set[i] > 0 ? 1 : 0,
                vscn_wrap_or(material->texture_slot_wrap_s[i], RT_MATERIAL3D_TEXTURE_WRAP_REPEAT),
                vscn_wrap_or(material->texture_slot_wrap_t[i], RT_MATERIAL3D_TEXTURE_WRAP_REPEAT),
                vscn_filter_or(material->texture_slot_filter[i],
                               RT_MATERIAL3D_TEXTURE_FILTER_LINEAR),
                vscn_clamp_abs_or(uvm[0], 1.0),
                vscn_clamp_abs_or(uvm[1], 0.0),
                vscn_clamp_abs_or(uvm[2], 0.0),
                vscn_clamp_abs_or(uvm[3], 1.0),
                vscn_clamp_abs_or(uvm[4], 0.0),
                vscn_clamp_abs_or(uvm[5], 0.0))) {
            return 0;
        }
    }
    return vscn_append(buf, len, cap, "]}");
}

/// @brief Emit a mesh as JSON: vertex/index buffers as base64 + the layout descriptor.
static int vscn_serialize_mesh(rt_mesh3d *mesh, char **buf, size_t *len, size_t *cap, int depth) {
    char indent[64];
    size_t vertex_bytes_len;
    size_t index_bytes_len;
    char *vertex_base64 = NULL;
    char *index_base64 = NULL;

    if (!mesh)
        return 0;
    if (mesh->vertex_count > 0 && !mesh->vertices)
        return 0;
    if (mesh->index_count > 0 && !mesh->indices)
        return 0;
    if ((size_t)mesh->vertex_count > SIZE_MAX / sizeof(vgfx3d_vertex_t) ||
        (size_t)mesh->index_count > SIZE_MAX / sizeof(uint32_t))
        return 0;
    vertex_bytes_len = (size_t)mesh->vertex_count * sizeof(vgfx3d_vertex_t);
    index_bytes_len = (size_t)mesh->index_count * sizeof(uint32_t);
    vscn_make_indent(indent, sizeof(indent), depth);
    vertex_base64 = vscn_base64_encode((const uint8_t *)mesh->vertices, vertex_bytes_len, NULL);
    index_base64 = vscn_base64_encode((const uint8_t *)mesh->indices, index_bytes_len, NULL);
    if (!vertex_base64 || !index_base64) {
        free(vertex_base64);
        free(index_base64);
        return 0;
    }

    {
        int ok = vscn_append(buf,
                             len,
                             cap,
                             "%s{\"vertexFormat\": \"vgfx3d_vertex_le_v2\", "
                             "\"vertexCount\": %u, "
                             "\"indexCount\": %u, "
                             "\"boneCount\": %d, "
                             "\"resident\": %s, "
                             "\"verticesBase64\": ",
                             indent,
                             mesh->vertex_count,
                             mesh->index_count,
                             mesh->bone_count,
                             rt_mesh3d_get_resident(mesh) ? "true" : "false") &&
                 vscn_append_json_string(buf, len, cap, vertex_base64) &&
                 vscn_append(buf, len, cap, ", \"indicesBase64\": ") &&
                 vscn_append_json_string(buf, len, cap, index_base64) &&
                 vscn_append(buf, len, cap, "}");
        free(vertex_base64);
        free(index_base64);
        return ok;
    }
}

/// @brief Recursively emit a scene node and its children as nested JSON objects.
///
/// Each node carries name, transform (TRS), mesh-index +
/// material-index references, and a children array. References
/// use the dedupe-table indices populated by the asset-collection pass.
static int vscn_serialize_node(rt_scene_node3d *node,
                               vscn_save_context_t *ctx,
                               char **buf,
                               size_t *len,
                               size_t *cap,
                               int depth) {
    if (!node)
        return 1;
    if (depth > VSCN_MAX_NODE_DEPTH * 2)
        return 0;
    char indent[64];
    vscn_make_indent(indent, sizeof(indent), depth);

    const char *name = node->name ? rt_string_cstr(node->name) : "node";
    if (!name)
        name = "node";
    double rotation[4] = {
        node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]};
    vscn_normalize_quat(rotation);
    int mesh_index = node->mesh ? vscn_ptr_table_index_or_add(&ctx->meshes, node->mesh) : -1;
    int material_index =
        node->material ? vscn_ptr_table_index_or_add(&ctx->materials, node->material) : -1;
    if ((node->mesh && mesh_index < 0) || (node->material && material_index < 0))
        return 0;

    if (!vscn_append(buf, len, cap, "%s{\n", indent) ||
        !vscn_append(buf, len, cap, "%s  \"name\": ", indent) ||
        !vscn_append_json_string(buf, len, cap, name) ||
        !vscn_append(buf,
                     len,
                     cap,
                     ",\n%s  \"position\": [%.17g, %.17g, %.17g],\n",
                     indent,
                     vscn_clamp_abs_or(node->position[0], 0.0),
                     vscn_clamp_abs_or(node->position[1], 0.0),
                     vscn_clamp_abs_or(node->position[2], 0.0)) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "%s  \"rotation\": [%.17g, %.17g, %.17g, %.17g],\n",
                     indent,
                     rotation[0],
                     rotation[1],
                     rotation[2],
                     rotation[3]) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "%s  \"scale\": [%.17g, %.17g, %.17g],\n",
                     indent,
                     vscn_clamp_abs_or(node->scale_xyz[0], 1.0),
                     vscn_clamp_abs_or(node->scale_xyz[1], 1.0),
                     vscn_clamp_abs_or(node->scale_xyz[2], 1.0)) ||
        !vscn_append(
            buf, len, cap, "%s  \"visible\": %s,\n", indent, node->visible ? "true" : "false") ||
        !vscn_append(
            buf, len, cap, "%s  \"hasMesh\": %s,\n", indent, node->mesh ? "true" : "false") ||
        !vscn_append(buf,
                     len,
                     cap,
                     "%s  \"hasMaterial\": %s,\n",
                     indent,
                     node->material ? "true" : "false") ||
        !vscn_append(buf,
                     len,
                     cap,
                     "%s  \"mesh\": %d,\n%s  \"material\": %d",
                     indent,
                     mesh_index,
                     indent,
                     material_index)) {
        return 0;
    }

    if (node->light) {
        rt_light3d *light = (rt_light3d *)node->light;
        double light_dir[3] = {vscn_clamp_abs_or(light->direction[0], 0.0),
                               vscn_clamp_abs_or(light->direction[1], 0.0),
                               vscn_clamp_abs_or(light->direction[2], -1.0)};
        vscn_normalize_vec3(light_dir, 0.0, 0.0, -1.0);
        if (!vscn_append(buf,
                         len,
                         cap,
                         ",\n%s  \"light\": {\"type\": %d, "
                         "\"direction\": [%.17g, %.17g, %.17g], "
                         "\"position\": [%.17g, %.17g, %.17g], "
                         "\"color\": [%.17g, %.17g, %.17g], "
                         "\"intensity\": %.17g, \"attenuation\": %.17g, "
                         "\"innerCos\": %.17g, \"outerCos\": %.17g, "
                         "\"castsShadows\": %s}",
                         indent,
                         (light->type >= 0 && light->type <= 3) ? light->type : 1,
                         light_dir[0],
                         light_dir[1],
                         light_dir[2],
                         vscn_clamp_abs_or(light->position[0], 0.0),
                         vscn_clamp_abs_or(light->position[1], 0.0),
                         vscn_clamp_abs_or(light->position[2], 0.0),
                         vscn_clamp_or(light->color[0], 1.0, 0.0, VSCN_ABS_MAX),
                         vscn_clamp_or(light->color[1], 1.0, 0.0, VSCN_ABS_MAX),
                         vscn_clamp_or(light->color[2], 1.0, 0.0, VSCN_ABS_MAX),
                         vscn_nonnegative_or(light->intensity, 1.0),
                         vscn_nonnegative_or(light->attenuation, 0.0),
                         vscn_clamp_or(light->inner_cos, 1.0, 0.0, 1.0),
                         vscn_clamp_or(light->outer_cos, 0.7071067811865476, 0.0, 1.0),
                         light->casts_shadows ? "true" : "false")) {
            return 0;
        }
    }

    int32_t lod_count = scene3d_node_lod_count(node);
    if (lod_count > 0) {
        if (!vscn_append(buf, len, cap, ",\n%s  \"lod\": [\n", indent))
            return 0;
        for (int32_t i = 0; i < lod_count; i++) {
            void *lod_mesh = rt_g3d_has_class(node->lod_levels[i].mesh, RT_G3D_MESH3D_CLASS_ID)
                                 ? node->lod_levels[i].mesh
                                 : NULL;
            int lod_mesh_index =
                lod_mesh ? vscn_ptr_table_index_or_add(&ctx->meshes, lod_mesh) : -1;
            if (lod_mesh && lod_mesh_index < 0)
                return 0;
            if (!vscn_append(buf,
                             len,
                             cap,
                             "%s    {\"distance\": %.17g, \"hasMesh\": %s, \"mesh\": %d}%s\n",
                             indent,
                             vscn_nonnegative_or(node->lod_levels[i].distance, 0.0),
                             lod_mesh ? "true" : "false",
                             lod_mesh_index,
                             i + 1 < lod_count ? "," : "")) {
                return 0;
            }
        }
        if (!vscn_append(buf, len, cap, "%s  ]", indent))
            return 0;
    }

    if (node->auto_lod_enabled) {
        if (!vscn_append(buf,
                         len,
                         cap,
                         ",\n%s  \"autoLOD\": {\"enabled\": true, \"screenErrorPx\": %.17g}",
                         indent,
                         vscn_nonnegative_or(node->auto_lod_screen_error_px, 8.0))) {
            return 0;
        }
    }

    int32_t child_count = scene3d_node_child_count(node);
    if (child_count > 0) {
        int32_t emitted = 0;
        if (!vscn_append(buf, len, cap, ",\n%s  \"children\": [\n", indent))
            return 0;
        for (int32_t i = 0; i < child_count; i++) {
            rt_scene_node3d *child = scene_node3d_checked(node->children[i]);
            if (!child)
                continue;
            if (emitted > 0 && !vscn_append(buf, len, cap, ","))
                return 0;
            if (!vscn_serialize_node(child, ctx, buf, len, cap, depth + 2))
                return 0;
            emitted++;
            if (!vscn_append(buf, len, cap, "\n"))
                return 0;
        }
        if (!vscn_append(buf, len, cap, "%s  ]", indent))
            return 0;
    }

    return vscn_append(buf, len, cap, "\n%s}", indent);
}

/// @brief Public API: save a Scene3D to a `.vscn` file at `path`.
///
/// Two-pass algorithm: (1) walk the scene to populate the mesh /
/// material / texture / cubemap dedupe tables, (2) emit the JSON
/// document with shared assets first, then nodes referencing them
/// by index. Output is pretty-printed for diff-friendliness.
/// @return 1 on success, 0 on any failure (open, alloc, write).
/// @brief Free the four shared-asset pointer tables collected for a save (every failure exit).
static void vscn_save_free_ctx(vscn_save_context_t *ctx) {
    vscn_free_ptr_table(&ctx->meshes);
    vscn_free_ptr_table(&ctx->materials);
    vscn_free_ptr_table(&ctx->textures);
    vscn_free_ptr_table(&ctx->cubemaps);
}

/// @brief Emit the `"textures": [ ... ],` array. @return 1 on success, 0 on append failure.
static int vscn_save_emit_textures(char **buf, size_t *len, size_t *cap, vscn_save_context_t *ctx) {
    if (!vscn_append(buf, len, cap, "  \"textures\": [\n"))
        return 0;
    for (int32_t i = 0; i < ctx->textures.count; i++) {
        if (!vscn_serialize_texture((rt_pixels_impl *)ctx->textures.items[i], buf, len, cap, 2) ||
            (i < ctx->textures.count - 1 && !vscn_append(buf, len, cap, ",")) ||
            !vscn_append(buf, len, cap, "\n"))
            return 0;
    }
    return vscn_append(buf, len, cap, "  ],\n");
}

/// @brief Emit the `"cubemaps": [ ... ],` array. @return 1 on success, 0 on append failure.
static int vscn_save_emit_cubemaps(char **buf, size_t *len, size_t *cap, vscn_save_context_t *ctx) {
    if (!vscn_append(buf, len, cap, "  \"cubemaps\": [\n"))
        return 0;
    for (int32_t i = 0; i < ctx->cubemaps.count; i++) {
        if (!vscn_serialize_cubemap(
                (rt_cubemap3d *)ctx->cubemaps.items[i], ctx, buf, len, cap, 2) ||
            (i < ctx->cubemaps.count - 1 && !vscn_append(buf, len, cap, ",")) ||
            !vscn_append(buf, len, cap, "\n"))
            return 0;
    }
    return vscn_append(buf, len, cap, "  ],\n");
}

/// @brief Emit the `"materials": [ ... ],` array. @return 1 on success, 0 on append failure.
static int vscn_save_emit_materials(char **buf,
                                    size_t *len,
                                    size_t *cap,
                                    vscn_save_context_t *ctx) {
    if (!vscn_append(buf, len, cap, "  \"materials\": [\n"))
        return 0;
    for (int32_t i = 0; i < ctx->materials.count; i++) {
        if (!vscn_serialize_material(
                (rt_material3d *)ctx->materials.items[i], ctx, buf, len, cap, 2) ||
            (i < ctx->materials.count - 1 && !vscn_append(buf, len, cap, ",")) ||
            !vscn_append(buf, len, cap, "\n"))
            return 0;
    }
    return vscn_append(buf, len, cap, "  ],\n");
}

/// @brief Emit the `"meshes": [ ... ],` array. @return 1 on success, 0 on append failure.
static int vscn_save_emit_meshes(char **buf, size_t *len, size_t *cap, vscn_save_context_t *ctx) {
    if (!vscn_append(buf, len, cap, "  \"meshes\": [\n"))
        return 0;
    for (int32_t i = 0; i < ctx->meshes.count; i++) {
        if (!vscn_serialize_mesh((rt_mesh3d *)ctx->meshes.items[i], buf, len, cap, 2) ||
            (i < ctx->meshes.count - 1 && !vscn_append(buf, len, cap, ",")) ||
            !vscn_append(buf, len, cap, "\n"))
            return 0;
    }
    return vscn_append(buf, len, cap, "  ],\n");
}

/// @brief Emit the closing `"nodes": [ ... ]` array (root's children; root is implicit).
/// @return 1 on success, 0 on append failure.
static int vscn_save_emit_nodes(
    char **buf, size_t *len, size_t *cap, vscn_save_context_t *ctx, rt_scene_node3d *root) {
    int32_t child_count = scene3d_node_child_count(root);
    int32_t emitted = 0;
    if (!vscn_append(buf, len, cap, "  \"nodes\": [\n"))
        return 0;
    for (int32_t i = 0; i < child_count; i++) {
        if (!root->children[i])
            continue;
        rt_scene_node3d *child = scene_node3d_checked(root->children[i]);
        if (!child)
            continue;
        if (emitted > 0 && !vscn_append(buf, len, cap, ","))
            return 0;
        if (!vscn_serialize_node(child, ctx, buf, len, cap, 2) || !vscn_append(buf, len, cap, "\n"))
            return 0;
        emitted++;
    }
    return vscn_append(buf, len, cap, "  ]\n");
}

/// @brief Serialize the scene to a .vscn file. @return 1 on success, 0 on failure.
int64_t rt_scene3d_save(void *scene_obj, rt_string path) {
    if (!scene_obj || !path)
        return 0;
    rt_scene3d *scene = (rt_scene3d *)rt_g3d_checked_or_null(scene_obj, RT_G3D_SCENE3D_CLASS_ID);
    if (!scene)
        return 0;
    if (!scene->root)
        return 0;

    const char *filepath = rt_string_cstr(path);
    if (!filepath)
        return 0;

    vscn_save_context_t ctx = {0};
    char *buf = NULL;
    size_t len = 0, cap = 0;
    FILE *f = NULL;
    char *tmp_path = NULL;
    size_t written = 0;
    int64_t result = 0;

    for (int32_t i = 0, child_count = scene3d_node_child_count(scene->root); i < child_count; i++) {
        if (!scene->root->children[i])
            continue;
        rt_scene_node3d *child = scene_node3d_checked(scene->root->children[i]);
        if (!child)
            continue;
        if (!vscn_collect_node_assets(child, &ctx)) {
            vscn_save_free_ctx(&ctx);
            return 0;
        }
    }

    if (!vscn_append(&buf, &len, &cap, "{\n") ||
        !vscn_append(&buf, &len, &cap, "  \"format\": \"vscn\",\n") ||
        !vscn_append(&buf, &len, &cap, "  \"version\": 2,\n") ||
        !vscn_save_emit_textures(&buf, &len, &cap, &ctx) ||
        !vscn_save_emit_cubemaps(&buf, &len, &cap, &ctx) ||
        !vscn_save_emit_materials(&buf, &len, &cap, &ctx) ||
        !vscn_save_emit_meshes(&buf, &len, &cap, &ctx) ||
        !vscn_save_emit_nodes(&buf, &len, &cap, &ctx, scene->root) ||
        !vscn_append(&buf, &len, &cap, "}\n")) {
        vscn_save_free_ctx(&ctx);
        free(buf);
        return 0;
    }

    f = rt_file_stdio_open_temp_for_replace_utf8(filepath, &tmp_path);
    if (!f) {
        vscn_save_free_ctx(&ctx);
        free(buf);
        return 0;
    }
    while (written < len) {
        size_t chunk = fwrite(buf + written, 1, len - written, f);
        if (chunk == 0) {
            fclose(f);
            (void)rt_file_stdio_unlink_utf8(tmp_path);
            free(tmp_path);
            vscn_save_free_ctx(&ctx);
            free(buf);
            return 0;
        }
        written += chunk;
    }
    if (fflush(f) != 0 || fclose(f) != 0) {
        (void)rt_file_stdio_unlink_utf8(tmp_path);
        free(tmp_path);
        vscn_save_free_ctx(&ctx);
        free(buf);
        return 0;
    }
    result = rt_file_stdio_replace_utf8(tmp_path, filepath) ? 1 : 0;
    if (!result)
        (void)rt_file_stdio_unlink_utf8(tmp_path);
    free(tmp_path);
    vscn_save_free_ctx(&ctx);
    free(buf);
    return result;
}

#endif // VIPER_ENABLE_GRAPHICS
