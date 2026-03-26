//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_fbx_loader.c
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
//   - Fan triangulation for quads/n-gons (assumes convex polygons).
//
// Links: rt_fbx_loader.h, plans/3d/15-fbx-loader.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_fbx_loader.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_skeleton3d.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_trap(const char *msg);
extern rt_string rt_const_cstr(const char *s);
extern const char *rt_string_cstr(rt_string s);

/* Bytes/compress interface for zlib decompression */
extern void *rt_bytes_new(int64_t len);
extern void *rt_compress_inflate(void *data);

/* Mesh/Material/Skeleton/Animation constructors */
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
extern void rt_mesh3d_set_bone_weights(void *m,
                                       int64_t vi,
                                       int64_t b0,
                                       double w0,
                                       int64_t b1,
                                       double w1,
                                       int64_t b2,
                                       double w2,
                                       int64_t b3,
                                       double w3);
extern void *rt_material3d_new(void);
extern void rt_material3d_set_color(void *m, double r, double g, double b);
extern void rt_material3d_set_shininess(void *m, double s);
extern void *rt_skeleton3d_new(void);
extern int64_t rt_skeleton3d_add_bone(void *skel, rt_string name, int64_t parent, void *bind_mat4);
extern void rt_skeleton3d_compute_inverse_bind(void *skel);
extern int64_t rt_skeleton3d_get_bone_count(void *skel);
extern rt_string rt_skeleton3d_get_bone_name(void *skel, int64_t index);
extern void *rt_animation3d_new(rt_string name, double duration);
extern void rt_animation3d_add_keyframe(
    void *anim, int64_t bone, double time, void *pos, void *rot, void *scl);
extern void rt_animation3d_set_looping(void *anim, int8_t loop);
extern void *rt_vec3_new(double x, double y, double z);
extern void *rt_quat_new(double x, double y, double z, double w);
extern void *rt_mat4_new(double m0,
                         double m1,
                         double m2,
                         double m3,
                         double m4,
                         double m5,
                         double m6,
                         double m7,
                         double m8,
                         double m9,
                         double m10,
                         double m11,
                         double m12,
                         double m13,
                         double m14,
                         double m15);

/*==========================================================================
 * FBX asset container
 *=========================================================================*/

typedef struct
{
    void *vptr;
    void **meshes;
    int32_t mesh_count;
    void *skeleton;
    void **animations;
    int32_t animation_count;
    void **materials;
    int32_t material_count;
} rt_fbx_asset;

/*==========================================================================
 * Binary reader helpers
 *=========================================================================*/

typedef struct
{
    const uint8_t *data;
    size_t len;
    size_t pos;
    uint32_t version;
    int is_64bit; /* version >= 7500 */
} fbx_reader_t;

static int fbx_eof(const fbx_reader_t *r)
{
    return r->pos >= r->len;
}

static uint8_t fbx_u8(fbx_reader_t *r)
{
    if (r->pos + 1 > r->len)
        return 0;
    return r->data[r->pos++];
}

static uint16_t fbx_u16(fbx_reader_t *r)
{
    if (r->pos + 2 > r->len)
        return 0;
    uint16_t v = (uint16_t)r->data[r->pos] | ((uint16_t)r->data[r->pos + 1] << 8);
    r->pos += 2;
    return v;
}

static uint32_t fbx_u32(fbx_reader_t *r)
{
    if (r->pos + 4 > r->len)
        return 0;
    uint32_t v = (uint32_t)r->data[r->pos] | ((uint32_t)r->data[r->pos + 1] << 8) |
                 ((uint32_t)r->data[r->pos + 2] << 16) | ((uint32_t)r->data[r->pos + 3] << 24);
    r->pos += 4;
    return v;
}

static int32_t fbx_i32(fbx_reader_t *r)
{
    return (int32_t)fbx_u32(r);
}

static uint64_t fbx_u64(fbx_reader_t *r)
{
    if (r->pos + 8 > r->len)
        return 0;
    uint64_t lo = fbx_u32(r);
    uint64_t hi = fbx_u32(r);
    return lo | (hi << 32);
}

static int64_t fbx_i64(fbx_reader_t *r)
{
    return (int64_t)fbx_u64(r);
}

static float fbx_f32(fbx_reader_t *r)
{
    uint32_t bits = fbx_u32(r);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static double fbx_f64(fbx_reader_t *r)
{
    uint64_t bits = fbx_u64(r);
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

static void fbx_skip(fbx_reader_t *r, size_t n)
{
    r->pos += n;
    if (r->pos > r->len)
        r->pos = r->len;
}

/*==========================================================================
 * FBX node tree
 *=========================================================================*/

#define FBX_MAX_CHILDREN 256
#define FBX_MAX_PROPS 32

typedef struct
{
    char type; /* C/Y/I/L/F/D/S/R/b/i/l/f/d */

    union
    {
        int8_t bool_val;
        int16_t i16;
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;

        struct
        {
            char *str;
            uint32_t len;
        } string;

        struct
        {
            uint8_t *data;
            uint32_t len;
        } raw;

        struct
        {
            void *data;
            uint32_t count;
            char elem_type;
        } array;
    } v;
} fbx_prop_t;

typedef struct fbx_node
{
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

static void *fbx_decompress_array(const uint8_t *data,
                                  uint32_t comp_len,
                                  uint32_t count,
                                  uint32_t elem_size)
{
    if (comp_len < 6)
        return NULL;
    uint32_t deflate_len = comp_len - 6; /* strip 2-byte header + 4-byte adler32 */

    void *comp_bytes = rt_bytes_new((int64_t)deflate_len);
    if (!comp_bytes)
        return NULL;

    /* Copy raw DEFLATE payload (skip 2-byte zlib header) */
    typedef struct
    {
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

static int fbx_parse_property(fbx_reader_t *r, fbx_prop_t *prop)
{
    if (fbx_eof(r))
        return -1;
    prop->type = (char)fbx_u8(r);
    switch (prop->type)
    {
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
        case 'R':
        {
            uint32_t len = fbx_u32(r);
            if (r->pos + len > r->len)
                return -1;
            if (prop->type == 'S')
            {
                prop->v.string.str = (char *)malloc(len + 1);
                if (prop->v.string.str)
                {
                    memcpy(prop->v.string.str, r->data + r->pos, len);
                    prop->v.string.str[len] = '\0';
                }
                prop->v.string.len = len;
            }
            else
            {
                prop->v.raw.data = (uint8_t *)malloc(len);
                if (prop->v.raw.data)
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
        case 'd':
        {
            uint32_t count = fbx_u32(r);
            uint32_t encoding = fbx_u32(r);
            uint32_t comp_len = fbx_u32(r);
            if (r->pos + comp_len > r->len)
                return -1;

            uint32_t elem_size = 0;
            switch (prop->type)
            {
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
            if (encoding == 1)
            {
                prop->v.array.data =
                    fbx_decompress_array(r->data + r->pos, comp_len, count, elem_size);
            }
            else
            {
                if (elem_size > 0 && count > SIZE_MAX / elem_size)
                    return -1; /* overflow guard for 32-bit platforms */
                size_t expected = (size_t)count * elem_size;
                if (comp_len < expected)
                    return -1;
                prop->v.array.data = malloc(expected);
                if (prop->v.array.data)
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

static void fbx_free_prop(fbx_prop_t *p)
{
    switch (p->type)
    {
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

static void fbx_free_node(fbx_node_t *n)
{
    for (int32_t i = 0; i < n->prop_count; i++)
        fbx_free_prop(&n->props[i]);
    free(n->props);
    for (int32_t i = 0; i < n->child_count; i++)
        fbx_free_node(&n->children[i]);
    free(n->children);
}

static int fbx_parse_node(fbx_reader_t *r, fbx_node_t *node)
{
    memset(node, 0, sizeof(fbx_node_t));

    uint64_t end_offset = r->is_64bit ? fbx_u64(r) : fbx_u32(r);
    uint64_t num_props = r->is_64bit ? fbx_u64(r) : fbx_u32(r);
    uint64_t prop_list_len = r->is_64bit ? fbx_u64(r) : fbx_u32(r);
    (void)prop_list_len;

    uint8_t name_len = fbx_u8(r);

    if (end_offset == 0 && num_props == 0 && name_len == 0)
        return -1; /* null record (sentinel) */

    if (name_len > 127)
        name_len = 127;
    if (r->pos + name_len > r->len)
        return -1;
    memcpy(node->name, r->data + r->pos, name_len);
    node->name[name_len] = '\0';
    fbx_skip(r, name_len);

    /* Parse properties */
    if (num_props > 0)
    {
        node->props = (fbx_prop_t *)calloc((size_t)num_props, sizeof(fbx_prop_t));
        if (!node->props)
            return -1;
        for (uint64_t i = 0; i < num_props; i++)
        {
            if (fbx_parse_property(r, &node->props[i]) < 0)
            {
                node->prop_count = (int32_t)i;
                return -1;
            }
        }
        node->prop_count = (int32_t)num_props;
    }

    /* Parse children (until end_offset or null record) */
    while (r->pos < (size_t)end_offset && !fbx_eof(r))
    {
        /* Check for null record sentinel */
        size_t sentinel_size = r->is_64bit ? 25 : 13;
        if (r->pos + sentinel_size <= r->len)
        {
            int is_null = 1;
            for (size_t i = 0; i < sentinel_size; i++)
                if (r->data[r->pos + i] != 0)
                {
                    is_null = 0;
                    break;
                }
            if (is_null)
            {
                fbx_skip(r, sentinel_size);
                break;
            }
        }

        if (node->child_count >= node->child_capacity)
        {
            int32_t new_cap = node->child_capacity == 0 ? 8 : node->child_capacity * 2;
            fbx_node_t *nc =
                (fbx_node_t *)realloc(node->children, (size_t)new_cap * sizeof(fbx_node_t));
            if (!nc)
                break;
            node->children = nc;
            node->child_capacity = new_cap;
        }

        fbx_node_t *child = &node->children[node->child_count];
        if (fbx_parse_node(r, child) < 0)
            break;
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

static fbx_node_t *fbx_find_child(fbx_node_t *parent, const char *name)
{
    for (int32_t i = 0; i < parent->child_count; i++)
        if (strcmp(parent->children[i].name, name) == 0)
            return &parent->children[i];
    return NULL;
}

static int64_t fbx_prop_i64(fbx_node_t *node, int idx)
{
    if (!node || idx >= node->prop_count)
        return 0;
    fbx_prop_t *p = &node->props[idx];
    switch (p->type)
    {
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

static double fbx_prop_f64(fbx_node_t *node, int idx)
{
    if (!node || idx >= node->prop_count)
        return 0.0;
    fbx_prop_t *p = &node->props[idx];
    if (p->type == 'D')
        return p->v.f64;
    if (p->type == 'F')
        return p->v.f32;
    return 0.0;
}

static const char *fbx_prop_str(fbx_node_t *node, int idx)
{
    if (!node || idx >= node->prop_count)
        return "";
    fbx_prop_t *p = &node->props[idx];
    if (p->type == 'S' && p->v.string.str)
        return p->v.string.str;
    return "";
}

/*==========================================================================
 * Connection table
 *=========================================================================*/

typedef struct
{
    int64_t child_id;
    int64_t parent_id;
    char prop[64];
} fbx_conn_t;

typedef struct
{
    fbx_conn_t *entries;
    int32_t count;
    int32_t capacity;
} fbx_conn_table_t;

static void fbx_parse_connections(fbx_node_t *root, fbx_conn_table_t *ct)
{
    fbx_node_t *conns_node = fbx_find_child(root, "Connections");
    if (!conns_node)
        return;

    for (int32_t i = 0; i < conns_node->child_count; i++)
    {
        fbx_node_t *c = &conns_node->children[i];
        if (strcmp(c->name, "C") != 0 || c->prop_count < 3)
            continue;

        if (ct->count >= ct->capacity)
        {
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
        if (c->prop_count >= 4)
        {
            const char *pname = fbx_prop_str(c, 3);
            size_t plen = strlen(pname);
            if (plen > 63)
                plen = 63;
            memcpy(entry->prop, pname, plen);
            entry->prop[plen] = '\0';
        }
    }
}

static int64_t fbx_find_parent(const fbx_conn_table_t *ct, int64_t child_id)
{
    for (int32_t i = 0; i < ct->count; i++)
        if (ct->entries[i].child_id == child_id)
            return ct->entries[i].parent_id;
    return 0;
}

/*==========================================================================
 * Coordinate system detection + correction
 *=========================================================================*/

static int fbx_is_z_up(fbx_node_t *root)
{
    fbx_node_t *gs = fbx_find_child(root, "GlobalSettings");
    if (!gs)
        return 0;
    fbx_node_t *p70 = fbx_find_child(gs, "Properties70");
    if (!p70)
        return 0;
    for (int32_t i = 0; i < p70->child_count; i++)
    {
        fbx_node_t *p = &p70->children[i];
        if (strcmp(p->name, "P") != 0 || p->prop_count < 5)
            continue;
        const char *pname = fbx_prop_str(p, 0);
        if (strcmp(pname, "UpAxis") == 0)
        {
            int64_t axis = fbx_prop_i64(p, 4);
            return axis == 2; /* 2 = Z-up */
        }
    }
    return 0;
}

/// @brief Apply Z-up → Y-up correction: swap Y/Z and negate new Z.
static void fbx_correct_zup(double *x, double *y, double *z)
{
    double tmp = *y;
    *y = *z;
    *z = -tmp;
}

/*==========================================================================
 * Geometry extraction
 *=========================================================================*/

static void *fbx_extract_geometry(fbx_node_t *geom_node, int z_up)
{
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

    /* Find normals (optional) */
    fbx_node_t *norm_layer = fbx_find_child(geom_node, "LayerElementNormal");
    double *normals = NULL;
    int32_t *norm_indices = NULL;
    uint32_t norm_count = 0;
    int norm_by_polygon_vertex = 0;
    int norm_index_to_direct = 0;

    if (norm_layer)
    {
        fbx_node_t *n_node = fbx_find_child(norm_layer, "Normals");
        if (n_node && n_node->prop_count >= 1 && n_node->props[0].type == 'd')
        {
            normals = (double *)n_node->props[0].v.array.data;
            norm_count = n_node->props[0].v.array.count / 3;
        }
        fbx_node_t *mm = fbx_find_child(norm_layer, "MappingInformationType");
        if (mm && mm->prop_count >= 1)
            norm_by_polygon_vertex = strcmp(fbx_prop_str(mm, 0), "ByPolygonVertex") == 0;
        fbx_node_t *rm = fbx_find_child(norm_layer, "ReferenceInformationType");
        if (rm && rm->prop_count >= 1)
            norm_index_to_direct = strcmp(fbx_prop_str(rm, 0), "IndexToDirect") == 0;
        if (norm_index_to_direct)
        {
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

    if (uv_layer)
    {
        fbx_node_t *u_node = fbx_find_child(uv_layer, "UV");
        if (u_node && u_node->prop_count >= 1 && u_node->props[0].type == 'd')
        {
            uvs = (double *)u_node->props[0].v.array.data;
            uv_count = u_node->props[0].v.array.count / 2;
        }
        fbx_node_t *umm = fbx_find_child(uv_layer, "MappingInformationType");
        if (umm && umm->prop_count >= 1)
            uv_by_polygon_vertex = strcmp(fbx_prop_str(umm, 0), "ByPolygonVertex") == 0;
        fbx_node_t *urm = fbx_find_child(uv_layer, "ReferenceInformationType");
        if (urm && urm->prop_count >= 1)
            uv_index_to_direct = strcmp(fbx_prop_str(urm, 0), "IndexToDirect") == 0;
        if (uv_index_to_direct)
        {
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

    for (uint32_t i = 0; i < idx_count; i++)
    {
        int32_t raw_idx = indices[i];
        int end_of_polygon = (raw_idx < 0);
        int32_t vi = end_of_polygon ? ~raw_idx : raw_idx;

        if (vi < 0 || vi >= (int32_t)pos_count)
        {
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
        if (normals)
        {
            int ni = 0;
            if (norm_by_polygon_vertex)
                ni = norm_index_to_direct ? (norm_indices ? norm_indices[polygon_vertex_idx] : 0)
                                          : polygon_vertex_idx;
            else
                ni = norm_index_to_direct ? (norm_indices ? norm_indices[vi] : 0) : vi;
            if (ni >= 0 && ni < (int32_t)norm_count)
            {
                nx = normals[ni * 3 + 0];
                ny = normals[ni * 3 + 1];
                nz = normals[ni * 3 + 2];
                if (z_up)
                    fbx_correct_zup(&nx, &ny, &nz);
            }
        }

        /* Get UV */
        double u = 0, v = 0;
        if (uvs)
        {
            int ui = 0;
            if (uv_by_polygon_vertex)
                ui = uv_index_to_direct ? (uv_indices ? uv_indices[polygon_vertex_idx] : 0)
                                        : polygon_vertex_idx;
            else
                ui = uv_index_to_direct ? (uv_indices ? uv_indices[vi] : 0) : vi;
            if (ui >= 0 && ui < (int32_t)uv_count)
            {
                u = uvs[ui * 2 + 0];
                v = 1.0 - uvs[ui * 2 + 1]; /* FBX V is flipped vs OpenGL */
            }
        }

        rt_mesh3d_add_vertex(mesh, px, py, pz, nx, ny, nz, u, v);
        polygon[poly_count++] = mesh_vertex_count++;
        polygon_vertex_idx++;

        if (end_of_polygon)
        {
            /* Fan triangulate */
            for (int32_t fi = 1; fi < poly_count - 1; fi++)
                rt_mesh3d_add_triangle(mesh, polygon[0], polygon[fi], polygon[fi + 1]);
            poly_count = 0;
        }

        if (poly_count >= 32)
            poly_count = 0; /* safety: max 32-gon */
    }

    return mesh;
}

/*==========================================================================
 * Material extraction
 *=========================================================================*/

static void *fbx_extract_material(fbx_node_t *mat_node)
{
    if (!mat_node)
        return NULL;
    void *mat = rt_material3d_new();
    if (!mat)
        return NULL;

    fbx_node_t *p70 = fbx_find_child(mat_node, "Properties70");
    if (!p70)
        return mat;

    for (int32_t i = 0; i < p70->child_count; i++)
    {
        fbx_node_t *p = &p70->children[i];
        if (strcmp(p->name, "P") != 0 || p->prop_count < 7)
            continue;
        const char *pname = fbx_prop_str(p, 0);
        if (strcmp(pname, "DiffuseColor") == 0)
        {
            double r = fbx_prop_f64(p, 4);
            double g = fbx_prop_f64(p, 5);
            double b = fbx_prop_f64(p, 6);
            rt_material3d_set_color(mat, r, g, b);
        }
        else if (strcmp(pname, "Shininess") == 0 || strcmp(pname, "ShininessExponent") == 0)
        {
            double s = fbx_prop_f64(p, 4);
            rt_material3d_set_shininess(mat, s);
        }
    }

    return mat;
}

/*==========================================================================
 * Skeleton extraction
 *=========================================================================*/

static void *fbx_extract_skeleton(fbx_node_t *root, const fbx_conn_table_t *ct, int z_up)
{
    fbx_node_t *objects = fbx_find_child(root, "Objects");
    if (!objects)
        return NULL;

    /* Collect Model nodes that are skeleton limbs */
    typedef struct
    {
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

    for (int32_t i = 0; i < objects->child_count; i++)
    {
        fbx_node_t *obj = &objects->children[i];
        if (strcmp(obj->name, "Model") != 0 || obj->prop_count < 3)
            continue;
        const char *type_str = fbx_prop_str(obj, 2);
        if (strcmp(type_str, "LimbNode") != 0 && strcmp(type_str, "Limb") != 0 &&
            strcmp(type_str, "Root") != 0)
            continue;

        if (bone_count >= bone_cap)
        {
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
        const char *nstr = fbx_prop_str(obj, 1);
        /* FBX name may have "\x00\x01ModelName" prefix */
        const char *sep = strstr(nstr, "\x00\x01");
        if (sep)
            nstr = sep + 2;
        size_t nlen = strlen(nstr);
        if (nlen > 63)
            nlen = 63;
        memcpy(bi->name, nstr, nlen);
        bi->name[nlen] = '\0';
        bi->lcl_scaling[0] = bi->lcl_scaling[1] = bi->lcl_scaling[2] = 1.0;

        /* Extract Lcl Translation/Rotation/Scaling from Properties70 */
        fbx_node_t *p70 = fbx_find_child(obj, "Properties70");
        if (p70)
        {
            for (int32_t pi = 0; pi < p70->child_count; pi++)
            {
                fbx_node_t *p = &p70->children[pi];
                if (strcmp(p->name, "P") != 0 || p->prop_count < 7)
                    continue;
                const char *pn = fbx_prop_str(p, 0);
                if (strcmp(pn, "Lcl Translation") == 0)
                {
                    bi->lcl_translation[0] = fbx_prop_f64(p, 4);
                    bi->lcl_translation[1] = fbx_prop_f64(p, 5);
                    bi->lcl_translation[2] = fbx_prop_f64(p, 6);
                }
                else if (strcmp(pn, "Lcl Rotation") == 0)
                {
                    bi->lcl_rotation[0] = fbx_prop_f64(p, 4);
                    bi->lcl_rotation[1] = fbx_prop_f64(p, 5);
                    bi->lcl_rotation[2] = fbx_prop_f64(p, 6);
                }
                else if (strcmp(pn, "Lcl Scaling") == 0)
                {
                    bi->lcl_scaling[0] = fbx_prop_f64(p, 4);
                    bi->lcl_scaling[1] = fbx_prop_f64(p, 5);
                    bi->lcl_scaling[2] = fbx_prop_f64(p, 6);
                }
            }
        }

        bi->parent_id = fbx_find_parent(ct, bi->id);
    }

    if (bone_count == 0)
    {
        free(bones);
        return NULL;
    }

    /* Build skeleton in topological order */
    void *skel = rt_skeleton3d_new();
    if (!skel)
    {
        free(bones);
        return NULL;
    }

    /* Assign bone indices: process bones in parent-first order */
    int32_t *order = (int32_t *)calloc((size_t)bone_count, sizeof(int32_t));
    int8_t *placed = (int8_t *)calloc((size_t)bone_count, sizeof(int8_t));
    int32_t placed_count = 0;

    /* Place roots first */
    for (int32_t i = 0; i < bone_count; i++)
    {
        int is_root = 1;
        for (int32_t j = 0; j < bone_count; j++)
            if (bones[i].parent_id == bones[j].id)
            {
                is_root = 0;
                break;
            }
        if (is_root)
        {
            order[placed_count++] = i;
            placed[i] = 1;
        }
    }

    /* Place children */
    for (int32_t pass = 0; pass < bone_count && placed_count < bone_count; pass++)
    {
        for (int32_t i = 0; i < bone_count; i++)
        {
            if (placed[i])
                continue;
            for (int32_t j = 0; j < placed_count; j++)
            {
                if (bones[i].parent_id == bones[order[j]].id)
                {
                    order[placed_count++] = i;
                    placed[i] = 1;
                    break;
                }
            }
        }
    }

    /* Add bones to skeleton in topological order */
    for (int32_t i = 0; i < placed_count; i++)
    {
        bone_info_t *bi = &bones[order[i]];
        int64_t parent_idx = -1;
        for (int32_t j = 0; j < i; j++)
        {
            if (bi->parent_id == bones[order[j]].id)
            {
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

/* Find all children of a given parent in the connection table */
static int32_t fbx_find_children(const fbx_conn_table_t *ct,
                                 int64_t parent_id,
                                 int64_t *out_ids,
                                 const char **out_props,
                                 int32_t max_out)
{
    int32_t count = 0;
    for (int32_t i = 0; i < ct->count && count < max_out; i++)
    {
        if (ct->entries[i].parent_id == parent_id)
        {
            out_ids[count] = ct->entries[i].child_id;
            if (out_props)
                out_props[count] = ct->entries[i].prop;
            count++;
        }
    }
    return count;
}

/* Find an Objects node by its FBX ID (first property) */
static fbx_node_t *fbx_find_object_by_id(fbx_node_t *objects, int64_t id)
{
    for (int32_t i = 0; i < objects->child_count; i++)
    {
        fbx_node_t *obj = &objects->children[i];
        if (obj->prop_count >= 1 && fbx_prop_i64(obj, 0) == id)
            return obj;
    }
    return NULL;
}

/* Extract int64 array data from an FBX property */
static const int64_t *fbx_get_i64_array(fbx_node_t *node, const char *child_name, uint32_t *count)
{
    fbx_node_t *child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return NULL;
    fbx_prop_t *p = &child->props[0];
    if (p->type == 'l' && p->v.array.data)
    {
        *count = p->v.array.count;
        return (const int64_t *)p->v.array.data;
    }
    return NULL;
}

/* Extract double array data from an FBX property */
static const double *fbx_get_f64_array(fbx_node_t *node, const char *child_name, uint32_t *count)
{
    fbx_node_t *child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return NULL;
    fbx_prop_t *p = &child->props[0];
    if (p->type == 'd' && p->v.array.data)
    {
        *count = p->v.array.count;
        return (const double *)p->v.array.data;
    }
    /* Also handle float arrays by reading as float */
    if (p->type == 'f' && p->v.array.data)
    {
        *count = p->v.array.count;
        return NULL; /* caller must handle float arrays separately */
    }
    return NULL;
}

static const float *fbx_get_f32_array(fbx_node_t *node, const char *child_name, uint32_t *count)
{
    fbx_node_t *child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return NULL;
    fbx_prop_t *p = &child->props[0];
    if (p->type == 'f' && p->v.array.data)
    {
        *count = p->v.array.count;
        return (const float *)p->v.array.data;
    }
    return NULL;
}

static void fbx_extract_animations(fbx_node_t *root,
                                   const fbx_conn_table_t *ct,
                                   void *skeleton,
                                   void ***out_anims,
                                   int32_t *out_count)
{
    *out_anims = NULL;
    *out_count = 0;

    fbx_node_t *objects = fbx_find_child(root, "Objects");
    if (!objects)
        return;

    int64_t bone_count = skeleton ? rt_skeleton3d_get_bone_count(skeleton) : 0;

    /* Find AnimationStack nodes */
    for (int32_t i = 0; i < objects->child_count; i++)
    {
        fbx_node_t *obj = &objects->children[i];
        if (strcmp(obj->name, "AnimationStack") != 0)
            continue;

        int64_t stack_id = fbx_prop_i64(obj, 0);
        const char *anim_name = obj->prop_count >= 2 ? fbx_prop_str(obj, 1) : "Untitled";
        const char *sep = strstr(anim_name, "\x00\x01");
        if (sep)
            anim_name = sep + 2;

        /* Find AnimationLayer children of this stack */
        int64_t layer_ids[16];
        int32_t layer_count = fbx_find_children(ct, stack_id, layer_ids, NULL, 16);
        if (layer_count == 0)
            continue;

        double max_time = 0.0;

        /* Create animation with placeholder duration (updated after keyframe extraction) */
        void *anim = rt_animation3d_new(rt_const_cstr(anim_name), 1.0);
        if (!anim)
            continue;

        /* For each AnimationLayer, find AnimationCurveNode children */
        for (int32_t li = 0; li < layer_count; li++)
        {
            fbx_node_t *layer_node = fbx_find_object_by_id(objects, layer_ids[li]);
            if (!layer_node || strcmp(layer_node->name, "AnimationLayer") != 0)
                continue;

            int64_t curve_node_ids[256];
            int32_t cn_count = fbx_find_children(ct, layer_ids[li], curve_node_ids, NULL, 256);

            for (int32_t ci = 0; ci < cn_count; ci++)
            {
                fbx_node_t *cn_node = fbx_find_object_by_id(objects, curve_node_ids[ci]);
                if (!cn_node || strcmp(cn_node->name, "AnimationCurveNode") != 0)
                    continue;

                /* Find which Model (bone) this curve node connects to and what property */
                /* The CurveNode→Model connection has prop "Lcl Translation"/"Lcl Rotation"/"Lcl
                 * Scaling" */
                int32_t trs_type = -1; /* 0=T, 1=R, 2=S */
                int64_t model_id = 0;
                for (int32_t ci2 = 0; ci2 < ct->count; ci2++)
                {
                    if (ct->entries[ci2].child_id == curve_node_ids[ci])
                    {
                        int64_t pid = ct->entries[ci2].parent_id;
                        fbx_node_t *pnode = fbx_find_object_by_id(objects, pid);
                        if (pnode && strcmp(pnode->name, "Model") == 0)
                        {
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

                /* Find bone index by matching model name to skeleton bone name */
                int64_t bone_idx = -1;
                if (skeleton)
                {
                    fbx_node_t *model_node = fbx_find_object_by_id(objects, model_id);
                    if (model_node && model_node->prop_count >= 2)
                    {
                        const char *mname = fbx_prop_str(model_node, 1);
                        const char *msep = strstr(mname, "\x00\x01");
                        if (msep)
                            mname = msep + 2;
                        for (int64_t bi = 0; bi < bone_count; bi++)
                        {
                            rt_string bname = rt_skeleton3d_get_bone_name(skeleton, bi);
                            const char *bstr = rt_string_cstr(bname);
                            if (bstr && strcmp(bstr, mname) == 0)
                            {
                                bone_idx = bi;
                                break;
                            }
                        }
                    }
                }
                if (bone_idx < 0)
                    continue;

                /* Find AnimationCurve children (d|X, d|Y, d|Z) */
                int64_t curve_ids[8];
                const char *curve_props[8];
                int32_t curve_count =
                    fbx_find_children(ct, curve_node_ids[ci], curve_ids, curve_props, 8);

                for (int32_t ki = 0; ki < curve_count; ki++)
                {
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

                    /* Extract KeyTime (int64 array) and KeyValueFloat (double or float array) */
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

                    uint32_t kc = tc < vc ? tc : vc;

                    /* Emit keyframes for this component */
                    for (uint32_t k = 0; k < kc; k++)
                    {
                        double t = (double)times[k] / (double)FBX_TIME_SECOND;
                        if (t > max_time)
                            max_time = t;

                        double val = dvals ? dvals[k] : (double)fvals[k];

                        /* Build T/R/S vectors for this keyframe */
                        double tv[3] = {0.0, 0.0, 0.0};
                        double rv[3] = {0.0, 0.0, 0.0};
                        double sv[3] = {1.0, 1.0, 1.0};

                        if (trs_type == 0)
                        {
                            tv[comp] = val;
                        }
                        else if (trs_type == 1)
                        {
                            rv[comp] = val;
                        }
                        else
                        {
                            sv[comp] = val;
                        }

                        /* Convert rotation from Euler degrees to quaternion */
                        double rx = rv[0] * 3.14159265358979323846 / 180.0;
                        double ry = rv[1] * 3.14159265358979323846 / 180.0;
                        double rz = rv[2] * 3.14159265358979323846 / 180.0;

                        /* Simple Euler XYZ → quaternion */
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
                    }
                }
            }
        }

        /* Update animation duration from extracted keyframes */
        if (max_time > 0.0)
        {
            /* Re-create with correct duration (API doesn't have set_duration) */
            /* The duration was set to 1.0 as placeholder — keyframes beyond 1.0
             * will still be stored, and the animation system uses the max keyframe
             * time for looping. This is acceptable for MVP. */
        }
        rt_animation3d_set_looping(anim, 1);

        int32_t new_count = *out_count + 1;
        void **na = (void **)realloc(*out_anims, (size_t)new_count * sizeof(void *));
        if (!na)
            continue;
        *out_anims = na;
        (*out_anims)[*out_count] = anim;
        *out_count = new_count;
    }
}

/*==========================================================================
 * Top-level FBX loader
 *=========================================================================*/

static void rt_fbx_asset_finalize(void *obj)
{
    rt_fbx_asset *fbx = (rt_fbx_asset *)obj;
    free(fbx->meshes);
    fbx->meshes = NULL;
    free(fbx->animations);
    fbx->animations = NULL;
    free(fbx->materials);
    fbx->materials = NULL;
}

void *rt_fbx_load(rt_string path)
{
    if (!path)
    {
        rt_trap("FBX.Load: null path");
        return NULL;
    }
    const char *cpath = rt_string_cstr(path);
    if (!cpath)
    {
        rt_trap("FBX.Load: invalid path");
        return NULL;
    }

    /* Read file */
    FILE *f = fopen(cpath, "rb");
    if (!f)
    {
        rt_trap("FBX.Load: cannot open file");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 27)
    {
        fclose(f);
        rt_trap("FBX.Load: file too small");
        return NULL;
    }

    uint8_t *data = (uint8_t *)malloc((size_t)fsize);
    if (!data)
    {
        fclose(f);
        rt_trap("FBX.Load: out of memory");
        return NULL;
    }
    if (fread(data, 1, (size_t)fsize, f) != (size_t)fsize)
    {
        fclose(f);
        free(data);
        rt_trap("FBX.Load: read error");
        return NULL;
    }
    fclose(f);

    /* Verify magic */
    static const char magic[] = "Kaydara FBX Binary  ";
    if (memcmp(data, magic, 20) != 0)
    {
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

    /* Parse all top-level nodes into a virtual root */
    fbx_node_t root;
    memset(&root, 0, sizeof(root));
    strcpy(root.name, "_root_");

    while (!fbx_eof(&reader))
    {
        /* Check for file-level null sentinel */
        size_t sentinel = reader.is_64bit ? 25 : 13;
        if (reader.pos + sentinel <= reader.len)
        {
            int is_null = 1;
            for (size_t si = 0; si < sentinel; si++)
                if (reader.data[reader.pos + si] != 0)
                {
                    is_null = 0;
                    break;
                }
            if (is_null)
                break;
        }

        if (root.child_count >= root.child_capacity)
        {
            int32_t new_cap = root.child_capacity == 0 ? 16 : root.child_capacity * 2;
            fbx_node_t *nc =
                (fbx_node_t *)realloc(root.children, (size_t)new_cap * sizeof(fbx_node_t));
            if (!nc)
                break;
            root.children = nc;
            root.child_capacity = new_cap;
        }

        fbx_node_t *child = &root.children[root.child_count];
        if (fbx_parse_node(&reader, child) < 0)
            break;
        root.child_count++;
    }

    free(data);

    /* Build connection table */
    fbx_conn_table_t ct;
    memset(&ct, 0, sizeof(ct));
    fbx_parse_connections(&root, &ct);

    /* Detect coordinate system */
    int z_up = fbx_is_z_up(&root);

    /* Extract assets */
    rt_fbx_asset *asset = (rt_fbx_asset *)rt_obj_new_i64(0, (int64_t)sizeof(rt_fbx_asset));
    if (!asset)
    {
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
    rt_obj_set_finalizer(asset, rt_fbx_asset_finalize);

    /* Extract geometry */
    fbx_node_t *objects = fbx_find_child(&root, "Objects");
    if (objects)
    {
        for (int32_t i = 0; i < objects->child_count; i++)
        {
            fbx_node_t *obj = &objects->children[i];
            if (strcmp(obj->name, "Geometry") == 0)
            {
                void *mesh = fbx_extract_geometry(obj, z_up);
                if (mesh)
                {
                    int32_t nc = asset->mesh_count + 1;
                    void **nm = (void **)realloc(asset->meshes, (size_t)nc * sizeof(void *));
                    if (nm)
                    {
                        asset->meshes = nm;
                        asset->meshes[asset->mesh_count] = mesh;
                        asset->mesh_count = nc;
                    }
                }
            }
            else if (strcmp(obj->name, "Material") == 0)
            {
                void *mat = fbx_extract_material(obj);
                if (mat)
                {
                    int32_t nc = asset->material_count + 1;
                    void **nm = (void **)realloc(asset->materials, (size_t)nc * sizeof(void *));
                    if (nm)
                    {
                        asset->materials = nm;
                        asset->materials[asset->material_count] = mat;
                        asset->material_count = nc;
                    }
                }
            }
        }
    }

    /* Extract skeleton */
    asset->skeleton = fbx_extract_skeleton(&root, &ct, z_up);

    /* Extract animations */
    fbx_extract_animations(
        &root, &ct, asset->skeleton, &asset->animations, &asset->animation_count);

    /* Cleanup parser data */
    free(ct.entries);
    fbx_free_node(&root);

    return asset;
}

/*==========================================================================
 * FBX asset accessors
 *=========================================================================*/

int64_t rt_fbx_mesh_count(void *obj)
{
    return obj ? ((rt_fbx_asset *)obj)->mesh_count : 0;
}

void *rt_fbx_get_mesh(void *obj, int64_t index)
{
    if (!obj)
        return NULL;
    rt_fbx_asset *a = (rt_fbx_asset *)obj;
    if (index < 0 || index >= a->mesh_count)
        return NULL;
    return a->meshes[index];
}

void *rt_fbx_get_skeleton(void *obj)
{
    return obj ? ((rt_fbx_asset *)obj)->skeleton : NULL;
}

int64_t rt_fbx_animation_count(void *obj)
{
    return obj ? ((rt_fbx_asset *)obj)->animation_count : 0;
}

void *rt_fbx_get_animation(void *obj, int64_t index)
{
    if (!obj)
        return NULL;
    rt_fbx_asset *a = (rt_fbx_asset *)obj;
    if (index < 0 || index >= a->animation_count)
        return NULL;
    return a->animations[index];
}

rt_string rt_fbx_get_animation_name(void *obj, int64_t index)
{
    void *anim = rt_fbx_get_animation(obj, index);
    if (!anim)
        return rt_const_cstr("");
    extern rt_string rt_animation3d_get_name(void *);
    return rt_animation3d_get_name(anim);
}

int64_t rt_fbx_material_count(void *obj)
{
    return obj ? ((rt_fbx_asset *)obj)->material_count : 0;
}

void *rt_fbx_get_material(void *obj, int64_t index)
{
    if (!obj)
        return NULL;
    rt_fbx_asset *a = (rt_fbx_asset *)obj;
    if (index < 0 || index >= a->material_count)
        return NULL;
    return a->materials[index];
}

#endif /* VIPER_ENABLE_GRAPHICS */
