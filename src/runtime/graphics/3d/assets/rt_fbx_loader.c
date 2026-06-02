//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_fbx_loader.c
// Purpose: FBX binary format parser and asset extractor. Parses the binary
//   node tree, resolves connections, and extracts geometry, skeleton,
//   animation, and material data into runtime objects.
//
// Key invariants:
//   - Supports FBX versions 7100-7700 (both 32-bit and 64-bit offsets).
//   - Array properties with zlib encoding: strip 2-byte header + 4-byte
//     Adler-32 trailer, then call rt_compress_inflate on raw DEFLATE.
//   - Negative polygon indices mark end-of-polygon (bitwise NOT to decode).
//   - Coordinate system correction applied if source is Z-up.
//   - Ear-clipping triangulation for quads/n-gons, with fan fallback only for
//     degenerate projected polygons.
//   - Skinning palette is reduced to the top 4 (bone, weight) influences per
//     vertex and renormalized to sum to 1.
//
// Ownership/Lifetime:
//   - rt_fbx_asset is GC-managed; finalizer releases every owned mesh,
//     material, animation, morph target, skeleton, and scene root.
//   - Parser scratch state (node tree, connection table, binding tables,
//     mesh remaps) is freed before returning from rt_fbx_load.
//   - Texture references loaded from disk are released after assignment to
//     the materials that retain them.
//
// Links: rt_fbx_loader.h, plans/3d/15-fbx-loader.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_fbx_loader.h"
#include "rt_bytes.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_compress.h"
#include "rt_gif.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_quat.h"
#include "rt_scene3d.h"
#include "rt_skeleton3d.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*==========================================================================
 * FBX asset container
 *=========================================================================*/

typedef struct {
    void *vptr;
    void **meshes;
    int32_t mesh_count;
    int32_t mesh_capacity;
    void *skeleton;
    void **animations;
    int32_t animation_count;
    int32_t animation_capacity;
    void **materials;
    int32_t material_count;
    int32_t material_capacity;
    void **morph_targets; // rt_morphtarget3d*[] parallel to meshes[]
    int32_t morph_count;
    int32_t morph_capacity;
    void *scene_root;
} rt_fbx_asset;

typedef struct {
    int32_t *triangle_slots;
    uint32_t triangle_count;
    int32_t slot_count;
    int8_t has_slots;
} fbx_mesh_material_map_t;

typedef struct {
    int64_t id;
    void *mesh;
    fbx_mesh_material_map_t material_map;
} fbx_mesh_binding_t;

typedef struct {
    int64_t id;
    void *material;
} fbx_material_binding_t;

typedef struct {
    int32_t *vertices;
    int32_t count;
    int32_t capacity;
} fbx_vertex_index_list_t;

typedef struct {
    int64_t id;
    fbx_vertex_index_list_t *control_vertices;
    int32_t control_count;
} fbx_mesh_remap_t;

typedef struct {
    int32_t bone_indices[4];
    double weights[4];
} fbx_skin_influence_t;

typedef struct {
    int64_t model_id;
    int32_t bone_index;
} fbx_bone_binding_t;

#define FBX_NUMERIC_ABS_MAX 1000000000000.0
#define FBX_UV_ABS_MAX 1000000.0
#define FBX_ROTATION_DEG_ABS_MAX 1000000.0
#define FBX_SKIN_WEIGHT_MAX 1000000.0
#define FBX_ANIM_TIME_SECONDS_MAX 100000000.0
#define FBX_ANIM_CURVE_KEYS_MAX 1000000u
#define FBX_MAX_SKELETON_BONES 256

static void fbx_release_ref(void **slot);

/// @brief Return @p value when finite, else @p fallback (scalar sanitizer).
static double fbx_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Clamp @p value into [lo, hi], substituting @p fallback when non-finite.
static double fbx_clamp_double(double value, double lo, double hi, double fallback) {
    value = fbx_finite_or(value, fallback);
    if (value < lo)
        value = lo;
    if (value > hi)
        value = hi;
    return value;
}

/// @brief Clamp @p value into [-limit, limit], substituting @p fallback when non-finite.
static double fbx_clamp_abs_or(double value, double fallback, double limit) {
    value = fbx_finite_or(value, fallback);
    if (value > limit)
        value = limit;
    if (value < -limit)
        value = -limit;
    return value;
}

/// @brief Sanitize a scale factor to a finite, bounded value, replacing ~zero magnitudes with 1.0.
static double fbx_scale_or_unit(double value) {
    value = fbx_clamp_abs_or(value, 1.0, FBX_NUMERIC_ABS_MAX);
    if (fabs(value) < 1e-12)
        value = 1.0;
    return value;
}

/// @brief Clamp a position triple into the FBX numeric bound; returns 0 (leaving the values
///   untouched) when any lane is non-finite.
static int fbx_sanitize_position3(double *x, double *y, double *z) {
    if (!x || !y || !z || !isfinite(*x) || !isfinite(*y) || !isfinite(*z))
        return 0;
    *x = fbx_clamp_abs_or(*x, 0.0, FBX_NUMERIC_ABS_MAX);
    *y = fbx_clamp_abs_or(*y, 0.0, FBX_NUMERIC_ABS_MAX);
    *z = fbx_clamp_abs_or(*z, 0.0, FBX_NUMERIC_ABS_MAX);
    return 1;
}

/// @brief Normalize a normal triple in place, falling back to +Y when it is non-finite or
///   of ~zero length.
static void fbx_sanitize_normal3(double *x, double *y, double *z) {
    double len2;
    double inv_len;
    if (!x || !y || !z || !isfinite(*x) || !isfinite(*y) || !isfinite(*z)) {
        if (x)
            *x = 0.0;
        if (y)
            *y = 1.0;
        if (z)
            *z = 0.0;
        return;
    }
    len2 = (*x) * (*x) + (*y) * (*y) + (*z) * (*z);
    if (!isfinite(len2) || len2 <= 1e-20) {
        *x = 0.0;
        *y = 1.0;
        *z = 0.0;
        return;
    }
    inv_len = 1.0 / sqrt(len2);
    *x *= inv_len;
    *y *= inv_len;
    *z *= inv_len;
}

/// @brief Clamp a rotation angle in degrees to ±FBX_ROTATION_DEG_ABS_MAX (non-finite → 0).
static double fbx_sanitize_rotation_degrees(double value) {
    return fbx_clamp_abs_or(value, 0.0, FBX_ROTATION_DEG_ABS_MAX);
}

/// @brief Clamp a (count, capacity) pair to a safe element count (0 when invalid, else min).
static int32_t fbx_asset_safe_count(void **items, int32_t count, int32_t capacity) {
    if (!items || count <= 0 || capacity <= 0)
        return 0;
    if (count > capacity)
        return capacity;
    return count;
}

/// @brief Ensure a growable reference array holds @p needed slots, doubling capacity and
///   zero-filling the new slots; returns 0 on overflow or allocation failure.
static int fbx_asset_reserve_ref_array(void ***items, int32_t *capacity, int32_t needed) {
    int32_t old_capacity;
    int32_t new_capacity;
    void **grown;
    if (!items || !capacity || needed < 0)
        return 0;
    if (!*items && *capacity > 0)
        *capacity = 0;
    if (*capacity < 0)
        *capacity = 0;
    if (needed <= *capacity)
        return 1;
    old_capacity = *capacity;
    new_capacity = old_capacity > 0 ? old_capacity : 4;
    while (new_capacity < needed) {
        if (new_capacity > INT32_MAX / 2)
            return 0;
        new_capacity *= 2;
    }
    if ((size_t)new_capacity > SIZE_MAX / sizeof(**items))
        return 0;
    grown = (void **)realloc(*items, (size_t)new_capacity * sizeof(**items));
    if (!grown)
        return 0;
    if (new_capacity > old_capacity)
        memset(grown + old_capacity, 0, (size_t)(new_capacity - old_capacity) * sizeof(*grown));
    *items = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Release every reference in a ref array (over its safe count) and free the backing
///   storage, resetting the count/capacity to zero.
static void fbx_asset_release_ref_array(void ***items, int32_t *count, int32_t *capacity) {
    void **array = items ? *items : NULL;
    int32_t safe_count =
        fbx_asset_safe_count(array, count ? *count : 0, capacity ? *capacity : 0);
    if (array) {
        for (int32_t i = 0; i < safe_count; i++)
            fbx_release_ref(&array[i]);
        free(array);
    }
    if (items)
        *items = NULL;
    if (count)
        *count = 0;
    if (capacity)
        *capacity = 0;
}

/*==========================================================================
 * Binary reader helpers
 *=========================================================================*/

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    uint32_t version;
    int is_64bit; /* version >= 7500 */
    int error;
} fbx_reader_t;

/// @brief Treat POSIX roots, UNC-style roots, and `C:` prefixes as absolute paths.
static int fbx_is_absolute_path(const char *path) {
    if (!path || !*path)
        return 0;
    if (path[0] == '/' || path[0] == '\\')
        return 1;
    return isalpha((unsigned char)path[0]) && path[1] == ':';
}

/// @brief Copy a path while normalising separators to `/`.
static void fbx_normalize_path(char *dst, size_t dst_size, const char *src) {
    size_t di = 0;
    if (!dst || dst_size == 0)
        return;
    if (!src)
        src = "";
    while (*src && di + 1 < dst_size) {
        char ch = *src++;
        dst[di++] = (ch == '\\') ? '/' : ch;
    }
    dst[di] = '\0';
}

/// @brief Return the last path component inside @p path.
static const char *fbx_path_basename(const char *path) {
    const char *last = path;
    if (!path)
        return "";
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            last = p + 1;
    }
    return last;
}

/// @brief Extract the directory portion of @p path into @p out (or empty if none).
static void fbx_parent_dir(char *out, size_t out_size, const char *path) {
    char normalized[1024];
    const char *last_sep;
    size_t dir_len;
    if (!out || out_size == 0)
        return;
    fbx_normalize_path(normalized, sizeof(normalized), path);
    last_sep = strrchr(normalized, '/');
    if (!last_sep) {
        out[0] = '\0';
        return;
    }
    dir_len = (size_t)(last_sep - normalized);
    if (dir_len == 0 && normalized[0] == '/')
        dir_len = 1;
    if (dir_len >= out_size)
        dir_len = out_size - 1;
    memcpy(out, normalized, dir_len);
    out[dir_len] = '\0';
}

/// @brief Join a directory and leaf filename using `/`.
static void fbx_join_path(char *out, size_t out_size, const char *dir, const char *leaf) {
    size_t dir_len;
    char clean_leaf[1024];
    size_t leaf_len;
    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (!leaf || !*leaf)
        return;
    if (strlen(leaf) >= sizeof(clean_leaf))
        return;
    fbx_normalize_path(clean_leaf, sizeof(clean_leaf), leaf);
    leaf_len = strlen(clean_leaf);
    if (leaf_len == 0 || leaf_len >= out_size)
        return;
    if (!dir || !*dir) {
        memcpy(out, clean_leaf, leaf_len + 1u);
        return;
    }
    fbx_normalize_path(out, out_size, dir);
    dir_len = strlen(out);
    if (dir_len > 0 && out[dir_len - 1] != '/' && dir_len + 1 < out_size) {
        out[dir_len++] = '/';
        out[dir_len] = '\0';
    }
    if (dir_len + leaf_len >= out_size) {
        out[0] = '\0';
        return;
    }
    memcpy(out + dir_len, clean_leaf, leaf_len + 1u);
}

/// @brief Sanitize a texture reference into a safe relative path in @p out (path-traversal
///   guard): normalizes separators, then rejects absolute paths, URI scheme separators, and any
///   ".." segment, collapsing "." and redundant slashes into forward-slash-joined segments.
/// @return Non-zero if a non-empty safe path was written; 0 if empty, unsafe, or overflowing.
static int fbx_normalize_relative_texture_ref(const char *src, char *out, size_t out_size) {
    char normalized[1024];
    const char *p;
    size_t out_len = 0;
    int wrote_segment = 0;
    if (!src || !*src || !out || out_size == 0)
        return 0;
    if (strlen(src) >= sizeof(normalized))
        return 0;
    fbx_normalize_path(normalized, sizeof(normalized), src);
    if (!*normalized || fbx_is_absolute_path(normalized) || strchr(normalized, ':'))
        return 0;
    for (const char *q = normalized; *q; ++q) {
        unsigned char ch = (unsigned char)*q;
        if (ch < 0x20u || ch == 0x7fu)
            return 0;
    }
    p = normalized;
    out[0] = '\0';
    while (*p) {
        const char *seg;
        size_t seg_len;
        while (*p == '/')
            p++;
        seg = p;
        while (*p && *p != '/')
            p++;
        seg_len = (size_t)(p - seg);
        if (seg_len == 0 || (seg_len == 1 && seg[0] == '.'))
            continue;
        if (seg_len == 2 && seg[0] == '.' && seg[1] == '.')
            return 0;
        if (wrote_segment) {
            if (out_len + 1 >= out_size)
                return 0;
            out[out_len++] = '/';
        }
        if (seg_len >= out_size || out_len > out_size - 1 - seg_len)
            return 0;
        memcpy(out + out_len, seg, seg_len);
        out_len += seg_len;
        out[out_len] = '\0';
        wrote_segment = 1;
    }
    return wrote_segment;
}

/// @brief Try a safe relative texture reference, then fall back to its basename beside the FBX
/// file.
static void *fbx_try_load_texture_path(const char *fbx_path, const char *texture_ref) {
    char normalized_ref[1024];
    char dir[1024];
    char candidate[1024];
    const char *basename;
    void *pixels;

    if (!texture_ref || !*texture_ref)
        return NULL;

    if (!fbx_normalize_relative_texture_ref(texture_ref, normalized_ref, sizeof(normalized_ref)))
        return NULL;

    fbx_parent_dir(dir, sizeof(dir), fbx_path);
    if (*dir) {
        fbx_join_path(candidate, sizeof(candidate), dir, normalized_ref);
        pixels = rt_pixels_load(rt_const_cstr(candidate));
    } else {
        pixels = rt_pixels_load(rt_const_cstr(normalized_ref));
    }
    if (pixels)
        return pixels;

    basename = fbx_path_basename(normalized_ref);
    if (!basename || !*basename)
        return NULL;
    if (strcmp(basename, normalized_ref) == 0 && !*dir)
        return NULL;

    if (*dir)
        fbx_join_path(candidate, sizeof(candidate), dir, basename);
    else
        fbx_normalize_path(candidate, sizeof(candidate), basename);
    return rt_pixels_load(rt_const_cstr(candidate));
}

/// @brief Case-insensitive test (ASCII) for whether @p text ends with @p suffix.
static int fbx_ascii_ends_with_i(const char *text, const char *suffix) {
    size_t text_len;
    size_t suffix_len;
    if (!text || !suffix)
        return 0;
    text_len = strlen(text);
    suffix_len = strlen(suffix);
    if (suffix_len > text_len)
        return 0;
    text += text_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        if (tolower((unsigned char)text[i]) != tolower((unsigned char)suffix[i]))
            return 0;
    }
    return 1;
}

/// @brief Build a Pixels object from a width×height RGBA32 buffer (copied), with
///   overflow-checked sizing. Returns NULL on bad dimensions or allocation failure.
static void *fbx_pixels_from_rgba32(uint32_t *rgba32, int64_t width, int64_t height) {
    rt_pixels_impl *pixels;
    size_t pixel_count;
    size_t pixel_bytes;
    if (!rgba32 || width <= 0 || height <= 0)
        return NULL;
    if ((uint64_t)width > SIZE_MAX || (uint64_t)height > SIZE_MAX)
        return NULL;
    if ((size_t)width > SIZE_MAX / (size_t)height)
        return NULL;
    pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > SIZE_MAX / sizeof(uint32_t))
        return NULL;
    pixel_bytes = pixel_count * sizeof(uint32_t);
    pixels = pixels_alloc(width, height);
    if (!pixels)
        return NULL;
    memcpy(pixels->data, rgba32, pixel_bytes);
    if (pixel_count > 0)
        pixels_touch(pixels);
    return pixels;
}

/// @brief Sniff a PNG payload by its 8-byte signature.
static int fbx_payload_looks_png(const uint8_t *data, size_t len) {
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    return data && len >= sizeof(sig) && memcmp(data, sig, sizeof(sig)) == 0;
}

/// @brief Sniff a JPEG payload by its 0xFFD8 start-of-image marker.
static int fbx_payload_looks_jpeg(const uint8_t *data, size_t len) {
    return data && len >= 2u && data[0] == 0xff && data[1] == 0xd8;
}

/// @brief Sniff a GIF payload by its "GIF87a"/"GIF89a" header.
static int fbx_payload_looks_gif(const uint8_t *data, size_t len) {
    return data && len >= 6u &&
           (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0);
}

/// @brief Decode an embedded texture payload into a Pixels object, selecting the codec by
///   magic bytes or by @p name's extension (PNG/JPEG/GIF/...). NULL if unrecognized or
///   undecodable.
static void *fbx_decode_texture_payload(const char *name, const uint8_t *data, size_t len) {
    uint32_t *rgba32 = NULL;
    int64_t width = 0;
    int64_t height = 0;
    void *pixels = NULL;
    if (!data || len == 0)
        return NULL;
    if (fbx_payload_looks_png(data, len) || fbx_ascii_ends_with_i(name, ".png")) {
        if (rt_png_decode_buffer_rgba32(data, len, &rgba32, &width, &height))
            pixels = fbx_pixels_from_rgba32(rgba32, width, height);
        free(rgba32);
        return pixels;
    }
    if (fbx_payload_looks_jpeg(data, len) || fbx_ascii_ends_with_i(name, ".jpg") ||
        fbx_ascii_ends_with_i(name, ".jpeg")) {
        if (rt_jpeg_decode_buffer_rgba32(data, len, &rgba32, &width, &height))
            pixels = fbx_pixels_from_rgba32(rgba32, width, height);
        free(rgba32);
        return pixels;
    }
    if (fbx_payload_looks_gif(data, len) || fbx_ascii_ends_with_i(name, ".gif")) {
        int gif_width = 0;
        int gif_height = 0;
        if (rt_gif_decode_memory_first_rgba32(data, len, &rgba32, &gif_width, &gif_height))
            pixels = fbx_pixels_from_rgba32(rgba32, (int64_t)gif_width, (int64_t)gif_height);
        free(rgba32);
        return pixels;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// FBX is little-endian; v >= 7500 promotes lengths/offsets to 64-bit
// (`is_64bit` flag). Short reads are marked as hard parse errors so corrupt
// assets do not surface as partially imported runtime content.
// ---------------------------------------------------------------------------

/// @brief True if the cursor has consumed the entire input.
static int fbx_eof(const fbx_reader_t *r) {
    return r->pos >= r->len;
}

/// @brief Ensure `n` bytes remain in the stream; sets the error flag and seeks to EOF if not.
static int fbx_require(fbx_reader_t *r, size_t n) {
    if (!r || r->error)
        return 0;
    if (r->pos > r->len || n > r->len - r->pos) {
        r->error = 1;
        r->pos = r->len;
        return 0;
    }
    return 1;
}

/// @brief Read one unsigned byte; marks the reader malformed at EOF.
static uint8_t fbx_u8(fbx_reader_t *r) {
    if (!fbx_require(r, 1))
        return 0;
    return r->data[r->pos++];
}

/// @brief Read a little-endian uint16; marks the reader malformed at short input.
static uint16_t fbx_u16(fbx_reader_t *r) {
    if (!fbx_require(r, 2))
        return 0;
    uint16_t v = (uint16_t)r->data[r->pos] | ((uint16_t)r->data[r->pos + 1] << 8);
    r->pos += 2;
    return v;
}

/// @brief Read a little-endian uint32; marks the reader malformed at short input.
static uint32_t fbx_u32(fbx_reader_t *r) {
    if (!fbx_require(r, 4))
        return 0;
    uint32_t v = (uint32_t)r->data[r->pos] | ((uint32_t)r->data[r->pos + 1] << 8) |
                 ((uint32_t)r->data[r->pos + 2] << 16) | ((uint32_t)r->data[r->pos + 3] << 24);
    r->pos += 4;
    return v;
}

/// @brief Read a little-endian int32; 0 at short input.
static int32_t fbx_i32(fbx_reader_t *r) {
    return (int32_t)fbx_u32(r);
}

/// @brief Read a little-endian uint64 (composed of two u32 halves).
static uint64_t fbx_u64(fbx_reader_t *r) {
    if (!fbx_require(r, 8))
        return 0;
    uint64_t lo = fbx_u32(r);
    uint64_t hi = fbx_u32(r);
    return lo | (hi << 32);
}

/// @brief Read a little-endian int64.
static int64_t fbx_i64(fbx_reader_t *r) {
    return (int64_t)fbx_u64(r);
}

/// @brief Read an IEEE 754 single-precision float (memcpy bit-pattern; safe for aliasing).
static float fbx_f32(fbx_reader_t *r) {
    uint32_t bits = fbx_u32(r);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

/// @brief Read an IEEE 754 double-precision float.
static double fbx_f64(fbx_reader_t *r) {
    uint64_t bits = fbx_u64(r);
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

/// @brief Advance the cursor by `n` bytes, marking malformed if it would overrun.
static void fbx_skip(fbx_reader_t *r, size_t n) {
    if (!fbx_require(r, n))
        return;
    r->pos += n;
}

/*==========================================================================
 * FBX node tree
 *=========================================================================*/

#define FBX_MAX_CHILDREN 256
#define FBX_MAX_PROPS 32
#define FBX_MAX_COMPRESSED_ARRAY_BYTES (256u * 1024u * 1024u)

typedef struct {
    char type; /* C/Y/I/L/F/D/S/R/b/i/l/f/d */

    union {
        int8_t bool_val;
        int16_t i16;
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;

        struct {
            char *str;
            uint32_t len;
        } string;

        struct {
            uint8_t *data;
            uint32_t len;
        } raw;

        struct {
            void *data;
            uint32_t count;
            char elem_type;
        } array;
    } v;
} fbx_prop_t;

typedef struct fbx_node {
    char name[128];
    fbx_prop_t *props;
    int32_t prop_count;
    struct fbx_node *children;
    int32_t child_count;
    int32_t child_capacity;
} fbx_node_t;

/*==========================================================================
 * Array decompression (zlib → raw DEFLATE → rt_compress_inflate)
 *=========================================================================*/

/// @brief Inflate a zlib-wrapped FBX array property to raw bytes.
///
/// FBX wraps array data in standard zlib (2-byte CMF/FLG header
/// + DEFLATE payload + 4-byte adler32). We strip the header,
/// hand the raw DEFLATE to `rt_compress_inflate`, and validate
/// the result equals `count * elem_size` bytes.
/// @return Newly-allocated buffer (caller `free`s) or NULL on failure.
static void *fbx_decompress_array(const uint8_t *data,
                                  uint32_t comp_len,
                                  uint32_t count,
                                  uint32_t elem_size) {
    if (comp_len < 6)
        return NULL;
    if (comp_len > FBX_MAX_COMPRESSED_ARRAY_BYTES)
        return NULL;
    uint32_t deflate_len = comp_len - 6; /* strip 2-byte header + 4-byte adler32 */

    void *comp_bytes = rt_bytes_new((int64_t)deflate_len);
    if (!comp_bytes)
        return NULL;

    /* Copy raw DEFLATE payload (skip 2-byte zlib header) */
    typedef struct {
        int64_t len;
        uint8_t *bdata;
    } bytes_view;

    bytes_view *bv = (bytes_view *)comp_bytes;
    memcpy(bv->bdata, data + 2, deflate_len);

    void *inflated = rt_compress_inflate(comp_bytes);
    if (rt_obj_release_check0(comp_bytes))
        rt_obj_free(comp_bytes);
    if (!inflated)
        return NULL;

    bytes_view *iv = (bytes_view *)inflated;
    if (elem_size > 0 && count > SIZE_MAX / elem_size) {
        if (rt_obj_release_check0(inflated))
            rt_obj_free(inflated);
        return NULL; /* overflow guard for 32-bit platforms */
    }
    size_t expected = (size_t)count * elem_size;
    if ((size_t)iv->len < expected) {
        if (rt_obj_release_check0(inflated))
            rt_obj_free(inflated);
        return NULL;
    }

    void *result = malloc(expected);
    if (result)
        memcpy(result, iv->bdata, expected);
    if (rt_obj_release_check0(inflated))
        rt_obj_free(inflated);
    return result;
}

/*==========================================================================
 * Property parsing
 *=========================================================================*/

/// @brief Parse one FBX node property into `*prop`.
///
/// FBX property types are encoded as a single ASCII type-byte:
///   - Y/I/L/F/D — scalar int16/int32/int64/float/double
///   - C/b      — bool/raw-byte
///   - S/R      — string / raw byte array
///   - lowercase i/l/f/d — array (with optional zlib compression)
/// Arrays carry a (count, encoding, compressed_len) header before
/// the data; if `encoding == 1` we hand the bytes to
/// `fbx_decompress_array`.
/// @return 1 on success, 0 on malformed property / unknown type.
static int fbx_parse_property(fbx_reader_t *r, fbx_prop_t *prop) {
    if (fbx_eof(r))
        return -1;
    prop->type = (char)fbx_u8(r);
    if (r->error)
        return -1;
    switch (prop->type) {
        case 'C':
            prop->v.bool_val = (int8_t)fbx_u8(r);
            break;
        case 'Y':
            prop->v.i16 = (int16_t)fbx_u16(r);
            break;
        case 'I':
            prop->v.i32 = fbx_i32(r);
            break;
        case 'L':
            prop->v.i64 = fbx_i64(r);
            break;
        case 'F':
            prop->v.f32 = fbx_f32(r);
            break;
        case 'D':
            prop->v.f64 = fbx_f64(r);
            break;
        case 'S':
        case 'R': {
            uint32_t len = fbx_u32(r);
            if (!fbx_require(r, len))
                return -1;
            if (prop->type == 'S') {
                prop->v.string.str = (char *)malloc(len + 1);
                if (!prop->v.string.str)
                    return -1;
                memcpy(prop->v.string.str, r->data + r->pos, len);
                prop->v.string.str[len] = '\0';
                prop->v.string.len = len;
            } else {
                prop->v.raw.data = (uint8_t *)malloc(len);
                if (len > 0 && !prop->v.raw.data)
                    return -1;
                if (len > 0)
                    memcpy(prop->v.raw.data, r->data + r->pos, len);
                prop->v.raw.len = len;
            }
            fbx_skip(r, len);
            break;
        }
        case 'b':
        case 'i':
        case 'l':
        case 'f':
        case 'd': {
            uint32_t count = fbx_u32(r);
            uint32_t encoding = fbx_u32(r);
            uint32_t comp_len = fbx_u32(r);
            if (!fbx_require(r, comp_len))
                return -1;

            uint32_t elem_size = 0;
            switch (prop->type) {
                case 'b':
                    elem_size = 1;
                    break;
                case 'i':
                    elem_size = 4;
                    break;
                case 'l':
                    elem_size = 8;
                    break;
                case 'f':
                    elem_size = 4;
                    break;
                case 'd':
                    elem_size = 8;
                    break;
            }

            prop->v.array.count = count;
            prop->v.array.elem_type = prop->type;
            if (encoding == 1) {
                prop->v.array.data =
                    fbx_decompress_array(r->data + r->pos, comp_len, count, elem_size);
                if (count > 0 && !prop->v.array.data)
                    return -1;
            } else {
                if (elem_size > 0 && count > SIZE_MAX / elem_size)
                    return -1; /* overflow guard for 32-bit platforms */
                size_t expected = (size_t)count * elem_size;
                if (comp_len < expected)
                    return -1;
                prop->v.array.data = malloc(expected);
                if (expected > 0 && !prop->v.array.data)
                    return -1;
                if (expected > 0)
                    memcpy(prop->v.array.data, r->data + r->pos, expected);
            }
            fbx_skip(r, comp_len);
            break;
        }
        default:
            return -1;
    }
    return 0;
}

/*==========================================================================
 * Node parsing (recursive)
 *=========================================================================*/

/// @brief Release any heap allocations owned by a property (string/raw/array buffers).
static void fbx_free_prop(fbx_prop_t *p) {
    switch (p->type) {
        case 'S':
            free(p->v.string.str);
            break;
        case 'R':
            free(p->v.raw.data);
            break;
        case 'b':
        case 'i':
        case 'l':
        case 'f':
        case 'd':
            free(p->v.array.data);
            break;
        default:
            break;
    }
}

/// @brief Recursively free an FBX node tree (props + children).
static void fbx_free_node(fbx_node_t *n) {
    for (int32_t i = 0; i < n->prop_count; i++)
        fbx_free_prop(&n->props[i]);
    free(n->props);
    for (int32_t i = 0; i < n->child_count; i++)
        fbx_free_node(&n->children[i]);
    free(n->children);
}

/// @brief Parse one FBX node (header + properties + children) at the cursor.
///
/// Each node header carries `end_offset`, `prop_count`,
/// `prop_list_len`, and a name. After consuming the properties we
/// recurse on child nodes until we reach the sentinel "null record"
/// (an all-zero header). The `is_64bit` flag widens the offset
/// fields from 4 to 8 bytes for FBX 7500+.
/// @return 1 on success, 0 if the buffer is malformed.
static int fbx_parse_node(fbx_reader_t *r, fbx_node_t *node) {
    uint8_t encoded_name_len;
    uint8_t copy_name_len;

    memset(node, 0, sizeof(fbx_node_t));

    uint64_t end_offset = r->is_64bit ? fbx_u64(r) : fbx_u32(r);
    uint64_t num_props = r->is_64bit ? fbx_u64(r) : fbx_u32(r);
    uint64_t prop_list_len = r->is_64bit ? fbx_u64(r) : fbx_u32(r);
    (void)prop_list_len;

    encoded_name_len = fbx_u8(r);
    if (r->error)
        return -1;

    if (end_offset == 0 && num_props == 0 && encoded_name_len == 0)
        return -1; /* null record (sentinel) */

    if (end_offset > r->len || end_offset < r->pos)
        return -1;
    if (num_props > FBX_MAX_PROPS)
        return -1;
    if (!fbx_require(r, encoded_name_len))
        return -1;
    copy_name_len = encoded_name_len > 127 ? 127 : encoded_name_len;
    memcpy(node->name, r->data + r->pos, copy_name_len);
    node->name[copy_name_len] = '\0';
    fbx_skip(r, encoded_name_len);

    /* Parse properties */
    if (num_props > 0) {
        node->props = (fbx_prop_t *)calloc((size_t)num_props, sizeof(fbx_prop_t));
        if (!node->props)
            return -1;
        for (uint64_t i = 0; i < num_props; i++) {
            if (fbx_parse_property(r, &node->props[i]) < 0) {
                node->prop_count = (int32_t)i;
                return -1;
            }
        }
        node->prop_count = (int32_t)num_props;
    }

    /* Parse children (until end_offset or null record) */
    while (r->pos < (size_t)end_offset && !fbx_eof(r)) {
        /* Check for null record sentinel */
        size_t sentinel_size = r->is_64bit ? 25 : 13;
        if (r->pos + sentinel_size <= r->len) {
            int is_null = 1;
            for (size_t i = 0; i < sentinel_size; i++)
                if (r->data[r->pos + i] != 0) {
                    is_null = 0;
                    break;
                }
            if (is_null) {
                fbx_skip(r, sentinel_size);
                break;
            }
        }

        if (node->child_count >= node->child_capacity) {
            if (node->child_capacity < 0 || node->child_capacity > INT32_MAX / 2)
                return -1;
            int32_t new_cap = node->child_capacity == 0 ? 8 : node->child_capacity * 2;
            if ((size_t)new_cap > SIZE_MAX / sizeof(fbx_node_t))
                return -1;
            fbx_node_t *nc =
                (fbx_node_t *)realloc(node->children, (size_t)new_cap * sizeof(fbx_node_t));
            if (!nc)
                return -1;
            node->children = nc;
            node->child_capacity = new_cap;
        }

        fbx_node_t *child = &node->children[node->child_count];
        if (fbx_parse_node(r, child) < 0)
            return -1;
        node->child_count++;
    }

    /* Ensure we're at end_offset */
    if (r->pos < (size_t)end_offset)
        r->pos = (size_t)end_offset;

    return 0;
}

/*==========================================================================
 * Node tree query helpers
 *=========================================================================*/

/// @brief Linear-search `parent->children` for the first child named `name`.
/// FBX node trees are small (few hundred nodes) so the O(n) cost is fine.
static fbx_node_t *fbx_find_child(fbx_node_t *parent, const char *name) {
    for (int32_t i = 0; i < parent->child_count; i++)
        if (strcmp(parent->children[i].name, name) == 0)
            return &parent->children[i];
    return NULL;
}

/// @brief Coerce property `idx` of `node` to int64 (handles Y/I/L variants).
static int64_t fbx_prop_i64(fbx_node_t *node, int idx) {
    if (!node || idx >= node->prop_count)
        return 0;
    fbx_prop_t *p = &node->props[idx];
    switch (p->type) {
        case 'L':
            return p->v.i64;
        case 'I':
            return p->v.i32;
        case 'Y':
            return p->v.i16;
        case 'C':
            return p->v.bool_val;
        default:
            return 0;
    }
}

/// @brief Coerce property `idx` of `node` to double (handles F/D variants).
static double fbx_prop_f64(fbx_node_t *node, int idx) {
    if (!node || idx >= node->prop_count)
        return 0.0;
    fbx_prop_t *p = &node->props[idx];
    if (p->type == 'D')
        return p->v.f64;
    if (p->type == 'F')
        return p->v.f32;
    return 0.0;
}

/// @brief Borrowed C-string view of property `idx` (S/R type). NULL otherwise.
static const char *fbx_prop_str(fbx_node_t *node, int idx) {
    if (!node || idx >= node->prop_count)
        return "";
    fbx_prop_t *p = &node->props[idx];
    if (p->type == 'S' && p->v.string.str)
        return p->v.string.str;
    return "";
}

/// @brief Strip the `Namespace::` prefix from an FBX object name and copy the remainder
/// into `out`. FBX stores names as `Model::Hips`, `Geometry::CubeMesh`, etc., where the
/// prefix is loader metadata that authors in Blender / Maya / Houdini never see. Walks
/// the full string to find the *last* `::` so names that legitimately contain double
/// colons in a sub-namespace still strip correctly. NULL-safe (treats NULL input as
/// empty), output is always NUL-terminated, truncates when the stripped name doesn't
/// fit in `out_size`.
static void fbx_decode_object_name(const char *raw_name, char *out, size_t out_size) {
    const char *start = raw_name ? raw_name : "";
    const char *end = start;
    const char *scope = NULL;
    size_t len;

    if (!out || out_size == 0)
        return;
    while (*end)
        end++;
    for (const char *p = start; p + 1 < end; p++) {
        if (p[0] == ':' && p[1] == ':')
            scope = p;
    }
    if (scope)
        start = scope + 2;
    len = (size_t)(end - start);
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
}

/*==========================================================================
 * Connection table
 *=========================================================================*/

typedef struct {
    int64_t child_id;
    int64_t parent_id;
    char prop[64];
} fbx_conn_t;

typedef struct {
    fbx_conn_t *entries;
    int32_t count;
    int32_t capacity;
} fbx_conn_table_t;

static fbx_node_t *fbx_find_object_by_id(fbx_node_t *objects, int64_t id);

/// @brief Walk the `Connections` block and populate the `(child_id, parent_id)` table.
///
/// FBX models use a flat object soup where parent/child relations
/// are encoded as numeric IDs in a separate connections section.
/// Pre-collecting them lets later passes look up the parent of any
/// node in O(n) (linear scan — typical scenes have a few hundred connections).
static void fbx_parse_connections(fbx_node_t *root, fbx_conn_table_t *ct) {
    fbx_node_t *conns_node = fbx_find_child(root, "Connections");
    if (!conns_node)
        return;

    for (int32_t i = 0; i < conns_node->child_count; i++) {
        fbx_node_t *c = &conns_node->children[i];
        if (strcmp(c->name, "C") != 0 || c->prop_count < 3)
            continue;

        if (ct->count >= ct->capacity) {
            if (ct->capacity < 0 || ct->capacity > INT32_MAX / 2)
                break;
            int32_t new_cap = ct->capacity == 0 ? 64 : ct->capacity * 2;
            if ((size_t)new_cap > SIZE_MAX / sizeof(fbx_conn_t))
                break;
            fbx_conn_t *nc =
                (fbx_conn_t *)realloc(ct->entries, (size_t)new_cap * sizeof(fbx_conn_t));
            if (!nc)
                break;
            ct->entries = nc;
            ct->capacity = new_cap;
        }

        fbx_conn_t *entry = &ct->entries[ct->count++];
        entry->child_id = fbx_prop_i64(c, 1);
        entry->parent_id = fbx_prop_i64(c, 2);
        entry->prop[0] = '\0';
        if (c->prop_count >= 4) {
            const char *pname = fbx_prop_str(c, 3);
            size_t plen = strlen(pname);
            if (plen > 63)
                plen = 63;
            memcpy(entry->prop, pname, plen);
            entry->prop[plen] = '\0';
        }
    }
}

/// @brief Return the parent ID of `child_id` from the connection table, or 0 if root/missing.
static int64_t fbx_find_parent(const fbx_conn_table_t *ct, int64_t child_id) {
    for (int32_t i = 0; i < ct->count; i++)
        if (ct->entries[i].child_id == child_id)
            return ct->entries[i].parent_id;
    return 0;
}

/*==========================================================================
 * Coordinate system detection + correction
 *=========================================================================*/

/// @brief Read `GlobalSettings.UpAxis` from the FBX header; returns 1 for Z-up, 0 for Y-up.
///
/// Most FBX exporters use Y-up but Maya / Blender / 3ds Max can
/// emit Z-up scenes. The extractors apply a coordinate-system swap
/// when this returns 1 so the scene is normalised to Viper's Y-up convention.
static int fbx_is_z_up(fbx_node_t *root) {
    fbx_node_t *gs = fbx_find_child(root, "GlobalSettings");
    if (!gs)
        return 0;
    fbx_node_t *p70 = fbx_find_child(gs, "Properties70");
    if (!p70)
        return 0;
    for (int32_t i = 0; i < p70->child_count; i++) {
        fbx_node_t *p = &p70->children[i];
        if (strcmp(p->name, "P") != 0 || p->prop_count < 5)
            continue;
        const char *pname = fbx_prop_str(p, 0);
        if (strcmp(pname, "UpAxis") == 0) {
            int64_t axis = fbx_prop_i64(p, 4);
            return axis == 2; /* 2 = Z-up */
        }
    }
    return 0;
}

/// @brief Apply Z-up → Y-up correction: swap Y/Z and negate new Z.
static void fbx_correct_zup(double *x, double *y, double *z) {
    double tmp = *y;
    *y = *z;
    *z = -tmp;
}

/// @brief Free the per-control-vertex index lists inside a single mesh remap entry.
static void fbx_mesh_remap_free(fbx_mesh_remap_t *remap) {
    if (!remap)
        return;
    if (remap->control_vertices) {
        for (int32_t i = 0; i < remap->control_count; i++)
            free(remap->control_vertices[i].vertices);
    }
    free(remap->control_vertices);
    remap->control_vertices = NULL;
    remap->control_count = 0;
    remap->id = 0;
}

/// @brief Free an array of `count` mesh remap entries and the array itself.
static void fbx_mesh_remaps_free(fbx_mesh_remap_t *remaps, int32_t count) {
    if (!remaps)
        return;
    for (int32_t i = 0; i < count; i++)
        fbx_mesh_remap_free(&remaps[i]);
    free(remaps);
}

/// @brief Append a completed mesh remap, taking ownership of @p remap on success.
static int fbx_mesh_remaps_append(fbx_mesh_remap_t **remaps,
                                  int32_t *count,
                                  fbx_mesh_remap_t *remap) {
    fbx_mesh_remap_t *grown;
    if (!remaps || !count || !remap || *count < 0 || *count == INT32_MAX)
        return 0;
    if ((size_t)(*count + 1) > SIZE_MAX / sizeof(**remaps))
        return 0;
    grown = (fbx_mesh_remap_t *)realloc(*remaps, (size_t)(*count + 1) * sizeof(**remaps));
    if (!grown)
        return 0;
    *remaps = grown;
    (*remaps)[*count] = *remap;
    (*count)++;
    memset(remap, 0, sizeof(*remap));
    return 1;
}

/// @brief Allocate the per-control-vertex index arrays for a mesh remap entry; returns 0 on OOM.
static int fbx_mesh_remap_init(fbx_mesh_remap_t *remap, int64_t id, int32_t control_count) {
    if (!remap)
        return 0;
    memset(remap, 0, sizeof(*remap));
    remap->id = id;
    remap->control_count = control_count;
    if (control_count <= 0)
        return 1;
    remap->control_vertices =
        (fbx_vertex_index_list_t *)calloc((size_t)control_count, sizeof(*remap->control_vertices));
    return remap->control_vertices != NULL;
}

/// @brief Append a new mesh vertex index to the list for a given control vertex (grows
/// dynamically).
static void fbx_mesh_remap_add_vertex(fbx_mesh_remap_t *remap,
                                      int32_t control_index,
                                      int32_t vertex_index) {
    fbx_vertex_index_list_t *list;
    int32_t new_capacity;
    int32_t *grown;
    if (!remap || !remap->control_vertices || control_index < 0 ||
        control_index >= remap->control_count || vertex_index < 0)
        return;
    list = &remap->control_vertices[control_index];
    if (list->count >= list->capacity) {
        if (list->capacity < 0 || list->capacity > INT32_MAX / 2)
            return;
        new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        if ((size_t)new_capacity > SIZE_MAX / sizeof(*list->vertices))
            return;
        grown = (int32_t *)realloc(list->vertices, (size_t)new_capacity * sizeof(*list->vertices));
        if (!grown)
            return;
        list->vertices = grown;
        list->capacity = new_capacity;
    }
    list->vertices[list->count++] = vertex_index;
}

/// @brief Linear-search `remaps` for the entry with the given `id`; returns NULL if not found.
static const fbx_mesh_remap_t *fbx_find_mesh_remap(const fbx_mesh_remap_t *remaps,
                                                   int32_t count,
                                                   int64_t id) {
    for (int32_t i = 0; i < count; i++)
        if (remaps[i].id == id)
            return &remaps[i];
    return NULL;
}

/// @brief Free a mesh's per-triangle material-slot map and zero the struct.
static void fbx_mesh_material_map_free(fbx_mesh_material_map_t *map) {
    if (!map)
        return;
    free(map->triangle_slots);
    memset(map, 0, sizeof(*map));
}

/// @brief Free an array of mesh bindings, releasing each binding's material map first.
static void fbx_mesh_bindings_free(fbx_mesh_binding_t *bindings, int32_t count) {
    if (!bindings)
        return;
    for (int32_t i = 0; i < count; i++)
        fbx_mesh_material_map_free(&bindings[i].material_map);
    free(bindings);
}

/// @brief Append @p triangle_count triangles, all assigned to @p material_slot, to the
///   map's per-triangle slot array (growing it with overflow checks) and widen the tracked
///   slot count. Negative slots are clamped to 0.
/// @return 1 on success or no-op (zero triangles); 0 on overflow or allocation failure.
static int fbx_mesh_material_map_append(fbx_mesh_material_map_t *map,
                                        int32_t material_slot,
                                        uint32_t triangle_count) {
    int32_t *grown;
    uint32_t old_count;
    if (!map || triangle_count == 0)
        return 1;
    if (material_slot < 0)
        material_slot = 0;
    if (map->triangle_count > UINT32_MAX - triangle_count)
        return 0;
    if ((size_t)(map->triangle_count + triangle_count) > SIZE_MAX / sizeof(*map->triangle_slots))
        return 0;
    grown = (int32_t *)realloc(map->triangle_slots,
                               (size_t)(map->triangle_count + triangle_count) *
                                   sizeof(*map->triangle_slots));
    if (!grown)
        return 0;
    map->triangle_slots = grown;
    old_count = map->triangle_count;
    for (uint32_t i = 0; i < triangle_count; i++)
        map->triangle_slots[old_count + i] = material_slot;
    map->triangle_count += triangle_count;
    map->has_slots = 1;
    if (material_slot + 1 > map->slot_count)
        map->slot_count = material_slot + 1;
    return 1;
}

/// @brief Resolve the material-slot index for polygon @p polygon_index per the FBX mapping
///   mode (@p by_polygon → per-polygon lookup, @p all_same → slot 0, else per-index).
///   Returns 0 for an out-of-range index or a negative slot value.
static int32_t fbx_material_slot_for_polygon(const int32_t *slots,
                                             uint32_t slot_count,
                                             int by_polygon,
                                             int all_same,
                                             uint32_t polygon_index) {
    uint32_t src_index = 0;
    if (!slots || slot_count == 0)
        return 0;
    if (by_polygon)
        src_index = polygon_index;
    else if (all_same)
        src_index = 0;
    else
        src_index = polygon_index;
    if (src_index >= slot_count)
        return 0;
    return slots[src_index] < 0 ? 0 : slots[src_index];
}

#include "rt_fbx_triangulation.inc"

/*==========================================================================
 * Geometry extraction
 *=========================================================================*/

/// @brief A parsed FBX vertex-attribute layer (LayerElementNormal / -UV): the raw
///   value array plus its optional index array and mapping/reference modes.
typedef struct {
    double *data;
    int32_t *indices;
    uint32_t count;
    uint32_t index_count;
    int by_polygon_vertex;
    int by_polygon;
    int index_to_direct;
} fbx_vertex_layer_t;

/// @brief A parsed FBX LayerElementMaterial: per-polygon material slot indices and
///   the mapping mode that selects how they apply.
typedef struct {
    int32_t *slots;
    uint32_t slot_count;
    int by_polygon;
    int all_same;
} fbx_material_layer_t;

/// @brief Parse a vertex-attribute layer (e.g. LayerElementNormal/-UV) of
///   `components` floats per element from @p geom_node into *out. @p index_name_alt
///   is an optional fallback index-array name (or NULL). All fields are zeroed when
///   the layer is absent or malformed.
static void fbx_parse_vertex_layer(fbx_node_t *geom_node,
                                   const char *layer_name,
                                   const char *data_name,
                                   const char *index_name,
                                   const char *index_name_alt,
                                   int components,
                                   fbx_vertex_layer_t *out) {
    memset(out, 0, sizeof(*out));
    fbx_node_t *layer = fbx_find_child(geom_node, layer_name);
    if (!layer)
        return;
    fbx_node_t *d_node = fbx_find_child(layer, data_name);
    if (d_node && d_node->prop_count >= 1 && d_node->props[0].type == 'd') {
        out->data = (double *)d_node->props[0].v.array.data;
        out->count = d_node->props[0].v.array.count / (uint32_t)components;
        if (out->count > (uint32_t)INT32_MAX) {
            out->data = NULL;
            out->count = 0;
        }
    }
    fbx_node_t *mm = fbx_find_child(layer, "MappingInformationType");
    if (mm && mm->prop_count >= 1) {
        out->by_polygon_vertex = strcmp(fbx_prop_str(mm, 0), "ByPolygonVertex") == 0;
        out->by_polygon = strcmp(fbx_prop_str(mm, 0), "ByPolygon") == 0;
    }
    fbx_node_t *rm = fbx_find_child(layer, "ReferenceInformationType");
    if (rm && rm->prop_count >= 1)
        out->index_to_direct = strcmp(fbx_prop_str(rm, 0), "IndexToDirect") == 0 ||
                               strcmp(fbx_prop_str(rm, 0), "Index") == 0;
    if (out->index_to_direct) {
        fbx_node_t *ni = fbx_find_child(layer, index_name);
        if (!ni && index_name_alt)
            ni = fbx_find_child(layer, index_name_alt);
        if (ni && ni->prop_count >= 1 && ni->props[0].type == 'i') {
            out->indices = (int32_t *)ni->props[0].v.array.data;
            out->index_count = ni->props[0].v.array.count;
        }
    }
}

/// @brief Parse the LayerElementMaterial of @p geom_node into *out (zeroed when
///   absent or malformed).
static void fbx_parse_material_layer(fbx_node_t *geom_node, fbx_material_layer_t *out) {
    memset(out, 0, sizeof(*out));
    fbx_node_t *mat_layer = fbx_find_child(geom_node, "LayerElementMaterial");
    if (!mat_layer)
        return;
    fbx_node_t *m_node = fbx_find_child(mat_layer, "Materials");
    fbx_node_t *mm = fbx_find_child(mat_layer, "MappingInformationType");
    if (mm && mm->prop_count >= 1) {
        const char *mapping = fbx_prop_str(mm, 0);
        out->by_polygon = strcmp(mapping, "ByPolygon") == 0;
        out->all_same = strcmp(mapping, "AllSame") == 0;
    }
    if (m_node && m_node->prop_count >= 1 && m_node->props[0].type == 'i' &&
        m_node->props[0].v.array.data) {
        out->slots = (int32_t *)m_node->props[0].v.array.data;
        out->slot_count = m_node->props[0].v.array.count;
        if (out->slot_count > (uint32_t)INT32_MAX) {
            out->slots = NULL;
            out->slot_count = 0;
        }
    }
}

/// @brief Resolve the direct value index for one polygon-vertex within a vertex
///   layer, honoring its mapping (ByPolygonVertex/ByPolygon/ByVertex) and optional
///   IndexToDirect indirection. Returns -1 when out of range or unmapped.
static int32_t fbx_resolve_layer_index(const fbx_vertex_layer_t *layer,
                                       int32_t polygon_vertex_idx,
                                       uint32_t polygon_idx,
                                       int32_t vi) {
    uint32_t source_index = layer->by_polygon_vertex ? (uint32_t)polygon_vertex_idx
                            : layer->by_polygon      ? polygon_idx
                                                     : (uint32_t)vi;
    int32_t idx;
    if (layer->index_to_direct) {
        if (layer->indices && source_index < layer->index_count)
            idx = layer->indices[source_index];
        else
            idx = -1;
    } else {
        idx = (int32_t)source_index;
    }
    if (idx >= 0 && idx < (int32_t)layer->count)
        return idx;
    return -1;
}

/// @brief Common geometry-extraction failure cleanup: free a heap-grown polygon
///   buffer, release the partially-built mesh, and return NULL.
static void *fbx_geom_fail(int32_t *polygon, int32_t *polygon_stack, void *mesh) {
    if (polygon != polygon_stack)
        free(polygon);
    if (rt_obj_release_check0(mesh))
        rt_obj_free(mesh);
    return NULL;
}

/// @brief Convert an FBX `Geometry` node into a Viper `rt_mesh3d_t`.
///
/// Decodes the `Vertices` (positions), `PolygonVertexIndex`
/// (vertex indices, with the last index of each polygon negated
/// XOR'd with bit 31 to mark polygon end), `LayerElementNormal`,
/// `LayerElementUV`, and `LayerElementMaterial`. Triangulates n-gons
/// using ear clipping. If `z_up` is set, applies an axis
/// swap (positions and normals) to convert to Y-up.
/// @return A new mesh on success, NULL on missing or malformed geometry.
static void *fbx_extract_geometry(fbx_node_t *geom_node,
                                  int z_up,
                                  fbx_mesh_remap_t *remap,
                                  fbx_mesh_material_map_t *material_map) {
    if (!geom_node)
        return NULL;

    /* Find Vertices (double array) and PolygonVertexIndex (int32 array) */
    fbx_node_t *verts_node = fbx_find_child(geom_node, "Vertices");
    fbx_node_t *idx_node = fbx_find_child(geom_node, "PolygonVertexIndex");
    if (!verts_node || !idx_node)
        return NULL;
    if (verts_node->prop_count < 1 || idx_node->prop_count < 1)
        return NULL;

    fbx_prop_t *vp = &verts_node->props[0];
    fbx_prop_t *ip = &idx_node->props[0];
    if (vp->type != 'd' || ip->type != 'i')
        return NULL;

    double *positions = (double *)vp->v.array.data;
    int32_t *indices = (int32_t *)ip->v.array.data;
    uint32_t pos_count = vp->v.array.count / 3;
    uint32_t idx_count = ip->v.array.count;
    if (pos_count > (uint32_t)INT32_MAX || idx_count > (uint32_t)INT32_MAX)
        return NULL;
    if (!positions || !indices || pos_count == 0 || idx_count == 0)
        return NULL;
    if (remap)
        fbx_mesh_remap_init(remap, fbx_prop_i64(geom_node, 0), (int32_t)pos_count);
    if (material_map)
        memset(material_map, 0, sizeof(*material_map));

    /* Vertex attribute layers (optional) + material assignment (optional) */
    fbx_vertex_layer_t norm_layer;
    fbx_parse_vertex_layer(
        geom_node, "LayerElementNormal", "Normals", "NormalsIndex", "NormalIndex", 3, &norm_layer);
    fbx_vertex_layer_t uv_layer;
    fbx_parse_vertex_layer(geom_node, "LayerElementUV", "UV", "UVIndex", NULL, 2, &uv_layer);
    fbx_material_layer_t mat;
    fbx_parse_material_layer(geom_node, &mat);

    /* Build mesh: iterate polygon indices, triangulate with fan */
    void *mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;

    int32_t polygon_stack[16];
    int32_t *polygon = polygon_stack;
    int32_t poly_count = 0;
    int32_t poly_capacity = (int32_t)(sizeof(polygon_stack) / sizeof(polygon_stack[0]));
    int32_t polygon_vertex_idx = 0; /* running index for ByPolygonVertex mapping */
    uint32_t polygon_idx = 0;
    int32_t mesh_vertex_count = 0;
    int polygon_invalid = 0;

    for (uint32_t i = 0; i < idx_count; i++) {
        int32_t raw_idx = indices[i];
        int end_of_polygon = (raw_idx < 0);
        int32_t vi = end_of_polygon ? ~raw_idx : raw_idx;

        if (vi < 0 || vi >= (int32_t)pos_count) {
            polygon_invalid = 1;
            polygon_vertex_idx++;
            if (end_of_polygon) {
                poly_count = 0;
                polygon_invalid = 0;
                polygon_idx++;
            }
            continue;
        }

        /* Get position */
        double px = positions[vi * 3 + 0];
        double py = positions[vi * 3 + 1];
        double pz = positions[vi * 3 + 2];
        if (z_up)
            fbx_correct_zup(&px, &py, &pz);
        if (!fbx_sanitize_position3(&px, &py, &pz)) {
            polygon_invalid = 1;
            polygon_vertex_idx++;
            if (end_of_polygon) {
                poly_count = 0;
                polygon_invalid = 0;
                polygon_idx++;
            }
            continue;
        }

        /* Get normal */
        double nx = 0, ny = 1, nz = 0;
        if (norm_layer.data) {
            int32_t ni = fbx_resolve_layer_index(&norm_layer, polygon_vertex_idx, polygon_idx, vi);
            if (ni >= 0) {
                nx = norm_layer.data[ni * 3 + 0];
                ny = norm_layer.data[ni * 3 + 1];
                nz = norm_layer.data[ni * 3 + 2];
                if (z_up)
                    fbx_correct_zup(&nx, &ny, &nz);
            }
        }
        fbx_sanitize_normal3(&nx, &ny, &nz);

        /* Get UV */
        double u = 0, v = 0;
        if (uv_layer.data) {
            int32_t ui = fbx_resolve_layer_index(&uv_layer, polygon_vertex_idx, polygon_idx, vi);
            if (ui >= 0) {
                u = uv_layer.data[ui * 2 + 0];
                v = 1.0 - uv_layer.data[ui * 2 + 1]; /* FBX V is flipped vs OpenGL */
            }
        }
        u = fbx_clamp_abs_or(u, 0.0, FBX_UV_ABS_MAX);
        v = fbx_clamp_abs_or(v, 0.0, FBX_UV_ABS_MAX);

        {
            int32_t emitted_vertex = mesh_vertex_count++;
            if (poly_count >= poly_capacity) {
                int32_t new_capacity = poly_capacity * 2;
                int32_t *grown;
                if (poly_capacity > INT32_MAX / 2 ||
                    (size_t)new_capacity > SIZE_MAX / sizeof(*polygon))
                    return fbx_geom_fail(polygon, polygon_stack, mesh);
                grown = (int32_t *)malloc((size_t)new_capacity * sizeof(*polygon));
                if (!grown)
                    return fbx_geom_fail(polygon, polygon_stack, mesh);
                memcpy(grown, polygon, (size_t)poly_count * sizeof(*polygon));
                if (polygon != polygon_stack)
                    free(polygon);
                polygon = grown;
                poly_capacity = new_capacity;
            }
            if (((rt_mesh3d *)mesh)->vertex_count == UINT32_MAX)
                return fbx_geom_fail(polygon, polygon_stack, mesh);
            rt_mesh3d_add_vertex(mesh, px, py, pz, nx, ny, nz, u, v);
            if (((rt_mesh3d *)mesh)->build_failed)
                return fbx_geom_fail(polygon, polygon_stack, mesh);
            fbx_mesh_remap_add_vertex(remap, vi, emitted_vertex);
            polygon[poly_count++] = emitted_vertex;
        }
        polygon_vertex_idx++;

        if (end_of_polygon) {
            uint32_t index_before = ((rt_mesh3d *)mesh)->index_count;
            int32_t material_slot = fbx_material_slot_for_polygon(
                mat.slots, mat.slot_count, mat.by_polygon, mat.all_same, polygon_idx);
            if (!polygon_invalid && !fbx_emit_polygon_triangles(mesh, polygon, poly_count))
                return fbx_geom_fail(polygon, polygon_stack, mesh);
            if (!polygon_invalid && material_map) {
                uint32_t index_after = ((rt_mesh3d *)mesh)->index_count;
                uint32_t added_triangles = (index_after - index_before) / 3u;
                if (!fbx_mesh_material_map_append(material_map, material_slot, added_triangles)) {
                    fbx_mesh_material_map_free(material_map);
                    return fbx_geom_fail(polygon, polygon_stack, mesh);
                }
            }
            poly_count = 0;
            polygon_invalid = 0;
            polygon_idx++;
        }
    }

    if (polygon != polygon_stack)
        free(polygon);
    if (((rt_mesh3d *)mesh)->index_count < 3u) {
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }
    if (!norm_layer.data || norm_layer.count == 0)
        rt_mesh3d_recalc_normals(mesh);
    return mesh;
}

/*==========================================================================
 * Material extraction
 *=========================================================================*/

/// @brief Convert an FBX `Material` node into a Viper `rt_material3d_t`.
///
/// Reads `DiffuseColor` and `Shininess` from the property table.
/// Other PBR attributes (metallic, roughness, normal/AO maps) are
/// not surfaced here — FBX stores them in extension blocks that
/// vary by exporter, so we leave them at the material's defaults.
static void *fbx_extract_material(fbx_node_t *mat_node) {
    if (!mat_node)
        return NULL;
    void *mat = rt_material3d_new();
    if (!mat)
        return NULL;

    fbx_node_t *p70 = fbx_find_child(mat_node, "Properties70");
    if (!p70)
        return mat;

    for (int32_t i = 0; i < p70->child_count; i++) {
        fbx_node_t *p = &p70->children[i];
        if (strcmp(p->name, "P") != 0 || p->prop_count < 7)
            continue;
        const char *pname = fbx_prop_str(p, 0);
        if (strcmp(pname, "DiffuseColor") == 0) {
            double r = fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1.0, 1.0);
            double g = fbx_clamp_double(fbx_prop_f64(p, 5), 0.0, 1.0, 1.0);
            double b = fbx_clamp_double(fbx_prop_f64(p, 6), 0.0, 1.0, 1.0);
            rt_material3d_set_color(mat, r, g, b);
        } else if (strcmp(pname, "Shininess") == 0 || strcmp(pname, "ShininessExponent") == 0) {
            double s = fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1000000.0, 32.0);
            rt_material3d_set_shininess(mat, s);
        } else if (strcmp(pname, "Opacity") == 0) {
            double alpha = fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1.0, 1.0);
            rt_material3d_set_alpha(mat, alpha);
            if (alpha < 1.0)
                rt_material3d_set_alpha_mode(mat, 2);
        } else if (strcmp(pname, "TransparencyFactor") == 0) {
            double alpha = 1.0 - fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1.0, 0.0);
            rt_material3d_set_alpha(mat, alpha);
            if (alpha < 1.0)
                rt_material3d_set_alpha_mode(mat, 2);
        } else if (strcmp(pname, "EmissiveColor") == 0) {
            double r = fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1.0, 0.0);
            double g = fbx_clamp_double(fbx_prop_f64(p, 5), 0.0, 1.0, 0.0);
            double b = fbx_clamp_double(fbx_prop_f64(p, 6), 0.0, 1.0, 0.0);
            rt_material3d_set_emissive_color(mat, r, g, b);
        } else if (strcmp(pname, "EmissiveFactor") == 0) {
            rt_material3d_set_emissive_intensity(
                mat, fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1000000.0, 0.0));
        }
    }

    return mat;
}

/*==========================================================================
 * Scene graph extraction
 *=========================================================================*/

/// @brief Release a GC-managed object held in `*slot` and NULL-out the slot.
static void fbx_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Stash a namespace-stripped FBX object name onto a `SceneNode3D`. Skips the
/// assignment entirely when the decoded name is empty so a node without a meaningful
/// name keeps whatever default was already set (instead of being overwritten with `""`).
/// 128-byte stack buffer matches the decode limit — longer FBX names get truncated.
static void fbx_set_clean_object_name(void *node, const char *raw_name) {
    char name[128];
    fbx_decode_object_name(raw_name, name, sizeof(name));
    if (*name)
        rt_scene_node3d_set_name(node, rt_const_cstr(name));
}

/// @brief Pull Lcl-Translation / Lcl-Rotation / Lcl-Scaling off an FBX `Model` node's
/// `Properties70` block and convert into the engine's TRS triple. FBX stores Euler
/// angles in degrees with XYZ order — this routine re-derives the quaternion from the
/// three Euler components using the standard "half-angle" construction (c = cos(θ/2),
/// s = sin(θ/2)) and multiplies in XYZ order to match FBX's rotation convention.
/// When `z_up` is set, the translation is run through `fbx_correct_zup` so the Z-up
/// authoring orientation maps onto Viper's Y-up runtime. Missing P-properties default
/// to (0,0,0) / identity / (1,1,1) so partial models still build cleanly.
static void fbx_extract_model_trs(
    fbx_node_t *model_node, int z_up, double *pos, double *quat, double *scale) {
    double tx = 0.0;
    double ty = 0.0;
    double tz = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double rz = 0.0;
    double pre_rx = 0.0;
    double pre_ry = 0.0;
    double pre_rz = 0.0;
    double post_rx = 0.0;
    double post_ry = 0.0;
    double post_rz = 0.0;
    double geo_tx = 0.0;
    double geo_ty = 0.0;
    double geo_tz = 0.0;
    double geo_rx = 0.0;
    double geo_ry = 0.0;
    double geo_rz = 0.0;
    double rot_off_x = 0.0;
    double rot_off_y = 0.0;
    double rot_off_z = 0.0;
    double scale_off_x = 0.0;
    double scale_off_y = 0.0;
    double scale_off_z = 0.0;
    double sx = 1.0;
    double sy = 1.0;
    double sz = 1.0;
    double geo_sx = 1.0;
    double geo_sy = 1.0;
    double geo_sz = 1.0;
    double hx;
    double hy;
    double hz;
    double cx;
    double cy;
    double cz;
    double sxh;
    double syh;
    double szh;
    double qx;
    double qy;
    double qz;
    double qw;
    fbx_node_t *p70;

    if (pos) {
        pos[0] = 0.0;
        pos[1] = 0.0;
        pos[2] = 0.0;
    }
    if (quat) {
        quat[0] = 0.0;
        quat[1] = 0.0;
        quat[2] = 0.0;
        quat[3] = 1.0;
    }
    if (scale) {
        scale[0] = 1.0;
        scale[1] = 1.0;
        scale[2] = 1.0;
    }
    if (!model_node)
        return;

    p70 = fbx_find_child(model_node, "Properties70");
    if (p70) {
        for (int32_t pi = 0; pi < p70->child_count; pi++) {
            fbx_node_t *p = &p70->children[pi];
            const char *pn;
            if (strcmp(p->name, "P") != 0 || p->prop_count < 7)
                continue;
            pn = fbx_prop_str(p, 0);
            if (strcmp(pn, "Lcl Translation") == 0) {
                tx = fbx_prop_f64(p, 4);
                ty = fbx_prop_f64(p, 5);
                tz = fbx_prop_f64(p, 6);
            } else if (strcmp(pn, "Lcl Rotation") == 0) {
                rx = fbx_prop_f64(p, 4);
                ry = fbx_prop_f64(p, 5);
                rz = fbx_prop_f64(p, 6);
            } else if (strcmp(pn, "Lcl Scaling") == 0) {
                sx = fbx_prop_f64(p, 4);
                sy = fbx_prop_f64(p, 5);
                sz = fbx_prop_f64(p, 6);
            } else if (strcmp(pn, "PreRotation") == 0) {
                pre_rx = fbx_prop_f64(p, 4);
                pre_ry = fbx_prop_f64(p, 5);
                pre_rz = fbx_prop_f64(p, 6);
            } else if (strcmp(pn, "PostRotation") == 0) {
                post_rx = fbx_prop_f64(p, 4);
                post_ry = fbx_prop_f64(p, 5);
                post_rz = fbx_prop_f64(p, 6);
            } else if (strcmp(pn, "GeometricTranslation") == 0) {
                geo_tx = fbx_prop_f64(p, 4);
                geo_ty = fbx_prop_f64(p, 5);
                geo_tz = fbx_prop_f64(p, 6);
            } else if (strcmp(pn, "GeometricRotation") == 0) {
                geo_rx = fbx_prop_f64(p, 4);
                geo_ry = fbx_prop_f64(p, 5);
                geo_rz = fbx_prop_f64(p, 6);
            } else if (strcmp(pn, "GeometricScaling") == 0) {
                geo_sx = fbx_prop_f64(p, 4);
                geo_sy = fbx_prop_f64(p, 5);
                geo_sz = fbx_prop_f64(p, 6);
            } else if (strcmp(pn, "RotationOffset") == 0) {
                rot_off_x = fbx_prop_f64(p, 4);
                rot_off_y = fbx_prop_f64(p, 5);
                rot_off_z = fbx_prop_f64(p, 6);
            } else if (strcmp(pn, "ScalingOffset") == 0) {
                scale_off_x = fbx_prop_f64(p, 4);
                scale_off_y = fbx_prop_f64(p, 5);
                scale_off_z = fbx_prop_f64(p, 6);
            }
        }
    }

    tx += geo_tx + rot_off_x + scale_off_x;
    ty += geo_ty + rot_off_y + scale_off_y;
    tz += geo_tz + rot_off_z + scale_off_z;
    rx += pre_rx + post_rx + geo_rx;
    ry += pre_ry + post_ry + geo_ry;
    rz += pre_rz + post_rz + geo_rz;
    sx *= geo_sx;
    sy *= geo_sy;
    sz *= geo_sz;

    tx = fbx_clamp_abs_or(tx, 0.0, FBX_NUMERIC_ABS_MAX);
    ty = fbx_clamp_abs_or(ty, 0.0, FBX_NUMERIC_ABS_MAX);
    tz = fbx_clamp_abs_or(tz, 0.0, FBX_NUMERIC_ABS_MAX);
    rx = fbx_sanitize_rotation_degrees(rx);
    ry = fbx_sanitize_rotation_degrees(ry);
    rz = fbx_sanitize_rotation_degrees(rz);
    sx = fbx_scale_or_unit(sx);
    sy = fbx_scale_or_unit(sy);
    sz = fbx_scale_or_unit(sz);

    if (z_up)
        fbx_correct_zup(&tx, &ty, &tz);
    fbx_sanitize_position3(&tx, &ty, &tz);

    hx = rx * 3.14159265358979323846 / 360.0;
    hy = ry * 3.14159265358979323846 / 360.0;
    hz = rz * 3.14159265358979323846 / 360.0;
    cx = cos(hx);
    cy = cos(hy);
    cz = cos(hz);
    sxh = sin(hx);
    syh = sin(hy);
    szh = sin(hz);
    qw = cx * cy * cz + sxh * syh * szh;
    qx = sxh * cy * cz - cx * syh * szh;
    qy = cx * syh * cz + sxh * cy * szh;
    qz = cx * cy * szh - sxh * syh * cz;
    {
        double qlen = sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (isfinite(qlen) && qlen > 1e-12) {
            qx /= qlen;
            qy /= qlen;
            qz /= qlen;
            qw /= qlen;
        } else {
            qx = qy = qz = 0.0;
            qw = 1.0;
        }
    }

    if (pos) {
        pos[0] = fbx_clamp_abs_or(tx, 0.0, FBX_NUMERIC_ABS_MAX);
        pos[1] = fbx_clamp_abs_or(ty, 0.0, FBX_NUMERIC_ABS_MAX);
        pos[2] = fbx_clamp_abs_or(tz, 0.0, FBX_NUMERIC_ABS_MAX);
    }
    if (quat) {
        quat[0] = qx;
        quat[1] = qy;
        quat[2] = qz;
        quat[3] = qw;
    }
    if (scale) {
        scale[0] = fbx_scale_or_unit(sx);
        scale[1] = fbx_scale_or_unit(sy);
        scale[2] = fbx_scale_or_unit(sz);
    }
}

/// @brief Linear-search @p bindings for the entry with the given @p id; NULL if not found.
static const fbx_mesh_binding_t *fbx_lookup_mesh_binding_entry(const fbx_mesh_binding_t *bindings,
                                                               int32_t count,
                                                               int64_t id) {
    for (int32_t i = 0; i < count; i++)
        if (bindings[i].id == id)
            return &bindings[i];
    return NULL;
}

/// @brief Build a new Mesh3D containing only the triangles of @p src assigned to
///   @p material_slot, compacting the referenced vertices through a remap table so the
///   sub-mesh has no unused vertices.
/// @return The new mesh object, or NULL if no triangle uses the slot or on allocation failure.
static void *fbx_clone_mesh_for_material_slot(const rt_mesh3d *src,
                                              const fbx_mesh_material_map_t *map,
                                              int32_t material_slot) {
    void *mesh_obj;
    rt_mesh3d *dst;
    uint32_t selected_triangles = 0;
    uint32_t *vertex_map = NULL;
    if (!src || !map || !map->triangle_slots || material_slot < 0)
        return NULL;
    for (uint32_t tri = 0; tri < map->triangle_count; tri++) {
        if (map->triangle_slots[tri] == material_slot)
            selected_triangles++;
    }
    if (selected_triangles == 0 || selected_triangles > UINT32_MAX / 3u)
        return NULL;
    mesh_obj = rt_mesh3d_new();
    if (!mesh_obj)
        return NULL;
    dst = (rt_mesh3d *)mesh_obj;
    rt_mesh3d_begin_geometry_batch(dst);
    rt_mesh3d_reserve(mesh_obj, (int64_t)selected_triangles * 3, selected_triangles);
    if (dst->build_failed)
        goto fail;
    if (src->positions64 && src->vertex_count > 0) {
        if ((size_t)dst->vertex_capacity > SIZE_MAX / (3u * sizeof(double)))
            goto fail;
        dst->positions64 = (double *)calloc((size_t)dst->vertex_capacity * 3u, sizeof(double));
        if (!dst->positions64)
            goto fail;
    }
    if ((size_t)src->vertex_count > SIZE_MAX / sizeof(*vertex_map))
        goto fail;
    vertex_map = (uint32_t *)malloc((size_t)src->vertex_count * sizeof(*vertex_map));
    if (!vertex_map)
        goto fail;
    for (uint32_t i = 0; i < src->vertex_count; i++)
        vertex_map[i] = UINT32_MAX;
    for (uint32_t tri = 0; tri < map->triangle_count; tri++) {
        if (map->triangle_slots[tri] != material_slot)
            continue;
        for (uint32_t corner = 0; corner < 3u; corner++) {
            uint32_t old_index = src->indices[tri * 3u + corner];
            uint32_t new_index;
            if (old_index >= src->vertex_count)
                goto fail;
            if (vertex_map[old_index] == UINT32_MAX) {
                if (dst->vertex_count >= dst->vertex_capacity)
                    goto fail;
                new_index = dst->vertex_count++;
                vertex_map[old_index] = new_index;
                dst->vertices[new_index] = src->vertices[old_index];
                if (dst->positions64 && src->positions64) {
                    memcpy(&dst->positions64[(size_t)new_index * 3u],
                           &src->positions64[(size_t)old_index * 3u],
                           3u * sizeof(double));
                }
            } else {
                new_index = vertex_map[old_index];
            }
            if (dst->index_count >= dst->index_capacity)
                goto fail;
            dst->indices[dst->index_count++] = new_index;
        }
    }
    dst->bone_count = src->bone_count;
    dst->bounds_dirty = 1;
    rt_mesh3d_end_geometry_batch(dst);
    free(vertex_map);
    return mesh_obj;
fail:
    rt_mesh3d_end_geometry_batch(dst);
    free(vertex_map);
    if (rt_obj_release_check0(mesh_obj))
        rt_obj_free(mesh_obj);
    return NULL;
}

/// @brief Linear search for a `Material3D` associated with an FBX object ID. Mirror of
/// `fbx_lookup_mesh_binding` for the material table.
static void *fbx_lookup_material_binding(const fbx_material_binding_t *bindings,
                                         int32_t count,
                                         int64_t id) {
    for (int32_t i = 0; i < count; i++)
        if (bindings[i].id == id)
            return bindings[i].material;
    return NULL;
}

/// @brief Three-phase builder that assembles a Viper `SceneNode3D` graph from the flat
/// FBX `Objects` / `Connections` soup. Phase 1: iterate `Model` entries, skip skeleton
/// limbs (handled by the skeleton importer), allocate a scene node per model with
/// decoded name and extracted TRS, recording its FBX id + parent id. Phase 2: walk
/// connections again to attach Meshes, Materials, and extra-mesh groupings (via
/// auxiliary child nodes) to their parent models. Phase 3: parent nodes to each other
/// using the recorded parent ids, rooting orphans under a synthesized scene root.
/// Returns the scene root or NULL on any allocation failure (intermediate nodes are
/// released via the GC on failure — no manual cleanup needed). This is the "scene
/// graph" half of the FBX importer; the flat mesh/material/animation lists live on
/// `rt_fbx_asset` alongside the root.
static void *fbx_build_scene_root(fbx_node_t *root,
                                  fbx_node_t *objects,
                                  const fbx_conn_table_t *ct,
                                  const fbx_mesh_binding_t *mesh_bindings,
                                  int32_t mesh_binding_count,
                                  const fbx_material_binding_t *material_bindings,
                                  int32_t material_binding_count,
                                  int z_up,
                                  int *out_failed) {
    typedef struct {
        int64_t id;
        int64_t parent_id;
        fbx_node_t *source;
        void *node;
    } fbx_model_binding_t;

    fbx_model_binding_t *models = NULL;
    int32_t model_count = 0;
    int32_t model_capacity = 0;
    void *scene_root = NULL;
    void *default_material = NULL;
    int failed = 0;

    if (out_failed)
        *out_failed = 0;

    if (!root || !objects)
        return NULL;

    for (int32_t i = 0; i < objects->child_count; i++) {
        fbx_node_t *obj = &objects->children[i];
        const char *type_str;
        void *node;
        void *quat_obj;
        double pos[3];
        double quat[4];
        double scale[3];
        int64_t model_id;

        if (strcmp(obj->name, "Model") != 0 || obj->prop_count < 3)
            continue;
        type_str = fbx_prop_str(obj, 2);
        if (strcmp(type_str, "LimbNode") == 0 || strcmp(type_str, "Limb") == 0 ||
            strcmp(type_str, "Root") == 0) {
            continue;
        }

        if (model_count >= model_capacity) {
            if (model_capacity < 0 || model_capacity > INT32_MAX / 2 ||
                (size_t)(model_capacity == 0 ? 16 : model_capacity * 2) >
                    SIZE_MAX / sizeof(*models)) {
                failed = 1;
                break;
            }
            int32_t new_capacity = model_capacity == 0 ? 16 : model_capacity * 2;
            void *nm = realloc(models, (size_t)new_capacity * sizeof(*models));
            if (!nm) {
                failed = 1;
                break;
            }
            models = (fbx_model_binding_t *)nm;
            model_capacity = new_capacity;
        }

        node = rt_scene_node3d_new();
        if (!node) {
            failed = 1;
            break;
        }
        model_id = fbx_prop_i64(obj, 0);
        fbx_set_clean_object_name(node, fbx_prop_str(obj, 1));
        fbx_extract_model_trs(obj, z_up, pos, quat, scale);
        rt_scene_node3d_set_position(node, pos[0], pos[1], pos[2]);
        quat_obj = rt_quat_new(quat[0], quat[1], quat[2], quat[3]);
        if (quat_obj) {
            rt_scene_node3d_set_rotation(node, quat_obj);
            fbx_release_ref(&quat_obj);
        }
        rt_scene_node3d_set_scale(node, scale[0], scale[1], scale[2]);

        models[model_count].id = model_id;
        models[model_count].parent_id = fbx_find_parent(ct, model_id);
        models[model_count].source = obj;
        models[model_count].node = node;
        model_count++;
    }

    if (!failed) {
        scene_root = rt_scene_node3d_new();
        if (!scene_root)
            failed = 1;
        default_material = rt_material3d_new();
        if (!default_material)
            failed = 1;
    }

    for (int32_t i = 0; !failed && i < model_count; i++) {
        void *primary_mesh = NULL;
        void *primary_material = NULL;
        int32_t extra_mesh_count = 0;
        void **model_materials = NULL;
        int32_t model_material_count = 0;
        int32_t model_material_capacity = 0;
        for (int32_t ci = 0; ci < ct->count; ci++) {
            int64_t child_id = ct->entries[ci].child_id;
            int64_t parent_id = ct->entries[ci].parent_id;
            fbx_node_t *child_obj;
            void *material;

            if (parent_id != models[i].id)
                continue;
            child_obj = fbx_find_object_by_id(objects, child_id);
            if (!child_obj || child_obj->prop_count < 1)
                continue;
            if (strcmp(child_obj->name, "Material") != 0)
                continue;
            material =
                fbx_lookup_material_binding(material_bindings, material_binding_count, child_id);
            if (material) {
                if (model_material_count >= model_material_capacity) {
                    int32_t new_capacity =
                        model_material_capacity == 0 ? 4 : model_material_capacity * 2;
                    void **grown;
                    if (model_material_capacity > INT32_MAX / 2 ||
                        (size_t)new_capacity > SIZE_MAX / sizeof(*model_materials)) {
                        failed = 1;
                        break;
                    }
                    grown =
                        (void **)realloc(model_materials, (size_t)new_capacity * sizeof(*grown));
                    if (!grown) {
                        failed = 1;
                        break;
                    }
                    model_materials = grown;
                    model_material_capacity = new_capacity;
                }
                model_materials[model_material_count++] = material;
                if (!primary_material)
                    primary_material = material;
            }
        }
        for (int32_t ci = 0; !failed && ci < ct->count; ci++) {
            int64_t child_id = ct->entries[ci].child_id;
            int64_t parent_id = ct->entries[ci].parent_id;
            fbx_node_t *child_obj;
            const fbx_mesh_binding_t *binding;
            void *mesh;
            void *material;
            void *mesh_node;

            if (parent_id != models[i].id)
                continue;
            child_obj = fbx_find_object_by_id(objects, child_id);
            if (!child_obj || child_obj->prop_count < 3)
                continue;
            if (strcmp(child_obj->name, "Geometry") != 0 ||
                strcmp(fbx_prop_str(child_obj, 2), "Mesh") != 0) {
                continue;
            }
            binding = fbx_lookup_mesh_binding_entry(mesh_bindings, mesh_binding_count, child_id);
            if (!binding || !binding->mesh)
                continue;
            mesh = binding->mesh;
            material = primary_material ? primary_material : default_material;
            if (binding->material_map.has_slots && binding->material_map.triangle_count > 0 &&
                binding->material_map.triangle_slots) {
                int32_t slot = binding->material_map.triangle_slots[0];
                if (slot >= 0 && slot < model_material_count && model_materials[slot])
                    material = model_materials[slot];
            }
            if (binding->material_map.has_slots && binding->material_map.slot_count > 1 &&
                model_material_count > 1) {
                for (int32_t slot = 0; slot < binding->material_map.slot_count; slot++) {
                    void *slot_mesh = fbx_clone_mesh_for_material_slot(
                        (const rt_mesh3d *)mesh, &binding->material_map, slot);
                    void *slot_material = slot < model_material_count && model_materials[slot]
                                              ? model_materials[slot]
                                              : default_material;
                    if (!slot_mesh)
                        continue;
                    mesh_node = rt_scene_node3d_new();
                    if (!mesh_node) {
                        fbx_release_ref(&slot_mesh);
                        failed = 1;
                        break;
                    }
                    {
                        char generated_name[96];
                        snprintf(generated_name,
                                 sizeof(generated_name),
                                 "mesh_%d_material_%d",
                                 extra_mesh_count,
                                 slot);
                        rt_scene_node3d_set_name(mesh_node, rt_const_cstr(generated_name));
                    }
                    rt_scene_node3d_set_mesh(mesh_node, slot_mesh);
                    rt_scene_node3d_set_material(mesh_node, slot_material);
                    if (!rt_scene_node3d_try_add_child(models[i].node, mesh_node)) {
                        fbx_release_ref(&mesh_node);
                        fbx_release_ref(&slot_mesh);
                        failed = 1;
                        break;
                    }
                    fbx_release_ref(&mesh_node);
                    fbx_release_ref(&slot_mesh);
                    extra_mesh_count++;
                }
                continue;
            }
            if (!primary_mesh) {
                primary_mesh = mesh;
                primary_material = material;
                continue;
            }
            mesh_node = rt_scene_node3d_new();
            if (!mesh_node) {
                failed = 1;
                break;
            }
            if (child_obj->prop_count >= 2)
                fbx_set_clean_object_name(mesh_node, fbx_prop_str(child_obj, 1));
            if (extra_mesh_count > 0) {
                char generated_name[64];
                snprintf(generated_name, sizeof(generated_name), "mesh_%d", extra_mesh_count);
                rt_scene_node3d_set_name(mesh_node, rt_const_cstr(generated_name));
            }
            rt_scene_node3d_set_mesh(mesh_node, mesh);
            rt_scene_node3d_set_material(mesh_node, material ? material : default_material);
            if (!rt_scene_node3d_try_add_child(models[i].node, mesh_node)) {
                fbx_release_ref(&mesh_node);
                failed = 1;
                break;
            }
            fbx_release_ref(&mesh_node);
            extra_mesh_count++;
        }
        free(model_materials);
        if (failed)
            break;
        if (primary_mesh) {
            rt_scene_node3d_set_mesh(models[i].node, primary_mesh);
            rt_scene_node3d_set_material(models[i].node,
                                         primary_material ? primary_material : default_material);
        }
    }

    for (int32_t i = 0; !failed && i < model_count; i++) {
        void *parent = scene_root;
        if (models[i].parent_id != 0) {
            for (int32_t pi = 0; pi < model_count; pi++) {
                if (models[pi].id == models[i].parent_id) {
                    parent = models[pi].node;
                    break;
                }
            }
        }
        if (!rt_scene_node3d_try_add_child(parent, models[i].node)) {
            failed = 1;
            break;
        }
    }

    for (int32_t i = 0; i < model_count; i++)
        fbx_release_ref(&models[i].node);
    free(models);
    fbx_release_ref(&default_material);

    if (failed) {
        if (out_failed)
            *out_failed = 1;
        fbx_release_ref(&scene_root);
        return NULL;
    }
    return scene_root;
}

/*==========================================================================
 * Skeleton extraction
 *=========================================================================*/

/// @brief Build a `rt_skeleton3d_t` from the FBX `Model` nodes of type LimbNode/Limb/Root.
///
/// Walks the connection table to determine each bone's parent
/// (translating bone-IDs to in-skeleton indices), pulls the
/// `Lcl Translation/Rotation/Scaling` properties for the bind
/// pose, and computes inverse-bind matrices for skinning.
/// `z_up` triggers the same axis-swap normalisation as the geometry pass.
static void *fbx_extract_skeleton(fbx_node_t *root,
                                  const fbx_conn_table_t *ct,
                                  int z_up,
                                  fbx_bone_binding_t **out_bindings,
                                  int32_t *out_binding_count) {
    fbx_bone_binding_t *bindings = NULL;
    int32_t binding_count = 0;
    if (out_bindings)
        *out_bindings = NULL;
    if (out_binding_count)
        *out_binding_count = 0;
    fbx_node_t *objects = fbx_find_child(root, "Objects");
    if (!objects)
        return NULL;

    /* Collect Model nodes that are skeleton limbs */
    typedef struct {
        int64_t id;
        char name[64];
        double lcl_translation[3];
        double lcl_rotation[3];
        double lcl_scaling[3];
        int64_t parent_id;
        int32_t bone_index; /* assigned after topological sort */
    } bone_info_t;

    bone_info_t *bones = NULL;
    int32_t bone_count = 0;
    int32_t bone_cap = 0;

    for (int32_t i = 0; i < objects->child_count; i++) {
        fbx_node_t *obj = &objects->children[i];
        if (strcmp(obj->name, "Model") != 0 || obj->prop_count < 3)
            continue;
        const char *type_str = fbx_prop_str(obj, 2);
        if (strcmp(type_str, "LimbNode") != 0 && strcmp(type_str, "Limb") != 0 &&
            strcmp(type_str, "Root") != 0)
            continue;
        if (bone_count >= FBX_MAX_SKELETON_BONES)
            continue;

        if (bone_count >= bone_cap) {
            if (bone_cap < 0 || bone_cap > INT32_MAX / 2 ||
                (size_t)(bone_cap == 0 ? 32 : bone_cap * 2) > SIZE_MAX / sizeof(bone_info_t))
                break;
            int32_t new_cap = bone_cap == 0 ? 32 : bone_cap * 2;
            bone_info_t *nb = (bone_info_t *)realloc(bones, (size_t)new_cap * sizeof(bone_info_t));
            if (!nb)
                break;
            bones = nb;
            bone_cap = new_cap;
        }

        bone_info_t *bi = &bones[bone_count++];
        memset(bi, 0, sizeof(bone_info_t));
        bi->id = fbx_prop_i64(obj, 0);
        char decoded_name[64];
        const char *nstr;
        fbx_decode_object_name(fbx_prop_str(obj, 1), decoded_name, sizeof(decoded_name));
        nstr = decoded_name;
        size_t nlen = strlen(nstr);
        if (nlen > 63)
            nlen = 63;
        memcpy(bi->name, nstr, nlen);
        bi->name[nlen] = '\0';
        bi->lcl_scaling[0] = bi->lcl_scaling[1] = bi->lcl_scaling[2] = 1.0;

        /* Extract Lcl Translation/Rotation/Scaling from Properties70 */
        fbx_node_t *p70 = fbx_find_child(obj, "Properties70");
        if (p70) {
            for (int32_t pi = 0; pi < p70->child_count; pi++) {
                fbx_node_t *p = &p70->children[pi];
                if (strcmp(p->name, "P") != 0 || p->prop_count < 7)
                    continue;
                const char *pn = fbx_prop_str(p, 0);
                if (strcmp(pn, "Lcl Translation") == 0) {
                    bi->lcl_translation[0] = fbx_prop_f64(p, 4);
                    bi->lcl_translation[1] = fbx_prop_f64(p, 5);
                    bi->lcl_translation[2] = fbx_prop_f64(p, 6);
                } else if (strcmp(pn, "Lcl Rotation") == 0) {
                    bi->lcl_rotation[0] = fbx_prop_f64(p, 4);
                    bi->lcl_rotation[1] = fbx_prop_f64(p, 5);
                    bi->lcl_rotation[2] = fbx_prop_f64(p, 6);
                } else if (strcmp(pn, "Lcl Scaling") == 0) {
                    bi->lcl_scaling[0] = fbx_prop_f64(p, 4);
                    bi->lcl_scaling[1] = fbx_prop_f64(p, 5);
                    bi->lcl_scaling[2] = fbx_prop_f64(p, 6);
                }
            }
        }

        bi->parent_id = fbx_find_parent(ct, bi->id);
    }

    if (bone_count == 0) {
        free(bones);
        return NULL;
    }

    /* Build skeleton in topological order */
    void *skel = rt_skeleton3d_new();
    if (!skel) {
        free(bones);
        return NULL;
    }

    /* Assign bone indices: process bones in parent-first order */
    int32_t *order = (int32_t *)calloc((size_t)bone_count, sizeof(int32_t));
    int8_t *placed = (int8_t *)calloc((size_t)bone_count, sizeof(int8_t));
    int32_t placed_count = 0;
    int failed = 0;
    if (!order || !placed) {
        free(order);
        free(placed);
        free(bones);
        fbx_release_ref(&skel);
        return NULL;
    }

    /* Place roots first */
    for (int32_t i = 0; i < bone_count; i++) {
        int is_root = 1;
        for (int32_t j = 0; j < bone_count; j++)
            if (bones[i].parent_id == bones[j].id) {
                is_root = 0;
                break;
            }
        if (is_root) {
            order[placed_count++] = i;
            placed[i] = 1;
        }
    }

    /* Place children */
    for (int32_t pass = 0; pass < bone_count && placed_count < bone_count; pass++) {
        for (int32_t i = 0; i < bone_count; i++) {
            if (placed[i])
                continue;
            for (int32_t j = 0; j < placed_count; j++) {
                if (bones[i].parent_id == bones[order[j]].id) {
                    order[placed_count++] = i;
                    placed[i] = 1;
                    break;
                }
            }
        }
    }
    if (placed_count <= 0) {
        free(order);
        free(placed);
        free(bones);
        fbx_release_ref(&skel);
        return NULL;
    }
    if (out_bindings && out_binding_count) {
        bindings = (fbx_bone_binding_t *)calloc((size_t)placed_count, sizeof(*bindings));
        if (!bindings) {
            free(order);
            free(placed);
            free(bones);
            fbx_release_ref(&skel);
            return NULL;
        }
    }

    /* Add bones to skeleton in topological order */
    for (int32_t i = 0; i < placed_count; i++) {
        bone_info_t *bi = &bones[order[i]];
        int64_t parent_idx = -1;
        for (int32_t j = 0; j < i; j++) {
            if (bi->parent_id == bones[order[j]].id) {
                parent_idx = j;
                break;
            }
        }

        double tx = bi->lcl_translation[0], ty = bi->lcl_translation[1],
               tz = bi->lcl_translation[2];
        if (z_up)
            fbx_correct_zup(&tx, &ty, &tz);
        if (!fbx_sanitize_position3(&tx, &ty, &tz)) {
            tx = 0.0;
            ty = 0.0;
            tz = 0.0;
        }

        /* Build full TRS bind matrix (rotation from Euler ZYX, then scale) */
        double rx =
            fbx_sanitize_rotation_degrees(bi->lcl_rotation[0]) * 3.14159265358979323846 / 180.0;
        double ry =
            fbx_sanitize_rotation_degrees(bi->lcl_rotation[1]) * 3.14159265358979323846 / 180.0;
        double rz =
            fbx_sanitize_rotation_degrees(bi->lcl_rotation[2]) * 3.14159265358979323846 / 180.0;
        double cxr = cos(rx), sxr = sin(rx);
        double cyr = cos(ry), syr = sin(ry);
        double czr = cos(rz), szr = sin(rz);
        /* R = Rz * Ry * Rx (standard FBX Euler order) */
        double r00 = cyr * czr, r01 = sxr * syr * czr - cxr * szr,
               r02 = cxr * syr * czr + sxr * szr;
        double r10 = cyr * szr, r11 = sxr * syr * szr + cxr * czr,
               r12 = cxr * syr * szr - sxr * czr;
        double r20 = -syr, r21 = sxr * cyr, r22 = cxr * cyr;
        double scx = fbx_scale_or_unit(bi->lcl_scaling[0]);
        double scy = fbx_scale_or_unit(bi->lcl_scaling[1]);
        double scz = fbx_scale_or_unit(bi->lcl_scaling[2]);
        void *bind_mat = rt_mat4_new(r00 * scx,
                                     r01 * scy,
                                     r02 * scz,
                                     tx,
                                     r10 * scx,
                                     r11 * scy,
                                     r12 * scz,
                                     ty,
                                     r20 * scx,
                                     r21 * scy,
                                     r22 * scz,
                                     tz,
                                     0,
                                     0,
                                     0,
                                     1);
        int64_t added = rt_skeleton3d_add_bone(skel, rt_const_cstr(bi->name), parent_idx, bind_mat);
        fbx_release_ref(&bind_mat);
        if (added < 0) {
            failed = 1;
            break;
        }
        bi->bone_index = (int32_t)added;
        if (bindings) {
            bindings[binding_count].model_id = bi->id;
            bindings[binding_count].bone_index = (int32_t)added;
            binding_count++;
        }
    }
    if (failed) {
        free(bindings);
        free(order);
        free(placed);
        free(bones);
        fbx_release_ref(&skel);
        return NULL;
    }

    rt_skeleton3d_compute_inverse_bind(skel);

    free(order);
    free(placed);
    free(bones);
    if (out_bindings)
        *out_bindings = bindings;
    if (out_binding_count)
        *out_binding_count = binding_count;
    return skel;
}

/*==========================================================================
 * Animation extraction
 *=========================================================================*/

#define FBX_TIME_SECOND 46186158000LL

/// @brief Find the direct child of the top-level Objects node whose first property matches @p id.
/// @details FBX assigns every scene object a unique 64-bit ID stored as the first property on
///          the node.  This linear scan is used for per-object resolution; call sites are bounded
///          by the number of clusters / connections, not the total object count.
/// @return Pointer into the @p objects children array, or NULL if no child carries that ID.
static fbx_node_t *fbx_find_object_by_id(fbx_node_t *objects, int64_t id) {
    for (int32_t i = 0; i < objects->child_count; i++) {
        fbx_node_t *obj = &objects->children[i];
        if (obj->prop_count >= 1 && fbx_prop_i64(obj, 0) == id)
            return obj;
    }
    return NULL;
}

/// @brief Locate a child node named @p child_name and return its first property as an int64 array.
/// @details Expects the property type byte to be `'l'` (FBX long-array).  Writes the element
///          count to *count and returns a pointer into the already-decoded in-memory array.
///          Returns NULL and leaves *count unchanged on a missing child or wrong property type.
static const int64_t *fbx_get_i64_array(fbx_node_t *node, const char *child_name, uint32_t *count) {
    fbx_node_t *child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return NULL;
    fbx_prop_t *p = &child->props[0];
    if (p->type == 'l' && p->v.array.data) {
        *count = p->v.array.count;
        return (const int64_t *)p->v.array.data;
    }
    return NULL;
}

/// @brief Locate a child node named @p child_name and return its first property as an int32 array.
/// @details Expects property type `'i'` (FBX int-array).  Used to read per-vertex indices such
///          as cluster Indexes.  Returns NULL and leaves *count unchanged on type mismatch.
static const int32_t *fbx_get_i32_array(fbx_node_t *node, const char *child_name, uint32_t *count) {
    fbx_node_t *child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return NULL;
    fbx_prop_t *p = &child->props[0];
    if (p->type == 'i' && p->v.array.data) {
        *count = p->v.array.count;
        return (const int32_t *)p->v.array.data;
    }
    return NULL;
}

/// @brief Locate a child node named @p child_name and return its first property as a double array.
/// @details Expects property type `'d'` (FBX double-array). When the property is type `'f'`
///          (float-array), writes *count but returns NULL — callers should fall back to
///          fbx_get_f32_array in that case. Used for animation curve values and blend weights.
static const double *fbx_get_f64_array(fbx_node_t *node, const char *child_name, uint32_t *count) {
    fbx_node_t *child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return NULL;
    fbx_prop_t *p = &child->props[0];
    if (p->type == 'd' && p->v.array.data) {
        *count = p->v.array.count;
        return (const double *)p->v.array.data;
    }
    /* Also handle float arrays by reading as float */
    if (p->type == 'f' && p->v.array.data) {
        *count = p->v.array.count;
        return NULL; /* caller must handle float arrays separately */
    }
    return NULL;
}

/// @brief Convenience: locate `node->child_name` and return its float-array data + count.
/// Returns NULL on missing child or non-float-array property.
static const float *fbx_get_f32_array(fbx_node_t *node, const char *child_name, uint32_t *count) {
    fbx_node_t *child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return NULL;
    fbx_prop_t *p = &child->props[0];
    if (p->type == 'f' && p->v.array.data) {
        *count = p->v.array.count;
        return (const float *)p->v.array.data;
    }
    return NULL;
}

/// @brief Resolve an FBX Model node ID to its engine bone index in @p skeleton.
/// @details Looks up the Model node by @p model_id, decodes its display name (stripping the
///          FBX `"Name\x00\x01Type"` suffix), then asks the engine skeleton for the matching
///          bone index via `rt_skeleton3d_find_bone`.
/// @return Engine bone index in [0, bone_count), or -1 if the model is not found or not a bone.
static int32_t fbx_find_bone_index_for_model(fbx_node_t *objects,
                                             void *skeleton,
                                             const fbx_bone_binding_t *bone_bindings,
                                             int32_t bone_binding_count,
                                             int64_t model_id) {
    fbx_node_t *model_node;
    char decoded_name[64];
    int64_t bone_index;
    if (!objects || !skeleton || model_id == 0)
        return -1;
    if (bone_bindings && bone_binding_count > 0) {
        for (int32_t i = 0; i < bone_binding_count; i++) {
            if (bone_bindings[i].model_id == model_id && bone_bindings[i].bone_index >= 0)
                return bone_bindings[i].bone_index;
        }
    }
    model_node = fbx_find_object_by_id(objects, model_id);
    if (!model_node || strcmp(model_node->name, "Model") != 0 || model_node->prop_count < 2)
        return -1;
    fbx_decode_object_name(fbx_prop_str(model_node, 1), decoded_name, sizeof(decoded_name));
    if (decoded_name[0] == '\0')
        return -1;
    bone_index = rt_skeleton3d_find_bone(skeleton, rt_const_cstr(decoded_name));
    if (bone_index < 0 || bone_index > INT32_MAX)
        return -1;
    return (int32_t)bone_index;
}

/// @brief Find the Model node that owns the given Cluster deformer in the FBX connection table.
/// @details A Cluster (sub-deformer) is connected child→parent to the Model node representing
///          the bone it drives. This function walks the connection table for an entry whose
///          child_id is @p cluster_id and whose parent is a "Model" node.
/// @return FBX object ID of the owning Model, or 0 if not found.
static int64_t fbx_find_cluster_bone_model(fbx_node_t *objects,
                                           const fbx_conn_table_t *ct,
                                           int64_t cluster_id) {
    if (!objects || !ct)
        return 0;
    for (int32_t i = 0; i < ct->count; i++) {
        fbx_node_t *parent;
        if (ct->entries[i].child_id != cluster_id)
            continue;
        parent = fbx_find_object_by_id(objects, ct->entries[i].parent_id);
        if (parent && strcmp(parent->name, "Model") == 0)
            return ct->entries[i].parent_id;
    }
    return 0;
}

/// @brief Walk the FBX connection table to find the geometry that owns a given skin.
/// @details In FBX, a Deformer (skin) is connected OO (object→object) upward to a
///          Geometry (Mesh) node. This scan finds the first matching parent of type
///          `Geometry` with subtype `"Mesh"` — that's the mesh the skin drives.
/// @return Geometry object ID, or 0 if not found / not a mesh skin.
static int64_t fbx_find_skin_geometry(fbx_node_t *objects,
                                      const fbx_conn_table_t *ct,
                                      int64_t skin_id) {
    if (!objects || !ct)
        return 0;
    for (int32_t i = 0; i < ct->count; i++) {
        fbx_node_t *parent;
        if (ct->entries[i].child_id != skin_id)
            continue;
        parent = fbx_find_object_by_id(objects, ct->entries[i].parent_id);
        if (parent && strcmp(parent->name, "Geometry") == 0 && parent->prop_count >= 3 &&
            strcmp(fbx_prop_str(parent, 2), "Mesh") == 0)
            return ct->entries[i].parent_id;
    }
    return 0;
}

/// @brief Insert a (bone, weight) influence into a 4-slot fixed-size influence record,
///        keeping the four largest weights.
/// @details GPU skinning palettes are capped at 4 influences per vertex. FBX clusters
///          can contribute an arbitrary number of (bone, weight) pairs per vertex, so
///          the loader must reduce them to the top four. Strategy:
///            1. If `bone_index` is already present, fold the weight in (handles
///               duplicate cluster entries that target the same bone/vertex pair).
///            2. Else, if any slot is empty (weight <= 0), fill it.
///            3. Else replace the weakest slot only when the new weight is strictly
///               greater — this drops the smallest contribution and keeps the four
///               most significant.
///          Weights are not renormalized here; that happens in
///          `fbx_apply_control_skin_to_vertex` after all clusters are accumulated.
/// @param dst Destination influence (mutated in place).
/// @param bone_index Engine bone index to add.
/// @param weight Non-negative weight; values <= 0 are silently dropped.
static void fbx_skin_add_influence(fbx_skin_influence_t *dst, int32_t bone_index, double weight) {
    int32_t weakest = 0;
    if (!dst || bone_index < 0 || !isfinite(weight) || weight <= 0.0)
        return;
    if (weight > FBX_SKIN_WEIGHT_MAX)
        weight = FBX_SKIN_WEIGHT_MAX;
    for (int i = 0; i < 4; i++) {
        if (dst->weights[i] > 0.0 && dst->bone_indices[i] == bone_index) {
            dst->weights[i] += weight;
            if (!isfinite(dst->weights[i]) || dst->weights[i] > FBX_SKIN_WEIGHT_MAX)
                dst->weights[i] = FBX_SKIN_WEIGHT_MAX;
            return;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (dst->weights[i] <= 0.0) {
            dst->bone_indices[i] = bone_index;
            dst->weights[i] = weight;
            return;
        }
        if (dst->weights[i] < dst->weights[weakest])
            weakest = i;
    }
    if (weight > dst->weights[weakest]) {
        dst->bone_indices[weakest] = bone_index;
        dst->weights[weakest] = weight;
    }
}

/// @brief Finalize and write a per-vertex influence record to the mesh's skinning data.
/// @details Copies up to 4 (bone, weight) pairs from `influence`, filters out empty
///          slots (weight <= 0), renormalizes the surviving weights to sum to 1, and
///          commits them via `rt_mesh3d_set_bone_weights`. Updates `mesh->bone_count`
///          as a running high-water mark so the skinning palette is sized correctly
///          for the shader upload. No-op if every influence slot is empty (the vertex
///          has no skin binding and should use the bind pose).
static void fbx_apply_control_skin_to_vertex(rt_mesh3d *mesh,
                                             int32_t vertex_index,
                                             const fbx_skin_influence_t *influence) {
    double weights[4];
    double sum = 0.0;
    int64_t bones[4];
    if (!mesh || !influence || vertex_index < 0 || vertex_index >= (int32_t)mesh->vertex_count)
        return;
    for (int i = 0; i < 4; i++) {
        bones[i] = influence->weights[i] > 0.0 && influence->bone_indices[i] >= 0
                       ? influence->bone_indices[i]
                       : 0;
        weights[i] = isfinite(influence->weights[i]) && influence->weights[i] > 0.0 &&
                             influence->bone_indices[i] >= 0
                         ? influence->weights[i]
                         : 0.0;
        if (weights[i] > FBX_SKIN_WEIGHT_MAX)
            weights[i] = FBX_SKIN_WEIGHT_MAX;
        sum += weights[i];
    }
    if (!isfinite(sum) || sum <= 0.0) {
        memset(mesh->vertices[vertex_index].bone_indices,
               0,
               sizeof(mesh->vertices[vertex_index].bone_indices));
        memset(mesh->vertices[vertex_index].bone_weights,
               0,
               sizeof(mesh->vertices[vertex_index].bone_weights));
        return;
    }
    for (int i = 0; i < 4; i++) {
        weights[i] /= sum;
        if (weights[i] > 0.0 && bones[i] + 1 > mesh->bone_count)
            mesh->bone_count = (int32_t)(bones[i] + 1);
    }
    rt_mesh3d_set_bone_weights(mesh,
                               vertex_index,
                               bones[0],
                               weights[0],
                               bones[1],
                               weights[1],
                               bones[2],
                               weights[2],
                               bones[3],
                               weights[3]);
}

/// @brief Stamp a fully-accumulated control-point influence table onto every vertex
///        of the mesh, respecting the control-point → vertex fan-out recorded in
///        `remap`.
/// @details FBX stores skinning at the "control point" granularity (the canonical
///          unique vertex positions), not the triangulated / duplicated vertices the
///          GPU consumes. When a control point maps to multiple mesh vertices (common
///          for hard edges where positions share but normals diverge), each of those
///          duplicate vertices must receive the same influences. The remap's
///          `control_vertices[ci]` is a flat list of mesh vertex indices spawned by
///          control point `ci` — we expand that fan-out here. If no remap exists we
///          assume 1:1 (no deduplication happened during load).
static void fbx_apply_skin_to_mesh(void *mesh_obj,
                                   const fbx_mesh_remap_t *remap,
                                   const fbx_skin_influence_t *controls,
                                   int32_t control_count) {
    rt_mesh3d *mesh = (rt_mesh3d *)mesh_obj;
    if (!mesh || !controls || control_count <= 0)
        return;
    for (int32_t ci = 0; ci < control_count; ci++) {
        if (remap && remap->control_vertices && ci < remap->control_count) {
            const fbx_vertex_index_list_t *list = &remap->control_vertices[ci];
            for (int32_t vi = 0; vi < list->count; vi++)
                fbx_apply_control_skin_to_vertex(mesh, list->vertices[vi], &controls[ci]);
        } else {
            fbx_apply_control_skin_to_vertex(mesh, ci, &controls[ci]);
        }
    }
}

/// @brief Apply all FBX Skin deformers to their target meshes, populating bone weights.
/// @details Iterates every Skin Deformer in the Objects section. For each skin it resolves
///          the target geometry (via `fbx_find_skin_geometry`), finds the engine mesh object
///          (via @p mesh_bindings), allocates a per-control-point influence accumulator, then
///          iterates all connected Cluster sub-deformers to accumulate (bone, weight) pairs.
///          Finalization (renormalization, write to mesh) is done by `fbx_apply_skin_to_mesh`.
///          Float and double weight arrays are both handled; the remap table is used when the
///          mesh was deduplicated to map control-point indices back to the original FBX layout.
static void fbx_apply_skinning(fbx_node_t *objects,
                               const fbx_conn_table_t *ct,
                               void *skeleton,
                               const fbx_bone_binding_t *bone_bindings,
                               int32_t bone_binding_count,
                               const fbx_mesh_binding_t *mesh_bindings,
                               int32_t mesh_binding_count,
                               const fbx_mesh_remap_t *mesh_remaps,
                               int32_t mesh_remap_count) {
    if (!objects || !ct || !skeleton || !mesh_bindings || mesh_binding_count <= 0)
        return;

    for (int32_t oi = 0; oi < objects->child_count; oi++) {
        fbx_node_t *skin = &objects->children[oi];
        int64_t skin_id;
        int64_t geometry_id;
        void *mesh_obj = NULL;
        const fbx_mesh_remap_t *remap;
        fbx_skin_influence_t *controls;
        int32_t control_count;

        if (strcmp(skin->name, "Deformer") != 0 || skin->prop_count < 3 ||
            strcmp(fbx_prop_str(skin, 2), "Skin") != 0)
            continue;

        skin_id = fbx_prop_i64(skin, 0);
        geometry_id = fbx_find_skin_geometry(objects, ct, skin_id);
        if (geometry_id == 0)
            continue;
        for (int32_t bi = 0; bi < mesh_binding_count; bi++) {
            if (mesh_bindings[bi].id == geometry_id) {
                mesh_obj = mesh_bindings[bi].mesh;
                break;
            }
        }
        if (!mesh_obj)
            continue;
        remap = fbx_find_mesh_remap(mesh_remaps, mesh_remap_count, geometry_id);
        if (!remap && ((rt_mesh3d *)mesh_obj)->vertex_count > (uint32_t)INT32_MAX)
            continue;
        control_count =
            remap ? remap->control_count : (int32_t)((rt_mesh3d *)mesh_obj)->vertex_count;
        if (control_count <= 0)
            continue;
        if ((size_t)control_count > SIZE_MAX / sizeof(*controls))
            continue;
        controls = (fbx_skin_influence_t *)calloc((size_t)control_count, sizeof(*controls));
        if (!controls)
            continue;

        for (int32_t ci = 0; ci < ct->count; ci++) {
            fbx_node_t *cluster;
            int32_t bone_index;
            int64_t bone_model_id;
            uint32_t index_count = 0;
            uint32_t weight_count = 0;
            const int32_t *indices;
            const double *weights64;
            const float *weights32 = NULL;

            if (ct->entries[ci].parent_id != skin_id)
                continue;
            cluster = fbx_find_object_by_id(objects, ct->entries[ci].child_id);
            if (!cluster || strcmp(cluster->name, "Deformer") != 0 || cluster->prop_count < 3 ||
                strcmp(fbx_prop_str(cluster, 2), "Cluster") != 0)
                continue;
            bone_model_id = fbx_find_cluster_bone_model(objects, ct, fbx_prop_i64(cluster, 0));
            bone_index = fbx_find_bone_index_for_model(
                objects, skeleton, bone_bindings, bone_binding_count, bone_model_id);
            if (bone_index < 0)
                continue;

            indices = fbx_get_i32_array(cluster, "Indexes", &index_count);
            weights64 = fbx_get_f64_array(cluster, "Weights", &weight_count);
            if (!weights64)
                weights32 = fbx_get_f32_array(cluster, "Weights", &weight_count);
            if (!indices || (!weights64 && !weights32))
                continue;
            if (weight_count < index_count)
                index_count = weight_count;
            for (uint32_t wi = 0; wi < index_count; wi++) {
                int32_t control_index = indices[wi];
                double weight = weights64 ? weights64[wi] : (double)weights32[wi];
                if (control_index >= 0 && control_index < control_count)
                    fbx_skin_add_influence(&controls[control_index], bone_index, weight);
            }
        }

        fbx_apply_skin_to_mesh(mesh_obj, remap, controls, control_count);
        free(controls);
    }
}

typedef struct {
    const int64_t *times;
    const double *values64;
    const float *values32;
    uint32_t count;
} fbx_anim_curve_view_t;

typedef struct {
    int8_t initialized;
    double base_translation[3];
    double base_rotation[3];
    double base_scale[3];
    fbx_anim_curve_view_t curves[3][3]; /* trs, component */
} fbx_anim_bone_builder_t;

/// @brief Extract the local-space TRS components from an FBX Model node's Properties70 block.
/// @details Scans the `Properties70` child for `"Lcl Translation"`, `"Lcl Rotation"`, and
///          `"Lcl Scaling"` P-nodes and writes their XYZ values to the caller's arrays.
///          NULL output pointers are silently skipped. Arrays are pre-zeroed (translation/rotation)
///          or set to {1,1,1} (scale) before parsing so the caller gets sane defaults when a
///          component is absent.
static void fbx_extract_model_lcl_components(fbx_node_t *model_node,
                                             double *translation,
                                             double *rotation,
                                             double *scale) {
    if (translation) {
        translation[0] = translation[1] = translation[2] = 0.0;
    }
    if (rotation) {
        rotation[0] = rotation[1] = rotation[2] = 0.0;
    }
    if (scale) {
        scale[0] = scale[1] = scale[2] = 1.0;
    }
    if (!model_node)
        return;
    fbx_node_t *p70 = fbx_find_child(model_node, "Properties70");
    if (!p70)
        return;
    for (int32_t pi = 0; pi < p70->child_count; pi++) {
        fbx_node_t *p = &p70->children[pi];
        const char *pn;
        if (strcmp(p->name, "P") != 0 || p->prop_count < 7)
            continue;
        pn = fbx_prop_str(p, 0);
        if (translation && strcmp(pn, "Lcl Translation") == 0) {
            double x = fbx_prop_f64(p, 4);
            double y = fbx_prop_f64(p, 5);
            double z = fbx_prop_f64(p, 6);
            if (fbx_sanitize_position3(&x, &y, &z)) {
                translation[0] = x;
                translation[1] = y;
                translation[2] = z;
            }
        } else if (rotation && strcmp(pn, "Lcl Rotation") == 0) {
            double x = fbx_prop_f64(p, 4);
            double y = fbx_prop_f64(p, 5);
            double z = fbx_prop_f64(p, 6);
            if (isfinite(x) && isfinite(y) && isfinite(z)) {
                rotation[0] = fbx_sanitize_rotation_degrees(x);
                rotation[1] = fbx_sanitize_rotation_degrees(y);
                rotation[2] = fbx_sanitize_rotation_degrees(z);
            }
        } else if (scale && strcmp(pn, "Lcl Scaling") == 0) {
            double x = fbx_prop_f64(p, 4);
            double y = fbx_prop_f64(p, 5);
            double z = fbx_prop_f64(p, 6);
            if (isfinite(x) && isfinite(y) && isfinite(z)) {
                scale[0] = fbx_scale_or_unit(x);
                scale[1] = fbx_scale_or_unit(y);
                scale[2] = fbx_scale_or_unit(z);
            }
        }
    }
}

/// @brief Return non-zero if @p curve contains at least one keyframe with usable value data.
static int fbx_anim_curve_has_data(const fbx_anim_curve_view_t *curve) {
    return curve && curve->times && (curve->values64 || curve->values32) && curve->count > 0 &&
           curve->count <= FBX_ANIM_CURVE_KEYS_MAX;
}

/// @brief Validate an FBX animation curve before the sampler assumes sorted finite data.
static int fbx_anim_curve_view_valid(const fbx_anim_curve_view_t *curve) {
    if (!fbx_anim_curve_has_data(curve))
        return 0;
    for (uint32_t i = 0; i < curve->count; i++) {
        double value = curve->values64 ? curve->values64[i] : (double)curve->values32[i];
        double seconds = (double)curve->times[i] / (double)FBX_TIME_SECOND;
        if (!isfinite(value) || curve->times[i] < 0 || !isfinite(seconds) ||
            seconds > FBX_ANIM_TIME_SECONDS_MAX)
            return 0;
        if (i > 0 && curve->times[i] <= curve->times[i - 1])
            return 0;
    }
    return 1;
}

/// @brief Case-insensitive ASCII string equality (NULL operands compare unequal).
static int fbx_ascii_streq_ignore_case(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb))
            return 0;
    }
    return *a == '\0' && *b == '\0';
}

/// @brief Map an AnimationCurve connection component label to XYZ index.
/// @details Binary and ASCII FBX commonly use `d|X`, `d|Y`, `d|Z`, but some exporters
///          write the bare aliases `X`, `Y`, `Z`; others preserve the meaning but vary
///          ASCII case. Accept these forms so valid curves do not disappear solely
///          because of exporter spelling.
static int32_t fbx_anim_component_from_connection_prop(const char *prop) {
    if (!prop)
        return -1;
    if (fbx_ascii_streq_ignore_case(prop, "d|X") || fbx_ascii_streq_ignore_case(prop, "X"))
        return 0;
    if (fbx_ascii_streq_ignore_case(prop, "d|Y") || fbx_ascii_streq_ignore_case(prop, "Y"))
        return 1;
    if (fbx_ascii_streq_ignore_case(prop, "d|Z") || fbx_ascii_streq_ignore_case(prop, "Z"))
        return 2;
    return -1;
}

/// @brief Sample the animation curve at the given FBX tick time using linear interpolation.
/// @details Times before the first keyframe return the first value; times after the last return
///          the last value (no extrapolation).  FBX allows either double (`'d'`) or float (`'f'`)
///          value arrays; both are handled by testing `values64` vs `values32`.  FBX time is in
///          units of 1/46,186,158,000 second (see FBX_TIME_SECOND).
/// @param fallback Value returned when the curve has no data.
static double fbx_anim_curve_value(const fbx_anim_curve_view_t *curve,
                                   int64_t fbx_time,
                                   double fallback) {
    uint32_t lo;
    uint32_t hi;
    int64_t t0;
    int64_t t1;
    double v0;
    double v1;
    double a;
    if (!fbx_anim_curve_has_data(curve))
        return fallback;
    v0 = curve->values64 ? curve->values64[0] : (double)curve->values32[0];
    v1 = curve->values64 ? curve->values64[curve->count - 1]
                         : (double)curve->values32[curve->count - 1];
    if (!isfinite(v0) || !isfinite(v1))
        return fallback;
    if (fbx_time <= curve->times[0])
        return v0;
    if (fbx_time >= curve->times[curve->count - 1])
        return v1;
    lo = 0;
    hi = curve->count - 1;
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (curve->times[mid] <= fbx_time)
            lo = mid;
        else
            hi = mid;
    }
    t0 = curve->times[lo];
    t1 = curve->times[hi];
    if (t1 <= t0)
        return fallback;
    v0 = curve->values64 ? curve->values64[lo] : (double)curve->values32[lo];
    v1 = curve->values64 ? curve->values64[hi] : (double)curve->values32[hi];
    if (!isfinite(v0) || !isfinite(v1))
        return fallback;
    a = (double)(fbx_time - t0) / (double)(t1 - t0);
    if (!isfinite(a) || a < 0.0)
        a = 0.0;
    if (a > 1.0)
        a = 1.0;
    return v0 + (v1 - v0) * a;
}

/// @brief qsort comparator for int64 keys, ascending (returns -1/0/1).
static int fbx_i64_compare(const void *a, const void *b) {
    int64_t av = *(const int64_t *)a;
    int64_t bv = *(const int64_t *)b;
    return (av > bv) - (av < bv);
}

/// @brief Append @p value to an unsorted key-time array; sorting/dedup happens once after collect.
static int fbx_anim_append_time(int64_t **times, int32_t *count, int32_t *capacity, int64_t value) {
    double seconds = (double)value / (double)FBX_TIME_SECOND;
    if (!times || !count || !capacity || *count < 0 || *capacity < 0 || *count > *capacity ||
        (*count > 0 && !*times) || value < 0)
        return 0;
    if (!isfinite(seconds) || seconds > FBX_ANIM_TIME_SECONDS_MAX)
        return 1;
    if (*count >= *capacity) {
        int32_t new_capacity;
        if (*capacity > INT32_MAX / 2)
            new_capacity = *count + 1;
        else
            new_capacity = *capacity == 0 ? 16 : *capacity * 2;
        if (new_capacity <= *capacity || (size_t)new_capacity > SIZE_MAX / sizeof(**times))
            return 0;
        int64_t *grown = (int64_t *)realloc(*times, (size_t)new_capacity * sizeof(*grown));
        if (!grown)
            return 0;
        *times = grown;
        *capacity = new_capacity;
    }
    (*times)[*count] = value;
    (*count)++;
    return 1;
}

/// @brief Sort key times and compact duplicates in place.
static void fbx_anim_sort_unique_times(int64_t *times, int32_t *count) {
    int32_t write = 0;
    if (!times || !count || *count <= 1)
        return;
    qsort(times, (size_t)*count, sizeof(*times), fbx_i64_compare);
    for (int32_t read = 0; read < *count; ++read) {
        if (write == 0 || times[read] != times[write - 1])
            times[write++] = times[read];
    }
    *count = write;
}

/// @brief Walk `AnimationStack` / `AnimationLayer` / `AnimationCurveNode` nodes and
///        build keyframe tracks per bone.
///
/// @details FBX's animation graph is deeply indirected. This function flattens it into
///          the engine's bone-oriented `rt_animation3d` representation by traversing:
///
///            AnimationStack  (one per "clip"/"take")
///                └── AnimationLayer+  (usually one; blending layers are flattened)
///                        └── AnimationCurveNode+  (one per animated channel group)
///                                ├── connects up to a `Model` (bone) by property name
///                                │     ("Lcl Translation" / "Lcl Rotation" / "Lcl Scaling")
///                                └── AnimationCurve (X, Y, Z) via subfield connections
///                                      ("d|X" / "d|Y" / "d|Z") each with parallel
///                                      `KeyTime` (i64 FBX ticks) + `KeyValueFloat` arrays.
///
///          For each AnimationStack we:
///            1. Allocate one `fbx_anim_bone_builder_t` per skeleton bone.
///            2. For each curve found, record the (time, axis-value) samples into the
///               matching bone/TRS slot.
///            3. Unify the sample time lists across T/R/S per bone, seeded by the
///               bind-pose TRS so bones with partial curves snap to rest on the
///               missing axis.
///            4. Emit one merged keyframe per unique time, converting Euler degrees to
///               quaternions for rotation channels. Z-up assets have the translation
///               axes swapped to Y-up via `fbx_correct_zup`.
///            5. Finalize the `rt_animation3d` with the total duration and push it
///               onto the output array.
///
///          FBX time units (1/46186158000 s) are converted to seconds up-front so the
///          engine never has to know about FBX ticks downstream. Missing / unresolved
///          connections are silently skipped rather than failing the whole load.
/// @param root FBX document root.
/// @param ct Connection table (OO/OP connections already resolved).
/// @param skeleton Target skeleton whose bone count determines builder allocation.
/// @param z_up Non-zero for Z-up source assets; triggers axis correction.
/// @param out_anims Receives an array of rt_animation3d* (caller owns).
/// @param out_count Receives the count written.
/// @brief Walk a stack's AnimationLayers → CurveNodes → AnimationCurves, filling @p builders
///        (per-bone T/R/S curve views + base local transform). The deep connection walk
///        resolves each curve node to its target bone and TRS channel.
static void fbx_anim_collect_curves(fbx_node_t *objects,
                                    const fbx_conn_table_t *ct,
                                    void *skeleton,
                                    const fbx_bone_binding_t *bone_bindings,
                                    int32_t bone_binding_count,
                                    int64_t bone_count,
                                    int64_t stack_id,
                                    fbx_anim_bone_builder_t *builders) {
    if (!objects || !ct || !builders)
        return;
    for (int32_t li = 0; li < ct->count; li++) {
        int64_t layer_id;
        fbx_node_t *layer_node;
        if (ct->entries[li].parent_id != stack_id)
            continue;
        layer_id = ct->entries[li].child_id;
        layer_node = fbx_find_object_by_id(objects, layer_id);
        if (!layer_node || strcmp(layer_node->name, "AnimationLayer") != 0)
            continue;

        for (int32_t ci = 0; ci < ct->count; ci++) {
            int64_t curve_node_id;
            fbx_node_t *cn_node;
            if (ct->entries[ci].parent_id != layer_id)
                continue;
            curve_node_id = ct->entries[ci].child_id;
            cn_node = fbx_find_object_by_id(objects, curve_node_id);
            if (!cn_node || strcmp(cn_node->name, "AnimationCurveNode") != 0)
                continue;

            /* Find which Model (bone) this curve node connects to and what property */
            /* The CurveNode→Model connection has prop "Lcl Translation"/"Lcl Rotation"/"Lcl
             * Scaling" */
            int32_t trs_type = -1; /* 0=T, 1=R, 2=S */
            int64_t model_id = 0;
            for (int32_t ci2 = 0; ci2 < ct->count; ci2++) {
                if (ct->entries[ci2].child_id == curve_node_id) {
                    int64_t pid = ct->entries[ci2].parent_id;
                    fbx_node_t *pnode = fbx_find_object_by_id(objects, pid);
                    if (pnode && strcmp(pnode->name, "Model") == 0) {
                        model_id = pid;
                        const char *cprop = ct->entries[ci2].prop;
                        if (!cprop)
                            continue;
                        if (strcmp(cprop, "Lcl Translation") == 0)
                            trs_type = 0;
                        else if (strcmp(cprop, "Lcl Rotation") == 0)
                            trs_type = 1;
                        else if (strcmp(cprop, "Lcl Scaling") == 0)
                            trs_type = 2;
                        break;
                    }
                }
            }
            if (trs_type < 0 || model_id == 0)
                continue;

            int64_t bone_idx = fbx_find_bone_index_for_model(
                objects, skeleton, bone_bindings, bone_binding_count, model_id);
            if (bone_idx < 0 || bone_idx >= bone_count)
                continue;
            if (!builders[bone_idx].initialized) {
                fbx_node_t *model_node = fbx_find_object_by_id(objects, model_id);
                fbx_extract_model_lcl_components(model_node,
                                                 builders[bone_idx].base_translation,
                                                 builders[bone_idx].base_rotation,
                                                 builders[bone_idx].base_scale);
                builders[bone_idx].initialized = 1;
            }

            /* Find AnimationCurve children (d|X, d|Y, d|Z) */
            for (int32_t ki = 0; ki < ct->count; ki++) {
                fbx_node_t *curve;
                const char *curve_prop;
                if (ct->entries[ki].parent_id != curve_node_id)
                    continue;
                curve = fbx_find_object_by_id(objects, ct->entries[ki].child_id);
                if (!curve || strcmp(curve->name, "AnimationCurve") != 0)
                    continue;

                curve_prop = ct->entries[ki].prop;
                int comp = fbx_anim_component_from_connection_prop(curve_prop);
                if (comp < 0)
                    continue;

                uint32_t tc = 0;
                const int64_t *times = fbx_get_i64_array(curve, "KeyTime", &tc);
                if (!times || tc == 0)
                    continue;

                uint32_t vc = 0;
                const double *dvals = fbx_get_f64_array(curve, "KeyValueFloat", &vc);
                const float *fvals = NULL;
                if (!dvals)
                    fvals = fbx_get_f32_array(curve, "KeyValueFloat", &vc);
                if (!dvals && !fvals)
                    continue;
                if (tc != vc)
                    continue;

                {
                    fbx_anim_curve_view_t *view = &builders[bone_idx].curves[trs_type][comp];
                    fbx_anim_curve_view_t candidate;
                    if (fbx_anim_curve_has_data(view))
                        continue;
                    candidate.times = times;
                    candidate.values64 = dvals;
                    candidate.values32 = fvals;
                    candidate.count = tc;
                    if (fbx_anim_curve_view_valid(&candidate))
                        *view = candidate;
                }
            }
        }
    }
}

/// @brief True if the given FBX animation stack id owns at least one AnimationLayer child in
///   the connection table.
static int fbx_anim_stack_has_layer(fbx_node_t *objects,
                                    const fbx_conn_table_t *ct,
                                    int64_t stack_id) {
    if (!objects || !ct)
        return 0;
    for (int32_t i = 0; i < ct->count; i++) {
        fbx_node_t *layer_node;
        if (ct->entries[i].parent_id != stack_id)
            continue;
        layer_node = fbx_find_object_by_id(objects, ct->entries[i].child_id);
        if (layer_node && strcmp(layer_node->name, "AnimationLayer") == 0)
            return 1;
    }
    return 0;
}

/// @brief Largest keyframe time (seconds) across all of @p builders' curves.
static double fbx_anim_compute_max_time(const fbx_anim_bone_builder_t *builders,
                                        int64_t bone_count) {
    double max_time = 0.0;
    for (int64_t bone_idx = 0; bone_idx < bone_count; bone_idx++) {
        if (!builders[bone_idx].initialized)
            continue;
        for (int trs = 0; trs < 3; trs++) {
            for (int comp = 0; comp < 3; comp++) {
                const fbx_anim_curve_view_t *curve = &builders[bone_idx].curves[trs][comp];
                if (!fbx_anim_curve_has_data(curve))
                    continue;
                for (uint32_t k = 0; k < curve->count; k++) {
                    double t = (double)curve->times[k] / (double)FBX_TIME_SECOND;
                    if (isfinite(t) && t > 0.0 && t <= FBX_ANIM_TIME_SECONDS_MAX &&
                        t > max_time)
                        max_time = t;
                }
            }
        }
    }
    return max_time;
}

/// @brief Sample one bone's T/R/S curves at each unique keyframe time and emit keyframes into
///        @p anim (Euler degrees → quaternion). @return 1 if any keyframe was emitted.
static int fbx_anim_build_bone_keyframes(void *anim,
                                         const fbx_anim_bone_builder_t *builders,
                                         int64_t bone_idx,
                                         int z_up) {
    int64_t *times = NULL;
    int32_t time_count = 0;
    int32_t time_capacity = 0;
    int emitted_any = 0;
    int time_failed = 0;
    if (!builders[bone_idx].initialized)
        return 0;
    for (int trs = 0; trs < 3; trs++) {
        for (int comp = 0; comp < 3; comp++) {
            const fbx_anim_curve_view_t *curve = &builders[bone_idx].curves[trs][comp];
            if (!fbx_anim_curve_has_data(curve))
                continue;
            for (uint32_t k = 0; k < curve->count; k++) {
                if (!fbx_anim_append_time(&times, &time_count, &time_capacity, curve->times[k])) {
                    time_failed = 1;
                    break;
                }
            }
            if (time_failed)
                break;
        }
        if (time_failed)
            break;
    }
    if (time_failed) {
        free(times);
        return 0;
    }
    fbx_anim_sort_unique_times(times, &time_count);
    for (int32_t ti = 0; ti < time_count; ti++) {
        int64_t fbx_time = times[ti];
        double t = (double)fbx_time / (double)FBX_TIME_SECOND;
        double tv[3];
        double rv[3];
        double sv[3];
        if (!isfinite(t) || t < 0.0 || t > FBX_ANIM_TIME_SECONDS_MAX)
            continue;
        memcpy(tv, builders[bone_idx].base_translation, sizeof(tv));
        memcpy(rv, builders[bone_idx].base_rotation, sizeof(rv));
        memcpy(sv, builders[bone_idx].base_scale, sizeof(sv));
        for (int comp = 0; comp < 3; comp++) {
            tv[comp] =
                fbx_anim_curve_value(&builders[bone_idx].curves[0][comp], fbx_time, tv[comp]);
            rv[comp] =
                fbx_anim_curve_value(&builders[bone_idx].curves[1][comp], fbx_time, rv[comp]);
            sv[comp] =
                fbx_anim_curve_value(&builders[bone_idx].curves[2][comp], fbx_time, sv[comp]);
        }
        if (z_up)
            fbx_correct_zup(&tv[0], &tv[1], &tv[2]);
        if (!fbx_sanitize_position3(&tv[0], &tv[1], &tv[2]))
            continue;
        rv[0] = fbx_sanitize_rotation_degrees(rv[0]);
        rv[1] = fbx_sanitize_rotation_degrees(rv[1]);
        rv[2] = fbx_sanitize_rotation_degrees(rv[2]);
        sv[0] = fbx_scale_or_unit(sv[0]);
        sv[1] = fbx_scale_or_unit(sv[1]);
        sv[2] = fbx_scale_or_unit(sv[2]);

        double rx = rv[0] * 3.14159265358979323846 / 180.0;
        double ry = rv[1] * 3.14159265358979323846 / 180.0;
        double rz = rv[2] * 3.14159265358979323846 / 180.0;
        double cx = cos(rx * 0.5), sx = sin(rx * 0.5);
        double cy = cos(ry * 0.5), sy = sin(ry * 0.5);
        double cz = cos(rz * 0.5), sz = sin(rz * 0.5);
        double qw = cx * cy * cz + sx * sy * sz;
        double qx = sx * cy * cz - cx * sy * sz;
        double qy = cx * sy * cz + sx * cy * sz;
        double qz = cx * cy * sz - sx * sy * cz;
        if (!isfinite(qx) || !isfinite(qy) || !isfinite(qz) || !isfinite(qw))
            continue;
        {
            double qlen = sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
            if (!isfinite(qlen) || qlen <= 1e-12)
                continue;
            qx /= qlen;
            qy /= qlen;
            qz /= qlen;
            qw /= qlen;
        }
        void *pos = rt_vec3_new(tv[0], tv[1], tv[2]);
        void *rot = rt_quat_new(qx, qy, qz, qw);
        void *scl = rt_vec3_new(sv[0], sv[1], sv[2]);
        if (pos && rot && scl) {
            rt_animation3d_add_keyframe(anim, bone_idx, t, pos, rot, scl);
            emitted_any = 1;
        }
        fbx_release_ref(&pos);
        fbx_release_ref(&rot);
        fbx_release_ref(&scl);
    }
    free(times);
    return emitted_any;
}

/// @brief Extract animation clips from the FBX node graph: for each animation stack, sample
///   its bone AnimCurves over the union of keyframe times and emit TRS keyframes (Euler
///   degrees converted to quaternions, with optional Z-up correction) into new Animation3Ds.
/// @param out_anims Receives a newly allocated array of animation objects (caller owns).
/// @param out_count Receives the number of animations written.
static void fbx_extract_animations(fbx_node_t *root,
                                   const fbx_conn_table_t *ct,
                                   void *skeleton,
                                   const fbx_bone_binding_t *bone_bindings,
                                   int32_t bone_binding_count,
                                   int z_up,
                                   void ***out_anims,
                                   int32_t *out_count,
                                   int32_t *out_capacity) {
    if (!out_anims || !out_count || !out_capacity)
        return;
    *out_anims = NULL;
    *out_count = 0;
    *out_capacity = 0;

    fbx_node_t *objects = fbx_find_child(root, "Objects");
    if (!objects)
        return;

    int64_t bone_count = skeleton ? rt_skeleton3d_get_bone_count(skeleton) : 0;
    if (bone_count <= 0)
        return;
    if (bone_count > FBX_MAX_SKELETON_BONES ||
        (size_t)bone_count > SIZE_MAX / sizeof(fbx_anim_bone_builder_t))
        return;

    /* Find AnimationStack nodes */
    for (int32_t i = 0; i < objects->child_count; i++) {
        fbx_node_t *obj = &objects->children[i];
        fbx_anim_bone_builder_t *builders;
        double max_time;
        if (strcmp(obj->name, "AnimationStack") != 0)
            continue;

        int64_t stack_id = fbx_prop_i64(obj, 0);
        char decoded_anim_name[64];
        const char *anim_name = "Untitled";
        if (obj->prop_count >= 2) {
            fbx_decode_object_name(
                fbx_prop_str(obj, 1), decoded_anim_name, sizeof(decoded_anim_name));
            if (decoded_anim_name[0] != '\0')
                anim_name = decoded_anim_name;
        }

        if (!fbx_anim_stack_has_layer(objects, ct, stack_id))
            continue;

        builders = (fbx_anim_bone_builder_t *)calloc((size_t)bone_count, sizeof(*builders));
        if (!builders)
            continue;

        fbx_anim_collect_curves(
            objects, ct, skeleton, bone_bindings, bone_binding_count, bone_count, stack_id, builders);
        max_time = fbx_anim_compute_max_time(builders, bone_count);

        max_time = fbx_clamp_double(max_time, 0.0, FBX_ANIM_TIME_SECONDS_MAX, 0.0);
        void *anim = rt_animation3d_new(rt_const_cstr(anim_name), max_time > 0.0 ? max_time : 1.0);
        int emitted_any = 0;
        if (!anim) {
            free(builders);
            continue;
        }

        for (int64_t bone_idx = 0; bone_idx < bone_count; bone_idx++) {
            if (fbx_anim_build_bone_keyframes(anim, builders, bone_idx, z_up))
                emitted_any = 1;
        }
        free(builders);

        if (!emitted_any) {
            fbx_release_ref(&anim);
            continue;
        }
        rt_animation3d_set_looping(anim, 1);

        if (*out_count < 0 || *out_count == INT32_MAX) {
            fbx_release_ref(&anim);
            continue;
        }
        if (!fbx_asset_reserve_ref_array(out_anims, out_capacity, *out_count + 1)) {
            fbx_release_ref(&anim);
            continue;
        }
        (*out_anims)[*out_count] = anim;
        (*out_count)++;
    }
}

/*==========================================================================
 * Top-level FBX loader
 *=========================================================================*/

/// @brief GC finalizer for `rt_fbx_asset` — release every owned mesh / material / skeleton /
/// animation.
static void rt_fbx_asset_finalize(void *obj) {
    rt_fbx_asset *fbx = (rt_fbx_asset *)obj;
    if (!fbx)
        return;
    fbx_asset_release_ref_array(&fbx->meshes, &fbx->mesh_count, &fbx->mesh_capacity);
    fbx_release_ref(&fbx->skeleton);
    fbx_asset_release_ref_array(
        &fbx->animations, &fbx->animation_count, &fbx->animation_capacity);
    fbx_asset_release_ref_array(&fbx->materials, &fbx->material_count, &fbx->material_capacity);
    fbx_asset_release_ref_array(
        &fbx->morph_targets, &fbx->morph_count, &fbx->morph_capacity);
    fbx_release_ref(&fbx->scene_root);
}

/// @brief Ensure a double array holds at least @p needed elements, doubling capacity (min 64)
///   with overflow checks. @return 1 on success, 0 on overflow or allocation failure.
static int fbx_ascii_grow_double_array(double **values, size_t *capacity, size_t needed) {
    double *grown;
    size_t new_capacity;
    if (needed <= *capacity)
        return 1;
    new_capacity = *capacity ? *capacity : 64;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2u)
            return 0;
        new_capacity *= 2u;
    }
    if (new_capacity > SIZE_MAX / sizeof(**values))
        return 0;
    grown = (double *)realloc(*values, new_capacity * sizeof(**values));
    if (!grown)
        return 0;
    *values = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Ensure an int32 array holds at least @p needed elements, doubling capacity (min 64)
///   with overflow checks. @return 1 on success, 0 on overflow or allocation failure.
static int fbx_ascii_grow_i32_array(int32_t **values, size_t *capacity, size_t needed) {
    int32_t *grown;
    size_t new_capacity;
    if (needed <= *capacity)
        return 1;
    new_capacity = *capacity ? *capacity : 64;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2u)
            return 0;
        new_capacity *= 2u;
    }
    if (new_capacity > SIZE_MAX / sizeof(**values))
        return 0;
    grown = (int32_t *)realloc(*values, new_capacity * sizeof(**values));
    if (!grown)
        return 0;
    *values = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Locate the "a:" value payload of the ASCII-FBX array node named @p key within
///   @p text; NULL if the node or its payload marker is absent.
static const char *fbx_ascii_array_payload(const char *text, const char *key) {
    const char *p = text ? strstr(text, key) : NULL;
    if (!p)
        return NULL;
    p = strstr(p, "a:");
    return p ? p + 2 : NULL;
}

/// @brief Parse a comma-separated list of doubles from an ASCII-FBX "a:" payload up to the
///   closing '}', rejecting non-finite values. @return 1 if ≥1 value parsed (sets @p out /
///   @p out_count), 0 on parse error or allocation failure.
static int fbx_ascii_parse_double_array(const char *payload, double **out, size_t *out_count) {
    double *values = NULL;
    size_t count = 0;
    size_t capacity = 0;
    const char *p = payload;
    if (out)
        *out = NULL;
    if (out_count)
        *out_count = 0;
    if (!payload || !out || !out_count)
        return 0;
    while (*p && *p != '}') {
        char *end = NULL;
        while (*p == ',' || isspace((unsigned char)*p))
            p++;
        if (*p == '}')
            break;
        double value = strtod(p, &end);
        if (end == p || !isfinite(value)) {
            free(values);
            return 0;
        }
        if (!fbx_ascii_grow_double_array(&values, &capacity, count + 1)) {
            free(values);
            return 0;
        }
        values[count++] = value;
        p = end;
    }
    *out = values;
    *out_count = count;
    return count > 0;
}

/// @brief Parse a comma-separated list of int32s from an ASCII-FBX "a:" payload up to the
///   closing '}', rejecting out-of-range values. @return 1 if ≥1 value parsed (sets @p out /
///   @p out_count), 0 on parse error or allocation failure.
static int fbx_ascii_parse_i32_array(const char *payload, int32_t **out, size_t *out_count) {
    int32_t *values = NULL;
    size_t count = 0;
    size_t capacity = 0;
    const char *p = payload;
    if (out)
        *out = NULL;
    if (out_count)
        *out_count = 0;
    if (!payload || !out || !out_count)
        return 0;
    while (*p && *p != '}') {
        char *end = NULL;
        while (*p == ',' || isspace((unsigned char)*p))
            p++;
        if (*p == '}')
            break;
        long value = strtol(p, &end, 10);
        if (end == p || value < INT32_MIN || value > INT32_MAX) {
            free(values);
            return 0;
        }
        if (!fbx_ascii_grow_i32_array(&values, &capacity, count + 1)) {
            free(values);
            return 0;
        }
        values[count++] = (int32_t)value;
        p = end;
    }
    *out = values;
    *out_count = count;
    return count > 0;
}

/// @brief Build a Mesh3D from ASCII-FBX vertex positions and polygon-vertex indices.
/// @details FBX marks each polygon's final index with a bitwise complement (a negative value);
///   this fan-triangulates arbitrary-sized polygons and skips out-of-range indices.
/// @return The new mesh object, or NULL on malformed input or allocation failure.
static void *fbx_ascii_build_mesh(const double *positions,
                                  size_t position_count,
                                  const int32_t *indices,
                                  size_t index_count) {
    int32_t polygon_stack[16];
    int32_t *polygon = polygon_stack;
    int32_t poly_count = 0;
    int32_t poly_capacity = (int32_t)(sizeof(polygon_stack) / sizeof(polygon_stack[0]));
    int polygon_invalid = 0;
    void *mesh;
    if (!positions || !indices || position_count % 3u != 0)
        return NULL;
    mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;
    rt_mesh3d_begin_geometry_batch((rt_mesh3d *)mesh);
    for (size_t i = 0; i < index_count; i++) {
        int32_t raw = indices[i];
        int end = raw < 0;
        int32_t vi = end ? ~raw : raw;
        int32_t emitted;
        if (vi < 0 || (size_t)vi >= position_count / 3u) {
            polygon_invalid = 1;
            if (end) {
                poly_count = 0;
                polygon_invalid = 0;
            }
            continue;
        }
        if (poly_count >= poly_capacity) {
            int32_t new_capacity = poly_capacity * 2;
            int32_t *grown;
            if (poly_capacity > INT32_MAX / 2 || (size_t)new_capacity > SIZE_MAX / sizeof(*polygon))
                goto fail;
            grown = (int32_t *)malloc((size_t)new_capacity * sizeof(*polygon));
            if (!grown)
                goto fail;
            memcpy(grown, polygon, (size_t)poly_count * sizeof(*polygon));
            if (polygon != polygon_stack)
                free(polygon);
            polygon = grown;
            poly_capacity = new_capacity;
        }
        emitted = (int32_t)((rt_mesh3d *)mesh)->vertex_count;
        rt_mesh3d_add_vertex(mesh,
                             positions[(size_t)vi * 3u + 0],
                             positions[(size_t)vi * 3u + 1],
                             positions[(size_t)vi * 3u + 2],
                             0.0,
                             1.0,
                             0.0,
                             0.0,
                             0.0);
        if (((rt_mesh3d *)mesh)->build_failed)
            goto fail;
        polygon[poly_count++] = emitted;
        if (end) {
            if (!polygon_invalid && !fbx_emit_polygon_triangles(mesh, polygon, poly_count))
                goto fail;
            poly_count = 0;
            polygon_invalid = 0;
        }
    }
    rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
    if (((rt_mesh3d *)mesh)->index_count < 3u) {
        if (polygon != polygon_stack)
            free(polygon);
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }
    if (polygon != polygon_stack)
        free(polygon);
    rt_mesh3d_recalc_normals(mesh);
    return mesh;
fail:
    rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
    if (polygon != polygon_stack)
        free(polygon);
    if (rt_obj_release_check0(mesh))
        rt_obj_free(mesh);
    return NULL;
}

/// @brief Minimal ASCII-FBX fallback loader: parse the "Vertices:" and "PolygonVertexIndex:"
///   arrays into a single mesh with a default material under a fresh scene root.
/// @return A new FBX asset, or NULL if the required arrays are missing or allocation fails.
static void *fbx_load_ascii_minimal(const char *text) {
    double *positions = NULL;
    int32_t *indices = NULL;
    size_t position_count = 0;
    size_t index_count = 0;
    void *mesh = NULL;
    void *material = NULL;
    void *scene_root = NULL;
    void *mesh_node = NULL;
    rt_fbx_asset *asset = NULL;
    if (!fbx_ascii_parse_double_array(
            fbx_ascii_array_payload(text, "Vertices:"), &positions, &position_count) ||
        !fbx_ascii_parse_i32_array(
            fbx_ascii_array_payload(text, "PolygonVertexIndex:"), &indices, &index_count))
        goto fail;
    mesh = fbx_ascii_build_mesh(positions, position_count, indices, index_count);
    material = rt_material3d_new();
    scene_root = rt_scene_node3d_new();
    mesh_node = rt_scene_node3d_new();
    asset =
        (rt_fbx_asset *)rt_obj_new_i64(RT_G3D_FBX_ASSET_CLASS_ID, (int64_t)sizeof(rt_fbx_asset));
    if (!mesh || !material || !scene_root || !mesh_node || !asset)
        goto fail;
    memset(asset, 0, sizeof(*asset));
    rt_obj_set_finalizer(asset, rt_fbx_asset_finalize);
    asset->meshes = (void **)calloc(1, sizeof(void *));
    asset->materials = (void **)calloc(1, sizeof(void *));
    if (!asset->meshes || !asset->materials)
        goto fail;
    asset->meshes[0] = mesh;
    asset->mesh_count = 1;
    asset->mesh_capacity = 1;
    asset->materials[0] = material;
    asset->material_count = 1;
    asset->material_capacity = 1;
    rt_scene_node3d_set_name(mesh_node, rt_const_cstr("mesh_0"));
    rt_scene_node3d_set_mesh(mesh_node, mesh);
    rt_scene_node3d_set_material(mesh_node, material);
    if (!rt_scene_node3d_try_add_child(scene_root, mesh_node))
        goto fail;
    asset->scene_root = scene_root;
    fbx_release_ref(&mesh_node);
    free(positions);
    free(indices);
    return asset;
fail:
    free(positions);
    free(indices);
    fbx_release_ref(&mesh_node);
    fbx_release_ref(&scene_root);
    fbx_release_ref(&material);
    fbx_release_ref(&mesh);
    fbx_release_ref((void **)&asset);
    return NULL;
}

/// @brief Collect Geometry/Material objects from the FBX `Objects` node onto the asset,
///   recording id->mesh/material bindings and per-mesh vertex remaps. Extracted phase of
///   rt_fbx_load; the binding/remap arrays grow in place via the in/out pointer params.
/// @brief Link FBX Texture nodes to their owning materials via the connection table.
///   Extracted phase of rt_fbx_load; reads the connection table + material bindings and
///   assigns decoded pixels to the matched material texture slots.
static const uint8_t *fbx_video_content_bytes(fbx_node_t *video, uint32_t *out_len) {
    fbx_node_t *content;
    if (out_len)
        *out_len = 0;
    if (!video)
        return NULL;
    content = fbx_find_child(video, "Content");
    if (!content || content->prop_count < 1)
        return NULL;
    if (content->props[0].type == 'R' && content->props[0].v.raw.data) {
        if (out_len)
            *out_len = content->props[0].v.raw.len;
        return content->props[0].v.raw.data;
    }
    return NULL;
}

/// @brief Best-effort texture filename for a Video node, checking its Properties70
///   RelativeFilename/FileName entries then child RelativeFilename/FileName nodes.
///   Returns "" when none is present.
static const char *fbx_video_filename_hint(fbx_node_t *video) {
    fbx_node_t *p70;
    if (!video)
        return "";
    p70 = fbx_find_child(video, "Properties70");
    if (p70) {
        for (int32_t pi = 0; pi < p70->child_count; pi++) {
            fbx_node_t *p = &p70->children[pi];
            const char *pname;
            if (strcmp(p->name, "P") != 0 || p->prop_count < 5)
                continue;
            pname = fbx_prop_str(p, 0);
            if (strcmp(pname, "RelativeFilename") == 0 || strcmp(pname, "FileName") == 0) {
                const char *value = fbx_prop_str(p, 4);
                if (value && *value)
                    return value;
            }
        }
    }
    {
        fbx_node_t *rfn = fbx_find_child(video, "RelativeFilename");
        if (rfn && rfn->prop_count > 0 && *fbx_prop_str(rfn, 0))
            return fbx_prop_str(rfn, 0);
    }
    {
        fbx_node_t *fn = fbx_find_child(video, "FileName");
        if (fn && fn->prop_count > 0 && *fbx_prop_str(fn, 0))
            return fbx_prop_str(fn, 0);
    }
    return "";
}

/// @brief Find the Video node connected to texture @p texture_id and decode its embedded
///   image content into a Pixels object, picking the codec from the Video filename hint (or
///   @p texture_hint / @p fallback_hint). Returns NULL if no embedded content decodes.
static void *fbx_try_decode_embedded_texture(fbx_node_t *objects,
                                             const fbx_conn_table_t *ct,
                                             int64_t texture_id,
                                             const char *texture_hint,
                                             const char *fallback_hint) {
    if (!objects || !ct)
        return NULL;
    for (int32_t ci = 0; ci < ct->count; ci++) {
        fbx_node_t *video;
        uint32_t content_len = 0;
        const uint8_t *content;
        const char *hint;
        void *pixels;
        if (ct->entries[ci].parent_id != texture_id)
            continue;
        video = fbx_find_object_by_id(objects, ct->entries[ci].child_id);
        if (!video || strcmp(video->name, "Video") != 0)
            continue;
        content = fbx_video_content_bytes(video, &content_len);
        if (!content || content_len == 0)
            continue;
        hint = fbx_video_filename_hint(video);
        pixels = fbx_decode_texture_payload(hint && *hint ? hint : texture_hint, content, content_len);
        if (!pixels && fallback_hint && fallback_hint != texture_hint)
            pixels = fbx_decode_texture_payload(fallback_hint, content, content_len);
        if (pixels)
            return pixels;
    }
    return NULL;
}

/// @brief Resolve and attach textures to the asset's materials: for each Texture node, load
///   its image (external file relative to @p cpath, or embedded Video content) and assign it
///   to the connected material's slots per @p material_bindings.
static void fbx_load_link_textures(rt_fbx_asset *asset,
                                   fbx_node_t *objects,
                                   const char *cpath,
                                   fbx_conn_table_t ct,
                                   fbx_material_binding_t *material_bindings,
                                   int32_t material_binding_count) {
    if (asset && objects) {
        int32_t material_count =
            fbx_asset_safe_count(asset->materials, asset->material_count, asset->material_capacity);
        if (material_count <= 0)
            return;
        if (!material_bindings || material_binding_count < 0)
            material_binding_count = 0;
        // Collect Texture nodes and their filenames
        for (int32_t i = 0; i < objects->child_count; i++) {
            fbx_node_t *obj = &objects->children[i];
            if (strcmp(obj->name, "Texture") != 0)
                continue;
            if (obj->prop_count < 1)
                continue;
            int64_t tex_id = fbx_prop_i64(obj, 0);

            // Extract RelativeFilename from Properties70
            fbx_node_t *p70 = fbx_find_child(obj, "Properties70");
            const char *rel_filename = NULL;
            const char *filename = NULL;
            if (p70) {
                for (int32_t pi = 0; pi < p70->child_count; pi++) {
                    fbx_node_t *p = &p70->children[pi];
                    if (strcmp(p->name, "P") != 0 || p->prop_count < 5)
                        continue;
                    const char *pname = fbx_prop_str(p, 0);
                    if (strcmp(pname, "RelativeFilename") == 0) {
                        rel_filename = fbx_prop_str(p, 4);
                    } else if (strcmp(pname, "FileName") == 0) {
                        filename = fbx_prop_str(p, 4);
                    }
                }
            }
            // Fallback: check for direct RelativeFilename child node
            if (!rel_filename || !*rel_filename) {
                fbx_node_t *rfn = fbx_find_child(obj, "RelativeFilename");
                if (rfn && rfn->prop_count > 0)
                    rel_filename = fbx_prop_str(rfn, 0);
            }
            if (!filename || !*filename) {
                fbx_node_t *fn = fbx_find_child(obj, "FileName");
                if (fn && fn->prop_count > 0)
                    filename = fbx_prop_str(fn, 0);
            }
            if ((!rel_filename || !*rel_filename) && (!filename || !*filename))
                continue;

            // Load texture via auto-detect loader
            void *pixels = fbx_try_load_texture_path(cpath, rel_filename);
            if (!pixels && filename && *filename &&
                (!rel_filename || !*rel_filename || strcmp(filename, rel_filename) != 0))
                pixels = fbx_try_load_texture_path(cpath, filename);
            if (!pixels)
                pixels = fbx_try_decode_embedded_texture(
                    objects, &ct, tex_id, rel_filename, filename);
            if (!pixels)
                continue;

            // Find which material this texture connects to via Connections
            for (int32_t ci = 0; ci < ct.count; ci++) {
                if (ct.entries[ci].child_id != tex_id)
                    continue;
                int64_t mat_id = ct.entries[ci].parent_id;
                const char *prop_name = ct.entries[ci].prop;

                void *mat = NULL;
                for (int32_t mi = 0; mi < material_binding_count; mi++) {
                    if (material_bindings[mi].id == mat_id) {
                        mat = material_bindings[mi].material;
                        break;
                    }
                }
                if (!mat)
                    continue;

                // Assign based on property name in Connection
                if (strcmp(prop_name, "DiffuseColor") == 0 || *prop_name == '\0')
                    rt_material3d_set_texture(mat, pixels);
                else if (strcmp(prop_name, "NormalMap") == 0 || strcmp(prop_name, "Bump") == 0)
                    rt_material3d_set_normal_map(mat, pixels);
                else if (strcmp(prop_name, "SpecularColor") == 0)
                    rt_material3d_set_specular_map(mat, pixels);
                else if (strcmp(prop_name, "EmissiveColor") == 0)
                    rt_material3d_set_emissive_map(mat, pixels);
                else
                    rt_material3d_set_texture(mat, pixels); // default to diffuse
                break;
            }
            fbx_release_ref(&pixels);
        }
    }
}

/// @brief Extract BlendShape (morph target) deformers and attach them to their meshes.
///   Extracted phase of rt_fbx_load; traces Shape -> BlendShapeChannel -> mesh via the
///   connection table and records per-vertex deltas as MorphTarget3D channels.
static void fbx_load_extract_morphs(rt_fbx_asset *asset,
                                    fbx_node_t *objects,
                                    fbx_conn_table_t ct,
                                    int z_up,
                                    fbx_mesh_remap_t *mesh_remaps,
                                    int32_t mesh_remap_count) {
    if (objects) {
        int32_t mesh_count =
            fbx_asset_safe_count(asset->meshes, asset->mesh_count, asset->mesh_capacity);
        if (mesh_count <= 0)
            return;
        // Allocate parallel morph_targets array (one per mesh, NULL if no morph)
        asset->morph_targets = (void **)calloc((size_t)mesh_count, sizeof(void *));
        if (!asset->morph_targets)
            return;
        asset->morph_count = mesh_count;
        asset->morph_capacity = mesh_count;

        // Collect Shape geometry nodes (type "Shape")
        for (int32_t i = 0; i < objects->child_count; i++) {
            fbx_node_t *obj = &objects->children[i];
            if (strcmp(obj->name, "Geometry") != 0 || obj->prop_count < 3)
                continue;
            const char *geo_type = fbx_prop_str(obj, 2);
            if (strcmp(geo_type, "Shape") != 0)
                continue;

            int64_t shape_id = fbx_prop_i64(obj, 0);
            // Get shape name from node (format: "ShapeName\x00\x01Geometry")
            const char *raw_name = fbx_prop_str(obj, 1);
            char shape_name[64];
            {
                const char *sep = strchr(raw_name, '\x00');
                size_t nlen = sep ? (size_t)(sep - raw_name) : strlen(raw_name);
                if (nlen >= sizeof(shape_name))
                    nlen = sizeof(shape_name) - 1;
                memcpy(shape_name, raw_name, nlen);
                shape_name[nlen] = '\0';
            }

            // Extract Indexes and Vertices arrays
            fbx_node_t *idx_node = fbx_find_child(obj, "Indexes");
            fbx_node_t *vtx_node = fbx_find_child(obj, "Vertices");
            if (!idx_node || !vtx_node || idx_node->prop_count < 1 || vtx_node->prop_count < 1)
                continue;

            // Trace connections: Shape → BlendShapeChannel → BlendShape → Mesh Geometry
            // Find which mesh this shape belongs to via the connection chain
            int64_t channel_id = -1, blendshape_id = -1, mesh_geo_id = -1;
            for (int32_t ci = 0; ci < ct.count; ci++) {
                if (ct.entries[ci].child_id == shape_id)
                    channel_id = ct.entries[ci].parent_id;
            }
            if (channel_id >= 0) {
                for (int32_t ci = 0; ci < ct.count; ci++) {
                    if (ct.entries[ci].child_id == channel_id)
                        blendshape_id = ct.entries[ci].parent_id;
                }
            }
            if (blendshape_id >= 0) {
                for (int32_t ci = 0; ci < ct.count; ci++) {
                    if (ct.entries[ci].child_id == blendshape_id)
                        mesh_geo_id = ct.entries[ci].parent_id;
                }
            }
            if (mesh_geo_id < 0)
                continue;

            // Find which mesh index corresponds to this geometry ID
            int mesh_idx = -1;
            {
                int counter = 0;
                for (int32_t oi = 0; oi < objects->child_count; oi++) {
                    fbx_node_t *geo = &objects->children[oi];
                    if (strcmp(geo->name, "Geometry") != 0 || geo->prop_count < 3)
                        continue;
                    const char *gt = fbx_prop_str(geo, 2);
                    if (strcmp(gt, "Mesh") != 0)
                        continue;
                    if (fbx_prop_i64(geo, 0) == mesh_geo_id) {
                        mesh_idx = counter;
                        break;
                    }
                    counter++;
                }
            }
            if (mesh_idx < 0 || mesh_idx >= mesh_count)
                continue;

            // Create morph target if not yet created for this mesh
            rt_mesh3d *mesh = (rt_mesh3d *)asset->meshes[mesh_idx];
            const fbx_mesh_remap_t *shape_remap =
                fbx_find_mesh_remap(mesh_remaps, mesh_remap_count, mesh_geo_id);
            if (!mesh)
                continue;
            if (!shape_remap && mesh->vertex_count > (uint32_t)INT32_MAX)
                continue;
            if (!asset->morph_targets[mesh_idx]) {
                asset->morph_targets[mesh_idx] = rt_morphtarget3d_new((int64_t)mesh->vertex_count);
            }
            void *morph = asset->morph_targets[mesh_idx];
            if (!morph)
                continue;

            // Add this shape
            rt_string sname = rt_const_cstr(shape_name);
            int64_t si = rt_morphtarget3d_add_shape(morph, sname);
            if (si < 0)
                continue;

            // Read delta data: Indexes (int32[]) and Vertices (double[3*count])
            // The Indexes array contains affected vertex indices,
            // Vertices contains corresponding position deltas (3 doubles per index)
            fbx_prop_t *idx_prop = &idx_node->props[0];
            fbx_prop_t *vtx_prop = &vtx_node->props[0];

            int32_t delta_count = 0;
            const int32_t *indices_ptr = NULL;
            const double *deltas_ptr = NULL;

            if (idx_prop->type == 'i' && idx_prop->v.array.count > 0 &&
                idx_prop->v.array.count <= (uint32_t)INT32_MAX) {
                delta_count = (int32_t)idx_prop->v.array.count;
                indices_ptr = (const int32_t *)idx_prop->v.array.data;
            }
            if (vtx_prop->type == 'd' && delta_count > 0 &&
                vtx_prop->v.array.count / 3u >= (uint32_t)delta_count) {
                deltas_ptr = (const double *)vtx_prop->v.array.data;
            }

            if (indices_ptr && deltas_ptr) {
                for (int32_t di = 0; di < delta_count; di++) {
                    int32_t vi = indices_ptr[di];
                    double dx = deltas_ptr[di * 3 + 0];
                    double dy = deltas_ptr[di * 3 + 1];
                    double dz = deltas_ptr[di * 3 + 2];
                    if (z_up) {
                        double tmp = dy;
                        dy = dz;
                        dz = -tmp;
                    }
                    if (!fbx_sanitize_position3(&dx, &dy, &dz))
                        continue;
                    if (shape_remap && shape_remap->control_vertices && vi >= 0 &&
                        vi < shape_remap->control_count) {
                        const fbx_vertex_index_list_t *list = &shape_remap->control_vertices[vi];
                        for (int32_t li = 0; li < list->count; li++)
                            rt_morphtarget3d_set_delta(
                                morph, si, (int64_t)list->vertices[li], dx, dy, dz);
                    } else if (vi >= 0 && vi < (int32_t)mesh->vertex_count) {
                        rt_morphtarget3d_set_delta(morph, si, (int64_t)vi, dx, dy, dz);
                    }
                }
            }
        }
    }
}

/// @brief Walk the FBX "Objects" node, importing each "Geometry" into a Mesh3D (with optional
///   Z-up correction) and accumulating mesh bindings, geometry-id remaps, and material
///   bindings through the in/out array and count pointers.
/// @return 1 on success, 0 on allocation failure.
static int fbx_load_collect_geometry(rt_fbx_asset *asset,
                                     fbx_node_t *objects,
                                     int z_up,
                                     fbx_mesh_binding_t **p_mesh_bindings,
                                     int32_t *p_mesh_binding_count,
                                     fbx_mesh_remap_t **p_mesh_remaps,
                                     int32_t *p_mesh_remap_count,
                                     fbx_material_binding_t **p_material_bindings,
                                     int32_t *p_material_binding_count) {
    fbx_mesh_binding_t *mesh_bindings = *p_mesh_bindings;
    int32_t mesh_binding_count = *p_mesh_binding_count;
    fbx_mesh_remap_t *mesh_remaps = *p_mesh_remaps;
    int32_t mesh_remap_count = *p_mesh_remap_count;
    fbx_material_binding_t *material_bindings = *p_material_bindings;
    int32_t material_binding_count = *p_material_binding_count;
    if (objects && objects->children && objects->child_count > 0) {
        for (int32_t i = 0; i < objects->child_count; i++) {
            fbx_node_t *obj = &objects->children[i];
            if (strcmp(obj->name, "Geometry") == 0) {
                fbx_mesh_remap_t remap;
                fbx_mesh_material_map_t material_map;
                memset(&remap, 0, sizeof(remap));
                memset(&material_map, 0, sizeof(material_map));
                void *mesh = fbx_extract_geometry(obj, z_up, &remap, &material_map);
                if (mesh) {
                    asset->mesh_count =
                        fbx_asset_safe_count(asset->meshes, asset->mesh_count, asset->mesh_capacity);
                    if (asset->mesh_count == INT32_MAX) {
                        fbx_release_ref(&mesh);
                        fbx_mesh_remap_free(&remap);
                        return 0;
                    }
                    int32_t nc = asset->mesh_count + 1;
                    if ((size_t)nc > SIZE_MAX / sizeof(*mesh_bindings)) {
                        fbx_release_ref(&mesh);
                        fbx_mesh_remap_free(&remap);
                        fbx_mesh_material_map_free(&material_map);
                        return 0;
                    }
                    if (!fbx_asset_reserve_ref_array(
                            &asset->meshes, &asset->mesh_capacity, nc)) {
                        fbx_release_ref(&mesh);
                        fbx_mesh_remap_free(&remap);
                        fbx_mesh_material_map_free(&material_map);
                        return 0;
                    }
                    void *nb = realloc(mesh_bindings, (size_t)nc * sizeof(*mesh_bindings));
                    if (!nb) {
                        fbx_release_ref(&mesh);
                        fbx_mesh_remap_free(&remap);
                        fbx_mesh_material_map_free(&material_map);
                        return 0;
                    }
                    mesh_bindings = (fbx_mesh_binding_t *)nb;
                    *p_mesh_bindings = mesh_bindings;
                    if (!fbx_mesh_remaps_append(&mesh_remaps, &mesh_remap_count, &remap)) {
                        fbx_release_ref(&mesh);
                        fbx_mesh_remap_free(&remap);
                        fbx_mesh_material_map_free(&material_map);
                        return 0;
                    }
                    *p_mesh_remaps = mesh_remaps;
                    *p_mesh_remap_count = mesh_remap_count;
                    asset->meshes[asset->mesh_count] = mesh;
                    asset->mesh_count = nc;
                    mesh_bindings[mesh_binding_count].id = fbx_prop_i64(obj, 0);
                    mesh_bindings[mesh_binding_count].mesh = mesh;
                    mesh_bindings[mesh_binding_count].material_map = material_map;
                    memset(&material_map, 0, sizeof(material_map));
                    mesh_binding_count++;
                    *p_mesh_binding_count = mesh_binding_count;
                }
                fbx_mesh_remap_free(&remap);
                fbx_mesh_material_map_free(&material_map);
            } else if (strcmp(obj->name, "Material") == 0) {
                void *mat = fbx_extract_material(obj);
                if (mat) {
                    asset->material_count = fbx_asset_safe_count(
                        asset->materials, asset->material_count, asset->material_capacity);
                    if (asset->material_count == INT32_MAX) {
                        fbx_release_ref(&mat);
                        return 0;
                    }
                    int32_t nc = asset->material_count + 1;
                    if ((size_t)nc > SIZE_MAX / sizeof(*material_bindings)) {
                        fbx_release_ref(&mat);
                        return 0;
                    }
                    if (!fbx_asset_reserve_ref_array(
                            &asset->materials, &asset->material_capacity, nc)) {
                        fbx_release_ref(&mat);
                        return 0;
                    }
                    void *nb = realloc(material_bindings, (size_t)nc * sizeof(*material_bindings));
                    if (!nb) {
                        fbx_release_ref(&mat);
                        return 0;
                    }
                    material_bindings = (fbx_material_binding_t *)nb;
                    *p_material_bindings = material_bindings;
                    asset->materials[asset->material_count] = mat;
                    asset->material_count = nc;
                    material_bindings[material_binding_count].id = fbx_prop_i64(obj, 0);
                    material_bindings[material_binding_count].material = mat;
                    material_binding_count++;
                    *p_material_binding_count = material_binding_count;
                }
            }
        }
    }
    *p_mesh_bindings = mesh_bindings;
    *p_mesh_binding_count = mesh_binding_count;
    *p_mesh_remaps = mesh_remaps;
    *p_mesh_remap_count = mesh_remap_count;
    *p_material_bindings = material_bindings;
    *p_material_binding_count = material_binding_count;
    return 1;
}

/// @brief Load an FBX binary file and extract meshes, skeleton, animations, and materials.
/// @details Parses the FBX binary format (magic header "Kaydara FBX Binary"),
///          decodes geometry, deformers (skin clusters for skeletal binding),
///          animation curves, and materials. Z-up models are automatically
///          corrected to Y-up. Supports FBX versions 7.1–7.5 (most common).
/// @param path File path to the .fbx file (runtime string).
/// @return Opaque FBX asset handle containing meshes/skeleton/animations, or NULL.
void *rt_fbx_load(rt_string path) {
    fbx_mesh_binding_t *mesh_bindings = NULL;
    int32_t mesh_binding_count = 0;
    fbx_mesh_remap_t *mesh_remaps = NULL;
    int32_t mesh_remap_count = 0;
    fbx_material_binding_t *material_bindings = NULL;
    int32_t material_binding_count = 0;
    fbx_bone_binding_t *bone_bindings = NULL;
    int32_t bone_binding_count = 0;
    if (!path) {
        rt_trap("FBX.Load: null path");
        return NULL;
    }
    const char *cpath = rt_string_cstr(path);
    if (!cpath) {
        rt_trap("FBX.Load: invalid path");
        return NULL;
    }

    /* Read file */
    FILE *f = fopen(cpath, "rb");
    if (!f) {
        rt_trap("FBX.Load: cannot open file");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 27) {
        fclose(f);
        rt_trap("FBX.Load: file too small");
        return NULL;
    }

    uint8_t *data = (uint8_t *)malloc((size_t)fsize + 1u);
    if (!data) {
        fclose(f);
        rt_trap("FBX.Load: out of memory");
        return NULL;
    }
    if (fread(data, 1, (size_t)fsize, f) != (size_t)fsize) {
        fclose(f);
        free(data);
        rt_trap("FBX.Load: read error");
        return NULL;
    }
    data[(size_t)fsize] = 0;
    fclose(f);

    /* Verify magic */
    static const char magic[] = "Kaydara FBX Binary  ";
    if (memcmp(data, magic, 20) != 0) {
        void *ascii_asset = fbx_load_ascii_minimal((const char *)data);
        free(data);
        if (ascii_asset)
            return ascii_asset;
        rt_trap("FBX.Load: not a supported FBX file");
        return NULL;
    }

    /* Parse header */
    fbx_reader_t reader;
    reader.data = data;
    reader.len = (size_t)fsize;
    reader.pos = 27; /* skip 23-byte magic + 2 unknown + 4 version */
    memcpy(&reader.version, data + 23, 4);
    reader.is_64bit = (reader.version >= 7500);
    reader.error = 0;

    /* Parse all top-level nodes into a virtual root */
    fbx_node_t root;
    memset(&root, 0, sizeof(root));
    strcpy(root.name, "_root_");

    while (!fbx_eof(&reader)) {
        /* Check for file-level null sentinel */
        size_t sentinel = reader.is_64bit ? 25 : 13;
        if (reader.pos + sentinel <= reader.len) {
            int is_null = 1;
            for (size_t si = 0; si < sentinel; si++)
                if (reader.data[reader.pos + si] != 0) {
                    is_null = 0;
                    break;
                }
            if (is_null)
                break;
        }

        if (root.child_count >= root.child_capacity) {
            if (root.child_capacity < 0 || root.child_capacity > INT32_MAX / 2 ||
                (size_t)(root.child_capacity == 0 ? 16 : root.child_capacity * 2) >
                    SIZE_MAX / sizeof(fbx_node_t))
                break;
            int32_t new_cap = root.child_capacity == 0 ? 16 : root.child_capacity * 2;
            fbx_node_t *nc =
                (fbx_node_t *)realloc(root.children, (size_t)new_cap * sizeof(fbx_node_t));
            if (!nc)
                break;
            root.children = nc;
            root.child_capacity = new_cap;
        }

        fbx_node_t *child = &root.children[root.child_count];
        if (fbx_parse_node(&reader, child) < 0) {
            reader.error = 1;
            break;
        }
        root.child_count++;
    }

    free(data);
    if (reader.error) {
        fbx_free_node(&root);
        rt_trap("FBX.Load: malformed or truncated binary FBX");
        return NULL;
    }

    /* Build connection table */
    fbx_conn_table_t ct;
    memset(&ct, 0, sizeof(ct));
    fbx_parse_connections(&root, &ct);

    /* Detect coordinate system */
    int z_up = fbx_is_z_up(&root);

    /* Extract assets */
    rt_fbx_asset *asset =
        (rt_fbx_asset *)rt_obj_new_i64(RT_G3D_FBX_ASSET_CLASS_ID, (int64_t)sizeof(rt_fbx_asset));
    if (!asset) {
        fbx_mesh_bindings_free(mesh_bindings, mesh_binding_count);
        fbx_mesh_remaps_free(mesh_remaps, mesh_remap_count);
        free(material_bindings);
        free(ct.entries);
        fbx_free_node(&root);
        rt_trap("FBX.Load: out of memory");
        return NULL;
    }
    asset->vptr = NULL;
    asset->meshes = NULL;
    asset->mesh_count = 0;
    asset->mesh_capacity = 0;
    asset->skeleton = NULL;
    asset->animations = NULL;
    asset->animation_count = 0;
    asset->animation_capacity = 0;
    asset->materials = NULL;
    asset->material_count = 0;
    asset->material_capacity = 0;
    asset->morph_targets = NULL;
    asset->morph_count = 0;
    asset->morph_capacity = 0;
    asset->scene_root = NULL;
    rt_obj_set_finalizer(asset, rt_fbx_asset_finalize);

    /* Extract geometry */
    fbx_node_t *objects = fbx_find_child(&root, "Objects");
    if (!fbx_load_collect_geometry(asset,
                                   objects,
                                   z_up,
                                   &mesh_bindings,
                                   &mesh_binding_count,
                                   &mesh_remaps,
                                   &mesh_remap_count,
                                   &material_bindings,
                                   &material_binding_count)) {
        fbx_mesh_bindings_free(mesh_bindings, mesh_binding_count);
        fbx_mesh_remaps_free(mesh_remaps, mesh_remap_count);
        free(material_bindings);
        free(ct.entries);
        fbx_free_node(&root);
        fbx_release_ref((void **)&asset);
        rt_trap("FBX.Load: failed to collect geometry/material bindings");
        return NULL;
    }

    /* Extract textures and link to materials */
    fbx_load_link_textures(asset, objects, cpath, ct, material_bindings, material_binding_count);

    /* Extract morph targets (BlendShape deformers) */
    fbx_load_extract_morphs(asset, objects, ct, z_up, mesh_remaps, mesh_remap_count);
    /* Extract skeleton */
    asset->skeleton = fbx_extract_skeleton(&root, &ct, z_up, &bone_bindings, &bone_binding_count);
    fbx_apply_skinning(objects,
                       &ct,
                       asset->skeleton,
                       bone_bindings,
                       bone_binding_count,
                       mesh_bindings,
                       mesh_binding_count,
                       mesh_remaps,
                       mesh_remap_count);

    /* Extract animations */
    fbx_extract_animations(&root,
                           &ct,
                           asset->skeleton,
                           bone_bindings,
                           bone_binding_count,
                           z_up,
                           &asset->animations,
                           &asset->animation_count,
                           &asset->animation_capacity);

    {
        int scene_failed = 0;
        asset->scene_root = fbx_build_scene_root(&root,
                                                 objects,
                                                 &ct,
                                                 mesh_bindings,
                                                 mesh_binding_count,
                                                 material_bindings,
                                                 material_binding_count,
                                                 z_up,
                                                 &scene_failed);
        if (scene_failed) {
            free(ct.entries);
            fbx_mesh_bindings_free(mesh_bindings, mesh_binding_count);
            fbx_mesh_remaps_free(mesh_remaps, mesh_remap_count);
            free(material_bindings);
            free(bone_bindings);
            fbx_free_node(&root);
            fbx_release_ref((void **)&asset);
            rt_trap("FBX.Load: failed to build scene hierarchy");
            return NULL;
        }
    }

    /* Cleanup parser data */
    free(ct.entries);
    fbx_mesh_bindings_free(mesh_bindings, mesh_binding_count);
    fbx_mesh_remaps_free(mesh_remaps, mesh_remap_count);
    free(material_bindings);
    free(bone_bindings);
    fbx_free_node(&root);

    return asset;
}

/*==========================================================================
 * FBX asset accessors
 *=========================================================================*/

/// @brief Get the number of meshes extracted from the FBX file.
int64_t rt_fbx_mesh_count(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? fbx_asset_safe_count(a->meshes, a->mesh_count, a->mesh_capacity) : 0;
}

/// @brief Get a mesh by index from the loaded FBX asset.
void *rt_fbx_get_mesh(void *obj, int64_t index) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    if (!a)
        return NULL;
    int32_t mesh_count = fbx_asset_safe_count(a->meshes, a->mesh_count, a->mesh_capacity);
    if (index < 0 || index >= mesh_count)
        return NULL;
    return a->meshes[index];
}

/// @brief Get the skeleton extracted from the FBX file (NULL if no skeleton).
void *rt_fbx_get_skeleton(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? a->skeleton : NULL;
}

/// @brief Get the `SceneNode3D` root of the imported scene graph — the tree of models
/// the FBX author created, with their world transforms and mesh/material bindings.
/// Returned reference is borrowed; the asset owns the lifetime. Distinct from the flat
/// `mesh_count` / `material_count` lists which expose every shared resource the scene
/// uses, regardless of whether it's actually attached to a node.
void *rt_fbx_get_scene_root(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? a->scene_root : NULL;
}

/// @brief Get the number of animation clips in the FBX file.
int64_t rt_fbx_animation_count(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? fbx_asset_safe_count(a->animations, a->animation_count, a->animation_capacity) : 0;
}

/// @brief Get an animation clip by index from the loaded FBX asset.
void *rt_fbx_get_animation(void *obj, int64_t index) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    if (!a)
        return NULL;
    int32_t animation_count =
        fbx_asset_safe_count(a->animations, a->animation_count, a->animation_capacity);
    if (index < 0 || index >= animation_count)
        return NULL;
    return a->animations[index];
}

/// @brief Get the name of an animation clip by index.
rt_string rt_fbx_get_animation_name(void *obj, int64_t index) {
    void *anim = rt_fbx_get_animation(obj, index);
    if (!anim)
        return rt_const_cstr("");
    return rt_animation3d_get_name(anim);
}

/// @brief Get the number of materials extracted from the FBX file.
int64_t rt_fbx_material_count(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? fbx_asset_safe_count(a->materials, a->material_count, a->material_capacity) : 0;
}

/// @brief Get a material by index from the loaded FBX asset.
void *rt_fbx_get_material(void *obj, int64_t index) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    if (!a)
        return NULL;
    int32_t material_count =
        fbx_asset_safe_count(a->materials, a->material_count, a->material_capacity);
    if (index < 0 || index >= material_count)
        return NULL;
    return a->materials[index];
}

/// @brief Get the morph target data for a mesh by its index in the FBX asset.
void *rt_fbx_get_morph_target(void *obj, int64_t mesh_index) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    if (!a)
        return NULL;
    int32_t morph_count =
        fbx_asset_safe_count(a->morph_targets, a->morph_count, a->morph_capacity);
    if (mesh_index < 0 || mesh_index >= morph_count)
        return NULL;
    return a->morph_targets[mesh_index];
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
