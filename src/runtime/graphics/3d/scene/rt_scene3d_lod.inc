//===----------------------------------------------------------------------===//
// Part of the Zanna project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/scene/rt_scene3d_lod.inc  (#included from rt_scene3d.c; not a
// standalone translation unit) Purpose: Scene3D per-node level-of-detail (LOD) management
//===----------------------------------------------------------------------===//
/*==========================================================================
 * LOD — Level of Detail per SceneNode3D
 *=========================================================================*/

/// @brief Register a mesh LOD to swap in at or beyond a given camera distance.
/// @details Grows the LOD array on demand (doubling, min 4 slots) and keeps
///   entries sorted ascending by `distance` so the draw path can pick the
///   highest threshold that does not exceed the current view distance. Duplicate
///   thresholds replace the existing mesh instead of creating ambiguous ties. The
///   mesh is retained here and released by `rt_scene_node3d_clear_lod` so
///   callers may drop their local reference immediately after adding.
void rt_scene_node3d_add_lod(void *obj, double distance, void *mesh) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node || !mesh)
        return;
    if (!rt_g3d_has_class(mesh, RT_G3D_MESH3D_CLASS_ID))
        return;
    distance = scene3d_finite_or(distance, 0.0);
    if (distance < 0.0)
        distance = 0.0;
    node->lod_count = scene3d_node_lod_count(node);
    if (!node->lod_levels)
        node->lod_capacity = 0;
    lod_count = node->lod_count;

    for (int32_t i = 0; i < lod_count; i++) {
        if (node->lod_levels[i].distance == distance) {
            rt_obj_retain_maybe(mesh);
            scene3d_release_class_ref(&node->lod_levels[i].mesh, RT_G3D_MESH3D_CLASS_ID);
            node->lod_levels[i].mesh = mesh;
            scene3d_node_reset_lod_view_states(node);
            scene3d_mark_spatial_dirty(node->owner_scene);
            return;
        }
    }

    if (node->lod_count >= node->lod_capacity) {
        if (node->lod_capacity >= INT32_MAX / 2) {
            rt_trap("SceneNode3D.AddLOD: too many LOD levels");
            return;
        }
        int32_t new_cap = node->lod_capacity < 4 ? 4 : node->lod_capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(node->lod_levels[0])) {
            rt_trap("SceneNode3D.AddLOD: LOD allocation overflow");
            return;
        }
        void *tmp = realloc(node->lod_levels, (size_t)new_cap * sizeof(node->lod_levels[0]));
        if (!tmp) {
            rt_trap("SceneNode3D.AddLOD: LOD allocation failed");
            return;
        }
        node->lod_levels = tmp;
        node->lod_capacity = new_cap;
    }

    /* Insert sorted by distance ascending */
    int32_t pos = node->lod_count;
    for (int32_t i = 0; i < node->lod_count; i++) {
        if (distance < node->lod_levels[i].distance) {
            pos = i;
            break;
        }
    }
    /* Shift elements right */
    for (int32_t i = node->lod_count; i > pos; i--)
        node->lod_levels[i] = node->lod_levels[i - 1];

    node->lod_levels[pos].distance = distance;
    node->lod_levels[pos].mesh = mesh;
    rt_obj_retain_maybe(mesh);
    node->lod_count++;
    scene3d_node_reset_lod_view_states(node);
    scene3d_mark_spatial_dirty(node->owner_scene);
}

/// @brief Enable or disable screen-error selection over authored LOD entries.
void rt_scene_node3d_set_auto_lod(void *obj, int8_t enabled, double screen_error_px) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    node->auto_lod_enabled = enabled ? 1 : 0;
    scene3d_node_reset_lod_view_states(node);
    if (!isfinite(screen_error_px))
        screen_error_px = 8.0;
    if (screen_error_px < 1.0)
        screen_error_px = 1.0;
    if (screen_error_px > 1000000.0)
        screen_error_px = 1000000.0;
    node->auto_lod_screen_error_px = screen_error_px;
}

/// @brief Build a camera-facing local XY quad with a UV window (impostor frames).
static void *scene3d_new_impostor_quad(rt_scene_node3d *node, double aspect, double u0, double u1) {
    double diameter = 1.0;
    double width;
    double height;
    void *mesh;
    scene_node_refresh_mesh_bounds(node);
    if (node && isfinite(node->bsphere_radius) && node->bsphere_radius > 0.0f)
        diameter = (double)node->bsphere_radius * 2.0;
    if (!isfinite(aspect) || aspect <= 0.0)
        aspect = 1.0;
    if (aspect > 128.0)
        aspect = 128.0;
    if (aspect < 1.0 / 128.0)
        aspect = 1.0 / 128.0;
    height = diameter;
    width = height * aspect;
    if (!isfinite(width) || width <= 0.0)
        width = 1.0;
    if (!isfinite(height) || height <= 0.0)
        height = 1.0;

    mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;
    rt_mesh3d_reserve(mesh, 4, 2);
    rt_mesh3d_add_vertex(mesh, -width * 0.5, -height * 0.5, 0.0, 0.0, 0.0, 1.0, u0, 1.0);
    rt_mesh3d_add_vertex(mesh, width * 0.5, -height * 0.5, 0.0, 0.0, 0.0, 1.0, u1, 1.0);
    rt_mesh3d_add_vertex(mesh, width * 0.5, height * 0.5, 0.0, 0.0, 0.0, 1.0, u1, 0.0);
    rt_mesh3d_add_vertex(mesh, -width * 0.5, height * 0.5, 0.0, 0.0, 0.0, 1.0, u0, 0.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_mesh3d_add_triangle(mesh, 0, 2, 3);
    return mesh;
}

/// @brief Release a node's multi-frame impostor mesh set (single-frame slots untouched).
static void scene3d_clear_impostor_frames(rt_scene_node3d *node) {
    if (!node)
        return;
    if (node->impostor_frame_meshes) {
        for (int32_t fi = 0; fi < node->impostor_frame_count; ++fi)
            scene3d_release_class_ref(&node->impostor_frame_meshes[fi], RT_G3D_MESH3D_CLASS_ID);
        free(node->impostor_frame_meshes);
        node->impostor_frame_meshes = NULL;
    }
    node->impostor_frame_count = 0;
    node->impostor_frame_index = 0;
}

/// @brief Build a camera-facing local XY quad used by SetImpostor.
static void *scene3d_new_impostor_mesh(rt_scene_node3d *node, void *pixels) {
    double aspect = 1.0;
    double diameter = 1.0;
    double width;
    double height;
    void *mesh;
    int64_t pw;
    int64_t ph;
    if (!pixels)
        return NULL;
    pw = rt_pixels_width(pixels);
    ph = rt_pixels_height(pixels);
    if (pw > 0 && ph > 0)
        aspect = (double)pw / (double)ph;
    scene_node_refresh_mesh_bounds(node);
    if (node && isfinite(node->bsphere_radius) && node->bsphere_radius > 0.0f)
        diameter = (double)node->bsphere_radius * 2.0;
    if (!isfinite(aspect) || aspect <= 0.0)
        aspect = 1.0;
    if (aspect > 128.0)
        aspect = 128.0;
    if (aspect < 1.0 / 128.0)
        aspect = 1.0 / 128.0;
    height = diameter;
    width = height * aspect;
    if (!isfinite(width) || width <= 0.0)
        width = 1.0;
    if (!isfinite(height) || height <= 0.0)
        height = 1.0;

    mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;
    rt_mesh3d_reserve(mesh, 4, 2);
    rt_mesh3d_add_vertex(mesh, -width * 0.5, -height * 0.5, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0);
    rt_mesh3d_add_vertex(mesh, width * 0.5, -height * 0.5, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
    rt_mesh3d_add_vertex(mesh, width * 0.5, height * 0.5, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, -width * 0.5, height * 0.5, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_mesh3d_add_triangle(mesh, 0, 2, 3);
    return mesh;
}

/// @brief Bind or clear a generated textured impostor proxy for distant draws.
void rt_scene_node3d_set_impostor(void *obj, double distance, void *pixels) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    void *mesh;
    void *material;
    if (!node)
        return;
    if (!pixels) {
        node->has_impostor = 0;
        node->impostor_distance = 0.0;
        scene3d_release_pixels_ref(&node->impostor_pixels);
        scene3d_release_class_ref(&node->impostor_mesh, RT_G3D_MESH3D_CLASS_ID);
        scene3d_release_class_ref(&node->impostor_material, RT_G3D_MATERIAL3D_CLASS_ID);
        scene3d_clear_impostor_frames(node);
        scene3d_node_reset_lod_view_states(node);
        scene3d_mark_spatial_dirty(node->owner_scene);
        return;
    }
    if (!rt_pixels_checked_impl_or_null(pixels)) {
        rt_trap("SceneNode3D.SetImpostor: pixels must be Pixels");
        return;
    }
    distance = scene3d_finite_or(distance, 0.0);
    if (distance < 0.0)
        distance = 0.0;

    mesh = scene3d_new_impostor_mesh(node, pixels);
    material = rt_material3d_new();
    if (!mesh || !material) {
        scene3d_release_ref(&mesh);
        scene3d_release_ref(&material);
        rt_trap("SceneNode3D.SetImpostor: proxy allocation failed");
        return;
    }
    rt_material3d_set_texture(material, pixels);
    rt_material3d_set_unlit(material, 1);
    rt_material3d_set_double_sided(material, 1);

    rt_obj_retain_maybe(pixels);
    scene3d_release_pixels_ref(&node->impostor_pixels);
    scene3d_release_class_ref(&node->impostor_mesh, RT_G3D_MESH3D_CLASS_ID);
    scene3d_release_class_ref(&node->impostor_material, RT_G3D_MATERIAL3D_CLASS_ID);
    node->impostor_pixels = pixels;
    node->impostor_mesh = mesh;
    node->impostor_material = material;
    node->impostor_distance = distance;
    node->has_impostor = 1;
    scene3d_clear_impostor_frames(node);
    scene3d_node_reset_lod_view_states(node);
    scene3d_mark_spatial_dirty(node->owner_scene);
}

/// @brief Bind a yaw-selected multi-frame impostor from a horizontal strip texture.
/// @details @p pixels holds @p frames equal-width views captured at yaw angles
///   `i * 2*pi/frames` (frame 0 looks down -Z toward the node). The draw path picks
///   the frame whose capture yaw is nearest the camera bearing. Passing NULL pixels
///   or frames < 2 falls back to `SetImpostor` semantics.
void rt_scene_node3d_set_impostor_frames(void *obj, double distance, void *pixels, int64_t frames) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    if (!pixels || frames < 2) {
        rt_scene_node3d_set_impostor(obj, distance, pixels);
        return;
    }
    if (!rt_pixels_checked_impl_or_null(pixels)) {
        rt_trap("SceneNode3D.SetImpostorFrames: pixels must be Pixels");
        return;
    }
    if (frames > 64)
        frames = 64;
    distance = scene3d_finite_or(distance, 0.0);
    if (distance < 0.0)
        distance = 0.0;

    int64_t pw = rt_pixels_width(pixels);
    int64_t ph = rt_pixels_height(pixels);
    double frame_aspect = 1.0;
    if (pw > 0 && ph > 0)
        frame_aspect = ((double)pw / (double)frames) / (double)ph;

    void **frame_meshes = (void **)calloc((size_t)frames, sizeof(void *));
    void *material = rt_material3d_new();
    if (!frame_meshes || !material) {
        free(frame_meshes);
        scene3d_release_ref(&material);
        rt_trap("SceneNode3D.SetImpostorFrames: proxy allocation failed");
        return;
    }
    for (int64_t i = 0; i < frames; ++i) {
        double u0 = (double)i / (double)frames;
        double u1 = (double)(i + 1) / (double)frames;
        frame_meshes[i] = scene3d_new_impostor_quad(node, frame_aspect, u0, u1);
        if (!frame_meshes[i]) {
            for (int64_t j = 0; j < i; ++j)
                scene3d_release_class_ref(&frame_meshes[j], RT_G3D_MESH3D_CLASS_ID);
            free(frame_meshes);
            scene3d_release_ref(&material);
            rt_trap("SceneNode3D.SetImpostorFrames: proxy allocation failed");
            return;
        }
    }
    rt_material3d_set_texture(material, pixels);
    rt_material3d_set_unlit(material, 1);
    rt_material3d_set_double_sided(material, 1);

    rt_obj_retain_maybe(pixels);
    scene3d_release_pixels_ref(&node->impostor_pixels);
    scene3d_release_class_ref(&node->impostor_mesh, RT_G3D_MESH3D_CLASS_ID);
    scene3d_release_class_ref(&node->impostor_material, RT_G3D_MATERIAL3D_CLASS_ID);
    scene3d_clear_impostor_frames(node);
    node->impostor_pixels = pixels;
    node->impostor_material = material;
    node->impostor_distance = distance;
    node->impostor_frame_meshes = frame_meshes;
    node->impostor_frame_count = (int32_t)frames;
    node->impostor_frame_index = 0;
    /* Frame 0 doubles as the single-mesh fallback slot for code that only knows
     * about impostor_mesh (spatial residency estimates, clear paths). */
    rt_obj_retain_maybe(frame_meshes[0]);
    node->impostor_mesh = frame_meshes[0];
    node->has_impostor = 1;
    scene3d_node_reset_lod_view_states(node);
    scene3d_mark_spatial_dirty(node->owner_scene);
}

/// @brief Last impostor frame index the draw path selected (0 for single-frame).
int64_t rt_scene_node3d_get_impostor_frame_index(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? (int64_t)node->impostor_frame_index : 0;
}

/// @brief Release every registered LOD mesh on this node and reset the count.
/// @details Preserves the underlying `lod_levels` allocation so subsequent
///   `add_lod` calls can reuse it without reallocating.
void rt_scene_node3d_clear_lod(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return;
    lod_count = scene3d_node_lod_count(node);
    for (int32_t i = 0; i < lod_count; i++)
        scene3d_release_class_ref(&node->lod_levels[i].mesh, RT_G3D_MESH3D_CLASS_ID);
    node->lod_count = 0;
    scene3d_node_reset_lod_view_states(node);
    scene3d_mark_spatial_dirty(node->owner_scene);
}

/// @brief Number of LOD levels currently registered on this node.
int64_t rt_scene_node3d_get_lod_count(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return 0;
    node->lod_count = scene3d_node_lod_count(node);
    return node->lod_count;
}

/// @brief Distance threshold (in world units) for the LOD at `index`.
/// @return Ascending-sorted threshold, or 0.0 for an out-of-range index or
///   null node; 0.0 is a safe sentinel because LOD 0 (if present) would
///   normally specify a non-zero distance anyway.
double rt_scene_node3d_get_lod_distance(void *obj, int64_t index) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return 0.0;
    lod_count = scene3d_node_lod_count(node);
    node->lod_count = lod_count;
    if (index < 0 || index >= lod_count)
        return 0.0;
    {
        double distance = scene3d_finite_or(node->lod_levels[index].distance, 0.0);
        return distance < 0.0 ? 0.0 : distance;
    }
}

/// @brief Borrowed pointer to the mesh registered at LOD `index`.
/// @return The mesh or NULL; ownership stays with the scene node.
void *rt_scene_node3d_get_lod_mesh(void *obj, int64_t index) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return NULL;
    lod_count = scene3d_node_lod_count(node);
    node->lod_count = lod_count;
    if (index < 0 || index >= lod_count)
        return NULL;
    return rt_g3d_checked_or_null(node->lod_levels[index].mesh, RT_G3D_MESH3D_CLASS_ID);
}

/// @brief Toggle resident state for the mesh backing the @p index-th LOD entry.
void rt_scene_node3d_set_lod_resident(void *obj, int64_t index, int8_t resident) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return;
    lod_count = scene3d_node_lod_count(node);
    node->lod_count = lod_count;
    if (index < 0 || index >= lod_count)
        return;
    if (!rt_g3d_has_class(node->lod_levels[index].mesh, RT_G3D_MESH3D_CLASS_ID))
        return;
    rt_mesh3d_set_resident(node->lod_levels[index].mesh, resident);
    scene3d_node_reset_lod_view_states(node);
    scene3d_mark_spatial_dirty(node->owner_scene);
}

/// @brief Return whether the @p index-th LOD mesh payload is resident.
int8_t rt_scene_node3d_get_lod_resident(void *obj, int64_t index) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return 0;
    lod_count = scene3d_node_lod_count(node);
    node->lod_count = lod_count;
    if (index < 0 || index >= lod_count)
        return 0;
    if (!rt_g3d_has_class(node->lod_levels[index].mesh, RT_G3D_MESH3D_CLASS_ID))
        return 0;
    return rt_mesh3d_get_resident(node->lod_levels[index].mesh);
}

/// @brief Estimated resident payload bytes for the @p index-th LOD mesh.
int64_t rt_scene_node3d_get_lod_resident_bytes(void *obj, int64_t index) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return 0;
    lod_count = scene3d_node_lod_count(node);
    node->lod_count = lod_count;
    if (index < 0 || index >= lod_count)
        return 0;
    if (!rt_g3d_has_class(node->lod_levels[index].mesh, RT_G3D_MESH3D_CLASS_ID))
        return 0;
    return rt_mesh3d_get_resident_bytes(node->lod_levels[index].mesh);
}
