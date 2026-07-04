//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/rt_game3d_world_api.inc  (#included from rt_game3d.c; not a
// standalone translation unit) Purpose: Game3D World3D construction plus the public
// World3D/WorldStream3D accessor surface
//===----------------------------------------------------------------------===//
/// @brief Point the world's camera at the origin from a default over-the-shoulder pose.
static void game3d_world_install_default_camera(rt_game3d_world *world) {
    rt_camera3d_look_at_components(world->camera, 0.0, 2.0, 6.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0);
}

/// @brief Create a world with default camera parameters. See header.
void *rt_game3d_world_new(rt_string title, int64_t width, int64_t height) {
    return rt_game3d_world_new_with_camera(title,
                                           width,
                                           height,
                                           RT_GAME3D_DEFAULT_FOV_DEG,
                                           RT_GAME3D_DEFAULT_NEAR,
                                           RT_GAME3D_DEFAULT_FAR);
}

/// @brief Shared World3D constructor for vertical- and horizontal-FOV camera creation.
/// @details Opens the canvas window and builds the scene, camera, physics, input, audio, and
///   effects subsystems, then applies default lighting/quality/ambient. When @p horizontal_fov is
///   non-zero, @p fov_deg is interpreted as a horizontal field of view and converted by Camera3D
///   using the window aspect; otherwise it remains the legacy vertical FOV value.
static void *game3d_world_new_with_camera_impl(rt_string title,
                                               int64_t width,
                                               int64_t height,
                                               double fov_deg,
                                               double near_plane,
                                               double far_plane,
                                               int8_t horizontal_fov) {
    double aspect;

    if (width <= 0 || height <= 0) {
        rt_trap("Game3D.World3D.New: dimensions must be positive");
        return NULL;
    }
    rt_game3d_world *world =
        (rt_game3d_world *)rt_obj_new_i64(RT_G3D_GAME3D_WORLD_CLASS_ID, (int64_t)sizeof(*world));
    if (!world) {
        rt_trap("Game3D.World3D.New: allocation failed");
        return NULL;
    }
    memset(world, 0, sizeof(*world));
    rt_obj_set_finalizer(world, game3d_world_finalize);
    world->width = width;
    world->height = height;
    world->next_entity_id = 1;
    world->dt = RT_GAME3D_DEFAULT_DT;
    world->fixed_interpolation_alpha = 0.0;
    world->worker_count = game3d_default_worker_count();
    world->jobs_enabled = world->worker_count > 1 ? 1 : 0;
    world->origin_rebase_threshold = RT_GAME3D_DEFAULT_REBASE_THRESHOLD;
    world->clear_r = 0.04;
    world->clear_g = 0.055;
    world->clear_b = 0.065;
    world->debug_axis_size = 1.0;

    world->canvas = rt_canvas3d_new(title, width, height);
    world->scene = rt_scene3d_new();
    aspect = (double)width / (double)height;
    world->camera = horizontal_fov
                        ? rt_camera3d_new_horizontal_fov(fov_deg, aspect, near_plane, far_plane)
                        : rt_camera3d_new(fov_deg, aspect, near_plane, far_plane);
    world->physics = rt_world3d_new(0.0, -9.81, 0.0);
    world->input = rt_game3d_input_new();
    world->audio = game3d_audio_new(world->camera);
    world->effects = game3d_effects_new(world->canvas, RT_GAME3D_QUALITY_BALANCED);

    if (!world->canvas || !world->scene || !world->camera || !world->physics || !world->input ||
        !world->audio || !world->effects) {
        game3d_world_release_runtime(world, 0);
        if (rt_obj_release_check0(world))
            rt_obj_free(world);
        rt_trap("Game3D.World3D.New: component allocation failed");
        return NULL;
    }
    game3d_world_install_default_camera(world);
    rt_canvas3d_set_default_lighting(world->canvas);
    rt_canvas3d_set_quality(world->canvas, RT_GAME3D_QUALITY_BALANCED);
    rt_canvas3d_set_frustum_culling(world->canvas, 1);
    rt_canvas3d_set_ambient(world->canvas, 0.28, 0.30, 0.34);
    return world;
}

/// @brief Create a world: open the canvas window and build the scene, camera, physics,
///   input, audio, and effects subsystems, then apply default lighting/quality/ambient.
///   Traps on non-positive dimensions or any component allocation failure. See header.
void *rt_game3d_world_new_with_camera(rt_string title,
                                      int64_t width,
                                      int64_t height,
                                      double fov_deg,
                                      double near_plane,
                                      double far_plane) {
    return game3d_world_new_with_camera_impl(
        title, width, height, fov_deg, near_plane, far_plane, 0);
}

/// @brief Create a world whose initial camera FOV is interpreted horizontally.
/// @details This keeps the ergonomic World3D constructor while avoiding wide-angle distortion from
///   treating a user-authored horizontal FOV as the renderer's vertical FOV. Existing
///   `NewWithCamera` behavior is unchanged; this constructor is opt-in for first-person and
///   open-world cameras that tune FOV in horizontal degrees.
void *rt_game3d_world_new_with_horizontal_camera(rt_string title,
                                                 int64_t width,
                                                 int64_t height,
                                                 double horizontal_fov_deg,
                                                 double near_plane,
                                                 double far_plane) {
    return game3d_world_new_with_camera_impl(
        title, width, height, horizontal_fov_deg, near_plane, far_plane, 1);
}

/// @brief Allocate and initialize a world-stream object bound to a world, with default
/// radii/budget.
static rt_game3d_world_stream *game3d_world_stream_create(rt_game3d_world *world,
                                                          int8_t retain_world,
                                                          const char *method) {
    if (!world) {
        rt_trap(method ? method : "Game3D.WorldStream3D.New: invalid world");
        return NULL;
    }
    rt_game3d_world_stream *stream = (rt_game3d_world_stream *)rt_obj_new_i64(
        RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID, (int64_t)sizeof(*stream));
    if (!stream) {
        rt_trap(method ? method : "Game3D.WorldStream3D.New: allocation failed");
        return NULL;
    }
    memset(stream, 0, sizeof(*stream));
    rt_obj_set_finalizer(stream, game3d_world_stream_finalize);
    stream->load_radius = 0.0;
    stream->unload_radius = 0.0;
    stream->residency_budget_bytes = -1;
    stream->retains_world = retain_world ? 1 : 0;
    if (stream->retains_world)
        game3d_assign_ref(&stream->world, world);
    else
        stream->world = world;
    game3d_world_stream_recompute(stream, -1);
    return stream;
}

/// @brief Create a WorldStream3D handle bound to a live World3D. See header.
void *rt_game3d_world_stream_new(void *world_obj) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.WorldStream3D.New: invalid world");
    if (!world)
        return NULL;
    return game3d_world_stream_create(world, 1, "Game3D.WorldStream3D.New: allocation failed");
}

/// @brief Number of streaming cells currently resident (loaded).
int64_t rt_game3d_world_stream_get_resident_cell_count(void *obj) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.get_residentCellCount: invalid stream");
    return game3d_world_stream_safe_resident_cell_count(stream);
}

/// @brief Number of terrain tiles currently resident (loaded).
int64_t rt_game3d_world_stream_get_resident_terrain_tile_count(void *obj) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.get_residentTerrainTileCount: invalid stream");
    return game3d_world_stream_safe_resident_terrain_tile_count(stream);
}

/// @brief Bounds-checked accessor for the cell at @p index (NULL if out of range).
static rt_game3d_stream_cell *game3d_world_stream_cell_at(rt_game3d_world_stream *stream,
                                                          int64_t index) {
    int32_t cell_count = game3d_world_stream_safe_cell_count(stream);
    if (!stream || index < 0 || index >= cell_count)
        return NULL;
    return &stream->cells[index];
}

/// @brief Bounds-checked accessor for the terrain tile at @p index (NULL if out of range).
static rt_game3d_stream_terrain_tile *game3d_world_stream_terrain_tile_at(
    rt_game3d_world_stream *stream, int64_t index) {
    int32_t tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
    if (!stream || index < 0 || index >= tile_count)
        return NULL;
    return &stream->terrain_tiles[index];
}

/// @brief Get the @p index-th resident terrain tile's runtime handle (NULL if out of range).
void *rt_game3d_world_stream_get_resident_terrain_tile(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getResidentTerrainTile: invalid stream");
    if (!stream || index < 0)
        return NULL;
    int64_t resident_index = 0;
    int32_t tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
    for (int32_t i = 0; i < tile_count; ++i) {
        rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
        if (!tile || !tile->resident)
            continue;
        void *terrain = game3d_stream_terrain_tile_terrain_ref(tile);
        if (!terrain)
            continue;
        if (resident_index == index)
            return terrain;
        resident_index++;
    }
    return NULL;
}

/// @brief Total number of cells declared in the stream's manifest (resident or not).
int64_t rt_game3d_world_stream_get_cell_count(void *obj) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellCount: invalid stream");
    return game3d_world_stream_safe_cell_count(stream);
}

/// @brief Name of the cell at @p index ("" if out of range).
rt_string rt_game3d_world_stream_get_cell_name(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellName: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return rt_string_ref(cell && cell->name ? cell->name : rt_const_cstr(""));
}

/// @brief World-space center of the cell at @p index as a fresh Vec3 (origin if out of range).
void *rt_game3d_world_stream_get_cell_center(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellCenter: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    if (!cell)
        return NULL;
    return rt_vec3_new(game3d_clamp_coord_or(cell->center[0], 0.0),
                       game3d_clamp_coord_or(cell->center[1], 0.0),
                       game3d_clamp_coord_or(cell->center[2], 0.0));
}

/// @brief Whether the cell at @p index is currently loaded.
int8_t rt_game3d_world_stream_get_cell_resident(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellResident: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell && cell->resident ? 1 : 0;
}

/// @brief Resident byte size of the cell at @p index (0 if not loaded / out of range).
int64_t rt_game3d_world_stream_get_cell_bytes(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellBytes: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell ? game3d_stream_cell_bytes(cell) : 0;
}

/// @brief Get parsed scene-cell material metadata, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_cell_material(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellMaterial: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return rt_string_ref(cell && cell->material ? cell->material : rt_const_cstr(""));
}

/// @brief Get parsed scene-cell optional binary sidecar path, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_cell_sidecar(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellSidecar: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return rt_string_ref(cell && cell->sidecar_path ? cell->sidecar_path : rt_const_cstr(""));
}

/// @brief Resident bytes of a scene-cell's loaded binary sidecar payload (0 if none/unloaded).
int64_t rt_game3d_world_stream_get_cell_sidecar_bytes(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getCellSidecarBytes: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell ? game3d_world_stream_nonnegative_i64(cell->sidecar_bytes) : 0;
}

/// @brief Get parsed scene-cell collision/render layer metadata, or 0 if unset/invalid.
int64_t rt_game3d_world_stream_get_cell_layer(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellLayer: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell && cell->has_layer ? game3d_stream_layer_or_valid(cell->layer, 0) : 0;
}

/// @brief Get parsed scene-cell collision mask metadata, or all bits if unset.
int64_t rt_game3d_world_stream_get_cell_collision_mask(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getCellCollisionMask: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell && cell->has_collision_mask ? cell->collision_mask : ~(int64_t)0;
}

/// @brief Return whether parsed scene-cell collision is enabled.
int8_t rt_game3d_world_stream_get_cell_collision_enabled(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getCellCollisionEnabled: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell && cell->collision_enabled ? 1 : 0;
}

/// @brief Get parsed scene-cell navigation area metadata, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_cell_nav_area(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getCellNavArea: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return rt_string_ref(cell && cell->nav_area ? cell->nav_area : rt_const_cstr(""));
}

/// @brief Get parsed scene-cell traversal cost metadata.
double rt_game3d_world_stream_get_cell_traversal_cost(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getCellTraversalCost: invalid stream");
    rt_game3d_stream_cell *cell = game3d_world_stream_cell_at(stream, index);
    return cell && isfinite(cell->traversal_cost) && cell->traversal_cost > 0.0
               ? cell->traversal_cost
               : 0.0;
}

/// @brief Total number of terrain tiles declared in the stream's manifest.
int64_t rt_game3d_world_stream_get_terrain_tile_count(void *obj) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileCount: invalid stream");
    return game3d_world_stream_safe_terrain_tile_count(stream);
}

/// @brief Name of the terrain tile at @p index ("" if out of range).
rt_string rt_game3d_world_stream_get_terrain_tile_name(void *obj, int64_t index) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.getTerrainTileName: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return rt_string_ref(tile && tile->name ? tile->name : rt_const_cstr(""));
}

/// @brief Heightmap path of the terrain tile at @p index ("" if out of range).
rt_string rt_game3d_world_stream_get_terrain_tile_heightmap(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileHeightmap: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return rt_string_ref(tile && tile->heightmap_path ? tile->heightmap_path : rt_const_cstr(""));
}

/// @brief World-space center of the terrain tile at @p index as a fresh Vec3 (origin if out of
/// range).
void *rt_game3d_world_stream_get_terrain_tile_center(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileCenter: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    if (!tile)
        return NULL;
    return rt_vec3_new(game3d_clamp_coord_or(tile->center[0], 0.0),
                       game3d_clamp_coord_or(tile->center[1], 0.0),
                       game3d_clamp_coord_or(tile->center[2], 0.0));
}

/// @brief Whether the terrain tile at @p index is currently loaded.
int8_t rt_game3d_world_stream_get_terrain_tile_resident(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileResident: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return game3d_stream_terrain_tile_terrain_ref(tile) ? 1 : 0;
}

/// @brief Resident byte size of the terrain tile at @p index (0 if not loaded / out of range).
int64_t rt_game3d_world_stream_get_terrain_tile_bytes(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileBytes: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile ? game3d_stream_terrain_tile_bytes(tile) : 0;
}

/// @brief Get parsed terrain-tile material metadata, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_terrain_tile_material(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileMaterial: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return rt_string_ref(tile && tile->material ? tile->material : rt_const_cstr(""));
}

/// @brief Get parsed terrain-tile optional binary sidecar path, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_terrain_tile_sidecar(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileSidecar: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return rt_string_ref(tile && tile->sidecar_path ? tile->sidecar_path : rt_const_cstr(""));
}

/// @brief Resident bytes of a terrain-tile's loaded binary sidecar payload (0 if none/unloaded).
int64_t rt_game3d_world_stream_get_terrain_tile_sidecar_bytes(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileSidecarBytes: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile ? game3d_world_stream_nonnegative_i64(tile->sidecar_bytes) : 0;
}

/// @brief Get parsed terrain-tile collision/render layer metadata.
int64_t rt_game3d_world_stream_get_terrain_tile_layer(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileLayer: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile ? game3d_stream_layer_or_valid(tile->layer, RT_GAME3D_LAYER_WORLD) : 0;
}

/// @brief Get parsed terrain-tile collision mask metadata.
int64_t rt_game3d_world_stream_get_terrain_tile_collision_mask(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileCollisionMask: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile && tile->has_collision_mask ? tile->collision_mask : ~(int64_t)0;
}

/// @brief Return whether parsed terrain-tile collision is enabled.
int8_t rt_game3d_world_stream_get_terrain_tile_collision_enabled(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileCollisionEnabled: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile && tile->collision_enabled ? 1 : 0;
}

/// @brief Get parsed terrain-tile navigation area metadata, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_terrain_tile_nav_area(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileNavArea: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return rt_string_ref(tile && tile->nav_area ? tile->nav_area : rt_const_cstr(""));
}

/// @brief Get the parsed terrain-tile pathfinding traversal cost, or 0 for an invalid/missing tile.
double rt_game3d_world_stream_get_terrain_tile_traversal_cost(void *obj, int64_t index) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.getTerrainTileTraversalCost: invalid stream");
    rt_game3d_stream_terrain_tile *tile = game3d_world_stream_terrain_tile_at(stream, index);
    return tile && isfinite(tile->traversal_cost) && tile->traversal_cost > 0.0
               ? tile->traversal_cost
               : 0.0;
}

/// @brief Number of cells/tiles queued for loading but not yet resident.
int64_t rt_game3d_world_stream_get_pending_request_count(void *obj) {
    rt_game3d_world_stream *stream = game3d_world_stream_checked(
        obj, "Game3D.WorldStream3D.get_pendingRequestCount: invalid stream");
    return stream ? game3d_world_stream_nonnegative_i64(stream->pending_request_count) : 0;
}

/// @brief Total resident bytes across all loaded cells and terrain tiles.
int64_t rt_game3d_world_stream_get_resident_bytes(void *obj) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.get_residentBytes: invalid stream");
    return stream ? game3d_world_stream_nonnegative_i64(stream->resident_bytes) : 0;
}

/// @brief Set the streaming focus point (usually the camera/player position) from a Vec3.
void rt_game3d_world_stream_set_center(void *obj, void *position) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.setCenter: invalid stream");
    double pos[3];
    if (!stream)
        return;
    if (!game3d_read_vec3(position, pos, "Game3D.WorldStream3D.setCenter: center must be Vec3"))
        return;
    stream->center[0] = game3d_clamp_coord_or(pos[0], 0.0);
    stream->center[1] = game3d_clamp_coord_or(pos[1], 0.0);
    stream->center[2] = game3d_clamp_coord_or(pos[2], 0.0);
}

/// @brief Set the load and unload radii (in world units) that bound which content streams in/out.
/// @details The unload radius should exceed the load radius to provide hysteresis against
/// thrashing.
void rt_game3d_world_stream_set_radii(void *obj, double load_radius, double unload_radius) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.setRadii: invalid stream");
    if (!stream)
        return;
    load_radius = game3d_nonnegative_clamped_or(load_radius, 0.0, RT_GAME3D_COORD_ABS_MAX);
    unload_radius =
        game3d_nonnegative_clamped_or(unload_radius, load_radius, RT_GAME3D_COORD_ABS_MAX);
    {
        double min_margin = fmax(1.0, load_radius * 0.05);
        if (unload_radius < load_radius + min_margin)
            unload_radius = load_radius + min_margin;
        if (unload_radius > RT_GAME3D_COORD_ABS_MAX)
            unload_radius = RT_GAME3D_COORD_ABS_MAX;
    }
    stream->load_radius = load_radius;
    stream->unload_radius = unload_radius;
    game3d_world_stream_recompute(stream, -1);
}

/// @brief Set the maximum total bytes of streamed content allowed resident at once.
void rt_game3d_world_stream_set_residency_budget(void *obj, int64_t bytes) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.setResidencyBudget: invalid stream");
    if (!stream)
        return;
    stream->residency_budget_bytes = bytes < 0 ? -1 : bytes;
    game3d_world_stream_recompute(stream, -1);
}

/// @brief Mount a tiled-terrain manifest, parsing its tile table so terrain streams with the world.
void rt_game3d_world_stream_mount_tiled_terrain(void *obj, rt_string manifest_path) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.mountTiledTerrain: invalid stream");
    if (!stream)
        return;
    game3d_world_stream_clear_terrain_tiles(stream);
    game3d_assign_ref((void **)&stream->terrain_manifest,
                      manifest_path ? manifest_path : rt_const_cstr(""));
    (void)game3d_world_stream_parse_terrain_manifest(stream);
    game3d_world_stream_recompute(stream, -1);
}

/// @brief Mount a cells manifest, parsing its cell table so scene chunks stream with the world.
void rt_game3d_world_stream_mount_cells(void *obj, rt_string manifest_path) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.mountCells: invalid stream");
    if (!stream)
        return;
    game3d_world_stream_clear_cells(stream);
    game3d_assign_ref((void **)&stream->cells_manifest,
                      manifest_path ? manifest_path : rt_const_cstr(""));
    (void)game3d_world_stream_parse_cells_manifest(stream);
    game3d_world_stream_recompute(stream, -1);
}

/// @brief Advance the streamer one frame: compute the budget and reconcile resident content.
/// @details Call once per frame after updating the center; loads/unloads cells and terrain tiles to
///          track the focus point within the configured radii and budget.
void rt_game3d_world_stream_update(void *obj, double dt) {
    rt_game3d_world_stream *stream =
        game3d_world_stream_checked(obj, "Game3D.WorldStream3D.update: invalid stream");
    if (!stream)
        return;
    dt = game3d_finite_or(dt, 0.0);
    game3d_world_stream_recompute(stream, game3d_world_stream_update_load_budget(dt));
}

/// @brief Tear down the world's runtime (despawn entities, release subsystems) and mark
///   it destroyed; idempotent. See header.
void rt_game3d_world_destroy(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked_allow_destroyed(obj, "Game3D.World3D.destroy: invalid world");
    if (!world || world->destroyed)
        return;
    game3d_world_release_runtime(world, 1);
    game3d_world_free_registries(world);
    world->destroyed = 1;
}

/// @brief True if the world has been destroyed. See header.
int8_t rt_game3d_world_is_destroyed(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked_allow_destroyed(obj, "Game3D.World3D.isDestroyed: invalid world");
    return world && world->destroyed ? 1 : 0;
}

/// @brief Get the world's rendering canvas (NULL if invalid).
void *rt_game3d_world_get_canvas(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Canvas: invalid world");
    return world ? rt_g3d_checked_or_null(world->canvas, RT_G3D_CANVAS3D_CLASS_ID) : NULL;
}

/// @brief Get the world's active camera (NULL if invalid).
void *rt_game3d_world_get_camera(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Camera: invalid world");
    return world ? rt_g3d_checked_or_null(world->camera, RT_G3D_CAMERA3D_CLASS_ID) : NULL;
}

/// @brief Get the world's scene graph (NULL if invalid).
void *rt_game3d_world_get_scene(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Scene: invalid world");
    return world ? rt_g3d_checked_or_null(world->scene, RT_G3D_SCENE3D_CLASS_ID) : NULL;
}

/// @brief Get the world's physics simulation (NULL if invalid).
void *rt_game3d_world_get_physics(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Physics: invalid world");
    return world ? rt_g3d_checked_or_null(world->physics, RT_G3D_WORLD3D_CLASS_ID) : NULL;
}

/// @brief Get the world's input-state object (NULL if invalid).
void *rt_game3d_world_get_input(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Input: invalid world");
    return world ? rt_g3d_checked_or_null(world->input, RT_G3D_GAME3D_INPUT_CLASS_ID) : NULL;
}

/// @brief Get the world's audio subsystem (NULL if invalid).
void *rt_game3d_world_get_audio(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Audio: invalid world");
    return world ? rt_g3d_checked_or_null(world->audio, RT_G3D_GAME3D_SOUND_CLASS_ID) : NULL;
}

/// @brief Get the world's effect registry (NULL if invalid).
void *rt_game3d_world_get_effects(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Effects: invalid world");
    return world ? rt_g3d_checked_or_null(world->effects, RT_G3D_GAME3D_EFFECTS_CLASS_ID) : NULL;
}

/// @brief Get the world's owned stream controller, creating it on first access.
void *rt_game3d_world_get_stream(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_stream: invalid world");
    if (!world)
        return NULL;
    if (world->stream && !rt_g3d_has_class(world->stream, RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID))
        world->stream = NULL;
    if (!world->stream) {
        rt_game3d_world_stream *stream =
            game3d_world_stream_create(world, 0, "Game3D.World3D.get_stream: allocation failed");
        if (!stream)
            return NULL;
        game3d_assign_ref(&world->stream, stream);
        game3d_release_ref((void **)&stream);
    }
    return world->stream;
}

/// @brief Get the most recent frame delta in seconds (0 if invalid).
double rt_game3d_world_get_dt(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Dt: invalid world");
    return world ? game3d_nonnegative_clamped_or(world->dt, 0.0, RT_GAME3D_MAX_DT) : 0.0;
}

/// @brief Get total elapsed time in seconds since the world started (0 if invalid).
double rt_game3d_world_get_elapsed(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Elapsed: invalid world");
    return world ? game3d_nonnegative_or(world->elapsed, 0.0) : 0.0;
}

/// @brief Get the current frame counter (0 if invalid).
int64_t rt_game3d_world_get_frame(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.get_Frame: invalid world");
    return world && world->frame > 0 ? world->frame : 0;
}

/// @brief Get how many fixed simulation steps were discarded by the spiral guard.
int64_t rt_game3d_world_get_dropped_fixed_steps(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_DroppedFixedSteps: invalid world");
    return world && world->dropped_fixed_steps > 0 ? world->dropped_fixed_steps : 0;
}

/// @brief Get the fixed-timestep interpolation fraction for the current render frame.
/// @details `runFixed` and `runFixedWithOverlay` set this to the remaining accumulator divided by
///   the fixed step after all simulation steps for the displayed frame have run. Render code can
///   use the value in `[0, 1)` to blend visual-only state between the previous and current fixed
///   simulation snapshots without changing deterministic physics stepping.
double rt_game3d_world_get_fixed_interpolation_alpha(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_fixedInterpolationAlpha: invalid world");
    if (!world || !isfinite(world->fixed_interpolation_alpha))
        return 0.0;
    if (world->fixed_interpolation_alpha < 0.0)
        return 0.0;
    if (world->fixed_interpolation_alpha >= 1.0)
        return 0.999999;
    return world->fixed_interpolation_alpha;
}

/// @brief Count spawned Entity3D objects currently owned by the world.
int64_t rt_game3d_world_get_entity_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_entityCount: invalid world");
    return world ? game3d_world_safe_entity_count(world) : 0;
}

/// @brief Count physics bodies currently registered through spawned entities.
int64_t rt_game3d_world_get_body_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_bodyCount: invalid world");
    void *physics = world ? rt_g3d_checked_or_null(world->physics, RT_G3D_WORLD3D_CLASS_ID) : NULL;
    return physics ? rt_world3d_body_count(physics) : 0;
}

/// @brief Count main 3D draw submissions queued by the latest ended frame.
int64_t rt_game3d_world_get_draw_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_drawCount: invalid world");
    void *canvas = world ? rt_g3d_checked_or_null(world->canvas, RT_G3D_CANVAS3D_CLASS_ID) : NULL;
    return canvas ? rt_canvas3d_get_draw_count(canvas) : 0;
}

/// @brief Count drawable scene nodes submitted by the latest scene draw.
int64_t rt_game3d_world_get_visible_node_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_visibleNodeCount: invalid world");
    void *scene = world ? rt_g3d_checked_or_null(world->scene, RT_G3D_SCENE3D_CLASS_ID) : NULL;
    return scene ? rt_scene3d_get_visible_node_count(scene) : 0;
}

/// @brief Count draw submissions skipped by latest visibility culling.
int64_t rt_game3d_world_get_occluded_draw_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_occludedDrawCount: invalid world");
    void *canvas = world ? rt_g3d_checked_or_null(world->canvas, RT_G3D_CANVAS3D_CLASS_ID) : NULL;
    return canvas ? rt_canvas3d_get_occluded_draw_count(canvas) : 0;
}

/// @brief Count draw submissions skipped specifically by CPU frustum culling.
int64_t rt_game3d_world_get_frustum_culled_draw_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_frustumCulledDrawCount: invalid world");
    void *canvas = world ? rt_g3d_checked_or_null(world->canvas, RT_G3D_CANVAS3D_CLASS_ID) : NULL;
    return canvas ? rt_canvas3d_get_frustum_culled_draw_count(canvas) : 0;
}

/// @brief Count draw submissions skipped specifically by the CPU occlusion grid.
int64_t rt_game3d_world_get_cpu_occluded_draw_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_cpuOccludedDrawCount: invalid world");
    void *canvas = world ? rt_g3d_checked_or_null(world->canvas, RT_G3D_CANVAS3D_CLASS_ID) : NULL;
    return canvas ? rt_canvas3d_get_cpu_occluded_draw_count(canvas) : 0;
}

/// @brief Count bytes resident in the world-owned stream controller, if any.
int64_t rt_game3d_world_get_stream_resident_bytes(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_streamResidentBytes: invalid world");
    void *stream =
        world ? rt_g3d_checked_or_null(world->stream, RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID) : NULL;
    return stream ? rt_game3d_world_stream_get_resident_bytes(stream) : 0;
}

/// @brief Get the configured worker count for internal deterministic jobs.
int64_t rt_game3d_world_get_worker_count(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_workerCount: invalid world");
    return world ? game3d_clamp_worker_count(world->worker_count) : 1;
}

/// @brief True when internal jobs are allowed to use worker threads.
int8_t rt_game3d_world_get_jobs_enabled(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_jobsEnabled: invalid world");
    return world && world->jobs_enabled ? 1 : 0;
}

/// @brief Set the internal worker count; values <= 1 keep jobs disabled.
void rt_game3d_world_set_worker_count(void *obj, int64_t worker_count) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.setWorkerCount: invalid world");
    if (!world)
        return;
    world->worker_count = game3d_clamp_worker_count(worker_count);
    world->jobs_enabled = world->worker_count > 1 ? 1 : 0;
    if (world->worker_count <= 1) {
        game3d_world_release_job_pool(world);
    } else if (world->job_pool && rt_threadpool_get_size(world->job_pool) != world->worker_count) {
        game3d_world_release_job_pool(world);
    }
}

/// @brief True when floating-origin rebasing is enabled.
int8_t rt_game3d_world_get_floating_origin(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_floatingOrigin: invalid world");
    return world && world->floating_origin ? 1 : 0;
}

/// @brief Enable or disable camera-relative floating-origin rebasing.
void rt_game3d_world_set_floating_origin(void *obj, int8_t enabled) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.set_floatingOrigin: invalid world");
    if (world) {
        int8_t next = enabled ? 1 : 0;
        if (world->floating_origin != next)
            game3d_canvas_clear_temporal_state(world->canvas);
        world->floating_origin = next;
    }
}

/// @brief Get the accumulated world-origin offset as a Vec3.
void *rt_game3d_world_get_world_origin(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.get_worldOrigin: invalid world");
    if (!world)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(game3d_clamp_coord_or(world->world_origin[0], 0.0),
                       game3d_clamp_coord_or(world->world_origin[1], 0.0),
                       game3d_clamp_coord_or(world->world_origin[2], 0.0));
}

/// @brief Set the camera-distance threshold that triggers a floating-origin rebase.
void rt_game3d_world_set_origin_rebase_threshold(void *obj, double meters) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.setOriginRebaseThreshold: invalid world");
    if (world)
        world->origin_rebase_threshold = game3d_rebase_threshold_or_default(meters);
}

/// @brief Manually apply one floating-origin rebase boundary between frames.
void rt_game3d_world_rebase_origin(void *obj, double dx, double dy, double dz) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.rebaseOrigin: invalid world");
    if (world && game3d_canvas_in_frame(world->canvas)) {
        rt_trap("Game3D.World3D.rebaseOrigin: must be called between frames");
        return;
    }
    double delta[3] = {dx, dy, dz};
    game3d_world_apply_origin_rebase(world, delta);
}

/// @brief Spawn an entity (and its child tree) into the world; returns the entity. See header.
void *rt_game3d_world_spawn(void *world_obj, void *entity_obj) {
    rt_game3d_world *world = game3d_world_checked(world_obj, "Game3D.World3D.spawn: invalid world");
    rt_game3d_entity *entity =
        game3d_entity_checked(entity_obj, "Game3D.World3D.spawn: entity must be Entity3D");
    int64_t next_id;
    if (!world || !entity)
        return entity_obj;
    next_id = world->next_entity_id;
    if (game3d_world_spawn_entity_tree(world, entity, 1, &next_id))
        world->next_entity_id = next_id;
    return entity_obj;
}

/// @brief Spawn an invisible static heightfield collider from a Pixels heightmap.
/// @details Streamed terrain auto-generates a `*_heightfield_collider` entity per
///   tile; this exposes the same mechanism for a standalone `Terrain3D` so its
///   surface becomes solid to physics bodies and character controllers. The
///   heightfield is centered on the collider entity (local X/Z span
///   `[-(w-1)*sx/2, +(w-1)*sx/2]`), so pass the terrain's center as
///   `(pos_x, pos_y, pos_z)` — `(terrainWorldMaxX/2, 0, terrainWorldMaxZ/2)` for a
///   terrain whose corner sits at the world origin — to align physics with the
///   visual mesh. The position is applied before spawn because a static body
///   drives its node (not vice-versa), so it cannot be repositioned afterwards.
///   Returns the spawned Entity3D (NULL on invalid world/heightmap or allocation
///   failure). See header.
void *rt_game3d_world_spawn_heightfield_collider(void *world_obj,
                                                 void *heightmap,
                                                 double scale_x,
                                                 double scale_y,
                                                 double scale_z,
                                                 double pos_x,
                                                 double pos_y,
                                                 double pos_z) {
    rt_game3d_world *world = game3d_world_checked(
        world_obj, "Game3D.World3D.spawnHeightfieldCollider: invalid world");
    void *collider;
    void *body;
    void *entity;
    if (!world || world->destroyed || !world->physics)
        return NULL;
    if (!heightmap) {
        rt_trap("Game3D.World3D.spawnHeightfieldCollider: heightmap must be non-null");
        return NULL;
    }

    collider = rt_collider3d_new_heightfield(heightmap, scale_x, scale_y, scale_z);
    if (!collider)
        return NULL;
    body = rt_body3d_new(0.0);
    if (!body) {
        game3d_release_ref(&collider);
        return NULL;
    }
    rt_body3d_set_static(body, 1);
    rt_body3d_set_collision_layer(body, RT_GAME3D_LAYER_WORLD);
    rt_body3d_set_collider(body, collider);

    entity = rt_game3d_entity_new();
    if (entity) {
        rt_game3d_entity_set_name(entity, rt_const_cstr("terrain_heightfield_collider"));
        rt_game3d_entity_set_position(entity,
                                      game3d_clamp_coord_or(pos_x, 0.0),
                                      game3d_clamp_coord_or(pos_y, 0.0),
                                      game3d_clamp_coord_or(pos_z, 0.0));
        rt_game3d_entity_attach_body(entity, body);
        rt_game3d_world_spawn(world_obj, entity);
    }
    game3d_release_ref(&body);
    game3d_release_ref(&collider);
    return entity;
}

/// @brief Despawn an entity (and its child tree); stale handles no-op, while live
///   unspawned entities still trap if they do not belong to this world. See header.
void rt_game3d_world_despawn(void *world_obj, void *entity_obj) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.World3D.despawn: invalid world");
    rt_game3d_entity *entity =
        game3d_entity_checked(entity_obj, "Game3D.World3D.despawn: entity must be Entity3D");
    if (!world || !entity)
        return;
    if (!entity->spawned || entity->world != world) {
        rt_trap("Game3D.World3D.despawn: entity is not spawned in this world");
        return;
    }
    game3d_world_despawn_entity_tree(world, entity, 1);
}

/// @brief Find a scene node by name (NULL if absent). See header.
void *rt_game3d_world_find_node(void *obj, rt_string name) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.findNode: invalid world");
    return world && world->scene ? rt_scene3d_find(world->scene, name) : NULL;
}

/// @brief Find a scene node by name and wrap the nullable result in an Option.
/// @details Uses the world's current scene graph lookup; absence is represented
///          as `None` instead of a NULL scene-node handle.
/// @param obj World3D handle.
/// @param name Exact scene-node name to match.
/// @return `Some(SceneNode3D)` when a node exists, otherwise `None`.
void *rt_game3d_world_find_node_option(void *obj, rt_string name) {
    void *node = rt_game3d_world_find_node(obj, name);
    return node ? rt_option_some(node) : rt_option_none();
}

/// @brief Find a spawned entity by name using the world's name index; NULL if absent.
void *rt_game3d_world_find_entity(void *obj, rt_string name) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.findEntity: invalid world");
    const char *needle = name ? rt_string_cstr(name) : "";
    if (!world || !needle)
        return NULL;
    if (!world->name_index_valid)
        game3d_world_rebuild_name_index(world);
    rt_game3d_entity *indexed = game3d_world_name_index_find(world, needle);
    if (indexed && indexed->alive && indexed->spawned && indexed->world == world)
        return indexed;
    if (indexed)
        world->name_index_valid = 0;
    if (world->name_index_valid)
        return NULL;
    int32_t entity_count = game3d_world_safe_entity_count(world);
    for (int32_t i = 0; i < entity_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        if (!entity || !entity->alive || !entity->spawned || entity->world != world)
            continue;
        const char *entity_name = entity && entity->name ? rt_string_cstr(entity->name) : "";
        if (entity_name && strcmp(entity_name, needle) == 0)
            return entity;
    }
    return NULL;
}

/// @brief Find a spawned entity by name and wrap the nullable result in an Option.
/// @details Uses the same name-index and fallback scan as
///          `rt_game3d_world_find_entity`, while exposing absence as `None`.
/// @param obj World3D handle.
/// @param name Exact entity name to match.
/// @return `Some(Entity3D)` when a spawned entity exists, otherwise `None`.
void *rt_game3d_world_find_entity_option(void *obj, rt_string name) {
    void *entity = rt_game3d_world_find_entity(obj, name);
    return entity ? rt_option_some(entity) : rt_option_none();
}

/// @brief Install a camera controller (or NULL to clear); traps on a non-Game3D
///   controller. See header.
void rt_game3d_world_set_camera_controller(void *obj, void *controller) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.setCameraController: invalid world");
    if (!game3d_camera_controller_is_valid(controller)) {
        rt_trap("Game3D.World3D.setCameraController: controller must be a built-in Game3D camera "
                "controller");
        return;
    }
    if (world) {
        void *controller_guard = NULL;
        rt_game3d_world *previous_world =
            (rt_game3d_world *)game3d_camera_controller_get_world_ref(controller);
        if (controller) {
            controller_guard = controller;
            rt_obj_retain_maybe(controller_guard);
        }
        if (previous_world && previous_world != world &&
            previous_world->camera_controller == controller)
            game3d_release_ref(&previous_world->camera_controller);
        if (world->camera_controller && world->camera_controller != controller)
            game3d_camera_controller_clear_world_ref_if(world->camera_controller, world);
        if (controller)
            game3d_camera_controller_bind_world_ref(controller, world);
        game3d_assign_ref(&world->camera_controller, controller);
        game3d_release_ref(&controller_guard);
    }
}

/// @brief Aim the camera at a Vec3 target from its current position; traps on a non-Vec3
///   target. See header.
void rt_game3d_world_look_at(void *obj, void *target) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.lookAt: invalid world");
    if (!rt_g3d_is_vec3(target)) {
        rt_trap("Game3D.World3D.lookAt: target must be Vec3");
        return;
    }
    if (world && world->camera) {
        double eye[3];
        if (rt_camera3d_get_position_components(world->camera, &eye[0], &eye[1], &eye[2])) {
            rt_camera3d_look_at_components(world->camera,
                                           eye[0],
                                           eye[1],
                                           eye[2],
                                           rt_vec3_x(target),
                                           rt_vec3_y(target),
                                           rt_vec3_z(target),
                                           0.0,
                                           1.0,
                                           0.0);
        }
    }
}

/// @brief Record a window resize and update the camera's render aspect ratio; traps on
///   non-positive dimensions. See header.
void rt_game3d_world_on_resize(void *obj, int64_t width, int64_t height) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.onResize: invalid world");
    if (width <= 0 || height <= 0) {
        rt_trap("Game3D.World3D.onResize: dimensions must be positive");
        return;
    }
    if (!world)
        return;
    world->width = width;
    world->height = height;
    if (world->canvas)
        rt_canvas3d_resize(world->canvas, width, height);
    if (world->camera)
        game3d_camera_sync_render_aspect(world->camera, (double)width / (double)height);
}

/// @brief Set the global ambient light color. See header.
void rt_game3d_world_set_ambient(void *obj, double r, double g, double b) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setAmbient: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_ambient(world->canvas, r, g, b);
}

/// @brief Enable/disable image-based lighting from the world's skybox environment.
void rt_game3d_world_set_ibl_enabled(void *obj, int8_t enabled) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.setIblEnabled: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_ibl_enabled(world->canvas, enabled);
}

/// @brief True when image-based lighting is enabled for the world's canvas.
int8_t rt_game3d_world_get_ibl_enabled(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.getIblEnabled: invalid world");
    return (world && world->canvas) ? rt_canvas3d_get_ibl_enabled(world->canvas) : 0;
}

/// @brief Scale the environment lighting contribution for the world's canvas.
void rt_game3d_world_set_ibl_intensity(void *obj, double intensity) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.setIblIntensity: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_ibl_intensity(world->canvas, intensity);
}

/// @brief Current environment lighting intensity for the world's canvas.
double rt_game3d_world_get_ibl_intensity(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.getIblIntensity: invalid world");
    return (world && world->canvas) ? rt_canvas3d_get_ibl_intensity(world->canvas) : 0.0;
}

/// @brief Bind a light into the given canvas light slot. See header.
void rt_game3d_world_add_light(void *obj, int64_t slot, void *light) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.addLight: invalid world");
    if (light && !rt_g3d_has_class(light, RT_G3D_LIGHT3D_CLASS_ID)) {
        rt_trap("Game3D.World3D.addLight: light must be Light3D or null");
        return;
    }
    if (world && world->canvas)
        rt_canvas3d_set_light(world->canvas, slot, light);
}

/// @brief Clear all bound light slots. See header.
void rt_game3d_world_clear_lights(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.clearLights: invalid world");
    if (world && world->canvas)
        rt_canvas3d_clear_lights(world->canvas);
}

/// @brief Defensively validate the canvas's skybox slot: drop it (and invalidate the
///   skybox cache) when it is missing, no longer a CubeMap3D, or an incomplete cubemap.
static void game3d_world_repair_canvas_skybox(rt_game3d_world *world) {
#ifdef VIPER_ENABLE_GRAPHICS
    rt_canvas3d *canvas;
    if (!world || !world->canvas || !rt_g3d_has_class(world->canvas, RT_G3D_CANVAS3D_CLASS_ID))
        return;
    canvas = (rt_canvas3d *)world->canvas;
    if (!canvas->skybox)
        return;
    if (!rt_g3d_has_class(canvas->skybox, RT_G3D_CUBEMAP3D_CLASS_ID)) {
        canvas->skybox = NULL;
        rt_canvas3d_invalidate_skybox_cache(canvas);
        return;
    }
    if (!rt_cubemap3d_is_complete(canvas->skybox)) {
        if (rt_obj_release_check0(canvas->skybox))
            rt_obj_free(canvas->skybox);
        canvas->skybox = NULL;
        rt_canvas3d_invalidate_skybox_cache(canvas);
    }
#else
    (void)world;
#endif
}

/// @brief Set the skybox from a cubemap, or clear it when passed NULL. See header.
void rt_game3d_world_set_skybox(void *obj, void *cubemap) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setSkybox: invalid world");
    game3d_world_repair_canvas_skybox(world);
#ifdef VIPER_ENABLE_GRAPHICS
    if (cubemap && (!rt_g3d_has_class(cubemap, RT_G3D_CUBEMAP3D_CLASS_ID) ||
                    !rt_cubemap3d_is_complete(cubemap))) {
#else
    if (cubemap && !rt_g3d_has_class(cubemap, RT_G3D_CUBEMAP3D_CLASS_ID)) {
#endif
        rt_trap("Game3D.World3D.setSkybox: cubemap must be CubeMap3D or null");
        return;
    }
    if (world && world->canvas) {
        if (cubemap)
            rt_canvas3d_set_skybox(world->canvas, cubemap);
        else
            rt_canvas3d_clear_skybox(world->canvas);
    }
}

/// @brief Configure distance fog (RGB color + near/far planes). See header.
void rt_game3d_world_set_fog(
    void *obj, double r, double g, double b, double near_plane, double far_plane) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setFog: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_fog(world->canvas, near_plane, far_plane, r, g, b);
}

/// @brief Apply a render quality preset to the canvas and rebuild the quality-scaled
///   post-FX stack. See header.
void rt_game3d_world_set_quality(void *obj, int64_t quality) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.setQuality: invalid world");
    void *new_postfx;
    if (!world || !world->canvas)
        return;
    rt_canvas3d_set_quality(world->canvas, quality);
    rt_game3d_effects *effects = (rt_game3d_effects *)world->effects;
    if (effects) {
        new_postfx = rt_postfx3d_new_quality(world->canvas, quality);
        if (new_postfx) {
            game3d_assign_ref(&effects->postfx, new_postfx);
            rt_canvas3d_set_post_fx(world->canvas, effects->postfx);
            game3d_release_ref(&new_postfx);
        } else if (effects->postfx) {
            rt_canvas3d_set_post_fx(world->canvas, effects->postfx);
        }
    }
}

/// @brief Bake a NavMesh3D from the world's current scene.
void *rt_game3d_world_bake_nav_mesh(
    void *obj, double agent_radius, double agent_height, double max_slope, double cell_size) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.World3D.bakeNavMesh: invalid world");
    if (!world || !world->scene)
        return NULL;
    return rt_navmesh3d_bake(world->scene, agent_radius, agent_height, max_slope, cell_size);
}

/// @brief Bake a tiled NavMesh3D from the world's current scene.
void *rt_game3d_world_bake_tiled_nav_mesh(void *obj,
                                          double tile_size,
                                          double agent_radius,
                                          double agent_height,
                                          double max_slope,
                                          double cell_size) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.World3D.bakeTiledNavMesh: invalid world");
    if (!world || !world->scene)
        return NULL;
    return rt_navmesh3d_bake_tiled(
        world->scene, tile_size, agent_radius, agent_height, max_slope, cell_size);
}
