//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_fbx_loader_geometry.inc  (#included from rt_fbx_loader.c; not a standalone translation unit)
// Purpose: FBX coordinate-system correction, geometry extraction, and material extraction
//===----------------------------------------------------------------------===//
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
///   Returns -1 for an out-of-range index or a negative slot value when a
///   material map is present; returns 0 when no material map exists.
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
        return -1;
    return slots[src_index] < 0 ? -1 : slots[src_index];
}

#include "rt_fbx_triangulation.inc"

/*==========================================================================
 * Geometry extraction
 *=========================================================================*/

/// @brief A parsed FBX vertex-attribute layer (LayerElementNormal / -UV): the raw
///   value array plus its optional index array and mapping/reference modes.
typedef struct {
    double *data;
    float *data32;
    int32_t *indices;
    uint32_t count;
    uint32_t index_count;
    int components;
    int by_polygon_vertex;
    int by_polygon;
    int all_same;
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

/// @brief Return true when a LayerElement node has an explicit numeric layer index.
/// @param layer Candidate FBX `LayerElement*` node.
/// @param out_index Receives the numeric layer index when present.
/// @return Non-zero when the first property is a numeric index.
static int fbx_layer_element_numeric_index(fbx_node_t *layer, int64_t *out_index) {
    fbx_prop_t *p;
    if (!layer || layer->prop_count < 1 || !out_index)
        return 0;
    p = &layer->props[0];
    if (p->type == 'Y' || p->type == 'I' || p->type == 'L' || p->type == 'C') {
        *out_index = fbx_prop_i64(layer, 0);
        return 1;
    }
    return 0;
}

/// @brief Locate a specific FBX vertex layer by numeric layer index or occurrence order.
/// @details Many FBX files contain several nodes with the same name, such as two
///          `LayerElementUV` children for UV0/UV1. The binary tree helper returns only the first
///          child, so this routine scans all siblings and prefers an explicit layer-index property
///          matching @p desired_index. When indices are absent, occurrence order is used.
/// @param geom_node Geometry node containing `LayerElement*` children.
/// @param layer_name Child node name, e.g. `LayerElementUV`.
/// @param desired_index Layer index to find.
/// @return Matching layer node, or NULL.
static fbx_node_t *fbx_find_vertex_layer(fbx_node_t *geom_node,
                                         const char *layer_name,
                                         int desired_index) {
    fbx_node_t *first = NULL;
    int occurrence = 0;
    if (!geom_node || !layer_name || desired_index < 0)
        return NULL;
    for (int32_t i = 0; i < geom_node->child_count; i++) {
        fbx_node_t *child = &geom_node->children[i];
        int64_t explicit_index = 0;
        if (strcmp(child->name, layer_name) != 0)
            continue;
        if (!first)
            first = child;
        if (fbx_layer_element_numeric_index(child, &explicit_index)) {
            if (explicit_index == desired_index)
                return child;
        } else if (occurrence == desired_index) {
            return child;
        }
        occurrence++;
    }
    return desired_index == 0 ? first : NULL;
}

/// @brief Parse a vertex-attribute layer (e.g. LayerElementNormal/-UV) of
///   `components` floats per element from @p geom_node into *out. @p index_name_alt
///   is an optional fallback index-array name (or NULL). @p layer_index selects among
///   repeated FBX layer nodes, allowing UV1/vertex-color imports in addition to the
///   first normal/UV layer. All fields are zeroed when the layer is absent or malformed.
static void fbx_parse_vertex_layer(fbx_node_t *geom_node,
                                   const char *layer_name,
                                   const char *data_name,
                                   const char *index_name,
                                   const char *index_name_alt,
                                   int components,
                                   int layer_index,
                                   fbx_vertex_layer_t *out) {
    memset(out, 0, sizeof(*out));
    fbx_node_t *layer = fbx_find_vertex_layer(geom_node, layer_name, layer_index);
    if (!layer)
        return;
    fbx_node_t *d_node = fbx_find_child(layer, data_name);
    out->components = components;
    if (d_node && d_node->prop_count >= 1 && d_node->props[0].type == 'd') {
        out->data = (double *)d_node->props[0].v.array.data;
        out->count = d_node->props[0].v.array.count / (uint32_t)components;
        if (out->count > (uint32_t)INT32_MAX) {
            out->data = NULL;
            out->count = 0;
        }
    } else if (d_node && d_node->prop_count >= 1 && d_node->props[0].type == 'f') {
        out->data32 = (float *)d_node->props[0].v.array.data;
        out->count = d_node->props[0].v.array.count / (uint32_t)components;
        if (out->count > (uint32_t)INT32_MAX) {
            out->data32 = NULL;
            out->count = 0;
        }
    }
    fbx_node_t *mm = fbx_find_child(layer, "MappingInformationType");
    if (mm && mm->prop_count >= 1) {
        const char *mapping = fbx_prop_str(mm, 0);
        out->by_polygon_vertex = fbx_ascii_equals_i(mapping, "ByPolygonVertex");
        out->by_polygon = fbx_ascii_equals_i(mapping, "ByPolygon");
        out->all_same = fbx_ascii_equals_i(mapping, "AllSame");
    }
    fbx_node_t *rm = fbx_find_child(layer, "ReferenceInformationType");
    if (rm && rm->prop_count >= 1)
        out->index_to_direct = fbx_ascii_equals_i(fbx_prop_str(rm, 0), "IndexToDirect") ||
                               fbx_ascii_equals_i(fbx_prop_str(rm, 0), "Index");
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

/// @brief Read one component from a parsed vertex layer as double.
/// @param layer Parsed layer.
/// @param element_index Direct element index.
/// @param component Component index within the element.
/// @param fallback Value returned when data is absent or invalid.
/// @return Finite component value or @p fallback.
static double fbx_vertex_layer_component(const fbx_vertex_layer_t *layer,
                                         int32_t element_index,
                                         int component,
                                         double fallback) {
    double value;
    if (!layer || element_index < 0 || element_index >= (int32_t)layer->count ||
        component < 0 || component >= layer->components)
        return fallback;
    if (layer->data)
        value = layer->data[(size_t)element_index * (size_t)layer->components + (size_t)component];
    else if (layer->data32)
        value =
            (double)layer->data32[(size_t)element_index * (size_t)layer->components + (size_t)component];
    else
        return fallback;
    return isfinite(value) ? value : fallback;
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
        out->by_polygon = fbx_ascii_equals_i(mapping, "ByPolygon");
        out->all_same = fbx_ascii_equals_i(mapping, "AllSame");
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
                            : layer->all_same        ? 0u
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

/// @brief Parse the optional FBX vertex attribute layers (normal, UV0/UV1, color) and the
///        material assignment for a geometry node into the caller-provided structs.
static void fbx_parse_geometry_layers(fbx_node_t *geom_node,
                                      fbx_vertex_layer_t *norm_layer,
                                      fbx_vertex_layer_t *uv_layer,
                                      fbx_vertex_layer_t *uv1_layer,
                                      fbx_vertex_layer_t *color_layer,
                                      fbx_material_layer_t *mat) {
    fbx_parse_vertex_layer(geom_node, "LayerElementNormal", "Normals", "NormalsIndex",
                           "NormalIndex", 3, 0, norm_layer);
    fbx_parse_vertex_layer(geom_node, "LayerElementUV", "UV", "UVIndex", NULL, 2, 0, uv_layer);
    fbx_parse_vertex_layer(geom_node, "LayerElementUV", "UV", "UVIndex", NULL, 2, 1, uv1_layer);
    fbx_parse_vertex_layer(geom_node, "LayerElementColor", "Colors", "ColorIndex", "ColorIndex",
                           4, 0, color_layer);
    fbx_parse_material_layer(geom_node, mat);
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
    if (remap && !fbx_mesh_remap_init(remap, fbx_prop_i64(geom_node, 0), (int32_t)pos_count))
        return NULL;
    if (material_map)
        memset(material_map, 0, sizeof(*material_map));

    /* Vertex attribute layers (optional) + material assignment (optional) */
    fbx_vertex_layer_t norm_layer;
    fbx_vertex_layer_t uv_layer;
    fbx_vertex_layer_t uv1_layer;
    fbx_vertex_layer_t color_layer;
    fbx_material_layer_t mat;
    fbx_parse_geometry_layers(geom_node, &norm_layer, &uv_layer, &uv1_layer, &color_layer, &mat);

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
        if (norm_layer.data || norm_layer.data32) {
            int32_t ni = fbx_resolve_layer_index(&norm_layer, polygon_vertex_idx, polygon_idx, vi);
            if (ni >= 0) {
                nx = fbx_vertex_layer_component(&norm_layer, ni, 0, nx);
                ny = fbx_vertex_layer_component(&norm_layer, ni, 1, ny);
                nz = fbx_vertex_layer_component(&norm_layer, ni, 2, nz);
                if (z_up)
                    fbx_correct_zup(&nx, &ny, &nz);
            }
        }
        fbx_sanitize_normal3(&nx, &ny, &nz);

        /* Get UV */
        double u = 0, v = 0;
        if (uv_layer.data || uv_layer.data32) {
            int32_t ui = fbx_resolve_layer_index(&uv_layer, polygon_vertex_idx, polygon_idx, vi);
            if (ui >= 0) {
                u = fbx_vertex_layer_component(&uv_layer, ui, 0, u);
                v = 1.0 -
                    fbx_vertex_layer_component(&uv_layer, ui, 1, 1.0); /* FBX V flips */
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
            if ((uv1_layer.data || uv1_layer.data32) &&
                emitted_vertex >= 0 &&
                emitted_vertex < (int32_t)((rt_mesh3d *)mesh)->vertex_count) {
                int32_t ui1 =
                    fbx_resolve_layer_index(&uv1_layer, polygon_vertex_idx, polygon_idx, vi);
                if (ui1 >= 0) {
                    double u1 = fbx_vertex_layer_component(&uv1_layer, ui1, 0, u);
                    double v1 =
                        1.0 - fbx_vertex_layer_component(&uv1_layer, ui1, 1, 1.0 - v);
                    ((rt_mesh3d *)mesh)->vertices[emitted_vertex].uv1[0] =
                        (float)fbx_clamp_abs_or(u1, u, FBX_UV_ABS_MAX);
                    ((rt_mesh3d *)mesh)->vertices[emitted_vertex].uv1[1] =
                        (float)fbx_clamp_abs_or(v1, v, FBX_UV_ABS_MAX);
                }
            }
            if ((color_layer.data || color_layer.data32) &&
                emitted_vertex >= 0 &&
                emitted_vertex < (int32_t)((rt_mesh3d *)mesh)->vertex_count) {
                int32_t ci =
                    fbx_resolve_layer_index(&color_layer, polygon_vertex_idx, polygon_idx, vi);
                if (ci >= 0) {
                    ((rt_mesh3d *)mesh)->vertices[emitted_vertex].color[0] =
                        (float)fbx_clamp_double(
                            fbx_vertex_layer_component(&color_layer, ci, 0, 1.0), 0.0, 1.0, 1.0);
                    ((rt_mesh3d *)mesh)->vertices[emitted_vertex].color[1] =
                        (float)fbx_clamp_double(
                            fbx_vertex_layer_component(&color_layer, ci, 1, 1.0), 0.0, 1.0, 1.0);
                    ((rt_mesh3d *)mesh)->vertices[emitted_vertex].color[2] =
                        (float)fbx_clamp_double(
                            fbx_vertex_layer_component(&color_layer, ci, 2, 1.0), 0.0, 1.0, 1.0);
                    ((rt_mesh3d *)mesh)->vertices[emitted_vertex].color[3] =
                        (float)fbx_clamp_double(
                            fbx_vertex_layer_component(&color_layer, ci, 3, 1.0), 0.0, 1.0, 1.0);
                }
            }
            fbx_mesh_remap_add_vertex(remap, vi, emitted_vertex);
            polygon[poly_count++] = emitted_vertex;
        }
        polygon_vertex_idx++;

        if (end_of_polygon) {
            uint32_t index_before = ((rt_mesh3d *)mesh)->index_count;
            int32_t material_slot = fbx_material_slot_for_polygon(
                mat.slots, mat.slot_count, mat.by_polygon, mat.all_same, polygon_idx);
            if (material_slot < 0)
                return fbx_geom_fail(polygon, polygon_stack, mesh);
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

    if (poly_count != 0 || polygon_invalid)
        return fbx_geom_fail(polygon, polygon_stack, mesh);
    if (polygon != polygon_stack)
        free(polygon);
    if (((rt_mesh3d *)mesh)->index_count < 3u) {
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }
    if ((!norm_layer.data && !norm_layer.data32) || norm_layer.count == 0)
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
        if (strcmp(p->name, "P") != 0 || p->prop_count < 5)
            continue;
        const char *pname = fbx_prop_str(p, 0);
        if ((fbx_ascii_equals_i(pname, "DiffuseColor") ||
             fbx_ascii_equals_i(pname, "BaseColor") ||
             fbx_ascii_equals_i(pname, "Albedo") || fbx_ascii_contains_i(pname, "basecolor") ||
             fbx_ascii_contains_i(pname, "base_color")) &&
            p->prop_count >= 7) {
            double r = fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1.0, 1.0);
            double g = fbx_clamp_double(fbx_prop_f64(p, 5), 0.0, 1.0, 1.0);
            double b = fbx_clamp_double(fbx_prop_f64(p, 6), 0.0, 1.0, 1.0);
            rt_material3d_set_color(mat, r, g, b);
        } else if (fbx_ascii_equals_i(pname, "Shininess") ||
                   fbx_ascii_equals_i(pname, "ShininessExponent")) {
            double s = fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1000000.0, 32.0);
            rt_material3d_set_shininess(mat, s);
        } else if (fbx_ascii_equals_i(pname, "Opacity") || fbx_ascii_equals_i(pname, "Alpha")) {
            double alpha = fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1.0, 1.0);
            rt_material3d_set_alpha(mat, alpha);
            if (alpha < 1.0)
                rt_material3d_set_alpha_mode(mat, 2);
        } else if (fbx_ascii_equals_i(pname, "TransparencyFactor") ||
                   fbx_ascii_equals_i(pname, "TransparentFactor")) {
            double alpha = 1.0 - fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1.0, 0.0);
            rt_material3d_set_alpha(mat, alpha);
            if (alpha < 1.0)
                rt_material3d_set_alpha_mode(mat, 2);
        } else if ((fbx_ascii_equals_i(pname, "EmissiveColor") ||
                    fbx_ascii_equals_i(pname, "EmissionColor") ||
                    fbx_ascii_contains_i(pname, "emissivecolor")) &&
                   p->prop_count >= 7) {
            double r = fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1.0, 0.0);
            double g = fbx_clamp_double(fbx_prop_f64(p, 5), 0.0, 1.0, 0.0);
            double b = fbx_clamp_double(fbx_prop_f64(p, 6), 0.0, 1.0, 0.0);
            rt_material3d_set_emissive_color(mat, r, g, b);
        } else if (fbx_ascii_equals_i(pname, "EmissiveFactor") ||
                   fbx_ascii_equals_i(pname, "EmissionFactor") ||
                   fbx_ascii_equals_i(pname, "EmissiveIntensity")) {
            rt_material3d_set_emissive_intensity(
                mat, fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1000000.0, 0.0));
        } else if (fbx_ascii_contains_i(pname, "metallic") ||
                   fbx_ascii_contains_i(pname, "metalness")) {
            rt_material3d_set_metallic(mat, fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1.0, 0.0));
        } else if (fbx_ascii_contains_i(pname, "roughness")) {
            rt_material3d_set_roughness(mat, fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1.0, 0.5));
        } else if (fbx_ascii_contains_i(pname, "occlusion") ||
                   fbx_ascii_equals_i(pname, "AO")) {
            rt_material3d_set_ao(mat, fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1.0, 1.0));
        } else if (fbx_ascii_equals_i(pname, "NormalMapScale") ||
                   fbx_ascii_equals_i(pname, "BumpFactor") ||
                   fbx_ascii_equals_i(pname, "Bump")) {
            rt_material3d_set_normal_scale(
                mat, fbx_clamp_double(fbx_prop_f64(p, 4), 0.0, 1000000.0, 1.0));
        } else if (fbx_ascii_equals_i(pname, "DoubleSided") ||
                   fbx_ascii_equals_i(pname, "TwoSided") ||
                   fbx_ascii_equals_i(pname, "DoubleSidedMaterial")) {
            if (fbx_prop_truthy(p, 4))
                rt_material3d_set_double_sided(mat, 1);
        } else if (fbx_ascii_equals_i(pname, "BackfaceCulling")) {
            if (!fbx_prop_truthy(p, 4))
                rt_material3d_set_double_sided(mat, 1);
        }
    }

    return mat;
}
