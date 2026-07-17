//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_mesh_simplify.c
// Purpose: From-scratch quadric-error-metric (Garland-Heckbert) mesh
//   simplification powering Mesh3D.Simplify and SceneNode.GenerateLODs.
//   Collapses use subset placement (an edge collapses onto one endpoint), so
//   vertex attributes — UVs, normals, colors, bone weights — are never
//   interpolated and skinned meshes decimate safely.
//
// Key invariants:
//   - Vertices weld on the FULL record (position + attributes, exact bytes):
//     attribute seams therefore split topologically and inherit the open-border
//     quadric penalty, which is what keeps texture seams from swimming.
//   - Boundary/seam edges receive a x10 perpendicular-plane quadric penalty.
//   - Collapses that flip any surviving triangle's normal are rejected.
//   - Deterministic: costs tie-break on (min index, max index); no randomness.
//   - The result is always a NEW mesh; the input is never mutated.
//
// Ownership/Lifetime:
//   - Returned meshes are ordinary GC-managed Mesh3D objects whose finalizer
//     frees the vertex/index arrays this file allocates.
//
// Links: rt_mesh_simplify.h, misc/plans/fps/09-asset-pipeline-upgrades.md
//
//===----------------------------------------------------------------------===//

#ifdef ZANNA_ENABLE_GRAPHICS

#include "rt_mesh_simplify.h"
#include "../render/rt_canvas3d.h"
#include "../render/rt_canvas3d_internal.h"
#include "../scene/rt_scene3d.h"
#include "../scene/rt_scene3d_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rt_trap.h"
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);

/*==========================================================================
 * Quadrics
 *=========================================================================*/

/// @brief Symmetric 4x4 quadric stored as its 10 unique coefficients.
typedef struct {
    double a2, ab, ac, ad;
    double b2, bc, bd;
    double c2, cd;
    double d2;
} simp_quadric_t;

static void quadric_add(simp_quadric_t *q, const simp_quadric_t *o) {
    q->a2 += o->a2;
    q->ab += o->ab;
    q->ac += o->ac;
    q->ad += o->ad;
    q->b2 += o->b2;
    q->bc += o->bc;
    q->bd += o->bd;
    q->c2 += o->c2;
    q->cd += o->cd;
    q->d2 += o->d2;
}

/// @brief Build a plane quadric (a,b,c,d normalized plane) scaled by @p weight.
static void quadric_from_plane(simp_quadric_t *q, double a, double b, double c, double d,
                               double weight) {
    q->a2 = a * a * weight;
    q->ab = a * b * weight;
    q->ac = a * c * weight;
    q->ad = a * d * weight;
    q->b2 = b * b * weight;
    q->bc = b * c * weight;
    q->bd = b * d * weight;
    q->c2 = c * c * weight;
    q->cd = c * d * weight;
    q->d2 = d * d * weight;
}

/// @brief Evaluate v^T Q v at position p.
static double quadric_eval(const simp_quadric_t *q, const float p[3]) {
    double x = p[0];
    double y = p[1];
    double z = p[2];
    return q->a2 * x * x + 2.0 * q->ab * x * y + 2.0 * q->ac * x * z + 2.0 * q->ad * x +
           q->b2 * y * y + 2.0 * q->bc * y * z + 2.0 * q->bd * y + q->c2 * z * z +
           2.0 * q->cd * z + q->d2;
}

/*==========================================================================
 * Working set
 *=========================================================================*/

typedef struct {
    double cost;
    uint32_t a; /* surviving endpoint candidate */
    uint32_t b; /* removed endpoint */
    uint32_t stamp_a;
    uint32_t stamp_b;
} simp_heap_entry_t;

typedef struct {
    vgfx3d_vertex_t *verts; /* welded working vertices */
    simp_quadric_t *quadrics;
    uint32_t *stamps;   /* bumped on every collapse touching the vertex */
    int8_t *alive;
    uint32_t vert_count;

    uint32_t *tris; /* 3 welded indices per face; alive when tris[i*3] != UINT32_MAX */
    uint32_t tri_count;
    uint32_t live_tris;

    /* CSR adjacency: faces incident to each vertex (rebuilt lazily per collapse
     * via linked lists to keep collapses O(valence)). */
    int32_t *vert_face_head; /* head index into face_links, -1 = none */
    int32_t *face_links;     /* per (face,corner): next link */

    simp_heap_entry_t *heap;
    uint32_t heap_count;
    uint32_t heap_capacity;
} simp_ctx_t;

static void simp_heap_swap(simp_heap_entry_t *h, uint32_t i, uint32_t j) {
    simp_heap_entry_t t = h[i];
    h[i] = h[j];
    h[j] = t;
}

/// @brief Deterministic ordering: cost, then (min,max) index pair.
static int simp_heap_less(const simp_heap_entry_t *x, const simp_heap_entry_t *y) {
    if (x->cost != y->cost)
        return x->cost < y->cost;
    if (x->a != y->a)
        return x->a < y->a;
    return x->b < y->b;
}

static int simp_heap_push(simp_ctx_t *cx, simp_heap_entry_t e) {
    if (cx->heap_count >= cx->heap_capacity) {
        uint32_t cap = cx->heap_capacity ? cx->heap_capacity * 2u : 256u;
        simp_heap_entry_t *grown =
            (simp_heap_entry_t *)realloc(cx->heap, (size_t)cap * sizeof(*grown));
        if (!grown)
            return 0;
        cx->heap = grown;
        cx->heap_capacity = cap;
    }
    uint32_t i = cx->heap_count++;
    cx->heap[i] = e;
    while (i > 0) {
        uint32_t parent = (i - 1) / 2;
        if (!simp_heap_less(&cx->heap[i], &cx->heap[parent]))
            break;
        simp_heap_swap(cx->heap, i, parent);
        i = parent;
    }
    return 1;
}

static int simp_heap_pop(simp_ctx_t *cx, simp_heap_entry_t *out) {
    if (cx->heap_count == 0)
        return 0;
    *out = cx->heap[0];
    cx->heap[0] = cx->heap[--cx->heap_count];
    uint32_t i = 0;
    for (;;) {
        uint32_t l = i * 2 + 1;
        uint32_t r = l + 1;
        uint32_t smallest = i;
        if (l < cx->heap_count && simp_heap_less(&cx->heap[l], &cx->heap[smallest]))
            smallest = l;
        if (r < cx->heap_count && simp_heap_less(&cx->heap[r], &cx->heap[smallest]))
            smallest = r;
        if (smallest == i)
            break;
        simp_heap_swap(cx->heap, i, smallest);
        i = smallest;
    }
    return 1;
}

/// @brief Exact-record vertex weld: dedupe identical vgfx3d_vertex_t payloads.
/// @details Attribute seams stay split (different UV/normal wedges never weld),
///   which converts them into protected open borders. Deterministic FNV hash.
static uint32_t *simp_weld(const vgfx3d_vertex_t *verts, uint32_t count,
                           vgfx3d_vertex_t **out_welded, uint32_t *out_welded_count) {
    uint32_t *remap = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
    vgfx3d_vertex_t *welded = (vgfx3d_vertex_t *)malloc((size_t)count * sizeof(*welded));
    uint32_t table_size = 1;
    while (table_size < count * 2u + 1u)
        table_size <<= 1;
    uint32_t *table = (uint32_t *)malloc((size_t)table_size * sizeof(uint32_t));
    if (!remap || !welded || !table) {
        free(remap);
        free(welded);
        free(table);
        return NULL;
    }
    memset(table, 0xFF, (size_t)table_size * sizeof(uint32_t));
    uint32_t welded_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *bytes = (const uint8_t *)&verts[i];
        uint32_t hash = 2166136261u;
        for (size_t k = 0; k < sizeof(vgfx3d_vertex_t); k++)
            hash = (hash ^ bytes[k]) * 16777619u;
        uint32_t slot = hash & (table_size - 1u);
        uint32_t found = UINT32_MAX;
        while (table[slot] != UINT32_MAX) {
            uint32_t cand = table[slot];
            if (memcmp(&welded[cand], &verts[i], sizeof(vgfx3d_vertex_t)) == 0) {
                found = cand;
                break;
            }
            slot = (slot + 1u) & (table_size - 1u);
        }
        if (found == UINT32_MAX) {
            welded[welded_count] = verts[i];
            table[slot] = welded_count;
            found = welded_count++;
        }
        remap[i] = found;
    }
    free(table);
    *out_welded = welded;
    *out_welded_count = welded_count;
    return remap;
}

/// @brief Recompute a face's normal; returns 0 for degenerate faces.
static int simp_face_normal(const simp_ctx_t *cx, uint32_t f, double n[3]) {
    const float *p0 = cx->verts[cx->tris[f * 3 + 0]].pos;
    const float *p1 = cx->verts[cx->tris[f * 3 + 1]].pos;
    const float *p2 = cx->verts[cx->tris[f * 3 + 2]].pos;
    double e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    double e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    double len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!isfinite(len) || len < 1e-14)
        return 0;
    n[0] /= len;
    n[1] /= len;
    n[2] /= len;
    return 1;
}

static void simp_link_face(simp_ctx_t *cx, uint32_t f) {
    for (int c = 0; c < 3; c++) {
        uint32_t v = cx->tris[f * 3 + c];
        cx->face_links[f * 3 + c] = cx->vert_face_head[v];
        cx->vert_face_head[v] = (int32_t)(f * 3 + c);
    }
}

/// @brief Cost of collapsing b onto a (subset placement at a's position).
static double simp_collapse_cost(const simp_ctx_t *cx, uint32_t a, uint32_t b) {
    simp_quadric_t q = cx->quadrics[a];
    quadric_add(&q, &cx->quadrics[b]);
    double c = quadric_eval(&q, cx->verts[a].pos);
    return c < 0.0 ? 0.0 : c;
}

static void simp_push_edge(simp_ctx_t *cx, uint32_t a, uint32_t b) {
    simp_heap_entry_t e;
    if (a == b)
        return;
    e.cost = simp_collapse_cost(cx, a, b);
    e.a = a;
    e.b = b;
    e.stamp_a = cx->stamps[a];
    e.stamp_b = cx->stamps[b];
    (void)simp_heap_push(cx, e);
}

/// @brief Core QEM loop. Returns a NEW Mesh3D or NULL on failure.
void *rt_mesh3d_simplify(void *mesh_obj, int64_t target_triangles) {
    rt_mesh3d *mesh = (rt_mesh3d *)rt_g3d_checked_or_null(mesh_obj, RT_G3D_MESH3D_CLASS_ID);
    if (!mesh) {
        rt_trap("Mesh3D.Simplify: invalid mesh");
        return NULL;
    }
    rt_mesh3d_repair_geometry_counts(mesh);
    uint32_t src_verts = rt_mesh3d_safe_vertex_count(mesh);
    uint32_t src_indices = rt_mesh3d_validated_index_count(mesh);
    if (src_verts == 0 || src_indices < 3) {
        rt_trap("Mesh3D.Simplify: mesh has no triangles");
        return NULL;
    }
    if (target_triangles < 1)
        target_triangles = 1;

    simp_ctx_t cx;
    memset(&cx, 0, sizeof(cx));
    uint32_t *remap = simp_weld(mesh->vertices, src_verts, &cx.verts, &cx.vert_count);
    if (!remap)
        return NULL;

    cx.tri_count = src_indices / 3u;
    cx.tris = (uint32_t *)malloc((size_t)cx.tri_count * 3u * sizeof(uint32_t));
    cx.quadrics = (simp_quadric_t *)calloc(cx.vert_count, sizeof(simp_quadric_t));
    cx.stamps = (uint32_t *)calloc(cx.vert_count, sizeof(uint32_t));
    cx.alive = (int8_t *)malloc(cx.vert_count);
    cx.vert_face_head = (int32_t *)malloc((size_t)cx.vert_count * sizeof(int32_t));
    cx.face_links = (int32_t *)malloc((size_t)cx.tri_count * 3u * sizeof(int32_t));
    if (!cx.tris || !cx.quadrics || !cx.stamps || !cx.alive || !cx.vert_face_head ||
        !cx.face_links)
        goto fail;
    memset(cx.alive, 1, cx.vert_count);
    for (uint32_t v = 0; v < cx.vert_count; v++)
        cx.vert_face_head[v] = -1;

    cx.live_tris = 0;
    for (uint32_t f = 0; f < cx.tri_count; f++) {
        uint32_t i0 = remap[mesh->indices[f * 3 + 0]];
        uint32_t i1 = remap[mesh->indices[f * 3 + 1]];
        uint32_t i2 = remap[mesh->indices[f * 3 + 2]];
        if (i0 == i1 || i1 == i2 || i0 == i2) {
            cx.tris[f * 3 + 0] = UINT32_MAX; /* degenerate at load */
            cx.tris[f * 3 + 1] = UINT32_MAX;
            cx.tris[f * 3 + 2] = UINT32_MAX;
            continue;
        }
        cx.tris[f * 3 + 0] = i0;
        cx.tris[f * 3 + 1] = i1;
        cx.tris[f * 3 + 2] = i2;
        cx.live_tris++;
        simp_link_face(&cx, f);
    }

    /* Face-plane quadrics (area-weighted) + boundary penalties. Boundary/seam
     * edges (used by exactly one face after welding) get a perpendicular plane
     * quadric x10 so borders and attribute seams hold their shape. */
    {
        /* Count directed edge occurrences via a small hash of undirected pairs. */
        uint32_t table_size = 1;
        while (table_size < cx.tri_count * 8u + 1u)
            table_size <<= 1;
        uint64_t *edge_keys = (uint64_t *)malloc((size_t)table_size * sizeof(uint64_t));
        uint32_t *edge_counts = (uint32_t *)malloc((size_t)table_size * sizeof(uint32_t));
        if (!edge_keys || !edge_counts) {
            free(edge_keys);
            free(edge_counts);
            goto fail;
        }
        memset(edge_keys, 0xFF, (size_t)table_size * sizeof(uint64_t));
        memset(edge_counts, 0, (size_t)table_size * sizeof(uint32_t));

        for (uint32_t f = 0; f < cx.tri_count; f++) {
            if (cx.tris[f * 3] == UINT32_MAX)
                continue;
            double n[3];
            if (!simp_face_normal(&cx, f, n))
                continue;
            const float *p0 = cx.verts[cx.tris[f * 3 + 0]].pos;
            /* Area weight: quadrics from big faces matter more. */
            double d = -(n[0] * p0[0] + n[1] * p0[1] + n[2] * p0[2]);
            simp_quadric_t q;
            quadric_from_plane(&q, n[0], n[1], n[2], d, 1.0);
            for (int c = 0; c < 3; c++)
                quadric_add(&cx.quadrics[cx.tris[f * 3 + c]], &q);
            for (int c = 0; c < 3; c++) {
                uint32_t va = cx.tris[f * 3 + c];
                uint32_t vb = cx.tris[f * 3 + (c + 1) % 3];
                uint64_t key = va < vb ? ((uint64_t)va << 32) | vb : ((uint64_t)vb << 32) | va;
                uint32_t slot = (uint32_t)((key ^ (key >> 29)) * 0x9E3779B97F4A7C15ull >> 40) &
                                (table_size - 1u);
                while (edge_keys[slot] != UINT64_MAX && edge_keys[slot] != key)
                    slot = (slot + 1u) & (table_size - 1u);
                edge_keys[slot] = key;
                edge_counts[slot]++;
            }
        }
        /* Boundary quadrics + seed the collapse heap from unique edges. */
        for (uint32_t slot = 0; slot < table_size; slot++) {
            if (edge_keys[slot] == UINT64_MAX)
                continue;
            uint32_t va = (uint32_t)(edge_keys[slot] >> 32);
            uint32_t vb = (uint32_t)edge_keys[slot];
            if (edge_counts[slot] == 1) {
                /* Open border or attribute seam: constrain with a plane through
                 * the edge, perpendicular to an adjacent-face-ish direction. */
                const float *pa = cx.verts[va].pos;
                const float *pb = cx.verts[vb].pos;
                double e[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
                double up[3] = {0.0, 1.0, 0.0};
                if (fabs(e[1]) > fabs(e[0]) && fabs(e[1]) > fabs(e[2])) {
                    up[0] = 1.0;
                    up[1] = 0.0;
                }
                double n[3] = {e[1] * up[2] - e[2] * up[1], e[2] * up[0] - e[0] * up[2],
                               e[0] * up[1] - e[1] * up[0]};
                double len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (isfinite(len) && len > 1e-12) {
                    n[0] /= len;
                    n[1] /= len;
                    n[2] /= len;
                    double d = -(n[0] * pa[0] + n[1] * pa[1] + n[2] * pa[2]);
                    simp_quadric_t q;
                    quadric_from_plane(&q, n[0], n[1], n[2], d, 10.0);
                    quadric_add(&cx.quadrics[va], &q);
                    quadric_add(&cx.quadrics[vb], &q);
                }
            }
        }
        for (uint32_t slot = 0; slot < table_size; slot++) {
            if (edge_keys[slot] == UINT64_MAX)
                continue;
            uint32_t va = (uint32_t)(edge_keys[slot] >> 32);
            uint32_t vb = (uint32_t)edge_keys[slot];
            simp_push_edge(&cx, va, vb);
            simp_push_edge(&cx, vb, va);
        }
        free(edge_keys);
        free(edge_counts);
    }

    /* Collapse loop with lazy heap invalidation and flip rejection. */
    while (cx.live_tris > (uint32_t)target_triangles) {
        simp_heap_entry_t e;
        if (!simp_heap_pop(&cx, &e))
            break;
        uint32_t a = e.a;
        uint32_t b = e.b;
        if (!cx.alive[a] || !cx.alive[b] || cx.stamps[a] != e.stamp_a ||
            cx.stamps[b] != e.stamp_b)
            continue;
        /* Verify a and b still share an edge, collect b's faces, flip-test. */
        int adjacent = 0;
        int flip = 0;
        for (int32_t link = cx.vert_face_head[b]; link >= 0; link = cx.face_links[link]) {
            uint32_t f = (uint32_t)link / 3u;
            if (cx.tris[f * 3] == UINT32_MAX)
                continue;
            uint32_t t0 = cx.tris[f * 3 + 0];
            uint32_t t1 = cx.tris[f * 3 + 1];
            uint32_t t2 = cx.tris[f * 3 + 2];
            int has_a = (t0 == a || t1 == a || t2 == a);
            if (has_a) {
                adjacent = 1;
                continue; /* face vanishes on collapse */
            }
            double before[3];
            if (!simp_face_normal(&cx, f, before))
                continue;
            /* Simulate the move: replace b with a. */
            uint32_t sim[3] = {t0 == b ? a : t0, t1 == b ? a : t1, t2 == b ? a : t2};
            const float *p0 = cx.verts[sim[0]].pos;
            const float *p1 = cx.verts[sim[1]].pos;
            const float *p2 = cx.verts[sim[2]].pos;
            double e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
            double e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
            double after[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                               e1[0] * e2[1] - e1[1] * e2[0]};
            double dot = before[0] * after[0] + before[1] * after[1] + before[2] * after[2];
            if (dot <= 0.0) {
                flip = 1;
                break;
            }
        }
        if (!adjacent || flip)
            continue;

        /* Apply: retire b, move its faces to a, merge quadrics, restamp. */
        quadric_add(&cx.quadrics[a], &cx.quadrics[b]);
        cx.alive[b] = 0;
        cx.stamps[a]++;
        cx.stamps[b]++;
        int32_t link = cx.vert_face_head[b];
        cx.vert_face_head[b] = -1;
        while (link >= 0) {
            int32_t next = cx.face_links[link];
            uint32_t f = (uint32_t)link / 3u;
            if (cx.tris[f * 3] != UINT32_MAX) {
                uint32_t t0 = cx.tris[f * 3 + 0];
                uint32_t t1 = cx.tris[f * 3 + 1];
                uint32_t t2 = cx.tris[f * 3 + 2];
                if (t0 == a || t1 == a || t2 == a) {
                    /* Shared face collapses away. */
                    cx.tris[f * 3 + 0] = UINT32_MAX;
                    cx.tris[f * 3 + 1] = UINT32_MAX;
                    cx.tris[f * 3 + 2] = UINT32_MAX;
                    if (cx.live_tris > 0)
                        cx.live_tris--;
                } else {
                    for (int c = 0; c < 3; c++) {
                        if (cx.tris[f * 3 + c] == b)
                            cx.tris[f * 3 + c] = a;
                    }
                    /* Relink this corner under a. */
                    cx.face_links[link] = cx.vert_face_head[a];
                    cx.vert_face_head[a] = link;
                }
            }
            link = next;
        }
        /* Refresh candidate collapses around a (deterministic scan order). */
        for (int32_t l2 = cx.vert_face_head[a]; l2 >= 0; l2 = cx.face_links[l2]) {
            uint32_t f = (uint32_t)l2 / 3u;
            if (cx.tris[f * 3] == UINT32_MAX)
                continue;
            for (int c = 0; c < 3; c++) {
                uint32_t v = cx.tris[f * 3 + c];
                if (v != a && cx.alive[v]) {
                    simp_push_edge(&cx, a, v);
                    simp_push_edge(&cx, v, a);
                }
            }
        }
    }

    /* Emit the surviving triangles into a new mesh (compacted vertices). */
    {
        uint32_t *out_map = (uint32_t *)malloc((size_t)cx.vert_count * sizeof(uint32_t));
        rt_mesh3d *out = (rt_mesh3d *)rt_mesh3d_new();
        if (!out_map || !out) {
            free(out_map);
            goto fail;
        }
        memset(out_map, 0xFF, (size_t)cx.vert_count * sizeof(uint32_t));
        uint32_t out_vert_count = 0;
        uint32_t out_index_count = 0;
        for (uint32_t f = 0; f < cx.tri_count; f++) {
            if (cx.tris[f * 3] == UINT32_MAX)
                continue;
            for (int c = 0; c < 3; c++) {
                uint32_t v = cx.tris[f * 3 + c];
                if (out_map[v] == UINT32_MAX)
                    out_map[v] = out_vert_count++;
            }
            out_index_count += 3;
        }
        vgfx3d_vertex_t *ov =
            (vgfx3d_vertex_t *)malloc((size_t)(out_vert_count ? out_vert_count : 1) *
                                      sizeof(vgfx3d_vertex_t));
        uint32_t *oi = (uint32_t *)malloc((size_t)(out_index_count ? out_index_count : 1) *
                                          sizeof(uint32_t));
        if (!ov || !oi) {
            free(ov);
            free(oi);
            free(out_map);
            goto fail;
        }
        for (uint32_t v = 0; v < cx.vert_count; v++) {
            if (out_map[v] != UINT32_MAX)
                ov[out_map[v]] = cx.verts[v];
        }
        uint32_t cursor = 0;
        for (uint32_t f = 0; f < cx.tri_count; f++) {
            if (cx.tris[f * 3] == UINT32_MAX)
                continue;
            oi[cursor++] = out_map[cx.tris[f * 3 + 0]];
            oi[cursor++] = out_map[cx.tris[f * 3 + 1]];
            oi[cursor++] = out_map[cx.tris[f * 3 + 2]];
        }
        free(out->vertices);
        free(out->indices);
        out->vertices = ov;
        out->vertex_count = out_vert_count;
        out->vertex_capacity = out_vert_count ? out_vert_count : 1;
        out->indices = oi;
        out->index_count = out_index_count;
        out->index_capacity = out_index_count ? out_index_count : 1;
        out->bounds_dirty = 1;
        rt_mesh3d_touch_geometry(out);
        rt_mesh3d_recalc_normals(out);
        free(out_map);
        free(remap);
        free(cx.verts);
        free(cx.tris);
        free(cx.quadrics);
        free(cx.stamps);
        free(cx.alive);
        free(cx.vert_face_head);
        free(cx.face_links);
        free(cx.heap);
        return out;
    }

fail:
    free(remap);
    free(cx.verts);
    free(cx.tris);
    free(cx.quadrics);
    free(cx.stamps);
    free(cx.alive);
    free(cx.vert_face_head);
    free(cx.face_links);
    free(cx.heap);
    return NULL;
}

/// @brief One-call LOD-chain generator feeding the existing AddLOD/SetAutoLOD
///   machinery: level k holds ~ratio^k of the source triangles; distance
///   thresholds derive from the mesh's bounding radius.
void rt_scene_node3d_generate_lods(void *node_obj, int64_t levels, double ratio) {
    rt_scene_node3d *node =
        (rt_scene_node3d *)rt_g3d_checked_or_null(node_obj, RT_G3D_SCENENODE3D_CLASS_ID);
    if (!node)
        return;
    if (!node->mesh) {
        rt_trap("SceneNode.GenerateLODs: node has no mesh");
        return;
    }
    if (levels < 1)
        levels = 1;
    if (levels > 4)
        levels = 4;
    if (!isfinite(ratio) || ratio <= 0.0 || ratio >= 1.0)
        ratio = 0.4;
    rt_mesh3d *mesh = (rt_mesh3d *)rt_g3d_checked_or_null(node->mesh, RT_G3D_MESH3D_CLASS_ID);
    if (!mesh)
        return;
    rt_mesh3d_repair_geometry_counts(mesh);
    uint32_t base_tris = rt_mesh3d_validated_index_count(mesh) / 3u;
    if (base_tris < 8)
        return; /* nothing worth decimating */
    rt_mesh3d_refresh_bounds(mesh);
    double radius = mesh->bsphere_radius > 0.0f ? (double)mesh->bsphere_radius : 1.0;
    double target = (double)base_tris;
    double distance = radius * 12.0;
    for (int64_t k = 0; k < levels; k++) {
        target *= ratio;
        int64_t tri_target = (int64_t)target;
        if (tri_target < 2)
            tri_target = 2;
        void *lod = rt_mesh3d_simplify(node->mesh, tri_target);
        if (!lod)
            break;
        rt_scene_node3d_add_lod(node_obj, distance, lod);
        /* add_lod retains the mesh; drop the construction reference. */
        if (rt_obj_release_check0(lod))
            rt_obj_free(lod);
        distance *= 2.0;
    }
    rt_scene_node3d_set_auto_lod(node_obj, 1, 3.0);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* ZANNA_ENABLE_GRAPHICS */
