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
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_quat.h"
#include "rt_scene3d.h"
#include "rt_skeleton3d.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"

#include <ctype.h>
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
    void *skeleton;
    void **animations;
    int32_t animation_count;
    void **materials;
    int32_t material_count;
    void **morph_targets; // rt_morphtarget3d*[] parallel to meshes[]
    int32_t morph_count;
    void *scene_root;
} rt_fbx_asset;

typedef struct {
    int64_t id;
    void *mesh;
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
    if (dir_len >= out_size)
        dir_len = out_size - 1;
    memcpy(out, normalized, dir_len);
    out[dir_len] = '\0';
}

/// @brief Join a directory and leaf filename using `/`.
static void fbx_join_path(char *out, size_t out_size, const char *dir, const char *leaf) {
    size_t dir_len;
    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (!leaf || !*leaf)
        return;
    if (!dir || !*dir) {
        fbx_normalize_path(out, out_size, leaf);
        return;
    }
    fbx_normalize_path(out, out_size, dir);
    dir_len = strlen(out);
    if (dir_len > 0 && out[dir_len - 1] != '/' && dir_len + 1 < out_size) {
        out[dir_len++] = '/';
        out[dir_len] = '\0';
    }
    if (dir_len + 1 < out_size) {
        fbx_normalize_path(out + dir_len, out_size - dir_len, leaf);
    }
}

/// @brief Try a texture reference verbatim, then fall back to its basename beside the FBX file.
static void *fbx_try_load_texture_path(const char *fbx_path, const char *texture_ref) {
    char normalized_ref[1024];
    char dir[1024];
    char candidate[1024];
    const char *basename;
    void *pixels;

    if (!texture_ref || !*texture_ref)
        return NULL;

    fbx_normalize_path(normalized_ref, sizeof(normalized_ref), texture_ref);
    if (!*normalized_ref)
        return NULL;

    fbx_parent_dir(dir, sizeof(dir), fbx_path);
    if (fbx_is_absolute_path(normalized_ref)) {
        pixels = rt_pixels_load(rt_const_cstr(normalized_ref));
    } else if (*dir) {
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
    if (!inflated)
        return NULL;

    bytes_view *iv = (bytes_view *)inflated;
    if (elem_size > 0 && count > SIZE_MAX / elem_size)
        return NULL; /* overflow guard for 32-bit platforms */
    size_t expected = (size_t)count * elem_size;
    if ((size_t)iv->len < expected)
        return NULL;

    void *result = malloc(expected);
    if (result)
        memcpy(result, iv->bdata, expected);
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
            int32_t new_cap = node->child_capacity == 0 ? 8 : node->child_capacity * 2;
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
            int32_t new_cap = ct->capacity == 0 ? 64 : ct->capacity * 2;
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
        new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
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

#include "rt_fbx_triangulation.inc"

/*==========================================================================
 * Geometry extraction
 *=========================================================================*/

/// @brief Convert an FBX `Geometry` node into a Viper `rt_mesh3d_t`.
///
/// Decodes the `Vertices` (positions), `PolygonVertexIndex`
/// (vertex indices, with the last index of each polygon negated
/// XOR'd with bit 31 to mark polygon end), `LayerElementNormal`,
/// `LayerElementUV`, and `LayerElementMaterial`. Triangulates n-gons
/// using ear clipping. If `z_up` is set, applies an axis
/// swap (positions and normals) to convert to Y-up.
/// @return A new mesh on success, NULL on missing or malformed geometry.
static void *fbx_extract_geometry(fbx_node_t *geom_node, int z_up, fbx_mesh_remap_t *remap) {
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
    if (remap)
        fbx_mesh_remap_init(remap, fbx_prop_i64(geom_node, 0), (int32_t)pos_count);

    /* Find normals (optional) */
    fbx_node_t *norm_layer = fbx_find_child(geom_node, "LayerElementNormal");
    double *normals = NULL;
    int32_t *norm_indices = NULL;
    uint32_t norm_count = 0;
    int norm_by_polygon_vertex = 0;
    int norm_index_to_direct = 0;

    if (norm_layer) {
        fbx_node_t *n_node = fbx_find_child(norm_layer, "Normals");
        if (n_node && n_node->prop_count >= 1 && n_node->props[0].type == 'd') {
            normals = (double *)n_node->props[0].v.array.data;
            norm_count = n_node->props[0].v.array.count / 3;
        }
        fbx_node_t *mm = fbx_find_child(norm_layer, "MappingInformationType");
        if (mm && mm->prop_count >= 1)
            norm_by_polygon_vertex = strcmp(fbx_prop_str(mm, 0), "ByPolygonVertex") == 0;
        fbx_node_t *rm = fbx_find_child(norm_layer, "ReferenceInformationType");
        if (rm && rm->prop_count >= 1)
            norm_index_to_direct = strcmp(fbx_prop_str(rm, 0), "IndexToDirect") == 0;
        if (norm_index_to_direct) {
            fbx_node_t *ni = fbx_find_child(norm_layer, "NormalsIndex");
            if (ni && ni->prop_count >= 1 && ni->props[0].type == 'i')
                norm_indices = (int32_t *)ni->props[0].v.array.data;
        }
    }

    /* Find UVs (optional) */
    fbx_node_t *uv_layer = fbx_find_child(geom_node, "LayerElementUV");
    double *uvs = NULL;
    int32_t *uv_indices = NULL;
    uint32_t uv_count = 0;
    int uv_by_polygon_vertex = 0;
    int uv_index_to_direct = 0;

    if (uv_layer) {
        fbx_node_t *u_node = fbx_find_child(uv_layer, "UV");
        if (u_node && u_node->prop_count >= 1 && u_node->props[0].type == 'd') {
            uvs = (double *)u_node->props[0].v.array.data;
            uv_count = u_node->props[0].v.array.count / 2;
        }
        fbx_node_t *umm = fbx_find_child(uv_layer, "MappingInformationType");
        if (umm && umm->prop_count >= 1)
            uv_by_polygon_vertex = strcmp(fbx_prop_str(umm, 0), "ByPolygonVertex") == 0;
        fbx_node_t *urm = fbx_find_child(uv_layer, "ReferenceInformationType");
        if (urm && urm->prop_count >= 1)
            uv_index_to_direct = strcmp(fbx_prop_str(urm, 0), "IndexToDirect") == 0;
        if (uv_index_to_direct) {
            fbx_node_t *ui = fbx_find_child(uv_layer, "UVIndex");
            if (ui && ui->prop_count >= 1 && ui->props[0].type == 'i')
                uv_indices = (int32_t *)ui->props[0].v.array.data;
        }
    }

    /* Build mesh: iterate polygon indices, triangulate with fan */
    void *mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;

    int32_t polygon[32];
    int32_t poly_count = 0;
    int32_t polygon_vertex_idx = 0; /* running index for ByPolygonVertex mapping */
    int32_t mesh_vertex_count = 0;

    for (uint32_t i = 0; i < idx_count; i++) {
        int32_t raw_idx = indices[i];
        int end_of_polygon = (raw_idx < 0);
        int32_t vi = end_of_polygon ? ~raw_idx : raw_idx;

        if (vi < 0 || vi >= (int32_t)pos_count) {
            polygon_vertex_idx++;
            continue;
        }

        /* Get position */
        double px = positions[vi * 3 + 0];
        double py = positions[vi * 3 + 1];
        double pz = positions[vi * 3 + 2];
        if (z_up)
            fbx_correct_zup(&px, &py, &pz);

        /* Get normal */
        double nx = 0, ny = 1, nz = 0;
        if (normals) {
            int ni = 0;
            if (norm_by_polygon_vertex)
                ni = norm_index_to_direct ? (norm_indices ? norm_indices[polygon_vertex_idx] : 0)
                                          : polygon_vertex_idx;
            else
                ni = norm_index_to_direct ? (norm_indices ? norm_indices[vi] : 0) : vi;
            if (ni >= 0 && ni < (int32_t)norm_count) {
                nx = normals[ni * 3 + 0];
                ny = normals[ni * 3 + 1];
                nz = normals[ni * 3 + 2];
                if (z_up)
                    fbx_correct_zup(&nx, &ny, &nz);
            }
        }

        /* Get UV */
        double u = 0, v = 0;
        if (uvs) {
            int ui = 0;
            if (uv_by_polygon_vertex)
                ui = uv_index_to_direct ? (uv_indices ? uv_indices[polygon_vertex_idx] : 0)
                                        : polygon_vertex_idx;
            else
                ui = uv_index_to_direct ? (uv_indices ? uv_indices[vi] : 0) : vi;
            if (ui >= 0 && ui < (int32_t)uv_count) {
                u = uvs[ui * 2 + 0];
                v = 1.0 - uvs[ui * 2 + 1]; /* FBX V is flipped vs OpenGL */
            }
        }

        {
            int32_t emitted_vertex = mesh_vertex_count++;
            if (poly_count >= (int32_t)(sizeof(polygon) / sizeof(polygon[0]))) {
                if (rt_obj_release_check0(mesh))
                    rt_obj_free(mesh);
                return NULL;
            }
            rt_mesh3d_add_vertex(mesh, px, py, pz, nx, ny, nz, u, v);
            fbx_mesh_remap_add_vertex(remap, vi, emitted_vertex);
            polygon[poly_count++] = emitted_vertex;
        }
        polygon_vertex_idx++;

        if (end_of_polygon) {
            if (!fbx_emit_polygon_triangles(mesh, polygon, poly_count)) {
                if (rt_obj_release_check0(mesh))
                    rt_obj_free(mesh);
                return NULL;
            }
            poly_count = 0;
        }
    }

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
            double r = fbx_prop_f64(p, 4);
            double g = fbx_prop_f64(p, 5);
            double b = fbx_prop_f64(p, 6);
            rt_material3d_set_color(mat, r, g, b);
        } else if (strcmp(pname, "Shininess") == 0 || strcmp(pname, "ShininessExponent") == 0) {
            double s = fbx_prop_f64(p, 4);
            rt_material3d_set_shininess(mat, s);
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
    double sx = 1.0;
    double sy = 1.0;
    double sz = 1.0;
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
            }
        }
    }

    if (z_up)
        fbx_correct_zup(&tx, &ty, &tz);

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

    if (pos) {
        pos[0] = tx;
        pos[1] = ty;
        pos[2] = tz;
    }
    if (quat) {
        quat[0] = qx;
        quat[1] = qy;
        quat[2] = qz;
        quat[3] = qw;
    }
    if (scale) {
        scale[0] = sx;
        scale[1] = sy;
        scale[2] = sz;
    }
}

/// @brief Linear search for a `Mesh3D` associated with an FBX object ID. Typical FBX
/// assets have a few dozen meshes so O(n) is fine — switching to a hash would cost
/// more in setup than it saves on lookup. Returns NULL when no binding matches.
static void *fbx_lookup_mesh_binding(const fbx_mesh_binding_t *bindings,
                                     int32_t count,
                                     int64_t id) {
    for (int32_t i = 0; i < count; i++)
        if (bindings[i].id == id)
            return bindings[i].mesh;
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
                                  int z_up) {
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
    int failed = 0;

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
    }

    for (int32_t i = 0; !failed && i < model_count; i++) {
        void *primary_mesh = NULL;
        void *primary_material = NULL;
        int32_t extra_mesh_count = 0;
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
            if (material && !primary_material)
                primary_material = material;
        }
        for (int32_t ci = 0; ci < ct->count; ci++) {
            int64_t child_id = ct->entries[ci].child_id;
            int64_t parent_id = ct->entries[ci].parent_id;
            fbx_node_t *child_obj;
            void *mesh;
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
            mesh = fbx_lookup_mesh_binding(mesh_bindings, mesh_binding_count, child_id);
            if (!mesh)
                continue;
            if (!primary_mesh) {
                primary_mesh = mesh;
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
            if (primary_material)
                rt_scene_node3d_set_material(mesh_node, primary_material);
            rt_scene_node3d_add_child(models[i].node, mesh_node);
            fbx_release_ref(&mesh_node);
            extra_mesh_count++;
        }
        if (failed)
            break;
        if (primary_mesh) {
            rt_scene_node3d_set_mesh(models[i].node, primary_mesh);
            if (primary_material)
                rt_scene_node3d_set_material(models[i].node, primary_material);
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
        rt_scene_node3d_add_child(parent, models[i].node);
    }

    for (int32_t i = 0; i < model_count; i++)
        fbx_release_ref(&models[i].node);
    free(models);

    if (failed) {
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
static void *fbx_extract_skeleton(fbx_node_t *root, const fbx_conn_table_t *ct, int z_up) {
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

        if (bone_count >= bone_cap) {
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

        /* Build full TRS bind matrix (rotation from Euler ZYX, then scale) */
        double rx = bi->lcl_rotation[0] * 3.14159265358979323846 / 180.0;
        double ry = bi->lcl_rotation[1] * 3.14159265358979323846 / 180.0;
        double rz = bi->lcl_rotation[2] * 3.14159265358979323846 / 180.0;
        double cxr = cos(rx), sxr = sin(rx);
        double cyr = cos(ry), syr = sin(ry);
        double czr = cos(rz), szr = sin(rz);
        /* R = Rz * Ry * Rx (standard FBX Euler order) */
        double r00 = cyr * czr, r01 = sxr * syr * czr - cxr * szr,
               r02 = cxr * syr * czr + sxr * szr;
        double r10 = cyr * szr, r11 = sxr * syr * szr + cxr * czr,
               r12 = cxr * syr * szr - sxr * czr;
        double r20 = -syr, r21 = sxr * cyr, r22 = cxr * cyr;
        double scx = bi->lcl_scaling[0], scy = bi->lcl_scaling[1], scz = bi->lcl_scaling[2];
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
        rt_skeleton3d_add_bone(skel, rt_const_cstr(bi->name), parent_idx, bind_mat);
        bi->bone_index = (int32_t)i;
    }

    rt_skeleton3d_compute_inverse_bind(skel);

    free(order);
    free(placed);
    free(bones);
    return skel;
}

/*==========================================================================
 * Animation extraction
 *=========================================================================*/

#define FBX_TIME_SECOND 46186158000LL

/// @brief Collect all direct children of @p parent_id from the FBX connection table.
/// @details Scans every entry in @p ct and copies matching child IDs (and optional
///          relationship property strings) into the caller-supplied output arrays up to
///          @p max_out entries. Does not sort or deduplicate results.
/// @param out_props May be NULL; when non-NULL receives the relationship prop string for each
/// child.
/// @return Number of children written to @p out_ids (capped at @p max_out).
static int32_t fbx_find_children(const fbx_conn_table_t *ct,
                                 int64_t parent_id,
                                 int64_t *out_ids,
                                 const char **out_props,
                                 int32_t max_out) {
    int32_t count = 0;
    for (int32_t i = 0; i < ct->count && count < max_out; i++) {
        if (ct->entries[i].parent_id == parent_id) {
            out_ids[count] = ct->entries[i].child_id;
            if (out_props)
                out_props[count] = ct->entries[i].prop;
            count++;
        }
    }
    return count;
}

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
                                             int64_t model_id) {
    fbx_node_t *model_node;
    char decoded_name[64];
    if (!objects || !skeleton || model_id == 0)
        return -1;
    model_node = fbx_find_object_by_id(objects, model_id);
    if (!model_node || strcmp(model_node->name, "Model") != 0 || model_node->prop_count < 2)
        return -1;
    fbx_decode_object_name(fbx_prop_str(model_node, 1), decoded_name, sizeof(decoded_name));
    if (decoded_name[0] == '\0')
        return -1;
    return (int32_t)rt_skeleton3d_find_bone(skeleton, rt_const_cstr(decoded_name));
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
    if (!dst || bone_index < 0 || weight <= 0.0)
        return;
    for (int i = 0; i < 4; i++) {
        if (dst->weights[i] > 0.0 && dst->bone_indices[i] == bone_index) {
            dst->weights[i] += weight;
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
        bones[i] = influence->weights[i] > 0.0 ? influence->bone_indices[i] : 0;
        weights[i] = influence->weights[i] > 0.0 ? influence->weights[i] : 0.0;
        sum += weights[i];
    }
    if (sum <= 0.0)
        return;
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
        control_count =
            remap ? remap->control_count : (int32_t)((rt_mesh3d *)mesh_obj)->vertex_count;
        if (control_count <= 0)
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
            bone_index = fbx_find_bone_index_for_model(objects, skeleton, bone_model_id);
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
            translation[0] = fbx_prop_f64(p, 4);
            translation[1] = fbx_prop_f64(p, 5);
            translation[2] = fbx_prop_f64(p, 6);
        } else if (rotation && strcmp(pn, "Lcl Rotation") == 0) {
            rotation[0] = fbx_prop_f64(p, 4);
            rotation[1] = fbx_prop_f64(p, 5);
            rotation[2] = fbx_prop_f64(p, 6);
        } else if (scale && strcmp(pn, "Lcl Scaling") == 0) {
            scale[0] = fbx_prop_f64(p, 4);
            scale[1] = fbx_prop_f64(p, 5);
            scale[2] = fbx_prop_f64(p, 6);
        }
    }
}

/// @brief Return non-zero if @p curve contains at least one keyframe with usable value data.
static int fbx_anim_curve_has_data(const fbx_anim_curve_view_t *curve) {
    return curve && curve->times && (curve->values64 || curve->values32) && curve->count > 0;
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
    if (!fbx_anim_curve_has_data(curve))
        return fallback;
    if (fbx_time <= curve->times[0])
        return curve->values64 ? curve->values64[0] : (double)curve->values32[0];
    for (uint32_t i = 1; i < curve->count; i++) {
        int64_t t0 = curve->times[i - 1];
        int64_t t1 = curve->times[i];
        double v0 = curve->values64 ? curve->values64[i - 1] : (double)curve->values32[i - 1];
        double v1 = curve->values64 ? curve->values64[i] : (double)curve->values32[i];
        if (fbx_time == t1)
            return v1;
        if (fbx_time < t1) {
            double a = t1 != t0 ? (double)(fbx_time - t0) / (double)(t1 - t0) : 0.0;
            if (a < 0.0)
                a = 0.0;
            if (a > 1.0)
                a = 1.0;
            return v0 + (v1 - v0) * a;
        }
    }
    return curve->values64 ? curve->values64[curve->count - 1]
                           : (double)curve->values32[curve->count - 1];
}

/// @brief Insert @p value into a sorted, dynamically-grown int64 time array, deduplicating.
/// @details Finds the insertion point with a linear scan, skips insertion if @p value already
///          exists (returns 1 without modification), otherwise shifts elements right and inserts.
///          The array is grown geometrically (starting at 16, doubling) via realloc.
/// @return 1 on success (inserted or already present); 0 on allocation failure.
static int fbx_anim_insert_time(int64_t **times, int32_t *count, int32_t *capacity, int64_t value) {
    int32_t pos = 0;
    while (pos < *count && (*times)[pos] < value)
        pos++;
    if (pos < *count && (*times)[pos] == value)
        return 1;
    if (*count >= *capacity) {
        int32_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
        int64_t *grown = (int64_t *)realloc(*times, (size_t)new_capacity * sizeof(*grown));
        if (!grown)
            return 0;
        *times = grown;
        *capacity = new_capacity;
    }
    memmove(&(*times)[pos + 1], &(*times)[pos], (size_t)(*count - pos) * sizeof(**times));
    (*times)[pos] = value;
    (*count)++;
    return 1;
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
                                    const int64_t *layer_ids,
                                    int32_t layer_count,
                                    fbx_anim_bone_builder_t *builders) {
    for (int32_t li = 0; li < layer_count; li++) {
        fbx_node_t *layer_node = fbx_find_object_by_id(objects, layer_ids[li]);
        if (!layer_node || strcmp(layer_node->name, "AnimationLayer") != 0)
            continue;

        int64_t curve_node_ids[256];
        int32_t cn_count = fbx_find_children(ct, layer_ids[li], curve_node_ids, NULL, 256);

        for (int32_t ci = 0; ci < cn_count; ci++) {
            fbx_node_t *cn_node = fbx_find_object_by_id(objects, curve_node_ids[ci]);
            if (!cn_node || strcmp(cn_node->name, "AnimationCurveNode") != 0)
                continue;

            /* Find which Model (bone) this curve node connects to and what property */
            /* The CurveNode→Model connection has prop "Lcl Translation"/"Lcl Rotation"/"Lcl
             * Scaling" */
            int32_t trs_type = -1; /* 0=T, 1=R, 2=S */
            int64_t model_id = 0;
            for (int32_t ci2 = 0; ci2 < ct->count; ci2++) {
                if (ct->entries[ci2].child_id == curve_node_ids[ci]) {
                    int64_t pid = ct->entries[ci2].parent_id;
                    fbx_node_t *pnode = fbx_find_object_by_id(objects, pid);
                    if (pnode && strcmp(pnode->name, "Model") == 0) {
                        model_id = pid;
                        const char *cprop = ct->entries[ci2].prop;
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

            int64_t bone_idx = fbx_find_bone_index_for_model(objects, skeleton, model_id);
            if (bone_idx < 0)
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
            int64_t curve_ids[8];
            const char *curve_props[8];
            int32_t curve_count =
                fbx_find_children(ct, curve_node_ids[ci], curve_ids, curve_props, 8);

            for (int32_t ki = 0; ki < curve_count; ki++) {
                fbx_node_t *curve = fbx_find_object_by_id(objects, curve_ids[ki]);
                if (!curve || strcmp(curve->name, "AnimationCurve") != 0)
                    continue;

                int comp = -1;
                if (strcmp(curve_props[ki], "d|X") == 0)
                    comp = 0;
                else if (strcmp(curve_props[ki], "d|Y") == 0)
                    comp = 1;
                else if (strcmp(curve_props[ki], "d|Z") == 0)
                    comp = 2;
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

                {
                    fbx_anim_curve_view_t *view = &builders[bone_idx].curves[trs_type][comp];
                    view->times = times;
                    view->values64 = dvals;
                    view->values32 = fvals;
                    view->count = tc < vc ? tc : vc;
                }
            }
        }
    }
}

/// @brief Largest keyframe time (seconds) across all of @p builders' curves.
static double fbx_anim_compute_max_time(const fbx_anim_bone_builder_t *builders, int64_t bone_count) {
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
                    if (t > max_time)
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
    if (!builders[bone_idx].initialized)
        return 0;
    for (int trs = 0; trs < 3; trs++) {
        for (int comp = 0; comp < 3; comp++) {
            const fbx_anim_curve_view_t *curve = &builders[bone_idx].curves[trs][comp];
            if (!fbx_anim_curve_has_data(curve))
                continue;
            for (uint32_t k = 0; k < curve->count; k++)
                fbx_anim_insert_time(&times, &time_count, &time_capacity, curve->times[k]);
        }
    }
    for (int32_t ti = 0; ti < time_count; ti++) {
        int64_t fbx_time = times[ti];
        double t = (double)fbx_time / (double)FBX_TIME_SECOND;
        double tv[3];
        double rv[3];
        double sv[3];
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
        void *pos = rt_vec3_new(tv[0], tv[1], tv[2]);
        void *rot = rt_quat_new(qx, qy, qz, qw);
        void *scl = rt_vec3_new(sv[0], sv[1], sv[2]);
        rt_animation3d_add_keyframe(anim, bone_idx, t, pos, rot, scl);
        fbx_release_ref(&pos);
        fbx_release_ref(&rot);
        fbx_release_ref(&scl);
        emitted_any = 1;
    }
    free(times);
    return emitted_any;
}

static void fbx_extract_animations(fbx_node_t *root,
                                   const fbx_conn_table_t *ct,
                                   void *skeleton,
                                   int z_up,
                                   void ***out_anims,
                                   int32_t *out_count) {
    *out_anims = NULL;
    *out_count = 0;

    fbx_node_t *objects = fbx_find_child(root, "Objects");
    if (!objects)
        return;

    int64_t bone_count = skeleton ? rt_skeleton3d_get_bone_count(skeleton) : 0;
    if (bone_count <= 0)
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

        /* Find AnimationLayer children of this stack */
        int64_t layer_ids[16];
        int32_t layer_count = fbx_find_children(ct, stack_id, layer_ids, NULL, 16);
        if (layer_count == 0)
            continue;

        builders = (fbx_anim_bone_builder_t *)calloc((size_t)bone_count, sizeof(*builders));
        if (!builders)
            continue;

        fbx_anim_collect_curves(objects, ct, skeleton, layer_ids, layer_count, builders);
        max_time = fbx_anim_compute_max_time(builders, bone_count);

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

        int32_t new_count = *out_count + 1;
        void **na = (void **)realloc(*out_anims, (size_t)new_count * sizeof(void *));
        if (!na) {
            fbx_release_ref(&anim);
            continue;
        }
        *out_anims = na;
        (*out_anims)[*out_count] = anim;
        *out_count = new_count;
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
    if (fbx->meshes) {
        for (int32_t i = 0; i < fbx->mesh_count; i++)
            fbx_release_ref(&fbx->meshes[i]);
    }
    free(fbx->meshes);
    fbx->meshes = NULL;
    fbx->mesh_count = 0;
    fbx_release_ref(&fbx->skeleton);
    if (fbx->animations) {
        for (int32_t i = 0; i < fbx->animation_count; i++)
            fbx_release_ref(&fbx->animations[i]);
    }
    free(fbx->animations);
    fbx->animations = NULL;
    fbx->animation_count = 0;
    if (fbx->materials) {
        for (int32_t i = 0; i < fbx->material_count; i++)
            fbx_release_ref(&fbx->materials[i]);
    }
    free(fbx->materials);
    fbx->materials = NULL;
    fbx->material_count = 0;
    if (fbx->morph_targets) {
        for (int32_t i = 0; i < fbx->morph_count; i++)
            fbx_release_ref(&fbx->morph_targets[i]);
    }
    free(fbx->morph_targets);
    fbx->morph_targets = NULL;
    fbx->morph_count = 0;
    fbx_release_ref(&fbx->scene_root);
}

/// @brief Collect Geometry/Material objects from the FBX `Objects` node onto the asset,
///   recording id->mesh/material bindings and per-mesh vertex remaps. Extracted phase of
///   rt_fbx_load; the binding/remap arrays grow in place via the in/out pointer params.
/// @brief Link FBX Texture nodes to their owning materials via the connection table.
///   Extracted phase of rt_fbx_load; reads the connection table + material bindings and
///   assigns decoded pixels to the matched material texture slots.
static void fbx_load_link_textures(rt_fbx_asset *asset,
                                   fbx_node_t *objects,
                                   const char *cpath,
                                   fbx_conn_table_t ct,
                                   fbx_material_binding_t *material_bindings,
                                   int32_t material_binding_count) {
    if (objects && asset->material_count > 0) {
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
    if (objects && asset->mesh_count > 0) {
        // Allocate parallel morph_targets array (one per mesh, NULL if no morph)
        asset->morph_targets = (void **)calloc((size_t)asset->mesh_count, sizeof(void *));
        asset->morph_count = asset->mesh_count;

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
            if (mesh_idx < 0 || mesh_idx >= asset->mesh_count)
                continue;

            // Create morph target if not yet created for this mesh
            rt_mesh3d *mesh = (rt_mesh3d *)asset->meshes[mesh_idx];
            const fbx_mesh_remap_t *shape_remap =
                fbx_find_mesh_remap(mesh_remaps, mesh_remap_count, mesh_geo_id);
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

            if (idx_prop->type == 'i' && idx_prop->v.array.count > 0) {
                delta_count = idx_prop->v.array.count;
                indices_ptr = (const int32_t *)idx_prop->v.array.data;
            }
            if (vtx_prop->type == 'd' && vtx_prop->v.array.count >= (uint32_t)(delta_count * 3)) {
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

static void fbx_load_collect_geometry(rt_fbx_asset *asset,
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
                memset(&remap, 0, sizeof(remap));
                void *mesh = fbx_extract_geometry(obj, z_up, &remap);
                if (mesh) {
                    int32_t nc = asset->mesh_count + 1;
                    void **nm = (void **)realloc(asset->meshes, (size_t)nc * sizeof(void *));
                    if (nm) {
                        asset->meshes = nm;
                        asset->meshes[asset->mesh_count] = mesh;
                        asset->mesh_count = nc;
                        {
                            void *nb = realloc(mesh_bindings,
                                               (size_t)asset->mesh_count * sizeof(*mesh_bindings));
                            if (nb) {
                                mesh_bindings = (fbx_mesh_binding_t *)nb;
                                mesh_bindings[mesh_binding_count].id = fbx_prop_i64(obj, 0);
                                mesh_bindings[mesh_binding_count].mesh = mesh;
                                mesh_binding_count++;
                            }
                        }
                        {
                            fbx_mesh_remap_t *nr = (fbx_mesh_remap_t *)realloc(
                                mesh_remaps, (size_t)(mesh_remap_count + 1) * sizeof(*mesh_remaps));
                            if (nr) {
                                mesh_remaps = nr;
                                mesh_remaps[mesh_remap_count++] = remap;
                                memset(&remap, 0, sizeof(remap));
                            }
                        }
                    } else {
                        fbx_release_ref(&mesh);
                    }
                }
                fbx_mesh_remap_free(&remap);
            } else if (strcmp(obj->name, "Material") == 0) {
                void *mat = fbx_extract_material(obj);
                if (mat) {
                    int32_t nc = asset->material_count + 1;
                    void **nm = (void **)realloc(asset->materials, (size_t)nc * sizeof(void *));
                    if (nm) {
                        asset->materials = nm;
                        asset->materials[asset->material_count] = mat;
                        asset->material_count = nc;
                        {
                            void *nb =
                                realloc(material_bindings,
                                        (size_t)asset->material_count * sizeof(*material_bindings));
                            if (nb) {
                                material_bindings = (fbx_material_binding_t *)nb;
                                material_bindings[material_binding_count].id = fbx_prop_i64(obj, 0);
                                material_bindings[material_binding_count].material = mat;
                                material_binding_count++;
                            }
                        }
                    } else {
                        fbx_release_ref(&mat);
                    }
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

    uint8_t *data = (uint8_t *)malloc((size_t)fsize);
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
    fclose(f);

    /* Verify magic */
    static const char magic[] = "Kaydara FBX Binary  ";
    if (memcmp(data, magic, 20) != 0) {
        free(data);
        rt_trap("FBX.Load: not a binary FBX file");
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
        free(mesh_bindings);
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
    asset->skeleton = NULL;
    asset->animations = NULL;
    asset->animation_count = 0;
    asset->materials = NULL;
    asset->material_count = 0;
    asset->morph_targets = NULL;
    asset->morph_count = 0;
    asset->scene_root = NULL;
    rt_obj_set_finalizer(asset, rt_fbx_asset_finalize);

    /* Extract geometry */
    fbx_node_t *objects = fbx_find_child(&root, "Objects");
    fbx_load_collect_geometry(asset,
                              objects,
                              z_up,
                              &mesh_bindings,
                              &mesh_binding_count,
                              &mesh_remaps,
                              &mesh_remap_count,
                              &material_bindings,
                              &material_binding_count);

    /* Extract textures and link to materials */
    fbx_load_link_textures(asset, objects, cpath, ct, material_bindings, material_binding_count);

    /* Extract morph targets (BlendShape deformers) */
    fbx_load_extract_morphs(asset, objects, ct, z_up, mesh_remaps, mesh_remap_count);
    /* Extract skeleton */
    asset->skeleton = fbx_extract_skeleton(&root, &ct, z_up);
    fbx_apply_skinning(objects,
                       &ct,
                       asset->skeleton,
                       mesh_bindings,
                       mesh_binding_count,
                       mesh_remaps,
                       mesh_remap_count);

    /* Extract animations */
    fbx_extract_animations(
        &root, &ct, asset->skeleton, z_up, &asset->animations, &asset->animation_count);

    asset->scene_root = fbx_build_scene_root(&root,
                                             objects,
                                             &ct,
                                             mesh_bindings,
                                             mesh_binding_count,
                                             material_bindings,
                                             material_binding_count,
                                             z_up);

    /* Cleanup parser data */
    free(ct.entries);
    free(mesh_bindings);
    fbx_mesh_remaps_free(mesh_remaps, mesh_remap_count);
    free(material_bindings);
    fbx_free_node(&root);

    return asset;
}

/*==========================================================================
 * FBX asset accessors
 *=========================================================================*/

/// @brief Get the number of meshes extracted from the FBX file.
int64_t rt_fbx_mesh_count(void *obj) {
    return obj ? ((rt_fbx_asset *)obj)->mesh_count : 0;
}

/// @brief Get a mesh by index from the loaded FBX asset.
void *rt_fbx_get_mesh(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_fbx_asset *a = (rt_fbx_asset *)obj;
    if (index < 0 || index >= a->mesh_count)
        return NULL;
    return a->meshes[index];
}

/// @brief Get the skeleton extracted from the FBX file (NULL if no skeleton).
void *rt_fbx_get_skeleton(void *obj) {
    return obj ? ((rt_fbx_asset *)obj)->skeleton : NULL;
}

/// @brief Get the `SceneNode3D` root of the imported scene graph — the tree of models
/// the FBX author created, with their world transforms and mesh/material bindings.
/// Returned reference is borrowed; the asset owns the lifetime. Distinct from the flat
/// `mesh_count` / `material_count` lists which expose every shared resource the scene
/// uses, regardless of whether it's actually attached to a node.
void *rt_fbx_get_scene_root(void *obj) {
    return obj ? ((rt_fbx_asset *)obj)->scene_root : NULL;
}

/// @brief Get the number of animation clips in the FBX file.
int64_t rt_fbx_animation_count(void *obj) {
    return obj ? ((rt_fbx_asset *)obj)->animation_count : 0;
}

/// @brief Get an animation clip by index from the loaded FBX asset.
void *rt_fbx_get_animation(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_fbx_asset *a = (rt_fbx_asset *)obj;
    if (index < 0 || index >= a->animation_count)
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
    return obj ? ((rt_fbx_asset *)obj)->material_count : 0;
}

/// @brief Get a material by index from the loaded FBX asset.
void *rt_fbx_get_material(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_fbx_asset *a = (rt_fbx_asset *)obj;
    if (index < 0 || index >= a->material_count)
        return NULL;
    return a->materials[index];
}

/// @brief Get the morph target data for a mesh by its index in the FBX asset.
void *rt_fbx_get_morph_target(void *obj, int64_t mesh_index) {
    if (!obj)
        return NULL;
    rt_fbx_asset *a = (rt_fbx_asset *)obj;
    if (!a->morph_targets || mesh_index < 0 || mesh_index >= a->morph_count)
        return NULL;
    return a->morph_targets[mesh_index];
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
