//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/rt_game3d_streaming.inc  (#included from rt_game3d.c; not a standalone translation unit)
// Purpose: Game3D WorldStream3D manifest parsing, cell/terrain load-unload, and recompute
//===----------------------------------------------------------------------===//
static int64_t game3d_stream_manifest_bytes(rt_string value);
static int64_t game3d_i64_saturating_add(int64_t a, int64_t b);

#if defined(_WIN32)
#define game3d_stream_fseek(fp, off, whence) _fseeki64((fp), (off), (whence))
#define game3d_stream_ftell(fp) _ftelli64((fp))
#else
#define game3d_stream_fseek(fp, off, whence) fseeko((fp), (off_t)(off), (whence))
#define game3d_stream_ftell(fp) ftello((fp))
#endif

/// @brief Whether a parsed JSON value is a map/object.
static int game3d_json_is_map(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_MAP_CLASS_ID;
}

/// @brief Whether a parsed JSON value is an array/sequence.
static int game3d_json_is_seq(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_SEQ_CLASS_ID;
}

/// @brief Look up @p key in a parsed JSON map (NULL if absent or not a map).
static void *game3d_json_get(void *map, const char *key) {
    if (!game3d_json_is_map(map) || !key)
        return NULL;
    rt_string k = rt_string_from_bytes(key, strlen(key));
    if (!k)
        return NULL;
    void *value = rt_map_get(map, k);
    rt_string_unref(k);
    return value;
}

/// @brief Coerce a parsed JSON value to a double; returns 0 if it is not numeric.
static int game3d_json_number(void *obj, double *out) {
    if (!out)
        return 0;
    double f = 0.0;
    if (rt_box_try_to_f64(obj, &f)) {
        *out = f;
        return 1;
    }
    int64_t i = 0;
    if (rt_box_try_to_i64(obj, &i)) {
        *out = (double)i;
        return 1;
    }
    return 0;
}

/// @brief Read map[key] as an int64, returning @p fallback if absent or non-numeric.
static int64_t game3d_json_i64_or(void *map, const char *key, int64_t fallback) {
    void *value = game3d_json_get(map, key);
    int64_t i = 0;
    if (rt_box_try_to_i64(value, &i))
        return i;
    double f = 0.0;
    if (rt_box_try_to_f64(value, &f) && isfinite(f)) {
        if (f <= (double)INT64_MIN)
            return INT64_MIN;
        if (f >= (double)INT64_MAX)
            return INT64_MAX;
        return (int64_t)f;
    }
    return fallback;
}

/// @brief Read map[key] as a double, returning @p fallback if absent or non-numeric.
static double game3d_json_f64_or(void *map, const char *key, double fallback) {
    double v = 0.0;
    if (game3d_json_number(game3d_json_get(map, key), &v) && isfinite(v))
        return v;
    return fallback;
}

/// @brief Read map[key] as a boolean, accepting boxed bools and numeric 0/1 values.
static int8_t game3d_json_bool_or(void *map, const char *key, int8_t fallback) {
    void *value = game3d_json_get(map, key);
    int8_t b = 0;
    if (rt_box_try_to_i1(value, &b))
        return b ? 1 : 0;
    int64_t i = 0;
    if (rt_box_try_to_i64(value, &i))
        return i != 0 ? 1 : 0;
    double f = 0.0;
    if (rt_box_try_to_f64(value, &f) && isfinite(f))
        return f != 0.0 ? 1 : 0;
    return fallback;
}

/// @brief Read map[key] as a borrowed string reference (NULL if absent or not a string).
static rt_string game3d_json_string_ref(void *map, const char *key) {
    void *value = game3d_json_get(map, key);
    if (!value || !rt_string_is_handle(value))
        return NULL;
    return rt_string_ref((rt_string)value);
}

/// @brief Read map[key] as a 3-element number array into @p out, falling back to @p f on absence.
static int game3d_json_vec3_or(void *map,
                               const char *key,
                               double out[3],
                               const double fallback[3]) {
    if (!out)
        return 0;
    out[0] = fallback ? fallback[0] : 0.0;
    out[1] = fallback ? fallback[1] : 0.0;
    out[2] = fallback ? fallback[2] : 0.0;
    void *seq = game3d_json_get(map, key);
    if (!game3d_json_is_seq(seq) || rt_seq_len(seq) < 3)
        return 0;
    double v[3];
    for (int i = 0; i < 3; ++i) {
        if (!game3d_json_number(rt_seq_get(seq, i), &v[i]) || !isfinite(v[i]))
            return 0;
    }
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
    return 1;
}

/// @brief Read optional manifest text through the asset manager or filesystem without trapping.
/// @details Packed/embedded asset lookup is tried first so `asset://` stream manifests work. The
///          byte cap is enforced for both sources; missing or oversized manifests return NULL.
static rt_string game3d_read_text_file_no_trap(rt_string path) {
    const char *cpath = path ? rt_string_cstr(path) : NULL;
    if (!cpath || cpath[0] == '\0')
        return NULL;
    size_t asset_size = 0;
    uint8_t *asset_bytes = rt_asset_load_raw(path, &asset_size);
    if (asset_bytes) {
        rt_string result = NULL;
        if ((uint64_t)asset_size <= RT_GAME3D_STREAM_MANIFEST_MAX_BYTES)
            result = rt_string_from_bytes((const char *)asset_bytes, asset_size);
        else
            rt_trap("Game3D.WorldStream3D: manifest asset exceeds byte budget");
        free(asset_bytes);
        return result;
    }
    FILE *file = fopen(cpath, "rb");
    if (!file)
        return NULL;
    if (game3d_stream_fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    int64_t size = (int64_t)game3d_stream_ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    if ((uint64_t)size > RT_GAME3D_STREAM_MANIFEST_MAX_BYTES) {
        rt_trap("Game3D.WorldStream3D: manifest file exceeds byte budget");
        fclose(file);
        return NULL;
    }
    if (game3d_stream_fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *bytes = (char *)malloc((size_t)size + 1u);
    if (!bytes) {
        fclose(file);
        return NULL;
    }
    if (size > 0 && fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    bytes[(size_t)size] = '\0';
    rt_string result = rt_string_from_bytes(bytes, (size_t)size);
    free(bytes);
    return result;
}

/// @brief Whether @p path is absolute (leading '/' on POSIX, drive/UNC prefix on Windows).
static int game3d_path_is_absolute(const char *path) {
    if (!path || path[0] == '\0')
        return 0;
    if (path[0] == '/' || path[0] == '\\')
        return 1;
#if defined(_WIN32)
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':')
        return 1;
#endif
    return 0;
}

/// @brief Resolve a manifest-relative resource path against the manifest's own directory.
/// @details Absolute references pass through; relative ones are joined to the manifest's folder so
/// a
///          streaming manifest can reference sibling assets portably.
static rt_string game3d_stream_resolve_manifest_path(rt_string manifest_path, rt_string payload) {
    const char *raw = payload ? rt_string_cstr(payload) : NULL;
    const char *manifest = manifest_path ? rt_string_cstr(manifest_path) : NULL;
    if (!raw || raw[0] == '\0')
        return rt_string_ref(rt_const_cstr(""));
    if (game3d_path_is_absolute(raw) || !manifest || manifest[0] == '\0')
        return rt_string_ref(payload);
    const char *slash = strrchr(manifest, '/');
    const char *backslash = strrchr(manifest, '\\');
    const char *sep =
        slash && backslash ? (slash > backslash ? slash : backslash) : (slash ? slash : backslash);
    if (!sep)
        return rt_string_ref(payload);
    size_t dir_len = (size_t)(sep - manifest + 1);
    size_t raw_len = strlen(raw);
    if (dir_len > SIZE_MAX - raw_len)
        return rt_string_ref(payload);
    char *joined = (char *)malloc(dir_len + raw_len + 1u);
    if (!joined)
        return rt_string_ref(payload);
    memcpy(joined, manifest, dir_len);
    memcpy(joined + dir_len, raw, raw_len + 1u);
    rt_string result = rt_string_from_bytes(joined, dir_len + raw_len);
    free(joined);
    return result;
}

/// @brief Resolve a manifest entry's "sidecar" (or fallback "binarySidecar") path against the
/// manifest.
/// @return Owned resolved path string, or an empty string when neither key is present.
static rt_string game3d_json_resolved_sidecar_ref(void *entry, rt_string manifest_path) {
    rt_string raw = game3d_json_string_ref(entry, "sidecar");
    if (!raw)
        raw = game3d_json_string_ref(entry, "binarySidecar");
    if (!raw)
        return rt_string_ref(rt_const_cstr(""));
    rt_string resolved = game3d_stream_resolve_manifest_path(manifest_path, raw);
    rt_string_unref(raw);
    return resolved;
}

/// @brief Return @p layer when it is a valid layer index, otherwise @p fallback.
static int64_t game3d_stream_layer_or_valid(int64_t layer, int64_t fallback) {
    return game3d_valid_layer(layer) ? layer : fallback;
}

/// @brief Parse collision metadata for a streaming cell/tile from its JSON manifest entry.
/// @details Reads the collision layer ("layer", with "collisionLayer" and a nested
/// "collision.layer"
///          taking precedence), the collision mask ("collisionMask" or nested "collision.mask",
///          sanitized to valid bits), and the enabled flag (defaulting on, overridden by a boolean
///          "collision" value or nested "collision.enabled"). Every out-pointer is optional; layer
///          and mask fall back to @p default_layer and all-bits when unspecified, and the matching
///          has-flag reports whether the manifest actually supplied a value.
static void game3d_stream_parse_collision_metadata(void *entry,
                                                   int64_t default_layer,
                                                   int64_t *out_layer,
                                                   int8_t *out_has_layer,
                                                   int64_t *out_mask,
                                                   int8_t *out_has_mask,
                                                   int8_t *out_enabled) {
    if (out_enabled)
        *out_enabled = 1;
    int64_t layer = game3d_json_i64_or(entry, "layer", 0);
    int8_t has_layer = game3d_valid_layer(layer) ? 1 : 0;
    int64_t collision_layer = game3d_json_i64_or(entry, "collisionLayer", 0);
    if (game3d_valid_layer(collision_layer)) {
        layer = collision_layer;
        has_layer = 1;
    }
    int64_t collision_mask = game3d_json_i64_or(entry, "collisionMask", 0);
    int8_t has_mask = game3d_json_get(entry, "collisionMask") ? 1 : 0;
    if (out_enabled && game3d_json_get(entry, "collision"))
        *out_enabled = game3d_json_bool_or(entry, "collision", 1);

    void *collision = game3d_json_get(entry, "collision");
    if (game3d_json_is_map(collision)) {
        if (out_enabled)
            *out_enabled = game3d_json_bool_or(collision, "enabled", 1);
        int64_t nested_layer = game3d_json_i64_or(collision, "layer", 0);
        if (game3d_valid_layer(nested_layer)) {
            layer = nested_layer;
            has_layer = 1;
        }
        int64_t nested_mask = game3d_json_i64_or(collision, "mask", 0);
        if (game3d_json_get(collision, "mask")) {
            collision_mask = nested_mask;
            has_mask = 1;
        }
    }

    if (out_layer)
        *out_layer = has_layer ? layer : default_layer;
    if (out_has_layer)
        *out_has_layer = has_layer;
    if (out_mask)
        *out_mask = has_mask ? game3d_sanitize_mask_bits(collision_mask) : ~(int64_t)0;
    if (out_has_mask)
        *out_has_mask = has_mask;
}

/// @brief Read a manifest entry's "traversalCost", defaulting to 1.0 and clamping to the range (0,
/// 1e6].
static double game3d_stream_traversal_cost_or_default(void *entry) {
    double cost = game3d_json_f64_or(entry, "traversalCost", 1.0);
    if (!isfinite(cost) || cost <= 0.0)
        cost = 1.0;
    if (cost > 1000000.0)
        cost = 1000000.0;
    return cost;
}

/// @brief Resident byte size attributed to a streaming cell (0 if not loaded).
static int64_t game3d_stream_cell_bytes(const rt_game3d_stream_cell *cell) {
    if (!cell)
        return 64 * 1024;
    if (cell->resident && cell->measured_resident_bytes > 0)
        return cell->measured_resident_bytes;
    if (cell->resident_bytes <= 0)
        return 64 * 1024;
    return cell->resident_bytes;
}

/// @brief Resident byte size attributed to a streaming terrain tile (0 if not loaded).
static int64_t game3d_stream_terrain_tile_bytes(const rt_game3d_stream_terrain_tile *tile) {
    if (!tile || tile->resident_bytes <= 0)
        return 256 * 1024;
    if (tile->sidecar_bytes <= 0)
        return tile->resident_bytes;
    return game3d_i64_saturating_add(tile->resident_bytes, tile->sidecar_bytes);
}

/// @brief Squared distance from the stream center to a cell's center (drives load/unload ordering).
static double game3d_stream_cell_distance_sq(const rt_game3d_world_stream *stream,
                                             const rt_game3d_stream_cell *cell) {
    if (!stream || !cell || !isfinite(stream->center[0]) || !isfinite(stream->center[2]) ||
        !isfinite(cell->center[0]) || !isfinite(cell->center[2]))
        return HUGE_VAL;
    double sx = game3d_clamp_coord_or(stream->center[0], 0.0);
    double sz = game3d_clamp_coord_or(stream->center[2], 0.0);
    double cx = game3d_clamp_coord_or(cell->center[0], 0.0);
    double cz = game3d_clamp_coord_or(cell->center[2], 0.0);
    double dx = sx - cx;
    double dz = sz - cz;
    if (!isfinite(dx) || !isfinite(dz))
        return HUGE_VAL;
    double distance_sq = dx * dx + dz * dz;
    return isfinite(distance_sq) ? distance_sq : HUGE_VAL;
}

/// @brief Squared distance from the stream center to a terrain tile's center.
static double game3d_stream_terrain_tile_distance_sq(const rt_game3d_world_stream *stream,
                                                     const rt_game3d_stream_terrain_tile *tile) {
    if (!stream || !tile || !isfinite(stream->center[0]) || !isfinite(stream->center[2]) ||
        !isfinite(tile->center[0]) || !isfinite(tile->center[2]))
        return HUGE_VAL;
    double sx = game3d_clamp_coord_or(stream->center[0], 0.0);
    double sz = game3d_clamp_coord_or(stream->center[2], 0.0);
    double tx = game3d_clamp_coord_or(tile->center[0], 0.0);
    double tz = game3d_clamp_coord_or(tile->center[2], 0.0);
    double dx = sx - tx;
    double dz = sz - tz;
    if (!isfinite(dx) || !isfinite(dz))
        return HUGE_VAL;
    double distance_sq = dx * dx + dz * dz;
    return isfinite(distance_sq) ? distance_sq : HUGE_VAL;
}

/// @brief Sanitize a terrain axis scale to a positive finite value (defaulting when invalid).
static double game3d_stream_terrain_axis_scale(double value) {
    if (!isfinite(value) || value <= 0.0)
        return 1.0;
    if (value > 1000000.0)
        return 1000000.0;
    return value;
}

/// @brief Compute the render-space bounds of a terrain tile from its manifest center + scale.
static void game3d_stream_terrain_tile_bounds(const rt_game3d_stream_terrain_tile *tile,
                                              double *min_x,
                                              double *max_x,
                                              double *min_z,
                                              double *max_z) {
    double sx = game3d_stream_terrain_axis_scale(tile ? tile->scale[0] : 1.0);
    double sz = game3d_stream_terrain_axis_scale(tile ? tile->scale[2] : 1.0);
    double width = tile && tile->width > 1 ? (double)(tile->width - 1) * sx : sx;
    double depth = tile && tile->depth > 1 ? (double)(tile->depth - 1) * sz : sz;
    double cx = game3d_clamp_coord_or(tile ? tile->center[0] : 0.0, 0.0);
    double cz = game3d_clamp_coord_or(tile ? tile->center[2] : 0.0, 0.0);
    if (!isfinite(width) || width < 0.0)
        width = 0.0;
    if (!isfinite(depth) || depth < 0.0)
        depth = 0.0;
    if (min_x)
        *min_x = cx - width * 0.5;
    if (max_x)
        *max_x = cx + width * 0.5;
    if (min_z)
        *min_z = cz - depth * 0.5;
    if (max_z)
        *max_z = cz + depth * 0.5;
}

/// @brief Floating-point tolerance for deciding whether two tile edges touch.
static double game3d_stream_terrain_seam_tolerance(const rt_game3d_stream_terrain_tile *a,
                                                   const rt_game3d_stream_terrain_tile *b) {
    double tol = 0.00001;
    if (a) {
        double ax = game3d_stream_terrain_axis_scale(a->scale[0]) * 0.0001;
        double az = game3d_stream_terrain_axis_scale(a->scale[2]) * 0.0001;
        if (ax > tol)
            tol = ax;
        if (az > tol)
            tol = az;
    }
    if (b) {
        double bx = game3d_stream_terrain_axis_scale(b->scale[0]) * 0.0001;
        double bz = game3d_stream_terrain_axis_scale(b->scale[2]) * 0.0001;
        if (bx > tol)
            tol = bx;
        if (bz > tol)
            tol = bz;
    }
    return tol;
}

/// @brief Finite-aware approximate equality: true when both values are finite and within @p
/// tolerance.
static int game3d_stream_almost_equal(double a, double b, double tolerance) {
    return isfinite(a) && isfinite(b) && fabs(a - b) <= tolerance;
}

/// @brief True when two [min,max] ranges match end-to-end within @p tolerance (terrain seam test).
static int game3d_stream_range_matches(
    double a_min, double a_max, double b_min, double b_max, double tolerance) {
    return game3d_stream_almost_equal(a_min, b_min, tolerance) &&
           game3d_stream_almost_equal(a_max, b_max, tolerance);
}

static double game3d_stream_radius_sq(double radius);

/// @brief qsort comparator ordering stream load candidates by ascending squared
///   distance, breaking ties by ascending index; NULL/non-finite distances sort last.
static int game3d_stream_load_candidate_cmp(const void *lhs, const void *rhs) {
    const rt_game3d_stream_load_candidate *a = (const rt_game3d_stream_load_candidate *)lhs;
    const rt_game3d_stream_load_candidate *b = (const rt_game3d_stream_load_candidate *)rhs;
    double ad = a ? a->distance_sq : HUGE_VAL;
    double bd = b ? b->distance_sq : HUGE_VAL;
    if (!isfinite(ad))
        ad = HUGE_VAL;
    if (!isfinite(bd))
        bd = HUGE_VAL;
    if (ad < bd)
        return -1;
    if (ad > bd)
        return 1;
    int32_t ai = a ? a->index : INT32_MAX;
    int32_t bi = b ? b->index : INT32_MAX;
    return (ai > bi) - (ai < bi);
}

static int32_t game3d_world_stream_safe_cell_count(const rt_game3d_world_stream *stream);
static int32_t game3d_world_stream_safe_terrain_tile_count(const rt_game3d_world_stream *stream);
static int game3d_stream_cell_desired(const rt_game3d_world_stream *stream,
                                      const rt_game3d_stream_cell *cell);
static int game3d_stream_terrain_tile_desired(const rt_game3d_world_stream *stream,
                                              const rt_game3d_stream_terrain_tile *tile);

/// @brief Grow a reusable stream-load candidate buffer to at least @p needed entries.
/// @details The stream owns candidate arrays for the lifetime of the WorldStream3D
///   object. Reusing them avoids per-update allocation churn while still allowing
///   manifests to grow after remount. Returns 0 on allocation failure; callers then
///   use the deterministic no-allocation scanner.
static int game3d_stream_reserve_load_candidates(rt_game3d_stream_load_candidate **items,
                                                 int32_t *capacity,
                                                 int32_t needed) {
    if (!items || !capacity || needed <= 0)
        return 1;
    if (*capacity >= needed && *items)
        return 1;
    rt_game3d_stream_load_candidate *grown =
        (rt_game3d_stream_load_candidate *)realloc(*items, (size_t)needed * sizeof(**items));
    if (!grown)
        return 0;
    *items = grown;
    *capacity = needed;
    return 1;
}

/// @brief True if a load candidate tuple sorts after the previously emitted tuple.
/// @details Used by the no-allocation nearest-first fallback. Distances are sanitized
///   to put non-finite values last, and index breaks ties so the ordering exactly
///   matches `game3d_stream_load_candidate_cmp`.
static int game3d_stream_candidate_after(double distance_sq,
                                         int32_t index,
                                         double previous_distance_sq,
                                         int32_t previous_index) {
    if (!isfinite(distance_sq))
        distance_sq = HUGE_VAL;
    if (!isfinite(previous_distance_sq))
        previous_distance_sq = HUGE_VAL;
    if (previous_index < 0)
        return 1;
    if (distance_sq > previous_distance_sq)
        return 1;
    if (distance_sq < previous_distance_sq)
        return 0;
    return index > previous_index;
}

/// @brief Pick the next desired cell in sorted order without allocating or marking cells.
static int32_t game3d_world_stream_pick_next_cell_noalloc(const rt_game3d_world_stream *stream,
                                                          double previous_distance_sq,
                                                          int32_t previous_index,
                                                          double *out_distance_sq) {
    int32_t best_index = -1;
    double best_distance = HUGE_VAL;
    int32_t cell_count = game3d_world_stream_safe_cell_count(stream);
    for (int32_t i = 0; i < cell_count; ++i) {
        const rt_game3d_stream_cell *cell = &stream->cells[i];
        if (!game3d_stream_cell_desired(stream, cell))
            continue;
        double distance_sq = game3d_stream_cell_distance_sq(stream, cell);
        if (!isfinite(distance_sq))
            distance_sq = HUGE_VAL;
        if (!game3d_stream_candidate_after(distance_sq, i, previous_distance_sq, previous_index))
            continue;
        if (best_index < 0 || distance_sq < best_distance ||
            (distance_sq == best_distance && i < best_index)) {
            best_index = i;
            best_distance = distance_sq;
        }
    }
    if (out_distance_sq)
        *out_distance_sq = best_distance;
    return best_index;
}

/// @brief Pick the next desired terrain tile in sorted order without allocating.
static int32_t game3d_world_stream_pick_next_terrain_noalloc(const rt_game3d_world_stream *stream,
                                                             double previous_distance_sq,
                                                             int32_t previous_index,
                                                             double *out_distance_sq) {
    int32_t best_index = -1;
    double best_distance = HUGE_VAL;
    int32_t tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
    for (int32_t i = 0; i < tile_count; ++i) {
        const rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
        if (!game3d_stream_terrain_tile_desired(stream, tile))
            continue;
        double distance_sq = game3d_stream_terrain_tile_distance_sq(stream, tile);
        if (!isfinite(distance_sq))
            distance_sq = HUGE_VAL;
        if (!game3d_stream_candidate_after(distance_sq, i, previous_distance_sq, previous_index))
            continue;
        if (best_index < 0 || distance_sq < best_distance ||
            (distance_sq == best_distance && i < best_index)) {
            best_index = i;
            best_distance = distance_sq;
        }
    }
    if (out_distance_sq)
        *out_distance_sq = best_distance;
    return best_index;
}

/// @brief Increment a pending-work counter, saturating at INT64_MAX and ignoring NULL.
static void game3d_stream_increment_pending(int64_t *pending) {
    if (!pending || *pending == INT64_MAX)
        return;
    ++*pending;
}

/// @brief Number of stream cells safe to read from the private manifest array.
static int32_t game3d_world_stream_safe_cell_count(const rt_game3d_world_stream *stream) {
    if (!stream || !stream->cells || stream->cell_count <= 0 || stream->cell_capacity <= 0)
        return 0;
    return stream->cell_count < stream->cell_capacity ? stream->cell_count : stream->cell_capacity;
}

/// @brief Number of terrain tiles safe to read from the private manifest array.
static int32_t game3d_world_stream_safe_terrain_tile_count(const rt_game3d_world_stream *stream) {
    if (!stream || !stream->terrain_tiles || stream->terrain_tile_count <= 0 ||
        stream->terrain_tile_capacity <= 0)
        return 0;
    return stream->terrain_tile_count < stream->terrain_tile_capacity
               ? stream->terrain_tile_count
               : stream->terrain_tile_capacity;
}

static int64_t game3d_world_stream_nonnegative_i64(int64_t value) {
    return value > 0 ? value : 0;
}

/// @brief Resident cell telemetry, capped to parsed manifest size when array-backed.
static int64_t game3d_world_stream_safe_resident_cell_count(const rt_game3d_world_stream *stream) {
    int64_t count = stream ? game3d_world_stream_nonnegative_i64(stream->resident_cell_count) : 0;
    if (stream && stream->cells_manifest_loaded) {
        int32_t cell_count = game3d_world_stream_safe_cell_count(stream);
        if (count > cell_count)
            count = cell_count;
    }
    return count;
}

/// @brief Resident terrain telemetry, capped to parsed manifest size when array-backed.
static int64_t game3d_world_stream_safe_resident_terrain_tile_count(
    const rt_game3d_world_stream *stream) {
    int64_t count =
        stream ? game3d_world_stream_nonnegative_i64(stream->resident_terrain_tile_count) : 0;
    if (stream && stream->terrain_manifest_loaded) {
        int32_t tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
        if (count > tile_count)
            count = tile_count;
    }
    return count;
}

/// @brief True when a streaming cell lies within its activation radius. Resident cells
///   test against the larger unload radius, giving load/unload hysteresis.
static int game3d_stream_cell_desired(const rt_game3d_world_stream *stream,
                                      const rt_game3d_stream_cell *cell) {
    if (!stream || !cell)
        return 0;
    double radius = cell->radius;
    double threshold = (cell->resident ? stream->unload_radius : stream->load_radius) + radius;
    if (!isfinite(threshold) || threshold < 0.0)
        threshold = radius;
    return game3d_stream_cell_distance_sq(stream, cell) <= game3d_stream_radius_sq(threshold);
}

/// @brief True when a terrain tile lies within its activation radius. Resident tiles
///   test against the larger unload radius, giving load/unload hysteresis.
static int game3d_stream_terrain_tile_desired(const rt_game3d_world_stream *stream,
                                              const rt_game3d_stream_terrain_tile *tile) {
    if (!stream || !tile)
        return 0;
    double radius = tile->radius;
    double threshold = (tile->resident ? stream->unload_radius : stream->load_radius) + radius;
    if (!isfinite(threshold) || threshold < 0.0)
        threshold = radius;
    return game3d_stream_terrain_tile_distance_sq(stream, tile) <=
           game3d_stream_radius_sq(threshold);
}

/// @brief Total resident bytes across all currently-loaded streaming cells.
static int64_t game3d_world_stream_resident_cell_bytes(const rt_game3d_world_stream *stream) {
    if (!stream)
        return 0;
    int64_t bytes = game3d_stream_manifest_bytes(stream->cells_manifest);
    int32_t cell_count = game3d_world_stream_safe_cell_count(stream);
    for (int32_t i = 0; i < cell_count; ++i) {
        if (stream->cells[i].resident)
            bytes = game3d_i64_saturating_add(bytes, game3d_stream_cell_bytes(&stream->cells[i]));
    }
    return bytes;
}

static void game3d_world_stream_unload_terrain_tile(rt_game3d_world_stream *stream,
                                                    rt_game3d_stream_terrain_tile *tile);

/// @brief Return a terrain tile's runtime Terrain3D only when the private slot is still valid.
static void *game3d_stream_terrain_tile_terrain_ref(const rt_game3d_stream_terrain_tile *tile) {
    return tile && tile->resident ? rt_g3d_checked_or_null(tile->terrain, RT_G3D_TERRAIN3D_CLASS_ID)
                                  : NULL;
}

/// @brief Return a terrain tile's attached Game3D entity only when the private slot is valid.
static void *game3d_stream_terrain_tile_entity_ref(void *entity) {
    return rt_g3d_checked_or_null(entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
}

/// @brief Despawn/release collider and nav entities attached to a resident terrain tile.
static void game3d_world_stream_detach_terrain_spatial_sources(
    rt_game3d_world_stream *stream, rt_game3d_stream_terrain_tile *tile) {
    if (!tile)
        return;
    if (tile->nav_entity && stream && stream->world) {
        rt_game3d_entity *entity =
            (rt_game3d_entity *)game3d_stream_terrain_tile_entity_ref(tile->nav_entity);
        if (entity && entity->spawned && entity->world == stream->world)
            rt_game3d_world_despawn(stream->world, tile->nav_entity);
    }
    game3d_release_typed_ref(&tile->nav_entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    if (tile->collider_entity && stream && stream->world) {
        rt_game3d_entity *entity =
            (rt_game3d_entity *)game3d_stream_terrain_tile_entity_ref(tile->collider_entity);
        if (entity && entity->spawned && entity->world == stream->world)
            rt_game3d_world_despawn(stream->world, tile->collider_entity);
    }
    game3d_release_typed_ref(&tile->collider_entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
}

/// @brief Unload and free every terrain tile tracked by the stream.
static void game3d_world_stream_clear_terrain_tiles(rt_game3d_world_stream *stream) {
    if (!stream)
        return;
    int32_t tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
    for (int32_t i = 0; i < tile_count; ++i) {
        rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
        game3d_world_stream_unload_terrain_tile(stream, tile);
        game3d_release_ref((void **)&tile->name);
        game3d_release_ref((void **)&tile->path);
        game3d_release_ref((void **)&tile->heightmap_path);
        game3d_release_ref((void **)&tile->material);
        game3d_release_ref((void **)&tile->nav_area);
        game3d_release_ref((void **)&tile->sidecar_path);
    }
    free(stream->terrain_tiles);
    stream->terrain_tiles = NULL;
    stream->terrain_tile_count = 0;
    stream->terrain_tile_capacity = 0;
    stream->terrain_manifest_loaded = 0;
}

/// @brief Unload a single terrain tile: detach its collider/nav source and release its resources.
static void game3d_world_stream_unload_terrain_tile(rt_game3d_world_stream *stream,
                                                    rt_game3d_stream_terrain_tile *tile) {
    if (!tile)
        return;
    game3d_world_stream_detach_terrain_spatial_sources(stream, tile);
    game3d_release_typed_ref(&tile->terrain, RT_G3D_TERRAIN3D_CLASS_ID);
    free(tile->sidecar_data);
    tile->sidecar_data = NULL;
    tile->sidecar_bytes = 0;
    tile->resident = 0;
}

/// @brief Advance past ASCII whitespace in a heightmap text buffer.
static const char *game3d_heightmap_skip_space(const char *p) {
    while (p && *p && isspace((unsigned char)*p))
        ++p;
    return p;
}

/// @brief Read the next whitespace-delimited token from a heightmap text buffer into @p out.
/// @return 1 on success, 0 at end of input or if the token would overflow.
static int game3d_heightmap_read_word(const char **cursor, char *out, size_t out_size) {
    if (!cursor || !*cursor || !out || out_size == 0)
        return 0;
    const char *p = game3d_heightmap_skip_space(*cursor);
    if (!p || *p == '\0')
        return 0;
    size_t len = 0;
    while (p[len] && !isspace((unsigned char)p[len]))
        ++len;
    if (len == 0 || len >= out_size)
        return 0;
    memcpy(out, p, len);
    out[len] = '\0';
    *cursor = p + len;
    return 1;
}

/// @brief Read the next integer token from a heightmap text buffer (0 if none/invalid).
static int game3d_heightmap_read_i64(const char **cursor, int64_t *out) {
    if (!cursor || !*cursor || !out)
        return 0;
    char *end = NULL;
    const char *p = game3d_heightmap_skip_space(*cursor);
    errno = 0;
    long long value = strtoll(p, &end, 10);
    if (!end || end == p || errno == ERANGE || (*end != '\0' && !isspace((unsigned char)*end)) ||
        value < (long long)INT64_MIN || value > (long long)INT64_MAX)
        return 0;
    *cursor = end;
    *out = (int64_t)value;
    return 1;
}

/// @brief Read the next floating-point token from a heightmap text buffer (0 if none/invalid).
static int game3d_heightmap_read_f64(const char **cursor, double *out) {
    if (!cursor || !*cursor || !out)
        return 0;
    char *end = NULL;
    const char *p = game3d_heightmap_skip_space(*cursor);
    errno = 0;
    double value = strtod(p, &end);
    if (!end || end == p || errno == ERANGE || !isfinite(value) ||
        (*end != '\0' && !isspace((unsigned char)*end)))
        return 0;
    *cursor = end;
    *out = value;
    return 1;
}

/// @brief Map a normalized height value to a grayscale RGBA color for the heightmap preview image.
static uint32_t game3d_heightmap_sample_rgba(double h) {
    if (h < 0.0)
        h = 0.0;
    else if (h > 1.0)
        h = 1.0;
    uint32_t sample = (uint32_t)llround(h * 65535.0);
    if (sample > 65535u)
        sample = 65535u;
    return ((sample >> 8) & 0xFFu) << 24 | (sample & 0xFFu) << 16 | 0x000000FFu;
}

/// @brief Map a destination heightmap sample to a source sample, preserving both endpoints.
static int64_t game3d_heightmap_resample_index(int64_t dst, int64_t dst_count, int64_t src_count) {
    int64_t numerator;
    int64_t denominator;
    int64_t value;
    if (dst_count <= 1 || src_count <= 1 || dst <= 0)
        return 0;
    if (dst >= dst_count - 1)
        return src_count - 1;
    numerator = dst * (src_count - 1);
    denominator = dst_count - 1;
    value = (numerator + denominator / 2) / denominator;
    if (value < 0)
        return 0;
    if (value >= src_count)
        return src_count - 1;
    return value;
}

/// @brief Load a terrain tile's heightmap sidecar file into a height grid (and optional preview
/// image).
/// @details Parses the text heightmap format (dimensions + height samples); returns NULL if the
///          sidecar is missing or malformed.
static void *game3d_load_heightmap_sidecar(rt_string path,
                                           int64_t target_width,
                                           int64_t target_depth) {
    if (!game3d_string_has_bytes(path))
        return NULL;
    rt_string text = game3d_read_text_file_no_trap(path);
    if (!text)
        return NULL;

    const char *cursor = rt_string_cstr(text);
    char magic[32];
    int64_t width = 0;
    int64_t depth = 0;
    if (!game3d_heightmap_read_word(&cursor, magic, sizeof(magic)) ||
        strcmp(magic, "viper-heightmap-v1") != 0 || !game3d_heightmap_read_i64(&cursor, &width) ||
        !game3d_heightmap_read_i64(&cursor, &depth) || width < 1 || depth < 1 || width > 4096 ||
        depth > 4096) {
        rt_string_unref(text);
        return NULL;
    }

    size_t sample_count = (size_t)width * (size_t)depth;
    if (sample_count > SIZE_MAX / sizeof(double)) {
        rt_string_unref(text);
        return NULL;
    }
    double *samples = (double *)calloc(sample_count, sizeof(double));
    if (!samples) {
        rt_string_unref(text);
        return NULL;
    }

    for (int64_t z = 0; z < depth; ++z) {
        for (int64_t x = 0; x < width; ++x) {
            double h = 0.0;
            if (!game3d_heightmap_read_f64(&cursor, &h)) {
                free(samples);
                rt_string_unref(text);
                return NULL;
            }
            if (h < 0.0)
                h = 0.0;
            else if (h > 1.0)
                h = 1.0;
            samples[(size_t)z * (size_t)width + (size_t)x] = h;
        }
    }
    cursor = game3d_heightmap_skip_space(cursor);
    if (cursor && *cursor != '\0') {
        free(samples);
        rt_string_unref(text);
        return NULL;
    }

    if (target_width < 1 || target_width > 4096)
        target_width = width;
    if (target_depth < 1 || target_depth > 4096)
        target_depth = depth;
    void *pixels = rt_pixels_new(target_width, target_depth);
    if (!pixels) {
        free(samples);
        rt_string_unref(text);
        return NULL;
    }

    for (int64_t z = 0; z < target_depth; ++z) {
        int64_t src_z = game3d_heightmap_resample_index(z, target_depth, depth);
        for (int64_t x = 0; x < target_width; ++x) {
            int64_t src_x = game3d_heightmap_resample_index(x, target_width, width);
            double h = samples[(size_t)src_z * (size_t)width + (size_t)src_x];
            rt_pixels_set(pixels, x, z, (int64_t)game3d_heightmap_sample_rgba(h));
        }
    }

    free(samples);
    rt_string_unref(text);
    return pixels;
}

/// @brief Load a terrain tile's optional binary sidecar payload into residency.
/// @details Terrain sidecars are opaque editor/gameplay metadata parallel to scene
///   cell sidecars. Missing, empty, or unreadable files are recoverable and simply
///   leave the tile with zero sidecar bytes. Any previous payload is freed first so
///   remounts and reloads do not leak memory.
static void game3d_stream_terrain_tile_load_sidecar(rt_game3d_stream_terrain_tile *tile) {
    if (!tile)
        return;
    free(tile->sidecar_data);
    tile->sidecar_data = NULL;
    tile->sidecar_bytes = 0;
    if (!game3d_string_has_bytes(tile->sidecar_path))
        return;
    size_t size = 0;
    uint8_t *data = game3d_asset_read_file_bytes(rt_string_cstr(tile->sidecar_path), &size);
    if (!data)
        return;
    if (size == 0) {
        free(data);
        return;
    }
    tile->sidecar_data = data;
    tile->sidecar_bytes = (int64_t)(size > (size_t)INT64_MAX ? (size_t)INT64_MAX : size);
}

/// @brief Build the unique collider name for a terrain tile (used to register/find its physics
/// collider).
static rt_string game3d_terrain_collider_name(const rt_game3d_stream_terrain_tile *tile) {
    const char *base = tile && tile->name ? rt_string_cstr(tile->name) : "terrain_tile";
    const char *suffix = "_heightfield_collider";
    size_t base_len = strlen(base);
    size_t suffix_len = strlen(suffix);
    if (base_len > SIZE_MAX - suffix_len - 1u)
        return rt_string_ref(rt_const_cstr("terrain_tile_heightfield_collider"));
    char *name = (char *)malloc(base_len + suffix_len + 1u);
    if (!name)
        return rt_string_ref(rt_const_cstr("terrain_tile_heightfield_collider"));
    memcpy(name, base, base_len);
    memcpy(name + base_len, suffix, suffix_len + 1u);
    rt_string result = rt_string_from_bytes(name, base_len + suffix_len);
    free(name);
    return result;
}

/// @brief Create and register a heightfield collider for a loaded terrain tile.
static void game3d_world_stream_attach_terrain_collider(rt_game3d_world_stream *stream,
                                                        rt_game3d_stream_terrain_tile *tile,
                                                        void *heightmap) {
    if (!stream || !tile || !heightmap)
        return;
    if (tile->collider_entity) {
        if (game3d_stream_terrain_tile_entity_ref(tile->collider_entity))
            return;
        game3d_release_typed_ref(&tile->collider_entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    }
    if (!tile->collision_enabled)
        return;
    rt_game3d_world *world =
        (rt_game3d_world *)rt_g3d_checked_or_null(stream->world, RT_G3D_GAME3D_WORLD_CLASS_ID);
    if (!world || world->destroyed || !world->physics)
        return;

    void *collider =
        rt_collider3d_new_heightfield(heightmap,
                                      game3d_stream_terrain_axis_scale(tile->scale[0]),
                                      game3d_stream_terrain_axis_scale(tile->scale[1]),
                                      game3d_stream_terrain_axis_scale(tile->scale[2]));
    if (!collider)
        return;
    void *body = rt_body3d_new(0.0);
    if (!body) {
        game3d_release_ref(&collider);
        return;
    }
    rt_body3d_set_static(body, 1);
    rt_body3d_set_collision_layer(body,
                                  game3d_stream_layer_or_valid(tile->layer, RT_GAME3D_LAYER_WORLD));
    rt_body3d_set_collision_mask(body,
                                 tile->has_collision_mask ? tile->collision_mask : ~(int64_t)0);
    rt_body3d_set_collider(body, collider);

    void *entity = rt_game3d_entity_new();
    if (entity) {
        rt_string name = game3d_terrain_collider_name(tile);
        rt_game3d_entity_set_name(entity, name);
        rt_string_unref(name);
        rt_game3d_entity_set_position(entity,
                                      game3d_clamp_coord_or(tile->center[0], 0.0),
                                      game3d_clamp_coord_or(tile->center[1], 0.0),
                                      game3d_clamp_coord_or(tile->center[2], 0.0));
        rt_game3d_entity_attach_body(entity, body);
        rt_game3d_world_spawn(stream->world, entity);
        game3d_assign_typed_ref(&tile->collider_entity, entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    }

    game3d_release_ref(&entity);
    game3d_release_ref(&body);
    game3d_release_ref(&collider);
}

/// @brief Build the unique nav-source name for a terrain tile (used to register/find its nav
/// geometry).
static rt_string game3d_terrain_nav_source_name(const rt_game3d_stream_terrain_tile *tile) {
    const char *base = tile && tile->name ? rt_string_cstr(tile->name) : "terrain_tile";
    const char *suffix = "_navmesh_source";
    size_t base_len = strlen(base);
    size_t suffix_len = strlen(suffix);
    if (base_len > SIZE_MAX - suffix_len - 1u)
        return rt_string_ref(rt_const_cstr("terrain_tile_navmesh_source"));
    char *name = (char *)malloc(base_len + suffix_len + 1u);
    if (!name)
        return rt_string_ref(rt_const_cstr("terrain_tile_navmesh_source"));
    memcpy(name, base, base_len);
    memcpy(name + base_len, suffix, suffix_len + 1u);
    rt_string result = rt_string_from_bytes(name, base_len + suffix_len);
    free(name);
    return result;
}

/// @brief Register a loaded terrain tile's geometry as a navmesh source for pathfinding.
static void game3d_world_stream_attach_terrain_nav_source(rt_game3d_world_stream *stream,
                                                          rt_game3d_stream_terrain_tile *tile) {
    if (!stream || !tile)
        return;
    void *terrain = game3d_stream_terrain_tile_terrain_ref(tile);
    if (!terrain)
        return;
    if (tile->nav_entity) {
        if (game3d_stream_terrain_tile_entity_ref(tile->nav_entity))
            return;
        game3d_release_typed_ref(&tile->nav_entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    }
    rt_game3d_world *world =
        (rt_game3d_world *)rt_g3d_checked_or_null(stream->world, RT_G3D_GAME3D_WORLD_CLASS_ID);
    if (!world || world->destroyed || !world->scene)
        return;

    void *mesh = rt_terrain3d_build_nav_mesh(terrain, 4);
    if (!mesh)
        return;
    void *entity = rt_game3d_entity_new();
    if (entity) {
        rt_game3d_entity_set_mesh(entity, mesh);
        rt_string name = game3d_terrain_nav_source_name(tile);
        rt_game3d_entity_set_name(entity, name);
        rt_string_unref(name);
        double sx = game3d_stream_terrain_axis_scale(tile->scale[0]);
        double sz = game3d_stream_terrain_axis_scale(tile->scale[2]);
        double width = tile->width > 1 ? (double)(tile->width - 1) * sx : sx;
        double depth = tile->depth > 1 ? (double)(tile->depth - 1) * sz : sz;
        double cx = game3d_clamp_coord_or(tile->center[0], 0.0);
        double cy = game3d_clamp_coord_or(tile->center[1], 0.0);
        double cz = game3d_clamp_coord_or(tile->center[2], 0.0);
        double tx = game3d_clamp_coord_or(cx - width * 0.5, cx);
        double tz = game3d_clamp_coord_or(cz - depth * 0.5, cz);
        rt_game3d_entity_set_position(entity, tx, cy, tz);
        rt_game3d_entity *impl = (rt_game3d_entity *)entity;
        if (impl->node)
            rt_scene_node3d_set_visible(impl->node, 0);
        rt_game3d_world_spawn(stream->world, entity);
        game3d_assign_typed_ref(&tile->nav_entity, entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    }
    game3d_release_ref(&entity);
    game3d_release_ref(&mesh);
}

/// @brief Recreate collider/nav sources so they reflect the terrain's post-stitch height grid.
static void game3d_world_stream_refresh_terrain_spatial_sources(
    rt_game3d_world_stream *stream, rt_game3d_stream_terrain_tile *tile) {
    void *terrain = game3d_stream_terrain_tile_terrain_ref(tile);
    if (!stream || !terrain)
        return;
    game3d_world_stream_detach_terrain_spatial_sources(stream, tile);
    void *heightmap = rt_terrain3d_build_heightmap_pixels(terrain);
    if (heightmap) {
        game3d_world_stream_attach_terrain_collider(stream, tile, heightmap);
        game3d_release_ref(&heightmap);
    }
    game3d_world_stream_attach_terrain_nav_source(stream, tile);
}

/// @brief Stitch a pair of resident terrain tiles when their full manifest edges touch.
static int64_t game3d_world_stream_stitch_terrain_pair(rt_game3d_stream_terrain_tile *a,
                                                       rt_game3d_stream_terrain_tile *b) {
    void *terrain_a = game3d_stream_terrain_tile_terrain_ref(a);
    void *terrain_b = game3d_stream_terrain_tile_terrain_ref(b);
    if (!terrain_a || !terrain_b)
        return 0;
    double a_min_x, a_max_x, a_min_z, a_max_z;
    double b_min_x, b_max_x, b_min_z, b_max_z;
    game3d_stream_terrain_tile_bounds(a, &a_min_x, &a_max_x, &a_min_z, &a_max_z);
    game3d_stream_terrain_tile_bounds(b, &b_min_x, &b_max_x, &b_min_z, &b_max_z);
    double tolerance = game3d_stream_terrain_seam_tolerance(a, b);
    if (game3d_stream_range_matches(a_min_z, a_max_z, b_min_z, b_max_z, tolerance)) {
        if (game3d_stream_almost_equal(a_max_x, b_min_x, tolerance)) {
            return rt_terrain3d_stitch_edge(
                terrain_a, RT_TERRAIN3D_EDGE_EAST, terrain_b, RT_TERRAIN3D_EDGE_WEST);
        }
        if (game3d_stream_almost_equal(b_max_x, a_min_x, tolerance)) {
            return rt_terrain3d_stitch_edge(
                terrain_a, RT_TERRAIN3D_EDGE_WEST, terrain_b, RT_TERRAIN3D_EDGE_EAST);
        }
    }
    if (game3d_stream_range_matches(a_min_x, a_max_x, b_min_x, b_max_x, tolerance)) {
        if (game3d_stream_almost_equal(a_max_z, b_min_z, tolerance)) {
            return rt_terrain3d_stitch_edge(
                terrain_a, RT_TERRAIN3D_EDGE_SOUTH, terrain_b, RT_TERRAIN3D_EDGE_NORTH);
        }
        if (game3d_stream_almost_equal(b_max_z, a_min_z, tolerance)) {
            return rt_terrain3d_stitch_edge(
                terrain_a, RT_TERRAIN3D_EDGE_NORTH, terrain_b, RT_TERRAIN3D_EDGE_SOUTH);
        }
    }
    return 0;
}

/// @brief Stitch @p tile against all currently resident terrain neighbors.
static int64_t game3d_world_stream_stitch_loaded_terrain_neighbors(
    rt_game3d_world_stream *stream, rt_game3d_stream_terrain_tile *tile) {
    if (!stream || !game3d_stream_terrain_tile_terrain_ref(tile))
        return 0;
    int64_t changed = 0;
    int32_t tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
    for (int32_t i = 0; i < tile_count; ++i) {
        rt_game3d_stream_terrain_tile *neighbor = &stream->terrain_tiles[i];
        if (neighbor == tile)
            continue;
        int64_t delta = game3d_world_stream_stitch_terrain_pair(tile, neighbor);
        if (delta > 0) {
            if (changed > INT64_MAX - delta)
                changed = INT64_MAX;
            else
                changed += delta;
        }
    }
    return changed;
}

/// @brief Refresh all resident streamed terrain collider/nav sources after seam changes.
static void game3d_world_stream_refresh_all_terrain_spatial_sources(
    rt_game3d_world_stream *stream) {
    if (!stream)
        return;
    int32_t tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
    for (int32_t i = 0; i < tile_count; ++i) {
        rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
        if (game3d_stream_terrain_tile_terrain_ref(tile))
            game3d_world_stream_refresh_terrain_spatial_sources(stream, tile);
    }
}

/// @brief Load a terrain tile: read its heightmap, build the mesh, and attach collider + nav
/// source.
/// @return 1 on success, 0 on load failure (tile left unloaded).
static int game3d_world_stream_load_terrain_tile(rt_game3d_world_stream *stream,
                                                 rt_game3d_stream_terrain_tile *tile) {
    if (!tile)
        return 0;
    if (game3d_stream_terrain_tile_terrain_ref(tile))
        return 1;
    game3d_release_typed_ref(&tile->terrain, RT_G3D_TERRAIN3D_CLASS_ID);
    if (tile->collider_entity || tile->nav_entity)
        game3d_world_stream_unload_terrain_tile(stream, tile);
    int64_t width = tile->width;
    int64_t depth = tile->depth;
    if (width < 2)
        width = 2;
    if (depth < 2)
        depth = 2;
    if (width > 4096)
        width = 4096;
    if (depth > 4096)
        depth = 4096;
    void *terrain = rt_terrain3d_new(width, depth);
    if (!terrain)
        return 0;
    void *heightmap = game3d_load_heightmap_sidecar(tile->heightmap_path, width, depth);
    if (!heightmap)
        heightmap = rt_pixels_new(width, depth);
    if (heightmap)
        rt_terrain3d_set_heightmap(terrain, heightmap);
    rt_terrain3d_set_scale(terrain,
                           game3d_stream_terrain_axis_scale(tile->scale[0]),
                           game3d_stream_terrain_axis_scale(tile->scale[1]),
                           game3d_stream_terrain_axis_scale(tile->scale[2]));
    void *material = rt_material3d_new_color(0.20, 0.34, 0.22);
    if (material) {
        rt_material3d_set_shading_model(material, RT_GAME3D_SHADING_PBR);
        rt_material3d_set_metallic(material, 0.0);
        rt_material3d_set_roughness(material, 0.88);
        rt_material3d_set_ao(material, 1.0);
        rt_terrain3d_set_material(terrain, material);
        game3d_release_ref(&material);
    }
    tile->terrain = terrain;
    tile->resident = 1;
    game3d_stream_terrain_tile_load_sidecar(tile);
    int64_t stitched = game3d_world_stream_stitch_loaded_terrain_neighbors(stream, tile);
    game3d_release_ref(&heightmap);
    if (stitched > 0)
        game3d_world_stream_refresh_all_terrain_spatial_sources(stream);
    else
        game3d_world_stream_refresh_terrain_spatial_sources(stream, tile);
    return 1;
}

/// @brief Load a streaming cell's optional binary sidecar payload into residency.
/// @details Reads the whole referenced file as opaque cell metadata. A missing, empty,
///   or oversized sidecar is recoverable: the cell simply carries zero sidecar bytes
///   (no trap). Frees any previously loaded payload first so reloads stay idempotent.
static void game3d_stream_cell_load_sidecar(rt_game3d_stream_cell *cell) {
    if (!cell)
        return;
    free(cell->sidecar_data);
    cell->sidecar_data = NULL;
    cell->sidecar_bytes = 0;
    if (!game3d_string_has_bytes(cell->sidecar_path))
        return;
    size_t size = 0;
    uint8_t *data = game3d_asset_read_file_bytes(rt_string_cstr(cell->sidecar_path), &size);
    if (!data)
        return;
    if (size == 0) {
        free(data);
        return;
    }
    cell->sidecar_data = data;
    cell->sidecar_bytes = (int64_t)(size > (size_t)INT64_MAX ? (size_t)INT64_MAX : size);
}

/// @brief Unload a streaming cell: despawn its spawned entities/nodes and release its scene.
static void game3d_world_stream_unload_cell(rt_game3d_world_stream *stream,
                                            rt_game3d_stream_cell *cell) {
    if (!cell)
        return;
    if (cell->resident && cell->entity && stream && stream->world) {
        rt_game3d_entity *entity =
            (rt_game3d_entity *)rt_g3d_checked_or_null(cell->entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
        if (entity && entity->spawned && entity->world == stream->world)
            game3d_world_despawn_entity_tree((rt_game3d_world *)stream->world, entity, 1);
    }
    game3d_release_ref(&cell->entity);
    game3d_release_ref(&cell->scene);
    free(cell->sidecar_data);
    cell->sidecar_data = NULL;
    cell->sidecar_bytes = 0;
    cell->resident = 0;
    cell->measured_resident_bytes = 0;
}

/// @brief Load a streaming cell: load its scene/model and spawn its content into the world.
/// @return 1 on success, 0 on load failure.
static int game3d_world_stream_load_cell(rt_game3d_world_stream *stream,
                                         rt_game3d_stream_cell *cell) {
    if (!stream || !cell)
        return 0;
    if (cell->resident)
        return 1;
    rt_game3d_world *world =
        (rt_game3d_world *)rt_g3d_checked_or_null(stream->world, RT_G3D_GAME3D_WORLD_CLASS_ID);
    if (!world || world->destroyed || !cell->path)
        return 0;
    void *scene = rt_scene3d_load(cell->path);
    if (!scene)
        return 0;
    void *root = rt_scene3d_get_root(scene);
    void *entity = rt_game3d_entity_from_node(root);
    if (entity && cell->name)
        rt_game3d_entity_set_name(entity, cell->name);
    if (entity) {
        if (cell->has_layer)
            rt_game3d_entity_set_layer(entity, cell->layer);
        if (cell->has_collision_mask) {
            void *mask = game3d_layermask_new_bits(cell->collision_mask);
            rt_game3d_entity_set_collision_mask(entity, mask);
            game3d_release_ref(&mask);
        }
        rt_game3d_entity_set_position(entity,
                                      game3d_clamp_coord_or(cell->center[0], 0.0),
                                      game3d_clamp_coord_or(cell->center[1], 0.0),
                                      game3d_clamp_coord_or(cell->center[2], 0.0));
        int64_t next_id = world->next_entity_id;
        if (game3d_world_spawn_entity_tree(world, (rt_game3d_entity *)entity, 1, &next_id)) {
            world->next_entity_id = next_id;
            cell->scene = scene;
            cell->entity = entity;
            cell->resident = 1;
            uint64_t measured = game3d_scene_estimate_resident_bytes(scene);
            cell->measured_resident_bytes = game3d_i64_from_u64_saturating(measured);
            if (cell->measured_resident_bytes <= 0)
                cell->measured_resident_bytes =
                    cell->resident_bytes > 0 ? cell->resident_bytes : 64 * 1024;
            game3d_stream_cell_load_sidecar(cell);
            if (cell->sidecar_bytes > 0)
                cell->measured_resident_bytes =
                    game3d_i64_from_u64_saturating(game3d_u64_saturating_add(
                        (uint64_t)cell->measured_resident_bytes, (uint64_t)cell->sidecar_bytes));
            scene = NULL;
        } else {
            game3d_release_ref(&entity);
        }
    }
    game3d_release_ref(&scene);
    return cell->resident ? 1 : 0;
}

/// @brief Remeasure a loaded cell's scene residency after mesh/texture resident-state changes.
static void game3d_world_stream_refresh_cell_residency_bytes(rt_game3d_stream_cell *cell) {
    uint64_t measured;
    if (!cell || !cell->resident || !cell->scene)
        return;
    measured = game3d_scene_estimate_resident_bytes(cell->scene);
    cell->measured_resident_bytes = game3d_i64_from_u64_saturating(measured);
    if (cell->measured_resident_bytes <= 0)
        cell->measured_resident_bytes = cell->resident_bytes > 0 ? cell->resident_bytes : 64 * 1024;
    if (cell->sidecar_bytes > 0)
        cell->measured_resident_bytes = game3d_i64_from_u64_saturating(game3d_u64_saturating_add(
            (uint64_t)cell->measured_resident_bytes, (uint64_t)cell->sidecar_bytes));
}

/// @brief Unload and free every streaming cell tracked by the stream.
static void game3d_world_stream_clear_cells(rt_game3d_world_stream *stream) {
    if (!stream)
        return;
    int32_t cell_count = game3d_world_stream_safe_cell_count(stream);
    for (int32_t i = 0; i < cell_count; ++i) {
        rt_game3d_stream_cell *cell = &stream->cells[i];
        game3d_world_stream_unload_cell(stream, cell);
        game3d_release_ref((void **)&cell->name);
        game3d_release_ref((void **)&cell->path);
        game3d_release_ref((void **)&cell->material);
        game3d_release_ref((void **)&cell->nav_area);
        game3d_release_ref((void **)&cell->sidecar_path);
    }
    free(stream->cells);
    stream->cells = NULL;
    stream->cell_count = 0;
    stream->cell_capacity = 0;
    stream->cells_manifest_loaded = 0;
}

/// @brief Parse the cells manifest (JSON) into the stream's cell table (name, center, path per
/// cell).
/// @return 1 on success, 0 on a missing or malformed manifest.
static int game3d_world_stream_parse_cells_manifest(rt_game3d_world_stream *stream) {
    if (!stream || !game3d_string_has_bytes(stream->cells_manifest))
        return 0;
    rt_string text = game3d_read_text_file_no_trap(stream->cells_manifest);
    if (!text)
        return 0;
    void *root = NULL;
    if (!rt_json_try_parse(text, &root, NULL, NULL, NULL) || !game3d_json_is_map(root)) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    void *cells = game3d_json_get(root, "cells");
    if (!game3d_json_is_seq(cells)) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    int64_t count64 = rt_seq_len(cells);
    if (count64 < 0 || count64 > INT32_MAX) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    rt_game3d_stream_cell *parsed = NULL;
    int32_t parsed_count = 0;
    if (count64 > 0) {
        parsed = (rt_game3d_stream_cell *)calloc((size_t)count64, sizeof(*parsed));
        if (!parsed) {
            rt_string_unref(text);
            game3d_release_ref(&root);
            return 0;
        }
    }
    for (int64_t i = 0; i < count64; ++i) {
        void *entry = rt_seq_get(cells, i);
        if (!game3d_json_is_map(entry))
            continue;
        rt_string raw_path = game3d_json_string_ref(entry, "path");
        if (!raw_path)
            continue;
        rt_game3d_stream_cell *cell = &parsed[parsed_count++];
        cell->path = game3d_stream_resolve_manifest_path(stream->cells_manifest, raw_path);
        rt_string_unref(raw_path);
        cell->name = game3d_json_string_ref(entry, "name");
        cell->material = game3d_json_string_ref(entry, "material");
        cell->nav_area = game3d_json_string_ref(entry, "navArea");
        cell->sidecar_path = game3d_json_resolved_sidecar_ref(entry, stream->cells_manifest);
        game3d_stream_parse_collision_metadata(entry,
                                               0,
                                               &cell->layer,
                                               &cell->has_layer,
                                               &cell->collision_mask,
                                               &cell->has_collision_mask,
                                               &cell->collision_enabled);
        cell->traversal_cost = game3d_stream_traversal_cost_or_default(entry);
        double zero[3] = {0.0, 0.0, 0.0};
        (void)game3d_json_vec3_or(entry, "center", cell->center, zero);
        cell->radius = game3d_json_f64_or(entry, "radius", 0.0);
        if (!isfinite(cell->radius) || cell->radius < 0.0)
            cell->radius = 0.0;
        cell->resident_bytes = game3d_json_i64_or(entry, "bytes", 64 * 1024);
        if (cell->resident_bytes < 0)
            cell->resident_bytes = 0;
    }
    game3d_world_stream_clear_cells(stream);
    stream->cells = parsed;
    stream->cell_count = parsed_count;
    stream->cell_capacity = (int32_t)count64;
    stream->cells_manifest_loaded = 1;
    rt_string_unref(text);
    game3d_release_ref(&root);
    return 1;
}

/// @brief Parse the terrain manifest (JSON) into the stream's terrain-tile table.
/// @return 1 on success, 0 on a missing or malformed manifest.
static int game3d_world_stream_parse_terrain_manifest(rt_game3d_world_stream *stream) {
    if (!stream || !game3d_string_has_bytes(stream->terrain_manifest))
        return 0;
    rt_string text = game3d_read_text_file_no_trap(stream->terrain_manifest);
    if (!text)
        return 0;
    void *root = NULL;
    if (!rt_json_try_parse(text, &root, NULL, NULL, NULL) || !game3d_json_is_map(root)) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    void *tiles = game3d_json_get(root, "tiles");
    if (!game3d_json_is_seq(tiles)) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    int64_t count64 = rt_seq_len(tiles);
    if (count64 < 0 || count64 > INT32_MAX) {
        rt_string_unref(text);
        game3d_release_ref(&root);
        return 0;
    }
    rt_game3d_stream_terrain_tile *parsed = NULL;
    int32_t parsed_count = 0;
    if (count64 > 0) {
        parsed = (rt_game3d_stream_terrain_tile *)calloc((size_t)count64, sizeof(*parsed));
        if (!parsed) {
            rt_string_unref(text);
            game3d_release_ref(&root);
            return 0;
        }
    }
    for (int64_t i = 0; i < count64; ++i) {
        void *entry = rt_seq_get(tiles, i);
        if (!game3d_json_is_map(entry))
            continue;
        rt_string raw_path = game3d_json_string_ref(entry, "path");
        if (!raw_path)
            continue;
        rt_game3d_stream_terrain_tile *tile = &parsed[parsed_count++];
        tile->path = game3d_stream_resolve_manifest_path(stream->terrain_manifest, raw_path);
        rt_string_unref(raw_path);
        rt_string raw_heightmap = game3d_json_string_ref(entry, "heightmap");
        tile->heightmap_path =
            raw_heightmap
                ? game3d_stream_resolve_manifest_path(stream->terrain_manifest, raw_heightmap)
                : rt_string_ref(rt_const_cstr(""));
        rt_string_unref(raw_heightmap);
        tile->name = game3d_json_string_ref(entry, "name");
        tile->material = game3d_json_string_ref(entry, "material");
        tile->nav_area = game3d_json_string_ref(entry, "navArea");
        tile->sidecar_path = game3d_json_resolved_sidecar_ref(entry, stream->terrain_manifest);
        game3d_stream_parse_collision_metadata(entry,
                                               RT_GAME3D_LAYER_WORLD,
                                               &tile->layer,
                                               &tile->has_layer,
                                               &tile->collision_mask,
                                               &tile->has_collision_mask,
                                               &tile->collision_enabled);
        tile->traversal_cost = game3d_stream_traversal_cost_or_default(entry);
        double zero[3] = {0.0, 0.0, 0.0};
        (void)game3d_json_vec3_or(entry, "center", tile->center, zero);
        double unit[3] = {1.0, 1.0, 1.0};
        (void)game3d_json_vec3_or(entry, "scale", tile->scale, unit);
        tile->width = game3d_json_i64_or(entry, "width", 2);
        tile->depth = game3d_json_i64_or(entry, "depth", tile->width);
        if (tile->width < 2)
            tile->width = 2;
        if (tile->depth < 2)
            tile->depth = 2;
        if (tile->width > 4096)
            tile->width = 4096;
        if (tile->depth > 4096)
            tile->depth = 4096;
        tile->radius = game3d_json_f64_or(entry, "radius", 0.0);
        if (!isfinite(tile->radius) || tile->radius < 0.0)
            tile->radius = 0.0;
        tile->resident_bytes = game3d_json_i64_or(entry, "bytes", 256 * 1024);
        if (tile->resident_bytes < 0)
            tile->resident_bytes = 0;
    }
    game3d_world_stream_clear_terrain_tiles(stream);
    stream->terrain_tiles = parsed;
    stream->terrain_tile_count = parsed_count;
    stream->terrain_tile_capacity = (int32_t)count64;
    stream->terrain_manifest_loaded = 1;
    rt_string_unref(text);
    game3d_release_ref(&root);
    return 1;
}

/// @brief GC finalizer for WorldStream3D: release world and manifest references.
static void game3d_world_stream_finalize(void *obj) {
    rt_game3d_world_stream *stream = (rt_game3d_world_stream *)obj;
    if (!stream)
        return;
    game3d_world_stream_clear_cells(stream);
    game3d_world_stream_clear_terrain_tiles(stream);
    if (stream->retains_world)
        game3d_release_ref(&stream->world);
    else
        stream->world = NULL;
    game3d_release_ref((void **)&stream->terrain_manifest);
    game3d_release_ref((void **)&stream->cells_manifest);
    free(stream->cell_candidates);
    stream->cell_candidates = NULL;
    stream->cell_candidate_capacity = 0;
    free(stream->terrain_candidates);
    stream->terrain_candidates = NULL;
    stream->terrain_candidate_capacity = 0;
}

/// @brief Convert a world-space load/unload radius to a cell-count radius (ceil of radius /
/// cell_size).
static int64_t game3d_stream_radius_slots(double radius, double cell_size) {
    if (!isfinite(radius) || radius <= 0.0)
        return 1;
    double slots = ceil(radius / cell_size);
    if (slots < 0.0)
        slots = 0.0;
    if (slots > 32767.0)
        slots = 32767.0;
    int64_t r = (int64_t)slots;
    int64_t side = r * 2 + 1;
    if (side > INT64_MAX / side)
        return INT64_MAX;
    return side * side;
}

/// @brief Case-insensitive ASCII equality for byte-size suffixes.
///
/// Manifest budget strings are ASCII configuration values, so this helper keeps
/// parsing independent of the process locale and avoids treating non-ASCII
/// bytes as meaningful suffix characters.
///
/// @param text Candidate suffix text.
/// @param len  Candidate suffix length in bytes.
/// @param lit  NUL-terminated literal to compare against.
/// @return Non-zero when @p text equals @p lit ignoring ASCII case.
static int game3d_ascii_ieq_len(const char *text, size_t len, const char *lit) {
    size_t lit_len;
    if (!text || !lit)
        return 0;
    lit_len = strlen(lit);
    if (len != lit_len)
        return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char a = (unsigned char)text[i];
        unsigned char b = (unsigned char)lit[i];
        if (a >= 'A' && a <= 'Z')
            a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = (unsigned char)(b + ('a' - 'A'));
        if (a != b)
            return 0;
    }
    return 1;
}

/// @brief Parse a byte-size literal with optional SI/IEC unit suffix.
///
/// Accepted examples include `1024`, `1024B`, `8 KB`, `8KiB`, `2MB`, and
/// `1 GiB`. Invalid text returns -1 so callers can fall back to non-literal
/// accounting paths such as manifest string storage.
///
/// @param text NUL-terminated candidate byte-size text.
/// @return Parsed byte count clamped to `INT64_MAX`, or -1 when not a literal.
static int64_t game3d_parse_byte_size_literal(const char *text) {
    const char *p = text;
    const char *suffix_start;
    const char *suffix_end;
    uint64_t value = 0;
    uint64_t multiplier = 1;
    int saw_digit = 0;
    if (!p)
        return -1;
    while (*p && isspace((unsigned char)*p))
        p++;
    while (*p && isdigit((unsigned char)*p)) {
        saw_digit = 1;
        uint64_t digit = (uint64_t)(*p - '0');
        if (value > (uint64_t)INT64_MAX / 10u ||
            (value == (uint64_t)INT64_MAX / 10u && digit > (uint64_t)INT64_MAX % 10u)) {
            value = (uint64_t)INT64_MAX;
            while (isdigit((unsigned char)*p))
                p++;
            break;
        }
        value = value * 10u + digit;
        p++;
    }
    if (!saw_digit)
        return -1;
    while (*p && isspace((unsigned char)*p))
        p++;
    suffix_start = p;
    while (*p && !isspace((unsigned char)*p))
        p++;
    suffix_end = p;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p != '\0')
        return -1;
    {
        size_t suffix_len = (size_t)(suffix_end - suffix_start);
        if (suffix_len == 0 || game3d_ascii_ieq_len(suffix_start, suffix_len, "b")) {
            multiplier = 1;
        } else if (game3d_ascii_ieq_len(suffix_start, suffix_len, "kb")) {
            multiplier = 1000u;
        } else if (game3d_ascii_ieq_len(suffix_start, suffix_len, "kib")) {
            multiplier = 1024u;
        } else if (game3d_ascii_ieq_len(suffix_start, suffix_len, "mb")) {
            multiplier = 1000u * 1000u;
        } else if (game3d_ascii_ieq_len(suffix_start, suffix_len, "mib")) {
            multiplier = 1024u * 1024u;
        } else if (game3d_ascii_ieq_len(suffix_start, suffix_len, "gb")) {
            multiplier = 1000u * 1000u * 1000u;
        } else if (game3d_ascii_ieq_len(suffix_start, suffix_len, "gib")) {
            multiplier = 1024u * 1024u * 1024u;
        } else {
            return -1;
        }
    }
    if (value > (uint64_t)INT64_MAX / multiplier)
        return INT64_MAX;
    return (int64_t)(value * multiplier);
}

/// @brief Measure a manifest reference or parse a byte-size literal.
///
/// Numeric manifest values are parsed as byte counts with optional units. Other
/// strings are treated as manifest path/reference storage and measured as their
/// NUL-terminated byte length to preserve existing residency accounting.
///
/// @param value Manifest reference/path or byte-size literal.
/// @return Byte count, clamped to `INT64_MAX`; 0 for NULL/empty strings.
static int64_t game3d_stream_manifest_bytes(rt_string value) {
    const char *bytes = value ? rt_string_cstr(value) : "";
    int64_t parsed;
    if (!bytes || bytes[0] == '\0')
        return 0;
    parsed = game3d_parse_byte_size_literal(bytes);
    if (parsed >= 0)
        return parsed;
    size_t len = strlen(bytes);
    if (len > (size_t)INT64_MAX - 1u)
        return INT64_MAX;
    return (int64_t)len + 1;
}

/// @brief Saturating signed 64-bit addition (clamps to INT64_MIN/MAX instead of overflowing).
static int64_t game3d_i64_saturating_add(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

/// @brief Saturating signed 64-bit multiplication (clamps to INT64_MIN/MAX on overflow).
static int64_t game3d_i64_saturating_mul(int64_t a, int64_t b) {
    if (a <= 0 || b <= 0)
        return 0;
    if (a > INT64_MAX / b)
        return INT64_MAX;
    return a * b;
}

/// @brief Compute this frame's streaming load budget (bytes) from the frame delta @p dt.
/// @details Caps how much may be streamed per frame so loading spikes don't stall the simulation.
static int64_t game3d_world_stream_update_load_budget(double dt) {
    if (!isfinite(dt) || dt <= 0.0)
        return 1;
    double budget = ceil(dt * 60.0);
    if (budget < 1.0)
        budget = 1.0;
    if (budget > 8.0)
        budget = 8.0;
    return (int64_t)budget;
}

/// @brief True when adding @p cost to @p current stays within @p budget without signed overflow.
static int game3d_i64_budget_can_fit(int64_t current, int64_t budget, int64_t cost) {
    if (budget < 0)
        return 1;
    if (current < 0 || cost < 0 || current > budget)
        return 0;
    return cost <= budget - current;
}

/// @brief Square a non-negative stream radius, saturating overflow and rejecting invalid radii.
static double game3d_stream_radius_sq(double radius) {
    if (!isfinite(radius) || radius < 0.0)
        return -1.0;
    if (radius == 0.0)
        return 0.0;
    if (radius > sqrt(DBL_MAX))
        return DBL_MAX;
    double sq = radius * radius;
    return isfinite(sq) ? sq : DBL_MAX;
}

typedef struct game3d_stream_recompute_state {
    int64_t load_budget;
    int64_t pending;
    int64_t cells;
    int64_t cell_bytes;
    int64_t terrain;
    int64_t terrain_bytes;
} game3d_stream_recompute_state;

static void game3d_world_stream_process_cell(rt_game3d_world_stream *stream,
                                             game3d_stream_recompute_state *state,
                                             rt_game3d_stream_cell *cell,
                                             int64_t budget) {
    int64_t bytes = game3d_stream_cell_bytes(cell);
    if (!game3d_i64_budget_can_fit(state->cell_bytes, budget, bytes)) {
        if (cell->resident)
            game3d_world_stream_unload_cell(stream, cell);
        return;
    }
    if (cell->resident) {
        state->cells++;
        state->cell_bytes = game3d_i64_saturating_add(state->cell_bytes, bytes);
        return;
    }
    if (state->load_budget == 0) {
        game3d_stream_increment_pending(&state->pending);
        return;
    }
    if (state->load_budget > 0)
        state->load_budget--;
    if (game3d_world_stream_load_cell(stream, cell)) {
        bytes = game3d_stream_cell_bytes(cell);
        if (!game3d_i64_budget_can_fit(state->cell_bytes, budget, bytes)) {
            game3d_world_stream_unload_cell(stream, cell);
            return;
        }
        state->cells++;
        state->cell_bytes = game3d_i64_saturating_add(state->cell_bytes, bytes);
    }
}

/// @brief Reconcile manifest-backed scene cells and update resident cell counters.
static void game3d_world_stream_recompute_cells(rt_game3d_world_stream *stream,
                                                game3d_stream_recompute_state *state) {
    if (stream->cells_manifest_loaded) {
        int32_t cell_count = game3d_world_stream_safe_cell_count(stream);
        int64_t manifest_bytes = game3d_stream_manifest_bytes(stream->cells_manifest);
        int64_t budget = stream->residency_budget_bytes;
        if (budget < 0 || manifest_bytes <= budget) {
            state->cell_bytes = manifest_bytes;
            rt_game3d_stream_load_candidate *candidates = NULL;
            int32_t candidate_count = 0;
            if (cell_count > 0 &&
                game3d_stream_reserve_load_candidates(
                    &stream->cell_candidates, &stream->cell_candidate_capacity, cell_count))
                candidates = stream->cell_candidates;
            for (int32_t i = 0; i < cell_count; ++i) {
                rt_game3d_stream_cell *cell = &stream->cells[i];
                if (cell->resident)
                    game3d_world_stream_refresh_cell_residency_bytes(cell);
                if (!game3d_stream_cell_desired(stream, cell)) {
                    game3d_world_stream_unload_cell(stream, cell);
                    continue;
                }
                if (candidates) {
                    candidates[candidate_count].index = i;
                    candidates[candidate_count].distance_sq =
                        game3d_stream_cell_distance_sq(stream, cell);
                    candidate_count++;
                }
            }
            if (candidates) {
                if (candidate_count > 1)
                    qsort(candidates,
                          (size_t)candidate_count,
                          sizeof(*candidates),
                          game3d_stream_load_candidate_cmp);
                for (int32_t ci = 0; ci < candidate_count; ++ci) {
                    rt_game3d_stream_cell *cell = &stream->cells[candidates[ci].index];
                    game3d_world_stream_process_cell(stream, state, cell, budget);
                }
            } else {
                double previous_distance_sq = -1.0;
                int32_t previous_index = -1;
                for (;;) {
                    double next_distance_sq = HUGE_VAL;
                    int32_t next_index = game3d_world_stream_pick_next_cell_noalloc(
                        stream, previous_distance_sq, previous_index, &next_distance_sq);
                    if (next_index < 0)
                        break;
                    game3d_world_stream_process_cell(
                        stream, state, &stream->cells[next_index], budget);
                    previous_distance_sq = next_distance_sq;
                    previous_index = next_index;
                }
            }
        } else {
            for (int32_t i = 0; i < cell_count; ++i)
                game3d_world_stream_unload_cell(stream, &stream->cells[i]);
            state->cell_bytes = 0;
        }
    } else {
        state->cells = game3d_string_has_bytes(stream->cells_manifest)
                           ? game3d_stream_radius_slots(stream->load_radius, 128.0)
                           : 0;
        state->cell_bytes =
            game3d_i64_saturating_add(game3d_stream_manifest_bytes(stream->cells_manifest),
                                      game3d_i64_saturating_mul(state->cells, 64 * 1024));
    }
}

static void game3d_world_stream_process_terrain_tile(rt_game3d_world_stream *stream,
                                                     game3d_stream_recompute_state *state,
                                                     rt_game3d_stream_terrain_tile *tile,
                                                     int terrain_unlimited,
                                                     int64_t remaining_budget) {
    int64_t tile_bytes = game3d_stream_terrain_tile_bytes(tile);
    if (!terrain_unlimited &&
        !game3d_i64_budget_can_fit(state->terrain_bytes, remaining_budget, tile_bytes)) {
        if (tile->resident)
            game3d_world_stream_unload_terrain_tile(stream, tile);
        return;
    }
    if (game3d_stream_terrain_tile_terrain_ref(tile)) {
        state->terrain++;
        state->terrain_bytes = game3d_i64_saturating_add(state->terrain_bytes, tile_bytes);
        return;
    }
    if (state->load_budget == 0) {
        game3d_stream_increment_pending(&state->pending);
        return;
    }
    if (state->load_budget > 0)
        state->load_budget--;
    if (game3d_world_stream_load_terrain_tile(stream, tile)) {
        tile_bytes = game3d_stream_terrain_tile_bytes(tile);
        if (!terrain_unlimited &&
            !game3d_i64_budget_can_fit(state->terrain_bytes, remaining_budget, tile_bytes)) {
            game3d_world_stream_unload_terrain_tile(stream, tile);
            return;
        }
        state->terrain++;
        state->terrain_bytes = game3d_i64_saturating_add(state->terrain_bytes, tile_bytes);
    }
}

static void game3d_world_stream_clamp_projected_terrain(rt_game3d_world_stream *stream,
                                                        game3d_stream_recompute_state *state,
                                                        int64_t budget) {
    int64_t bytes = game3d_i64_saturating_add(state->cell_bytes, state->terrain_bytes);
    while (budget >= 0 && bytes > budget && (state->cells > 0 || state->terrain > 0)) {
        if (!stream->cells_manifest_loaded && state->cells >= state->terrain && state->cells > 0)
            state->cells--;
        else if (state->terrain > 0)
            state->terrain--;
        else if (stream->cells_manifest_loaded)
            break;
        if (stream->cells_manifest_loaded)
            state->cell_bytes = game3d_world_stream_resident_cell_bytes(stream);
        else
            state->cell_bytes =
                game3d_i64_saturating_add(game3d_stream_manifest_bytes(stream->cells_manifest),
                                          game3d_i64_saturating_mul(state->cells, 64 * 1024));
        state->terrain_bytes =
            game3d_i64_saturating_add(game3d_stream_manifest_bytes(stream->terrain_manifest),
                                      game3d_i64_saturating_mul(state->terrain, 256 * 1024));
        bytes = game3d_i64_saturating_add(state->cell_bytes, state->terrain_bytes);
    }
}

/// @brief Reconcile manifest-backed terrain tiles and update resident terrain counters.
static void game3d_world_stream_recompute_terrain(rt_game3d_world_stream *stream,
                                                  game3d_stream_recompute_state *state) {
    int64_t budget = stream->residency_budget_bytes;
    if (stream->terrain_manifest_loaded) {
        int32_t tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
        int64_t manifest_bytes = game3d_stream_manifest_bytes(stream->terrain_manifest);
        int terrain_unlimited = budget < 0;
        int64_t remaining_budget =
            terrain_unlimited ? INT64_MAX
                              : (budget >= state->cell_bytes ? budget - state->cell_bytes : -1);
        if (terrain_unlimited || (remaining_budget >= 0 && manifest_bytes <= remaining_budget)) {
            state->terrain_bytes = manifest_bytes;
            rt_game3d_stream_load_candidate *candidates = NULL;
            int32_t candidate_count = 0;
            if (tile_count > 0 &&
                game3d_stream_reserve_load_candidates(
                    &stream->terrain_candidates, &stream->terrain_candidate_capacity, tile_count))
                candidates = stream->terrain_candidates;
            for (int32_t i = 0; i < tile_count; ++i) {
                rt_game3d_stream_terrain_tile *tile = &stream->terrain_tiles[i];
                if (!game3d_stream_terrain_tile_desired(stream, tile)) {
                    game3d_world_stream_unload_terrain_tile(stream, tile);
                    continue;
                }
                if (candidates) {
                    candidates[candidate_count].index = i;
                    candidates[candidate_count].distance_sq =
                        game3d_stream_terrain_tile_distance_sq(stream, tile);
                    candidate_count++;
                }
            }
            if (candidates) {
                if (candidate_count > 1)
                    qsort(candidates,
                          (size_t)candidate_count,
                          sizeof(*candidates),
                          game3d_stream_load_candidate_cmp);
                for (int32_t ci = 0; ci < candidate_count; ++ci) {
                    rt_game3d_stream_terrain_tile *tile =
                        &stream->terrain_tiles[candidates[ci].index];
                    game3d_world_stream_process_terrain_tile(
                        stream, state, tile, terrain_unlimited, remaining_budget);
                }
            } else {
                double previous_distance_sq = -1.0;
                int32_t previous_index = -1;
                for (;;) {
                    double next_distance_sq = HUGE_VAL;
                    int32_t next_index = game3d_world_stream_pick_next_terrain_noalloc(
                        stream, previous_distance_sq, previous_index, &next_distance_sq);
                    if (next_index < 0)
                        break;
                    game3d_world_stream_process_terrain_tile(stream,
                                                             state,
                                                             &stream->terrain_tiles[next_index],
                                                             terrain_unlimited,
                                                             remaining_budget);
                    previous_distance_sq = next_distance_sq;
                    previous_index = next_index;
                }
            }
        } else {
            for (int32_t i = 0; i < tile_count; ++i)
                game3d_world_stream_unload_terrain_tile(stream, &stream->terrain_tiles[i]);
            state->terrain_bytes = 0;
        }
    } else {
        state->terrain = game3d_string_has_bytes(stream->terrain_manifest)
                             ? game3d_stream_radius_slots(stream->load_radius, 256.0)
                             : 0;
        state->terrain_bytes =
            game3d_i64_saturating_add(game3d_stream_manifest_bytes(stream->terrain_manifest),
                                      game3d_i64_saturating_mul(state->terrain, 256 * 1024));
        game3d_world_stream_clamp_projected_terrain(stream, state, budget);
    }
}

/// @brief Apply final residency-budget clamping and publish telemetry counters.
static void game3d_world_stream_commit_recompute(rt_game3d_world_stream *stream,
                                                 game3d_stream_recompute_state *state) {
    int64_t budget = stream->residency_budget_bytes;
    int64_t bytes = game3d_i64_saturating_add(state->cell_bytes, state->terrain_bytes);
    if (budget >= 0 && bytes > budget) {
        if (stream->cells_manifest_loaded) {
            int32_t cell_count = game3d_world_stream_safe_cell_count(stream);
            for (int32_t i = 0; i < cell_count; ++i)
                game3d_world_stream_unload_cell(stream, &stream->cells[i]);
        }
        if (stream->terrain_manifest_loaded) {
            int32_t tile_count = game3d_world_stream_safe_terrain_tile_count(stream);
            for (int32_t i = 0; i < tile_count; ++i)
                game3d_world_stream_unload_terrain_tile(stream, &stream->terrain_tiles[i]);
        }
        state->cells = 0;
        state->terrain = 0;
        bytes = 0;
    }
    stream->resident_cell_count = state->cells;
    stream->resident_terrain_tile_count = state->terrain;
    stream->resident_bytes = bytes;
    stream->pending_request_count = state->pending;
}

/// @brief Reconcile streamed content with the camera: load in-radius cells/tiles and unload far
/// ones.
/// @details Loads nearest-first within the per-frame byte budget and the residency budget, and
/// unloads
///          anything beyond the unload radius — the heart of the open-world streaming loop.
static void game3d_world_stream_recompute(rt_game3d_world_stream *stream, int64_t load_budget) {
    if (!stream)
        return;
    game3d_stream_recompute_state state;
    memset(&state, 0, sizeof(state));
    state.load_budget = load_budget;
    game3d_world_stream_recompute_cells(stream, &state);
    game3d_world_stream_recompute_terrain(stream, &state);
    game3d_world_stream_commit_recompute(stream, &state);
}
