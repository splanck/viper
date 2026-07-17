//===----------------------------------------------------------------------===//
// Part of the Zanna project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_gltf_preload.inc  (#included from rt_gltf.c; not a
// standalone translation unit) Purpose: glTF preload-bundle staging (buffers, views, accessors,
// meshes, morphs, images)
//===----------------------------------------------------------------------===//

/// @brief Resolve a raw JSON texture object's source, including KHR_texture_basisu.
static int gltf_json_texture_source_index(const char *json,
                                          size_t len,
                                          size_t obj_start,
                                          size_t obj_end) {
    size_t extensions_start;
    size_t extensions_end;
    size_t basisu_start;
    size_t basisu_end;
    int source = gltf_json_object_get_int(json, len, obj_start, obj_end, "source", -1);
    if (!gltf_json_object_find_value(
            json, len, obj_start, obj_end, "extensions", &extensions_start, &extensions_end))
        return source;
    extensions_start = gltf_json_skip_ws(json, len, extensions_start);
    if (extensions_start >= extensions_end || json[extensions_start] != '{')
        return source;
    if (!gltf_json_object_find_value(json,
                                     len,
                                     extensions_start,
                                     extensions_end,
                                     "KHR_texture_basisu",
                                     &basisu_start,
                                     &basisu_end))
        return source;
    basisu_start = gltf_json_skip_ws(json, len, basisu_start);
    if (basisu_start >= basisu_end || json[basisu_start] != '{')
        return source;
    {
        int basisu_source =
            gltf_json_object_get_int(json, len, basisu_start, basisu_end, "source", -1);
        return basisu_source >= 0 ? basisu_source : source;
    }
}

/// @brief Extract the glTF JSON text from raw bytes, handling both .gltf and binary .glb.
/// @details For GLB (magic "glTF", version 2) it validates the chunk table and copies the first
///          (JSON) chunk; for plain .gltf it copies the bytes verbatim. Returns a NUL-terminated
///          owned copy (caller frees) with @p out_len set, or NULL on a malformed container.
static char *gltf_json_copy_from_root_bytes(const uint8_t *data, size_t len, size_t *out_len) {
    char *json_copy = NULL;
    if (out_len)
        *out_len = 0;
    if (!data || len == 0)
        return NULL;
    if (len > (size_t)UINT32_MAX)
        return NULL;
    if (len >= 12 && data[0] == 0x67 && data[1] == 0x6C && data[2] == 0x54 && data[3] == 0x46) {
        uint32_t version = gltf_read_u32_le(data + 4);
        uint32_t declared_len = gltf_read_u32_le(data + 8);
        size_t pos = 12;
        int chunk_index = 0;
        if (version != 2 || declared_len != (uint32_t)len)
            return NULL;
        while (pos + 8u <= len) {
            uint32_t chunk_len = gltf_read_u32_le(data + pos);
            uint32_t chunk_type = gltf_read_u32_le(data + pos + 4u);
            pos += 8u;
            if ((chunk_len & 3u) != 0 || chunk_len > len - pos)
                return NULL;
            if (chunk_index == 0 && chunk_type != 0x4E4F534A)
                return NULL;
            if (chunk_type == 0x4E4F534A) {
                json_copy = (char *)malloc((size_t)chunk_len + 1u);
                if (!json_copy)
                    return NULL;
                memcpy(json_copy, data + pos, (size_t)chunk_len);
                json_copy[chunk_len] = '\0';
                if (out_len)
                    *out_len = (size_t)chunk_len;
                return json_copy;
            }
            pos += (size_t)chunk_len;
            chunk_index++;
        }
        return NULL;
    }
    if (len > SIZE_MAX - 1u)
        return NULL;
    json_copy = (char *)malloc(len + 1u);
    if (!json_copy)
        return NULL;
    memcpy(json_copy, data, len);
    json_copy[len] = '\0';
    if (out_len)
        *out_len = len;
    return json_copy;
}

/// @brief Stage an image payload under @p key, decoding to RGBA up front when possible.
/// @details Takes ownership of @p data. If it decodes to an RGBA blob the dependency is tagged
///          IMAGE_RGBA (ready for streaming upload); otherwise the raw bytes are kept as IMAGE.
///          When @p required and a supported format fails to decode, sets @p error and fails.
/// @return 1 on success (including the no-op for empty data), 0 on a required decode/stage failure.
static int gltf_preload_bundle_add_image_payload(rt_gltf_preload_bundle *bundle,
                                                 const char *key,
                                                 const char *mime_or_name,
                                                 uint8_t *data,
                                                 size_t data_len,
                                                 int required,
                                                 char *error,
                                                 size_t error_cap) {
    uint8_t *rgba_blob = NULL;
    size_t rgba_len = 0;
    gltf_preload_dependency_kind_t kind = GLTF_PRELOAD_DEP_IMAGE;
    if (!data || data_len == 0)
        return 1;
    if (gltf_decode_image_payload_to_rgba_blob(
            mime_or_name, data, data_len, &rgba_blob, &rgba_len)) {
        free(data);
        data = rgba_blob;
        data_len = rgba_len;
        kind = GLTF_PRELOAD_DEP_IMAGE_RGBA;
    }
    if (required && kind != GLTF_PRELOAD_DEP_IMAGE_RGBA &&
        gltf_preload_image_is_supported_format(mime_or_name)) {
        free(data);
        gltf_preload_set_error(error, error_cap, "invalid glTF image payload");
        return 0;
    }
    if (!gltf_preload_bundle_add_dependency(bundle, key, kind, data, data_len)) {
        free(data);
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    return 1;
}

/// @brief Resolve and stage an external (non-data-URI) image referenced by @p uri.
/// @details Resolves @p uri relative to @p model_path, loads the bytes (from the asset system or
///          disk), and stages them via add_image_payload keyed by the resolved path. data: URIs and
///          unreadable optional images are skipped; an unreadable required image sets @p error.
static int gltf_preload_stage_external_image(rt_gltf_preload_bundle *bundle,
                                             const char *model_path,
                                             const char *uri,
                                             int load_assets,
                                             int required,
                                             char *error,
                                             size_t error_cap) {
    char *resource_path;
    uint8_t *data;
    size_t data_len = 0;
    int ok;
    if (!uri || strncmp(uri, "data:", 5) == 0)
        return 1;
    resource_path = gltf_resolve_relative_path_alloc(model_path, uri);
    if (!resource_path)
        return 1;
    data = gltf_load_dependency_bytes(resource_path, load_assets, 0u, NULL, &data_len);
    if (!data) {
        if (required && gltf_preload_image_is_supported_format(resource_path)) {
            gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
            free(resource_path);
            return 0;
        }
        free(resource_path);
        return 1;
    }
    ok = gltf_preload_bundle_add_image_payload(
        bundle, resource_path, resource_path, data, data_len, required, error, error_cap);
    free(resource_path);
    return ok;
}

/// @brief Format the dependency key for inline buffer @p index ("<gltf:inline-buffer:N>").
static void gltf_preload_buffer_key(int index, char *out, size_t out_cap) {
    if (!out || out_cap == 0)
        return;
    snprintf(out, out_cap, "<gltf:inline-buffer:%d>", index);
}

/// @brief Format the dependency key for inline image @p index, with an extension chosen by MIME
/// type.
static void gltf_preload_image_key(int index, const char *mime_type, char *out, size_t out_cap) {
    const char *ext = ".bin";
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
    snprintf(out, out_cap, "inline-image-%d%s", index, ext);
}

/// @brief Format the mesh-POD dependency key for a (mesh, primitive) pair.
static void gltf_preload_mesh_key(int mesh_index, int primitive_index, char *out, size_t out_cap) {
    if (!out || out_cap == 0)
        return;
    snprintf(out, out_cap, "<gltf:mesh-pod:%d:%d>", mesh_index, primitive_index);
}

/// @brief Format the morph-POD dependency key for a (mesh, primitive) pair.
static void gltf_preload_morph_key(int mesh_index, int primitive_index, char *out, size_t out_cap) {
    if (!out || out_cap == 0)
        return;
    snprintf(out, out_cap, "<gltf:morph-pod:%d:%d>", mesh_index, primitive_index);
}

/// @brief Locate the binary (BIN) chunk inside a GLB container.
/// @details Validates the "glTF" magic/version/length and walks the chunk table (the JSON chunk
///          must come first) to find the 0x004E4942 ("BIN\0") chunk.
/// @return 1 with @p out_bin / @p out_bin_len set, 0 for a plain .gltf or a malformed/absent BIN.
static int gltf_root_find_glb_bin(const uint8_t *data,
                                  size_t len,
                                  const uint8_t **out_bin,
                                  size_t *out_bin_len) {
    size_t pos = 12;
    int chunk_index = 0;
    if (out_bin)
        *out_bin = NULL;
    if (out_bin_len)
        *out_bin_len = 0;
    if (!data || len < 12 || data[0] != 0x67 || data[1] != 0x6C || data[2] != 0x54 ||
        data[3] != 0x46)
        return 0;
    if (len > (size_t)UINT32_MAX)
        return 0;
    if (gltf_read_u32_le(data + 4) != 2 || gltf_read_u32_le(data + 8) != (uint32_t)len)
        return 0;
    while (pos + 8u <= len) {
        uint32_t chunk_len = gltf_read_u32_le(data + pos);
        uint32_t chunk_type = gltf_read_u32_le(data + pos + 4u);
        pos += 8u;
        if ((chunk_len & 3u) != 0 || chunk_len > len - pos)
            return 0;
        if (chunk_index == 0 && chunk_type != 0x4E4F534A)
            return 0;
        if (chunk_type == 0x004E4942) {
            if (out_bin)
                *out_bin = data + pos;
            if (out_bin_len)
                *out_bin_len = (size_t)chunk_len;
            return 1;
        }
        pos += (size_t)chunk_len;
        chunk_index++;
    }
    return 0;
}

/// @brief Ensure the buffer-ref array holds at least @p required_count entries, zero-filling
/// growth.
/// @return 1 on success, 0 on bad args or reallocation failure.
static int gltf_preload_grow_buffer_refs(gltf_preload_buffer_ref_t **refs,
                                         int *capacity,
                                         int required_count) {
    gltf_preload_buffer_ref_t *grown;
    int next_capacity;
    if (!refs || !capacity || required_count <= 0)
        return 0;
    if (*capacity >= required_count)
        return 1;
    if (!gltf_preload_next_capacity_int(
            *capacity, required_count, 8, sizeof(**refs), &next_capacity))
        return 0;
    grown = (gltf_preload_buffer_ref_t *)realloc(*refs, (size_t)next_capacity * sizeof(**refs));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(next_capacity - *capacity) * sizeof(*grown));
    *refs = grown;
    *capacity = next_capacity;
    return 1;
}

static void gltf_preload_set_buffer_ref(gltf_preload_buffer_ref_t *refs,
                                        int index,
                                        const uint8_t *data,
                                        size_t len) {
    refs[index].data = data;
    refs[index].len = len;
}

static int gltf_preload_stage_inline_buffer(rt_gltf_preload_bundle *bundle,
                                            int index,
                                            const char *uri,
                                            size_t required_len,
                                            gltf_preload_buffer_ref_t *refs) {
    char key[64];
    char mime_type[64];
    uint8_t *data = NULL;
    size_t data_len = 0;
    if (!gltf_parse_data_uri(uri, mime_type, sizeof(mime_type), &data, &data_len) ||
        data_len < required_len) {
        free(data);
        return required_len == 0u;
    }
    gltf_preload_buffer_key(index, key, sizeof(key));
    if (!gltf_preload_bundle_add_dependency(bundle, key, GLTF_PRELOAD_DEP_BUFFER, data, data_len)) {
        free(data);
        return 0;
    }
    gltf_preload_set_buffer_ref(refs, index, data, data_len);
    return 1;
}

static int gltf_preload_stage_external_buffer(rt_gltf_preload_bundle *bundle,
                                              const char *model_path,
                                              int index,
                                              const char *uri,
                                              int load_assets,
                                              size_t required_len,
                                              gltf_preload_buffer_ref_t *refs) {
    char *resource_path;
    uint8_t *data = NULL;
    size_t data_len = 0;
    resource_path = gltf_resolve_relative_path_alloc(model_path, uri);
    if (resource_path) {
        data =
            gltf_load_dependency_bytes(resource_path, load_assets, required_len, NULL, &data_len);
        if (data) {
            if (!gltf_preload_bundle_add_dependency(
                    bundle, resource_path, GLTF_PRELOAD_DEP_BUFFER, data, data_len)) {
                free(data);
                free(resource_path);
                return 0;
            }
            gltf_preload_set_buffer_ref(refs, index, data, data_len);
        }
        free(resource_path);
    }
    return data || required_len == 0u;
}

static int gltf_preload_stage_buffer_uri(rt_gltf_preload_bundle *bundle,
                                         const char *model_path,
                                         int index,
                                         const char *uri,
                                         int load_assets,
                                         size_t required_len,
                                         gltf_preload_buffer_ref_t *refs) {
    if (strncmp(uri, "data:", 5) == 0)
        return gltf_preload_stage_inline_buffer(bundle, index, uri, required_len, refs);
    return gltf_preload_stage_external_buffer(
        bundle, model_path, index, uri, load_assets, required_len, refs);
}

static int gltf_preload_bind_glb_buffer(int index,
                                        const uint8_t *glb_bin,
                                        size_t glb_bin_len,
                                        size_t required_len,
                                        gltf_preload_buffer_ref_t *refs) {
    if (index != 0 || !glb_bin || required_len > glb_bin_len || required_len > SIZE_MAX - 3u ||
        glb_bin_len > required_len + 3u)
        return 0;
    gltf_preload_set_buffer_ref(refs, index, glb_bin, required_len);
    return 1;
}

/// @brief Stage every glTF buffer and build the buffer-ref table the views/accessors resolve
/// against.
/// @details Walks the top-level "buffers" array: a data: URI is decoded inline, an external URI is
///          resolved/loaded, and an absent URI binds to the GLB BIN chunk. Each ref records the
///          bytes so later stages can slice them. Sets @p error and returns 0 on a staging failure.
static int gltf_preload_stage_buffers(rt_gltf_preload_bundle *bundle,
                                      const char *model_path,
                                      const char *json,
                                      size_t json_len,
                                      int load_assets,
                                      gltf_preload_buffer_ref_t **out_refs,
                                      int *out_count,
                                      char *error,
                                      size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    const uint8_t *glb_bin = NULL;
    size_t glb_bin_len = 0;
    gltf_preload_buffer_ref_t *refs = NULL;
    int refs_capacity = 0;
    int index = 0;
    if (out_refs)
        *out_refs = NULL;
    if (out_count)
        *out_count = 0;
    gltf_root_find_glb_bin(
        bundle ? bundle->root_data : NULL, bundle ? bundle->root_size : 0, &glb_bin, &glb_bin_len);
    if (!gltf_json_find_top_level_array(json, json_len, "buffers", &array_start, &array_end))
        return 1;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            char *uri;
            size_t required_len = 0;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (!rt_untrusted_count_ok((int64_t)index + 1, 1u, json_len) ||
                !gltf_preload_grow_buffer_refs(&refs, &refs_capacity, index + 1)) {
                free(refs);
                gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                return 0;
            }
            uri = gltf_json_object_get_string(json, json_len, pos, object_end, "uri");
            required_len =
                gltf_json_object_get_size(json, json_len, pos, object_end, "byteLength", 0u);
            if (uri) {
                int ok = gltf_preload_stage_buffer_uri(
                    bundle, model_path, index, uri, load_assets, required_len, refs);
                free(uri);
                if (!ok) {
                    free(refs);
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    return 0;
                }
            } else {
                if (!gltf_preload_bind_glb_buffer(
                        index, glb_bin, glb_bin_len, required_len, refs)) {
                    free(refs);
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    return 0;
                }
            }
            index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    if (out_refs)
        *out_refs = refs;
    else
        free(refs);
    if (out_count)
        *out_count = index;
    return 1;
}

/// @brief Ensure the buffer-view-ref array holds at least @p required_count entries (zero-filled
/// growth).
static int gltf_preload_grow_view_refs(gltf_preload_buffer_view_ref_t **views,
                                       int *capacity,
                                       int required_count) {
    gltf_preload_buffer_view_ref_t *grown;
    int next_capacity;
    if (!views || !capacity || required_count <= 0)
        return 0;
    if (*capacity >= required_count)
        return 1;
    if (!gltf_preload_next_capacity_int(
            *capacity, required_count, 8, sizeof(**views), &next_capacity))
        return 0;
    grown =
        (gltf_preload_buffer_view_ref_t *)realloc(*views, (size_t)next_capacity * sizeof(**views));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(next_capacity - *capacity) * sizeof(*grown));
    *views = grown;
    *capacity = next_capacity;
    return 1;
}

/// @brief Parse the top-level "bufferViews" array into a ref table (buffer, offset, length,
/// stride).
/// @details A view is marked valid when it names a buffer and has a positive length. Returns the
///          table via @p out_views / @p out_count; on allocation failure sets @p error and returns
///          0.
static int gltf_preload_parse_buffer_views(const char *json,
                                           size_t json_len,
                                           gltf_preload_buffer_view_ref_t **out_views,
                                           int *out_count,
                                           char *error,
                                           size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    gltf_preload_buffer_view_ref_t *views = NULL;
    int views_capacity = 0;
    int index = 0;
    if (out_views)
        *out_views = NULL;
    if (out_count)
        *out_count = 0;
    if (!gltf_json_find_top_level_array(json, json_len, "bufferViews", &array_start, &array_end))
        return 1;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (!rt_untrusted_count_ok((int64_t)index + 1, 1u, json_len) ||
                !gltf_preload_grow_view_refs(&views, &views_capacity, index + 1)) {
                free(views);
                gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                return 0;
            }
            views[index].buffer =
                gltf_json_object_get_int(json, json_len, pos, object_end, "buffer", -1);
            views[index].byte_offset =
                gltf_json_object_get_size(json, json_len, pos, object_end, "byteOffset", 0u);
            views[index].byte_length =
                gltf_json_object_get_size(json, json_len, pos, object_end, "byteLength", 0u);
            views[index].byte_stride =
                gltf_json_object_get_size(json, json_len, pos, object_end, "byteStride", 0u);
            views[index].valid = views[index].buffer >= 0 && views[index].byte_length > 0;
            /* EXT_meshopt_compression views resolve through the sync loader's decoded
             * shadow table; the parent range may point at a data-less placeholder
             * buffer, so worker staging must not read it. Marking the view invalid
             * makes dependent primitives fall back to the commit-time sync decode. */
            {
                size_t ext_start;
                size_t ext_end;
                if (gltf_json_object_find_value(
                        json, json_len, pos, object_end, "extensions", &ext_start, &ext_end) &&
                    ext_start < ext_end && json[ext_start] == '{' &&
                    gltf_json_object_find_value(json,
                                                json_len,
                                                ext_start,
                                                ext_end,
                                                "EXT_meshopt_compression",
                                                &ext_start,
                                                &ext_end))
                    views[index].valid = 0;
            }
            index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    if (out_views)
        *out_views = views;
    else
        free(views);
    if (out_count)
        *out_count = index;
    return 1;
}

/// @brief Verify one accessor's element range lies entirely within its buffer view and buffer.
/// @details Computes element size from componentType x type, derives the stride (explicit or
///          tightly-packed), and overflow-checks that the last element's end offset fits both the
///          view's byteLength and the backing buffer. This is the security gate that stops a
///          malformed accessor from inducing out-of-bounds reads during mesh packing.
/// @return 1 if valid (or count 0 / no bufferView), 0 if any bound is exceeded or types are bad.
static int gltf_preload_validate_accessor(const char *json,
                                          size_t json_len,
                                          size_t obj_start,
                                          size_t obj_end,
                                          const gltf_preload_buffer_ref_t *buffers,
                                          int buffer_count,
                                          const gltf_preload_buffer_view_ref_t *views,
                                          int view_count) {
    int view_index;
    int component_type;
    int component_size;
    int component_count;
    int count;
    size_t accessor_offset;
    size_t element_size;
    size_t stride;
    size_t last_offset;
    size_t accessor_last;
    size_t accessor_end;
    size_t buffer_end;
    char *type;
    const gltf_preload_buffer_view_ref_t *view;

    count = gltf_json_object_get_int(json, json_len, obj_start, obj_end, "count", -1);
    if (count < 0)
        return 0;
    if (count == 0)
        return 1;

    component_type =
        gltf_json_object_get_int(json, json_len, obj_start, obj_end, "componentType", 0);
    component_size = gltf_component_size(component_type);
    type = gltf_json_object_get_string(json, json_len, obj_start, obj_end, "type");
    component_count = gltf_component_count(type);
    free(type);
    if (component_size <= 0 || component_count <= 0)
        return 0;
    if (!gltf_checked_mul_size((size_t)component_size, (size_t)component_count, &element_size))
        return 0;

    view_index = gltf_json_object_get_int(json, json_len, obj_start, obj_end, "bufferView", -1);
    if (view_index < 0)
        return 1;
    if (!views || view_index >= view_count || !views[view_index].valid)
        return 0;
    view = &views[view_index];
    if (view->buffer < 0 || view->buffer >= buffer_count || !buffers || !buffers[view->buffer].data)
        return 0;

    accessor_offset =
        gltf_json_object_get_size(json, json_len, obj_start, obj_end, "byteOffset", 0u);
    stride = view->byte_stride > 0u ? view->byte_stride : element_size;
    if (stride < element_size)
        return 0;
    if (accessor_offset > view->byte_length ||
        !rt_untrusted_count_ok((int64_t)count, element_size, view->byte_length - accessor_offset))
        return 0;
    if (!gltf_checked_mul_size((size_t)(count - 1), stride, &last_offset) ||
        !gltf_checked_add_size(accessor_offset, last_offset, &accessor_last) ||
        !gltf_checked_add_size(accessor_last, element_size, &accessor_end))
        return 0;
    if (accessor_end > view->byte_length)
        return 0;
    if (!gltf_checked_add_size(view->byte_offset, accessor_end, &buffer_end))
        return 0;
    return buffer_end <= buffers[view->buffer].len;
}

/// @brief Validate every accessor in the top-level "accessors" array against its view/buffer
/// bounds.
/// @details Fails fast (setting @p error) on the first out-of-range accessor. Returns 1 if all
/// pass.
static int gltf_preload_validate_accessors(const char *json,
                                           size_t json_len,
                                           const gltf_preload_buffer_ref_t *buffers,
                                           int buffer_count,
                                           const gltf_preload_buffer_view_ref_t *views,
                                           int view_count,
                                           char *error,
                                           size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    if (!gltf_json_find_top_level_array(json, json_len, "accessors", &array_start, &array_end))
        return 1;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (!gltf_preload_validate_accessor(
                    json, json_len, pos, object_end, buffers, buffer_count, views, view_count)) {
                gltf_preload_set_error(error, error_cap, "invalid glTF accessor range");
                return 0;
            }
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    return 1;
}

/// @brief Ensure the accessor-ref array holds at least @p required_count entries (zero-filled
/// growth).
static int gltf_preload_grow_accessor_refs(gltf_preload_accessor_ref_t **accessors,
                                           int *capacity,
                                           int required_count) {
    gltf_preload_accessor_ref_t *grown;
    int next_capacity;
    if (!accessors || !capacity || required_count <= 0)
        return 0;
    if (*capacity >= required_count)
        return 1;
    if (!gltf_preload_next_capacity_int(
            *capacity, required_count, 8, sizeof(**accessors), &next_capacity))
        return 0;
    grown = (gltf_preload_accessor_ref_t *)realloc(*accessors,
                                                   (size_t)next_capacity * sizeof(**accessors));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(next_capacity - *capacity) * sizeof(*grown));
    *accessors = grown;
    *capacity = next_capacity;
    return 1;
}

/// @brief Parse the top-level "accessors" array (including sparse substitutions) into a ref table.
/// @details Records component type/count, element count, normalization, and any sparse
///          indices/values sub-views. Marks each accessor valid only with a positive count and
///          known component size. Returns the table via @p out_accessors / @p out_count.
static int gltf_preload_parse_accessors(const char *json,
                                        size_t json_len,
                                        gltf_preload_accessor_ref_t **out_accessors,
                                        int *out_count,
                                        char *error,
                                        size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    gltf_preload_accessor_ref_t *accessors = NULL;
    int accessors_capacity = 0;
    int index = 0;
    if (out_accessors)
        *out_accessors = NULL;
    if (out_count)
        *out_count = 0;
    if (!gltf_json_find_top_level_array(json, json_len, "accessors", &array_start, &array_end))
        return 1;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            char *type;
            size_t sparse_start;
            size_t sparse_end;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (!rt_untrusted_count_ok((int64_t)index + 1, 1u, json_len) ||
                !gltf_preload_grow_accessor_refs(&accessors, &accessors_capacity, index + 1)) {
                free(accessors);
                gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                return 0;
            }
            type = gltf_json_object_get_string(json, json_len, pos, object_end, "type");
            accessors[index].view =
                gltf_json_object_get_int(json, json_len, pos, object_end, "bufferView", -1);
            accessors[index].sparse_indices_view = -1;
            accessors[index].sparse_values_view = -1;
            accessors[index].byte_offset =
                gltf_json_object_get_size(json, json_len, pos, object_end, "byteOffset", 0u);
            accessors[index].comp_type =
                gltf_json_object_get_int(json, json_len, pos, object_end, "componentType", 0);
            accessors[index].comp_count = gltf_component_count(type);
            accessors[index].count =
                gltf_json_object_get_int(json, json_len, pos, object_end, "count", 0);
            accessors[index].normalized = (int8_t)gltf_json_object_get_boolish(
                json, json_len, pos, object_end, "normalized", 0);
            if (gltf_json_object_find_value(
                    json, json_len, pos, object_end, "sparse", &sparse_start, &sparse_end) &&
                sparse_start < sparse_end && json[sparse_start] == '{') {
                size_t indices_start;
                size_t indices_end;
                size_t values_start;
                size_t values_end;
                accessors[index].sparse_count =
                    gltf_json_object_get_int(json, json_len, sparse_start, sparse_end, "count", 0);
                if (accessors[index].sparse_count > 0 &&
                    gltf_json_object_find_value(json,
                                                json_len,
                                                sparse_start,
                                                sparse_end,
                                                "indices",
                                                &indices_start,
                                                &indices_end) &&
                    gltf_json_object_find_value(json,
                                                json_len,
                                                sparse_start,
                                                sparse_end,
                                                "values",
                                                &values_start,
                                                &values_end) &&
                    indices_start < indices_end && values_start < values_end &&
                    json[indices_start] == '{' && json[values_start] == '{') {
                    accessors[index].has_sparse = 1;
                    accessors[index].sparse_indices_view = gltf_json_object_get_int(
                        json, json_len, indices_start, indices_end, "bufferView", -1);
                    accessors[index].sparse_indices_offset = gltf_json_object_get_size(
                        json, json_len, indices_start, indices_end, "byteOffset", 0u);
                    accessors[index].sparse_index_comp_type = gltf_json_object_get_int(
                        json, json_len, indices_start, indices_end, "componentType", 0);
                    accessors[index].sparse_values_view = gltf_json_object_get_int(
                        json, json_len, values_start, values_end, "bufferView", -1);
                    accessors[index].sparse_values_offset = gltf_json_object_get_size(
                        json, json_len, values_start, values_end, "byteOffset", 0u);
                }
            }
            accessors[index].valid = accessors[index].count > 0 &&
                                     gltf_component_size(accessors[index].comp_type) > 0 &&
                                     accessors[index].comp_count > 0;
            free(type);
            index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    if (out_accessors)
        *out_accessors = accessors;
    else
        free(accessors);
    if (out_count)
        *out_count = index;
    return 1;
}

/// @brief Resolve an accessor index to a concrete, bounds-checked view over buffer memory.
/// @details Computes element size and stride, validates the dense range fits the view and buffer,
///          and — for sparse accessors — validates the index/value sub-views and that sparse
///          indices are strictly increasing and in-range. Fills @p out with raw pointers the
///          packers read.
/// @return 1 with @p out populated, or 0 if the accessor is invalid or any bound/ordering check
/// fails.
static int gltf_preload_resolve_accessor_view(const gltf_preload_accessor_ref_t *accessors,
                                              int accessor_count,
                                              int accessor_index,
                                              const gltf_preload_buffer_ref_t *buffers,
                                              int buffer_count,
                                              const gltf_preload_buffer_view_ref_t *views,
                                              int view_count,
                                              gltf_accessor_view_t *out) {
    const gltf_preload_accessor_ref_t *accessor;
    const gltf_preload_buffer_view_ref_t *view;
    size_t element_size;
    size_t stride;
    size_t last_offset;
    size_t accessor_last;
    size_t accessor_end;
    size_t buffer_offset;
    size_t buffer_end;
    int comp_size;
    if (!accessors || accessor_index < 0 || accessor_index >= accessor_count || !out)
        return 0;
    accessor = &accessors[accessor_index];
    if (!accessor->valid)
        return 0;
    comp_size = gltf_component_size(accessor->comp_type);
    if (comp_size <= 0 || accessor->comp_count <= 0 || accessor->count <= 0)
        return 0;
    if (!gltf_checked_mul_size((size_t)comp_size, (size_t)accessor->comp_count, &element_size))
        return 0;
    if (element_size > INT32_MAX)
        return 0;
    memset(out, 0, sizeof(*out));
    out->count = (int32_t)accessor->count;
    out->stride = (int32_t)element_size;
    out->comp_type = accessor->comp_type;
    out->comp_count = accessor->comp_count;
    out->normalized = accessor->normalized;
    if (accessor->view >= 0) {
        if (accessor->view >= view_count)
            return 0;
        view = views ? &views[accessor->view] : NULL;
        if (!view || !view->valid || view->buffer < 0 || view->buffer >= buffer_count || !buffers ||
            !buffers[view->buffer].data)
            return 0;
        stride = view->byte_stride > 0u ? view->byte_stride : element_size;
        if (stride < element_size || stride > INT32_MAX)
            return 0;
        if (accessor->byte_offset > view->byte_length ||
            !rt_untrusted_count_ok(
                (int64_t)accessor->count, element_size, view->byte_length - accessor->byte_offset))
            return 0;
        if (!gltf_checked_mul_size((size_t)(accessor->count - 1), stride, &last_offset) ||
            !gltf_checked_add_size(accessor->byte_offset, last_offset, &accessor_last) ||
            !gltf_checked_add_size(accessor_last, element_size, &accessor_end))
            return 0;
        if (accessor_end > view->byte_length)
            return 0;
        if (!gltf_checked_add_size(view->byte_offset, accessor->byte_offset, &buffer_offset) ||
            !gltf_checked_add_size(view->byte_offset, accessor_end, &buffer_end))
            return 0;
        if (buffer_end > buffers[view->buffer].len)
            return 0;
        out->data = buffers[view->buffer].data + buffer_offset;
        out->stride = (int32_t)stride;
    } else if (!accessor->has_sparse) {
        return 0;
    }
    if (accessor->has_sparse) {
        const gltf_preload_buffer_view_ref_t *indices_view;
        const gltf_preload_buffer_view_ref_t *values_view;
        int index_comp_size = gltf_component_size(accessor->sparse_index_comp_type);
        size_t index_bytes;
        size_t value_bytes;
        size_t index_end;
        size_t value_end;
        size_t index_buffer_offset;
        size_t value_buffer_offset;
        uint32_t previous_sparse_index = 0;
        if (accessor->sparse_count <= 0 ||
            (accessor->sparse_index_comp_type != 5121 && accessor->sparse_index_comp_type != 5123 &&
             accessor->sparse_index_comp_type != 5125) ||
            index_comp_size <= 0 || accessor->sparse_indices_view < 0 ||
            accessor->sparse_indices_view >= view_count || accessor->sparse_values_view < 0 ||
            accessor->sparse_values_view >= view_count || !views || !buffers)
            return 0;
        indices_view = &views[accessor->sparse_indices_view];
        values_view = &views[accessor->sparse_values_view];
        if (!indices_view->valid || !values_view->valid || indices_view->buffer < 0 ||
            values_view->buffer < 0 || indices_view->buffer >= buffer_count ||
            values_view->buffer >= buffer_count || !buffers[indices_view->buffer].data ||
            !buffers[values_view->buffer].data)
            return 0;
        if (accessor->sparse_indices_offset > indices_view->byte_length ||
            accessor->sparse_values_offset > values_view->byte_length ||
            !rt_untrusted_count_ok((int64_t)accessor->sparse_count,
                                   (size_t)index_comp_size,
                                   indices_view->byte_length - accessor->sparse_indices_offset) ||
            !rt_untrusted_count_ok((int64_t)accessor->sparse_count,
                                   element_size,
                                   values_view->byte_length - accessor->sparse_values_offset))
            return 0;
        if (!gltf_checked_mul_size(
                (size_t)accessor->sparse_count, (size_t)index_comp_size, &index_bytes) ||
            !gltf_checked_mul_size((size_t)accessor->sparse_count, element_size, &value_bytes) ||
            !gltf_checked_add_size(accessor->sparse_indices_offset, index_bytes, &index_end) ||
            !gltf_checked_add_size(accessor->sparse_values_offset, value_bytes, &value_end))
            return 0;
        if (index_end > indices_view->byte_length || value_end > values_view->byte_length)
            return 0;
        if (!gltf_checked_add_size(
                indices_view->byte_offset, accessor->sparse_indices_offset, &index_buffer_offset) ||
            !gltf_checked_add_size(
                values_view->byte_offset, accessor->sparse_values_offset, &value_buffer_offset) ||
            !gltf_checked_add_size(index_buffer_offset, index_bytes, &buffer_end) ||
            buffer_end > buffers[indices_view->buffer].len ||
            !gltf_checked_add_size(value_buffer_offset, value_bytes, &buffer_end) ||
            buffer_end > buffers[values_view->buffer].len)
            return 0;
        out->sparse_indices = buffers[indices_view->buffer].data + index_buffer_offset;
        out->sparse_values = buffers[values_view->buffer].data + value_buffer_offset;
        out->sparse_count = (int32_t)accessor->sparse_count;
        out->sparse_index_comp_type = accessor->sparse_index_comp_type;
        out->sparse_index_stride = index_comp_size;
        out->sparse_value_stride = (int32_t)element_size;
        for (int32_t si = 0; si < out->sparse_count; si++) {
            uint32_t sparse_index = gltf_decode_component_u32(
                out->sparse_indices + (size_t)si * (size_t)out->sparse_index_stride,
                out->sparse_index_comp_type);
            if (sparse_index >= (uint32_t)out->count ||
                (si > 0 && sparse_index <= previous_sparse_index))
                return 0;
            previous_sparse_index = sparse_index;
        }
    }
    return 1;
}

/// @brief Whether all @p count floats are finite (no NaN/Inf); false for NULL or non-positive
/// count.
static int gltf_preload_floats_are_finite(const float *values, int count) {
    if (!values || count <= 0)
        return 0;
    for (int i = 0; i < count; i++) {
        if (!isfinite(values[i]))
            return 0;
    }
    return 1;
}

/// @brief Whether three vertices form a non-degenerate triangle (positive area).
/// @details Computes the squared area from the cross product of two edges and rejects values
///          below 1e-20, dropping zero-area triangles that would produce invalid normals.
static int gltf_preload_pod_positions_form_triangle(
    const vgfx3d_vertex_t *vertices, uint32_t vertex_count, uint32_t i0, uint32_t i1, uint32_t i2) {
    const float *p0;
    const float *p1;
    const float *p2;
    double e1[3];
    double e2[3];
    double nx;
    double ny;
    double nz;
    double area_sq;
    if (!vertices || i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
        return 0;
    p0 = vertices[i0].pos;
    p1 = vertices[i1].pos;
    p2 = vertices[i2].pos;
    e1[0] = (double)p1[0] - (double)p0[0];
    e1[1] = (double)p1[1] - (double)p0[1];
    e1[2] = (double)p1[2] - (double)p0[2];
    e2[0] = (double)p2[0] - (double)p0[0];
    e2[1] = (double)p2[1] - (double)p0[1];
    e2[2] = (double)p2[2] - (double)p0[2];
    nx = e1[1] * e2[2] - e1[2] * e2[1];
    ny = e1[2] * e2[0] - e1[0] * e2[2];
    nz = e1[0] * e2[1] - e1[1] * e2[0];
    area_sq = nx * nx + ny * ny + nz * nz;
    return isfinite(area_sq) && area_sq > 1e-20;
}

/// @brief Append a triangle's three indices if it is non-degenerate (distinct, positive-area).
/// @return 1 if emitted, 0 if the triangle was rejected (degenerate or repeated indices).
static int gltf_preload_emit_pod_triangle(const vgfx3d_vertex_t *vertices,
                                          uint32_t vertex_count,
                                          uint32_t i0,
                                          uint32_t i1,
                                          uint32_t i2,
                                          uint32_t *indices,
                                          uint32_t *index_count) {
    if (!indices || !index_count || i0 == i1 || i1 == i2 || i0 == i2)
        return 0;
    if (!gltf_preload_pod_positions_form_triangle(vertices, vertex_count, i0, i1, i2))
        return 0;
    indices[(*index_count)++] = i0;
    indices[(*index_count)++] = i1;
    indices[(*index_count)++] = i2;
    return 1;
}

/// @brief Read an index from the index accessor, or use @p element_index directly for non-indexed
/// geometry.
static uint32_t gltf_preload_read_index_or_vertex(const gltf_accessor_view_t *view,
                                                  int32_t element_index) {
    uint32_t value = (uint32_t)element_index;
    if (view)
        gltf_accessor_read_u32(view, element_index, &value, 1);
    return value;
}

/// @brief Compute the triangle-list index capacity needed to triangulate a primitive topology.
/// @details mode 4 = triangles (count/3 tris), 5/6 = strip/fan (count-2 tris); each yields 3
///          indices per triangle. Overflow-checked. Returns 0 for unsupported topologies.
static int gltf_preload_topology_index_capacity(int mode,
                                                int32_t source_index_count,
                                                uint32_t *out_capacity) {
    size_t tri_count;
    size_t index_capacity;
    if (out_capacity)
        *out_capacity = 0;
    if (source_index_count < 3 || !out_capacity)
        return 1;
    if (mode == 4)
        tri_count = (size_t)(source_index_count / 3);
    else if (mode == 5 || mode == 6)
        tri_count = (size_t)(source_index_count - 2);
    else
        return 0;
    if (!gltf_checked_mul_size(tri_count, 3u, &index_capacity) || index_capacity > UINT32_MAX) {
        return 0;
    }
    *out_capacity = (uint32_t)index_capacity;
    return 1;
}

/// @brief Write a vertex's 4 skinning joints/weights, normalizing the weights to sum to 1.
/// @details Out-of-range joints and non-positive weights are zeroed; weights are renormalized by
///          their total (or all zeroed if the total is ~0).
/// @return One past the highest referenced bone index (i.e. a bone-count hint), or 0 if none.
static uint32_t gltf_preload_write_vertex_bone_weights(vgfx3d_vertex_t *vertex,
                                                       const uint32_t joints[4],
                                                       const float weights[4]) {
    double sum = 0.0;
    int32_t max_bone_index = -1;
    if (!vertex || !joints || !weights)
        return 0u;
    for (int i = 0; i < 4; i++) {
        if (joints[i] >= VGFX3D_MAX_BONES) {
            vertex->bone_indices[i] = 0u;
            vertex->bone_weights[i] = 0.0f;
            continue;
        }
        vertex->bone_indices[i] = (uint8_t)joints[i];
        if (isfinite(weights[i]) && weights[i] > 0.0f) {
            vertex->bone_weights[i] = weights[i];
            sum += (double)weights[i];
            if ((int32_t)joints[i] > max_bone_index)
                max_bone_index = (int32_t)joints[i];
        } else {
            vertex->bone_weights[i] = 0.0f;
        }
    }
    if (sum > 1e-12) {
        for (int i = 0; i < 4; i++)
            vertex->bone_weights[i] = (float)((double)vertex->bone_weights[i] / sum);
    } else {
        for (int i = 0; i < 4; i++)
            vertex->bone_weights[i] = 0.0f;
    }
    return max_bone_index >= 0 ? (uint32_t)(max_bone_index + 1) : 0u;
}

/// @brief Serialize a packed mesh primitive into a "VGMP" POD blob and stage it as a dependency.
/// @details Writes the 40-byte header (magic, version, vertex/index counts, flags, byte spans, bone
///          count, skin-import diagnostics) followed by the raw vertex and index arrays, all
///          overflow-checked, then stores it under the mesh/primitive key for
///          gltf_preload_take_decoded_mesh to consume later.
static int gltf_preload_pack_mesh_pod(rt_gltf_preload_bundle *bundle,
                                      int mesh_index,
                                      int primitive_index,
                                      uint32_t flags,
                                      vgfx3d_vertex_t *vertices,
                                      uint32_t vertex_count,
                                      uint32_t *indices,
                                      uint32_t index_count,
                                      uint32_t bone_count,
                                      const gltf_skin_import_stats_t *skin_stats,
                                      char *error,
                                      size_t error_cap) {
    uint8_t *blob;
    size_t vertex_bytes;
    size_t index_bytes;
    size_t payload_offset;
    size_t blob_len;
    char key[96];
    if (!vertices || !indices || vertex_count == 0 || index_count == 0)
        return 1;
    if (bone_count > VGFX3D_MAX_BONES)
        return 1;
    if (!gltf_checked_mul_size((size_t)vertex_count, sizeof(vgfx3d_vertex_t), &vertex_bytes) ||
        !gltf_checked_mul_size((size_t)index_count, sizeof(uint32_t), &index_bytes) ||
        !gltf_checked_add_size(GLTF_PRELOAD_MESH_POD_HEADER_SIZE, vertex_bytes, &payload_offset) ||
        !gltf_checked_add_size(payload_offset, index_bytes, &blob_len) ||
        vertex_bytes > UINT32_MAX || index_bytes > UINT32_MAX) {
        return 1;
    }
    blob = (uint8_t *)malloc(blob_len);
    if (!blob) {
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    memset(blob, 0, GLTF_PRELOAD_MESH_POD_HEADER_SIZE);
    gltf_write_u32_le(blob + 0, GLTF_PRELOAD_MESH_POD_MAGIC);
    gltf_write_u32_le(blob + 4, GLTF_PRELOAD_MESH_POD_VERSION);
    gltf_write_u32_le(blob + 8, vertex_count);
    gltf_write_u32_le(blob + 12, index_count);
    gltf_write_u32_le(blob + 16, flags);
    gltf_write_u32_le(blob + 20, (uint32_t)vertex_bytes);
    gltf_write_u32_le(blob + 24, (uint32_t)index_bytes);
    gltf_write_u32_le(blob + 28, bone_count);
    gltf_write_u32_le(blob + 32, skin_stats ? skin_stats->truncated_vertices : 0u);
    gltf_write_u32_le(blob + 36, skin_stats ? skin_stats->out_of_range_vertices : 0u);
    memcpy(blob + GLTF_PRELOAD_MESH_POD_HEADER_SIZE, vertices, vertex_bytes);
    memcpy(blob + payload_offset, indices, index_bytes);
    gltf_preload_mesh_key(mesh_index, primitive_index, key, sizeof(key));
    if (!gltf_preload_bundle_add_dependency(
            bundle, key, GLTF_PRELOAD_DEP_MESH_POD, blob, blob_len)) {
        free(blob);
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    return 1;
}

/// @brief One decoded morph shape staged for packing: its name, base weight, present-channel
///        flags, and owned per-vertex position/normal/tangent delta arrays.
typedef struct {
    char *name;
    float weight;
    uint32_t flags;
    float *pos_deltas;
    float *normal_deltas;
    float *tangent_deltas;
} gltf_preload_morph_shape_t;

/// @brief Free an array of morph shapes and every owned name/delta buffer inside it.
static void gltf_preload_free_morph_shapes(gltf_preload_morph_shape_t *shapes, size_t shape_count) {
    if (!shapes)
        return;
    for (size_t i = 0; i < shape_count; i++) {
        free(shapes[i].name);
        free(shapes[i].pos_deltas);
        free(shapes[i].normal_deltas);
        free(shapes[i].tangent_deltas);
    }
    free(shapes);
}

/// @brief Resolve a morph target's display name, falling back to "target_N".
/// @details Reads the optional mesh `extras.targetNames[target_index]`; if absent or empty,
///          synthesizes a name from the index. Returns an owned string (caller frees).
static char *gltf_preload_target_name_alloc(
    const char *json, size_t json_len, size_t mesh_start, size_t mesh_end, int target_index) {
    size_t extras_start;
    size_t extras_end;
    size_t names_start;
    size_t names_end;
    char fallback[64];
    char *name = NULL;
    if (gltf_json_object_find_value(
            json, json_len, mesh_start, mesh_end, "extras", &extras_start, &extras_end) &&
        extras_start < extras_end && json[extras_start] == '{' &&
        gltf_json_object_find_value(
            json, json_len, extras_start, extras_end, "targetNames", &names_start, &names_end) &&
        names_start < names_end && json[names_start] == '[') {
        name =
            gltf_json_array_get_string_alloc(json, json_len, names_start, names_end, target_index);
    }
    if (name && name[0] != '\0')
        return name;
    free(name);
    snprintf(fallback, sizeof(fallback), "target_%d", target_index);
    return gltf_strdup_cstr(fallback);
}

/// @brief Read a morph target's default weight from the mesh "weights" array (0.0 if absent).
static double gltf_preload_target_weight(
    const char *json, size_t json_len, size_t mesh_start, size_t mesh_end, int target_index) {
    size_t weights_start;
    size_t weights_end;
    double weight;
    if (!gltf_json_object_find_value(
            json, json_len, mesh_start, mesh_end, "weights", &weights_start, &weights_end) ||
        weights_start >= weights_end || json[weights_start] != '[')
        return 0.0;
    weight =
        gltf_json_array_get_number(json, json_len, weights_start, weights_end, target_index, 0.0);
    if (!isfinite(weight))
        return 0.0;
    if (weight < -1.0)
        return -1.0;
    if (weight > 1.0)
        return 1.0;
    return weight;
}

/// @brief Copy one morph channel (vec3 deltas) from an accessor view into an owned float array.
/// @details Allocates vertex_count*3 floats, samples up to the accessor's count (zero-padding the
///          rest), and sanitizes non-finite components to 0. NULL view yields a NULL out (no-op).
static int gltf_preload_copy_morph_channel(const gltf_accessor_view_t *view,
                                           int32_t vertex_count,
                                           float **out_deltas) {
    size_t bytes;
    float *deltas;
    int32_t limit;
    if (out_deltas)
        *out_deltas = NULL;
    if (!view || !out_deltas || vertex_count <= 0)
        return 1;
    if (!gltf_checked_mul_size((size_t)vertex_count, 3u * sizeof(float), &bytes))
        return 0;
    deltas = (float *)calloc(1, bytes);
    if (!deltas)
        return 0;
    limit = view->count < vertex_count ? view->count : vertex_count;
    for (int32_t vi = 0; vi < limit; vi++) {
        float sample[3] = {0.0f, 0.0f, 0.0f};
        gltf_accessor_read_f32(view, vi, sample, 3);
        for (int c = 0; c < 3; c++)
            deltas[(size_t)vi * 3u + (size_t)c] = isfinite(sample[c]) ? sample[c] : 0.0f;
    }
    *out_deltas = deltas;
    return 1;
}

/// @brief Serialize a primitive's morph shapes into a "VGMT" POD blob and stage it as a dependency.
/// @details Lays out a header, fixed-size per-shape records, a packed names region, and a deltas
///          payload (position/normal/tangent channels per the flags), all overflow-checked, then
///          stores it under the morph key for gltf_preload_take_decoded_morph to consume later.
static int gltf_preload_pack_morph_pod(rt_gltf_preload_bundle *bundle,
                                       int mesh_index,
                                       int primitive_index,
                                       const gltf_preload_morph_shape_t *shapes,
                                       size_t shape_count,
                                       uint32_t vertex_count,
                                       char *error,
                                       size_t error_cap) {
    size_t channel_bytes;
    size_t record_bytes;
    size_t names_bytes = 0;
    size_t payload_bytes = 0;
    size_t names_offset;
    size_t payload_offset;
    size_t blob_len;
    size_t name_cursor = 0;
    size_t payload_cursor = 0;
    uint8_t *blob;
    char key[96];
    if (!bundle || !shapes || shape_count == 0 || vertex_count == 0)
        return 1;
    if (shape_count > INT32_MAX || vertex_count > INT32_MAX) {
        gltf_preload_set_error(error, error_cap, "glTF morph payload is too large");
        return 0;
    }
    if (!gltf_checked_mul_size((size_t)vertex_count, 3u * sizeof(float), &channel_bytes) ||
        !gltf_checked_mul_size(shape_count, GLTF_PRELOAD_MORPH_POD_RECORD_SIZE, &record_bytes)) {
        gltf_preload_set_error(error, error_cap, "glTF morph payload is too large");
        return 0;
    }
    for (size_t i = 0; i < shape_count; i++) {
        size_t name_len = shapes[i].name ? strlen(shapes[i].name) : 0u;
        if (name_len > SIZE_MAX - 1u) {
            gltf_preload_set_error(error, error_cap, "glTF morph target name is too large");
            return 0;
        }
        name_len += 1u;
        if (!gltf_checked_add_size(names_bytes, name_len, &names_bytes)) {
            gltf_preload_set_error(error, error_cap, "glTF morph payload is too large");
            return 0;
        }
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_POSITIONS) != 0 &&
            !gltf_checked_add_size(payload_bytes, channel_bytes, &payload_bytes)) {
            gltf_preload_set_error(error, error_cap, "glTF morph payload is too large");
            return 0;
        }
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_NORMALS) != 0 &&
            !gltf_checked_add_size(payload_bytes, channel_bytes, &payload_bytes)) {
            gltf_preload_set_error(error, error_cap, "glTF morph payload is too large");
            return 0;
        }
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_TANGENTS) != 0 &&
            !gltf_checked_add_size(payload_bytes, channel_bytes, &payload_bytes)) {
            gltf_preload_set_error(error, error_cap, "glTF morph payload is too large");
            return 0;
        }
    }
    if (!gltf_checked_add_size(GLTF_PRELOAD_MORPH_POD_HEADER_SIZE, record_bytes, &names_offset) ||
        !gltf_checked_add_size(names_offset, names_bytes, &payload_offset) ||
        !gltf_checked_add_size(payload_offset, payload_bytes, &blob_len) ||
        record_bytes > UINT32_MAX || names_bytes > UINT32_MAX || payload_bytes > UINT32_MAX) {
        gltf_preload_set_error(error, error_cap, "glTF morph payload is too large");
        return 0;
    }
    blob = (uint8_t *)calloc(1, blob_len);
    if (!blob) {
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    gltf_write_u32_le(blob + 0, GLTF_PRELOAD_MORPH_POD_MAGIC);
    gltf_write_u32_le(blob + 4, GLTF_PRELOAD_MORPH_POD_VERSION);
    gltf_write_u32_le(blob + 8, vertex_count);
    gltf_write_u32_le(blob + 12, (uint32_t)shape_count);
    gltf_write_u32_le(blob + 16, (uint32_t)record_bytes);
    gltf_write_u32_le(blob + 20, (uint32_t)names_bytes);
    gltf_write_u32_le(blob + 24, (uint32_t)payload_bytes);
    gltf_write_u32_le(blob + 28, 0u);

    for (size_t i = 0; i < shape_count; i++) {
        uint8_t *record =
            blob + GLTF_PRELOAD_MORPH_POD_HEADER_SIZE + i * GLTF_PRELOAD_MORPH_POD_RECORD_SIZE;
        const char *name = shapes[i].name ? shapes[i].name : "";
        size_t name_len = strlen(name) + 1u;
        uint32_t pos_offset = 0u;
        uint32_t norm_offset = 0u;
        uint32_t tan_offset = 0u;
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_POSITIONS) != 0) {
            pos_offset = (uint32_t)payload_cursor;
            memcpy(blob + payload_offset + payload_cursor, shapes[i].pos_deltas, channel_bytes);
            payload_cursor += channel_bytes;
        }
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_NORMALS) != 0) {
            norm_offset = (uint32_t)payload_cursor;
            memcpy(blob + payload_offset + payload_cursor, shapes[i].normal_deltas, channel_bytes);
            payload_cursor += channel_bytes;
        }
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_TANGENTS) != 0) {
            tan_offset = (uint32_t)payload_cursor;
            memcpy(blob + payload_offset + payload_cursor, shapes[i].tangent_deltas, channel_bytes);
            payload_cursor += channel_bytes;
        }
        memcpy(blob + names_offset + name_cursor, name, name_len);
        gltf_write_u32_le(record + 0, shapes[i].flags);
        gltf_write_u32_le(record + 4, (uint32_t)name_cursor);
        gltf_write_u32_le(record + 8, (uint32_t)name_len);
        gltf_write_f32_le(record + 12, shapes[i].weight);
        gltf_write_u32_le(record + 16, pos_offset);
        gltf_write_u32_le(record + 20, norm_offset);
        gltf_write_u32_le(record + 24, tan_offset);
        gltf_write_u32_le(record + 28, 0u);
        name_cursor += name_len;
    }

    gltf_preload_morph_key(mesh_index, primitive_index, key, sizeof(key));
    if (!gltf_preload_bundle_add_dependency(
            bundle, key, GLTF_PRELOAD_DEP_MORPH_POD, blob, blob_len)) {
        free(blob);
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    return 1;
}

/// @brief Decode a primitive's morph targets and stage them as a morph POD dependency.
/// @details Parses the primitive's "targets" JSON array; for each entry resolves the optional
///          POSITION/NORMAL/TANGENT accessor views, copies their per-vertex deltas into a
///          growing morph-shape list (tagging present channels via GLTF_PRELOAD_MORPH_POD_HAS_*
///          and recording each target's name and morph weight), then hands the collected shapes
///          to gltf_preload_pack_morph_pod so gltf_preload_take_decoded_morph can rehydrate them
///          at load time. A primitive without a "targets" array is a successful no-op.
/// @return 1 on success or when there is nothing to stage; 0 on allocation/staging failure
///         (with @p error populated).
static int gltf_preload_stage_morph_targets(rt_gltf_preload_bundle *bundle,
                                            const char *json,
                                            size_t json_len,
                                            int mesh_index,
                                            int primitive_index,
                                            size_t mesh_start,
                                            size_t mesh_end,
                                            size_t prim_start,
                                            size_t prim_end,
                                            const gltf_preload_buffer_ref_t *buffers,
                                            int buffer_count,
                                            const gltf_preload_buffer_view_ref_t *views,
                                            int view_count,
                                            const gltf_preload_accessor_ref_t *accessors,
                                            int accessor_count,
                                            int32_t vertex_count,
                                            char *error,
                                            size_t error_cap) {
    size_t targets_start;
    size_t targets_end;
    size_t target_pos;
    gltf_preload_morph_shape_t *shapes = NULL;
    size_t shape_count = 0;
    int target_index = 0;
    int ok = 1;
    if (!bundle || !json || vertex_count <= 0)
        return 1;
    if (!gltf_json_object_find_value(
            json, json_len, prim_start, prim_end, "targets", &targets_start, &targets_end) ||
        targets_start >= targets_end || json[targets_start] != '[')
        return 1;
    target_pos = targets_start + 1u;
    while (target_pos < targets_end) {
        target_pos = gltf_json_skip_ws(json, json_len, target_pos);
        if (target_pos >= targets_end || json[target_pos] == ']')
            break;
        if (json[target_pos] == '{') {
            size_t target_end = gltf_json_find_matching(json, json_len, target_pos, '{', '}');
            int pos_acc;
            int norm_acc;
            int tangent_acc;
            int has_pos;
            int has_norm;
            int has_tangent;
            gltf_accessor_view_t pos_view;
            gltf_accessor_view_t norm_view;
            gltf_accessor_view_t tangent_view;
            if (target_end == SIZE_MAX || target_end > targets_end)
                break;
            pos_acc =
                gltf_json_object_get_int(json, json_len, target_pos, target_end, "POSITION", -1);
            norm_acc =
                gltf_json_object_get_int(json, json_len, target_pos, target_end, "NORMAL", -1);
            tangent_acc =
                gltf_json_object_get_int(json, json_len, target_pos, target_end, "TANGENT", -1);
            has_pos = gltf_preload_resolve_accessor_view(accessors,
                                                         accessor_count,
                                                         pos_acc,
                                                         buffers,
                                                         buffer_count,
                                                         views,
                                                         view_count,
                                                         &pos_view) &&
                      gltf_accessor_has_payload(&pos_view) &&
                      gltf_accessor_is_position(&pos_view);
            has_norm = gltf_preload_resolve_accessor_view(accessors,
                                                          accessor_count,
                                                          norm_acc,
                                                          buffers,
                                                          buffer_count,
                                                          views,
                                                          view_count,
                                                          &norm_view) &&
                       gltf_accessor_has_payload(&norm_view) &&
                       gltf_accessor_is_normal(&norm_view);
            has_tangent = gltf_preload_resolve_accessor_view(accessors,
                                                             accessor_count,
                                                             tangent_acc,
                                                             buffers,
                                                             buffer_count,
                                                             views,
                                                             view_count,
                                                             &tangent_view) &&
                          gltf_accessor_has_payload(&tangent_view) &&
                          gltf_accessor_is_tangent(&tangent_view, 3);
            if (has_pos || has_norm || has_tangent) {
                if (shape_count > (SIZE_MAX / sizeof(*shapes)) - 1u) {
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    ok = 0;
                    goto done;
                }
                gltf_preload_morph_shape_t *grown = (gltf_preload_morph_shape_t *)realloc(
                    shapes, (shape_count + 1u) * sizeof(*shapes));
                if (!grown) {
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    ok = 0;
                    goto done;
                }
                shapes = grown;
                memset(&shapes[shape_count], 0, sizeof(shapes[shape_count]));
                shapes[shape_count].name = gltf_preload_target_name_alloc(
                    json, json_len, mesh_start, mesh_end, target_index);
                if (!shapes[shape_count].name) {
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    ok = 0;
                    goto done;
                }
                shapes[shape_count].weight = (float)gltf_preload_target_weight(
                    json, json_len, mesh_start, mesh_end, target_index);
                if (has_pos) {
                    if (!gltf_preload_copy_morph_channel(
                            &pos_view, vertex_count, &shapes[shape_count].pos_deltas)) {
                        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                        ok = 0;
                        goto done;
                    }
                    shapes[shape_count].flags |= GLTF_PRELOAD_MORPH_POD_HAS_POSITIONS;
                }
                if (has_norm) {
                    if (!gltf_preload_copy_morph_channel(
                            &norm_view, vertex_count, &shapes[shape_count].normal_deltas)) {
                        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                        ok = 0;
                        goto done;
                    }
                    shapes[shape_count].flags |= GLTF_PRELOAD_MORPH_POD_HAS_NORMALS;
                }
                if (has_tangent) {
                    if (!gltf_preload_copy_morph_channel(
                            &tangent_view, vertex_count, &shapes[shape_count].tangent_deltas)) {
                        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                        ok = 0;
                        goto done;
                    }
                    shapes[shape_count].flags |= GLTF_PRELOAD_MORPH_POD_HAS_TANGENTS;
                }
                shape_count++;
            }
            target_index++;
            target_pos = target_end;
        } else {
            target_pos = gltf_json_skip_value(json, json_len, target_pos);
            if (target_pos == SIZE_MAX)
                break;
        }
        target_pos = gltf_json_skip_ws(json, json_len, target_pos);
        if (target_pos < targets_end && json[target_pos] == ',')
            target_pos++;
    }
    ok = gltf_preload_pack_morph_pod(bundle,
                                     mesh_index,
                                     primitive_index,
                                     shapes,
                                     shape_count,
                                     (uint32_t)vertex_count,
                                     error,
                                     error_cap);

done:
    gltf_preload_free_morph_shapes(shapes, shape_count);
    return ok;
}

/// @brief Decode one mesh primitive into interleaved vertices + indices and stage it as a mesh POD
/// dependency.
/// @details Reads the primitive's "attributes" (POSITION required; NORMAL, TEXCOORD_0/1, COLOR_0,
///          TANGENT, JOINTS_0/1, WEIGHTS_0/1 optional) and "mode" (4=triangle list, 5=strip,
///          6=fan), resolves each accessor view, and assembles a vgfx3d_vertex_t array. Up to
///          eight bone influences (JOINTS_0 + JOINTS_1) are merged down to the top four and
///          renormalized, with out-of-range bone indices dropped. The source topology is
///          triangulated into a 32-bit index buffer, channel-presence flags are recorded, and the
///          result is packed via gltf_preload_pack_mesh_pod and then
///          gltf_preload_stage_morph_targets for any blend shapes. Non-triangle modes and
///          position-less primitives are skipped.
/// @return 1 on success or skip; 0 on allocation/staging failure (with @p error populated).
/// @brief Resolved POD-staging state for one glTF mesh primitive: every attribute's
///        accessor view + presence flag, plus derived vertex/index counts and topology.
typedef struct {
    int mode;
    gltf_accessor_view_t pos_view;
    gltf_accessor_view_t norm_view;
    gltf_accessor_view_t uv0_view;
    gltf_accessor_view_t uv1_view;
    gltf_accessor_view_t color_view;
    gltf_accessor_view_t tangent_view;
    gltf_accessor_view_t joints_view;
    gltf_accessor_view_t weights_view;
    gltf_accessor_view_t joints1_view;
    gltf_accessor_view_t weights1_view;
    gltf_accessor_view_t idx_view;
    int has_normals;
    int has_uv0;
    int has_uv1;
    int has_colors;
    int has_tangents;
    int has_joints;
    int has_weights;
    int has_joints1;
    int has_weights1;
    int has_skinning_attrs;
    int has_indices;
    int32_t vertex_count_i;
    int32_t source_index_count;
    uint32_t index_capacity_count;
} gltf_primitive_attribs_t;

/// @brief Resolve every attribute/index accessor view of a primitive into @p out.
/// @return 1 when the primitive is stageable, 0 to skip it (unsupported mode/attrs/topology).
static int gltf_preload_resolve_primitive_attribs(const char *json,
                                                  size_t json_len,
                                                  size_t prim_start,
                                                  size_t prim_end,
                                                  const gltf_preload_buffer_ref_t *buffers,
                                                  int buffer_count,
                                                  const gltf_preload_buffer_view_ref_t *views,
                                                  int view_count,
                                                  const gltf_preload_accessor_ref_t *accessors,
                                                  int accessor_count,
                                                  gltf_primitive_attribs_t *out) {
    size_t attrs_start;
    size_t attrs_end;
    int pos_acc;
    int norm_acc;
    int uv0_acc;
    int uv1_acc;
    int color_acc;
    int tangent_acc;
    int joints_acc;
    int weights_acc;
    int joints1_acc;
    int weights1_acc;
    int idx_acc;

    memset(out, 0, sizeof(*out));
    if (!gltf_json_object_find_value(
            json, json_len, prim_start, prim_end, "attributes", &attrs_start, &attrs_end) ||
        attrs_start >= attrs_end || json[attrs_start] != '{')
        return 0;
    out->mode = gltf_json_object_get_int(json, json_len, prim_start, prim_end, "mode", 4);
    if (out->mode != 4 && out->mode != 5 && out->mode != 6)
        return 0;

    pos_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "POSITION", -1);
    norm_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "NORMAL", -1);
    if (pos_acc < 0)
        return 0;

    if (!gltf_preload_resolve_accessor_view(accessors,
                                            accessor_count,
                                            pos_acc,
                                            buffers,
                                            buffer_count,
                                            views,
                                            view_count,
                                            &out->pos_view))
        return 0;
    out->has_normals = gltf_preload_resolve_accessor_view(accessors,
                                                          accessor_count,
                                                          norm_acc,
                                                          buffers,
                                                          buffer_count,
                                                          views,
                                                          view_count,
                                                          &out->norm_view) &&
                       out->norm_view.comp_count >= 3 &&
                       out->norm_view.count >= out->pos_view.count;
    if (out->pos_view.comp_count < 3 || out->pos_view.count <= 0)
        return 0;

    uv0_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "TEXCOORD_0", -1);
    uv1_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "TEXCOORD_1", -1);
    color_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "COLOR_0", -1);
    tangent_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "TANGENT", -1);
    joints_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "JOINTS_0", -1);
    weights_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "WEIGHTS_0", -1);
    joints1_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "JOINTS_1", -1);
    weights1_acc =
        gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "WEIGHTS_1", -1);
    idx_acc = gltf_json_object_get_int(json, json_len, prim_start, prim_end, "indices", -1);

    out->has_uv0 = gltf_preload_resolve_accessor_view(accessors,
                                                      accessor_count,
                                                      uv0_acc,
                                                      buffers,
                                                      buffer_count,
                                                      views,
                                                      view_count,
                                                      &out->uv0_view) &&
                   gltf_accessor_has_payload(&out->uv0_view) &&
                   gltf_accessor_is_texcoord(&out->uv0_view) &&
                   out->uv0_view.count >= out->pos_view.count;
    out->has_uv1 = gltf_preload_resolve_accessor_view(accessors,
                                                      accessor_count,
                                                      uv1_acc,
                                                      buffers,
                                                      buffer_count,
                                                      views,
                                                      view_count,
                                                      &out->uv1_view) &&
                   gltf_accessor_has_payload(&out->uv1_view) &&
                   gltf_accessor_is_texcoord(&out->uv1_view) &&
                   out->uv1_view.count >= out->pos_view.count;
    out->has_colors = gltf_preload_resolve_accessor_view(accessors,
                                                         accessor_count,
                                                         color_acc,
                                                         buffers,
                                                         buffer_count,
                                                         views,
                                                         view_count,
                                                         &out->color_view) &&
                      gltf_accessor_has_payload(&out->color_view) &&
                      gltf_accessor_is_color(&out->color_view) &&
                      out->color_view.count >= out->pos_view.count;
    out->has_tangents = gltf_preload_resolve_accessor_view(accessors,
                                                           accessor_count,
                                                           tangent_acc,
                                                           buffers,
                                                           buffer_count,
                                                           views,
                                                           view_count,
                                                           &out->tangent_view) &&
                        gltf_accessor_has_payload(&out->tangent_view) &&
                        gltf_accessor_is_tangent(&out->tangent_view, 4) &&
                        out->tangent_view.count >= out->pos_view.count;
    out->has_joints = gltf_preload_resolve_accessor_view(accessors,
                                                         accessor_count,
                                                         joints_acc,
                                                         buffers,
                                                         buffer_count,
                                                         views,
                                                         view_count,
                                                         &out->joints_view) &&
                      gltf_accessor_has_payload(&out->joints_view) &&
                      gltf_accessor_is_joints(&out->joints_view) &&
                      out->joints_view.count >= out->pos_view.count;
    out->has_weights = gltf_preload_resolve_accessor_view(accessors,
                                                          accessor_count,
                                                          weights_acc,
                                                          buffers,
                                                          buffer_count,
                                                          views,
                                                          view_count,
                                                          &out->weights_view) &&
                       gltf_accessor_has_payload(&out->weights_view) &&
                       gltf_accessor_is_weights(&out->weights_view) &&
                       out->weights_view.count >= out->pos_view.count;
    out->has_joints1 = gltf_preload_resolve_accessor_view(accessors,
                                                          accessor_count,
                                                          joints1_acc,
                                                          buffers,
                                                          buffer_count,
                                                          views,
                                                          view_count,
                                                          &out->joints1_view) &&
                       gltf_accessor_has_payload(&out->joints1_view) &&
                       gltf_accessor_is_joints(&out->joints1_view) &&
                       out->joints1_view.count >= out->pos_view.count;
    out->has_weights1 = gltf_preload_resolve_accessor_view(accessors,
                                                           accessor_count,
                                                           weights1_acc,
                                                           buffers,
                                                           buffer_count,
                                                           views,
                                                           view_count,
                                                           &out->weights1_view) &&
                        gltf_accessor_has_payload(&out->weights1_view) &&
                        gltf_accessor_is_weights(&out->weights1_view) &&
                        out->weights1_view.count >= out->pos_view.count;
    if ((out->has_joints != out->has_weights) || (out->has_joints1 != out->has_weights1))
        return 0;
    out->has_skinning_attrs =
        (out->has_joints && out->has_weights) || (out->has_joints1 && out->has_weights1);
    out->has_indices = gltf_preload_resolve_accessor_view(accessors,
                                                          accessor_count,
                                                          idx_acc,
                                                          buffers,
                                                          buffer_count,
                                                          views,
                                                          view_count,
                                                          &out->idx_view) &&
                       out->idx_view.comp_count == 1 &&
                       (out->idx_view.comp_type == 5121 || out->idx_view.comp_type == 5123 ||
                        out->idx_view.comp_type == 5125);
    out->vertex_count_i = out->pos_view.count;
    out->source_index_count = out->has_indices ? out->idx_view.count : out->vertex_count_i;
    if (out->vertex_count_i < 3 || out->source_index_count < 3)
        return 0;
    if (!gltf_preload_topology_index_capacity(
            out->mode, out->source_index_count, &out->index_capacity_count) ||
        out->index_capacity_count == 0)
        return 0;
    return 1;
}

/// @brief Decode each vertex's attributes into @p vertices, normalizing skin influences.
/// @details Skin influences that cannot be represented (a fifth meaningful influence, or a
///          joint index past VGFX3D_MAX_BONES) are counted into @p skin_stats_io so the
///          committing thread can surface load warnings — the decode itself runs on a
///          preload worker where the thread-local warning store is not the loader's.
/// @return 1 on success, 0 to skip the primitive (non-finite source data). Sets @p out_bone_count.
static int gltf_preload_build_primitive_vertices(const gltf_primitive_attribs_t *a,
                                                 vgfx3d_vertex_t *vertices,
                                                 uint32_t *out_bone_count,
                                                 int *out_valid_normals,
                                                 int *out_valid_tangents,
                                                 gltf_skin_import_stats_t *skin_stats_io) {
    uint32_t bone_count = 0u;
    int valid_normals = a && a->has_normals;
    int valid_tangents = a && a->has_tangents;
    if (out_valid_normals)
        *out_valid_normals = 0;
    if (out_valid_tangents)
        *out_valid_tangents = 0;
    if (!a || !vertices || !out_bone_count)
        return 0;
    for (int32_t vi = 0; vi < a->vertex_count_i; vi++) {
        float pos[3] = {0.0f, 0.0f, 0.0f};
        float nrm[3] = {0.0f, 0.0f, 0.0f};
        float uv[2] = {0.0f, 0.0f};
        float uv1[2] = {0.0f, 0.0f};
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float tangent[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t joints[4] = {0u, 0u, 0u, 0u};
        float weights1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t joints1[4] = {0u, 0u, 0u, 0u};
        gltf_accessor_read_f32(&a->pos_view, vi, pos, 3);
        if (a->has_normals)
            gltf_accessor_read_f32(&a->norm_view, vi, nrm, 3);
        if (!gltf_preload_floats_are_finite(pos, 3) ||
            (a->has_normals && !gltf_preload_floats_are_finite(nrm, 3)))
            return 0;
        if (a->has_normals && !gltf_normalize_vec3f_in_place(nrm)) {
            valid_normals = 0;
            nrm[0] = 0.0f;
            nrm[1] = 0.0f;
            nrm[2] = 0.0f;
        }
        if (a->has_uv0) {
            gltf_accessor_read_f32(&a->uv0_view, vi, uv, 2);
            if (!gltf_preload_floats_are_finite(uv, 2))
                return 0;
        }
        if (a->has_uv1) {
            gltf_accessor_read_f32(&a->uv1_view, vi, uv1, 2);
            if (!gltf_preload_floats_are_finite(uv1, 2))
                return 0;
        } else {
            uv1[0] = uv[0];
            uv1[1] = uv[1];
        }
        if (a->has_colors) {
            gltf_accessor_read_f32(&a->color_view, vi, color, 4);
            if (a->color_view.comp_count < 4)
                color[3] = 1.0f;
            if (!gltf_preload_floats_are_finite(color, 4))
                return 0;
        }
        if (a->has_tangents) {
            gltf_accessor_read_f32(&a->tangent_view, vi, tangent, 4);
            if (a->tangent_view.comp_count < 4)
                tangent[3] = 1.0f;
            if (!gltf_preload_floats_are_finite(tangent, 4))
                return 0;
            if (!gltf_sanitize_tangent4(tangent)) {
                valid_tangents = 0;
                tangent[0] = 1.0f;
                tangent[1] = 0.0f;
                tangent[2] = 0.0f;
                tangent[3] = 1.0f;
            }
        }
        memcpy(vertices[vi].pos, pos, sizeof(vertices[vi].pos));
        memcpy(vertices[vi].normal, nrm, sizeof(vertices[vi].normal));
        memcpy(vertices[vi].uv, uv, sizeof(vertices[vi].uv));
        memcpy(vertices[vi].uv1, uv1, sizeof(vertices[vi].uv1));
        memcpy(vertices[vi].color, color, sizeof(vertices[vi].color));
        memcpy(vertices[vi].tangent, tangent, sizeof(vertices[vi].tangent));
        if (a->has_skinning_attrs) {
            uint32_t vertex_bone_count;
            if (a->has_joints)
                gltf_accessor_read_u32(&a->joints_view, vi, joints, 4);
            if (a->has_weights)
                gltf_accessor_read_f32(&a->weights_view, vi, weights, 4);
            if (a->has_joints1)
                gltf_accessor_read_u32(&a->joints1_view, vi, joints1, 4);
            if (a->has_weights1)
                gltf_accessor_read_f32(&a->weights1_view, vi, weights1, 4);
            int truncated = 0;
            int out_of_range = 0;
            if (a->has_joints1 && a->has_weights1) {
                uint32_t merged_joints[4] = {0u, 0u, 0u, 0u};
                float merged_weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                if (a->has_joints && a->has_weights) {
                    for (int j = 0; j < 4; j++)
                        gltf_add_top_joint_influence(joints[j],
                                                     weights[j],
                                                     merged_joints,
                                                     merged_weights,
                                                     &truncated,
                                                     &out_of_range);
                }
                for (int j = 0; j < 4; j++)
                    gltf_add_top_joint_influence(joints1[j],
                                                 weights1[j],
                                                 merged_joints,
                                                 merged_weights,
                                                 &truncated,
                                                 &out_of_range);
                gltf_normalize_joint_influences(merged_weights);
                memcpy(joints, merged_joints, sizeof(joints));
                memcpy(weights, merged_weights, sizeof(weights));
            } else {
                for (int j = 0; j < 4; j++) {
                    if (joints[j] >= VGFX3D_MAX_BONES) {
                        if (isfinite(weights[j]) && weights[j] > 1e-8f)
                            out_of_range = 1;
                        weights[j] = 0.0f;
                    }
                }
            }
            if (skin_stats_io) {
                if (truncated)
                    skin_stats_io->truncated_vertices++;
                if (out_of_range)
                    skin_stats_io->out_of_range_vertices++;
            }
            vertex_bone_count =
                gltf_preload_write_vertex_bone_weights(&vertices[vi], joints, weights);
            if (vertex_bone_count > bone_count)
                bone_count = vertex_bone_count;
        }
    }
    *out_bone_count = bone_count;
    if (out_valid_normals)
        *out_valid_normals = valid_normals;
    if (out_valid_tangents)
        *out_valid_tangents = valid_tangents;
    return 1;
}

/// @brief Triangulate the primitive's topology (modes 4/5/6) into @p indices.
static void gltf_preload_triangulate_primitive(const gltf_primitive_attribs_t *a,
                                               vgfx3d_vertex_t *vertices,
                                               uint32_t *indices,
                                               uint32_t *out_index_count) {
    const gltf_accessor_view_t *idx = a->has_indices ? &a->idx_view : NULL;
    uint32_t index_count = 0;
    if (a->mode == 4) {
        for (int32_t ii = 0; ii + 2 < a->source_index_count; ii += 3) {
            gltf_preload_emit_pod_triangle(vertices,
                                           (uint32_t)a->vertex_count_i,
                                           gltf_preload_read_index_or_vertex(idx, ii),
                                           gltf_preload_read_index_or_vertex(idx, ii + 1),
                                           gltf_preload_read_index_or_vertex(idx, ii + 2),
                                           indices,
                                           &index_count);
        }
    } else if (a->mode == 5) {
        for (int32_t ii = 0; ii + 2 < a->source_index_count; ii++) {
            uint32_t i0 = gltf_preload_read_index_or_vertex(idx, ii);
            uint32_t i1 = gltf_preload_read_index_or_vertex(idx, ii + 1);
            uint32_t i2 = gltf_preload_read_index_or_vertex(idx, ii + 2);
            if ((ii & 1) != 0) {
                uint32_t tmp = i0;
                i0 = i1;
                i1 = tmp;
            }
            gltf_preload_emit_pod_triangle(
                vertices, (uint32_t)a->vertex_count_i, i0, i1, i2, indices, &index_count);
        }
    } else {
        uint32_t base = gltf_preload_read_index_or_vertex(idx, 0);
        for (int32_t ii = 1; ii + 1 < a->source_index_count; ii++) {
            gltf_preload_emit_pod_triangle(vertices,
                                           (uint32_t)a->vertex_count_i,
                                           base,
                                           gltf_preload_read_index_or_vertex(idx, ii),
                                           gltf_preload_read_index_or_vertex(idx, ii + 1),
                                           indices,
                                           &index_count);
        }
    }
    *out_index_count = index_count;
}

/// @brief Stage one mesh primitive during the preload pass: resolve its vertex attributes
///   and indices from the glTF JSON and buffer views, then record the built geometry into
///   @p bundle keyed by (@p mesh_index, @p primitive_index) for later mesh assembly. Writes a
///   message into @p error (capacity @p error_cap) on a fatal failure.
/// @return Non-zero to continue staging (primitive staged or harmlessly skipped); 0 on a
///   fatal error such as an allocation failure.
static int gltf_preload_stage_mesh_primitive(rt_gltf_preload_bundle *bundle,
                                             const char *json,
                                             size_t json_len,
                                             int mesh_index,
                                             int primitive_index,
                                             size_t mesh_start,
                                             size_t mesh_end,
                                             size_t prim_start,
                                             size_t prim_end,
                                             const gltf_preload_buffer_ref_t *buffers,
                                             int buffer_count,
                                             const gltf_preload_buffer_view_ref_t *views,
                                             int view_count,
                                             const gltf_preload_accessor_ref_t *accessors,
                                             int accessor_count,
                                             char *error,
                                             size_t error_cap) {
    gltf_primitive_attribs_t attribs;
    uint32_t flags = 0u;
    uint32_t bone_count = 0u;
    int valid_normals = 0;
    int valid_tangents = 0;
    vgfx3d_vertex_t *vertices = NULL;
    uint32_t *indices = NULL;
    uint32_t index_count = 0;
    size_t vertex_bytes;
    size_t index_bytes;
    gltf_skin_import_stats_t skin_stats = {0u, 0u};
    int ok = 1;

    if (!bundle || !json || !accessors)
        return 1;
    if (!gltf_preload_resolve_primitive_attribs(json,
                                                json_len,
                                                prim_start,
                                                prim_end,
                                                buffers,
                                                buffer_count,
                                                views,
                                                view_count,
                                                accessors,
                                                accessor_count,
                                                &attribs))
        return 1;
    if (!gltf_checked_mul_size(
            (size_t)attribs.vertex_count_i, sizeof(vgfx3d_vertex_t), &vertex_bytes) ||
        !gltf_checked_mul_size(
            (size_t)attribs.index_capacity_count, sizeof(uint32_t), &index_bytes))
        return 1;
    if (vertex_bytes == 0 || index_bytes == 0)
        return 1;

    vertices = (vgfx3d_vertex_t *)calloc((size_t)attribs.vertex_count_i, sizeof(*vertices));
    indices = (uint32_t *)malloc(index_bytes);
    if (!vertices || !indices) {
        free(vertices);
        free(indices);
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }

    if (!gltf_preload_build_primitive_vertices(
            &attribs, vertices, &bone_count, &valid_normals, &valid_tangents, &skin_stats))
        goto done;

    gltf_preload_triangulate_primitive(&attribs, vertices, indices, &index_count);
    if (index_count == 0)
        goto done;
    if (valid_normals)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_NORMALS;
    if (attribs.has_uv0)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_UV0;
    if (valid_tangents)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_TANGENTS;
    if (attribs.has_skinning_attrs)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_SKINNING;
    ok = gltf_preload_pack_mesh_pod(bundle,
                                    mesh_index,
                                    primitive_index,
                                    flags,
                                    vertices,
                                    (uint32_t)attribs.vertex_count_i,
                                    indices,
                                    index_count,
                                    bone_count,
                                    &skin_stats,
                                    error,
                                    error_cap);
    if (ok) {
        ok = gltf_preload_stage_morph_targets(bundle,
                                              json,
                                              json_len,
                                              mesh_index,
                                              primitive_index,
                                              mesh_start,
                                              mesh_end,
                                              prim_start,
                                              prim_end,
                                              buffers,
                                              buffer_count,
                                              views,
                                              view_count,
                                              accessors,
                                              accessor_count,
                                              attribs.vertex_count_i,
                                              error,
                                              error_cap);
    }

done:
    free(vertices);
    free(indices);
    return ok;
}

/// @brief Stage every mesh primitive as a packed POD blob (and its morph targets) off the main
/// thread.
/// @details Walks "meshes"[].primitives[], resolves each attribute/index accessor, triangulates the
///          topology, normalizes skinning, and emits "VGMP"/"VGMT" dependencies so the main-thread
///          load can build meshes without re-parsing. Sets @p error and returns 0 on a staging
///          failure.
static int gltf_preload_stage_meshes(rt_gltf_preload_bundle *bundle,
                                     const char *json,
                                     size_t json_len,
                                     const gltf_preload_buffer_ref_t *buffers,
                                     int buffer_count,
                                     const gltf_preload_buffer_view_ref_t *views,
                                     int view_count,
                                     char *error,
                                     size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    gltf_preload_accessor_ref_t *accessors = NULL;
    int accessor_count = 0;
    int mesh_index = 0;
    if (!gltf_json_find_top_level_array(json, json_len, "meshes", &array_start, &array_end))
        return 1;
    if (!gltf_preload_parse_accessors(
            json, json_len, &accessors, &accessor_count, error, error_cap))
        return 0;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            size_t prims_start;
            size_t prims_end;
            int primitive_index = 0;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (gltf_json_object_find_value(
                    json, json_len, pos, object_end, "primitives", &prims_start, &prims_end) &&
                prims_start < prims_end && json[prims_start] == '[') {
                size_t prim_pos = prims_start + 1u;
                while (prim_pos < prims_end) {
                    prim_pos = gltf_json_skip_ws(json, json_len, prim_pos);
                    if (prim_pos >= prims_end || json[prim_pos] == ']')
                        break;
                    if (json[prim_pos] == '{') {
                        size_t prim_end =
                            gltf_json_find_matching(json, json_len, prim_pos, '{', '}');
                        if (prim_end == SIZE_MAX || prim_end > prims_end)
                            break;
                        if (!gltf_preload_stage_mesh_primitive(bundle,
                                                               json,
                                                               json_len,
                                                               mesh_index,
                                                               primitive_index,
                                                               pos,
                                                               object_end,
                                                               prim_pos,
                                                               prim_end,
                                                               buffers,
                                                               buffer_count,
                                                               views,
                                                               view_count,
                                                               accessors,
                                                               accessor_count,
                                                               error,
                                                               error_cap)) {
                            free(accessors);
                            return 0;
                        }
                        primitive_index++;
                        prim_pos = prim_end;
                    } else {
                        prim_pos = gltf_json_skip_value(json, json_len, prim_pos);
                        if (prim_pos == SIZE_MAX)
                            break;
                    }
                    prim_pos = gltf_json_skip_ws(json, json_len, prim_pos);
                    if (prim_pos < prims_end && json[prim_pos] == ',')
                        prim_pos++;
                }
            }
            mesh_index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    free(accessors);
    return 1;
}

/// @brief Stage an inline image's bytes under its image-index key (decoding to RGBA when possible).
static int gltf_preload_stage_image_bytes(rt_gltf_preload_bundle *bundle,
                                          int image_index,
                                          const char *mime_type,
                                          uint8_t *data,
                                          size_t data_len,
                                          int required,
                                          char *error,
                                          size_t error_cap) {
    char key[96];
    if (!data || data_len == 0) {
        free(data);
        if (required) {
            gltf_preload_set_error(error, error_cap, "invalid glTF image payload");
            return 0;
        }
        return 1;
    }
    gltf_preload_image_key(image_index, mime_type, key, sizeof(key));
    return gltf_preload_bundle_add_image_payload(
        bundle, key, mime_type ? mime_type : key, data, data_len, required, error, error_cap);
}

/// @brief Mark which image sources are actually referenced by a texture, in the @p required bitmap.
/// @details Scans the top-level "textures" array and sets required[source] for each texture's image
///          index, so only images a material can use are treated as mandatory to stage.
static void gltf_preload_mark_required_images(const char *json,
                                              size_t json_len,
                                              uint8_t *required,
                                              int required_count) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    if (!json || !required || required_count <= 0)
        return;
    if (!gltf_json_find_top_level_array(json, json_len, "textures", &array_start, &array_end))
        return;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            int source;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            source = gltf_json_texture_source_index(json, json_len, pos, object_end);
            if (source >= 0 && source < required_count)
                required[source] = 1u;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
}

static int gltf_json_count_objects_in_array(const char *json,
                                            size_t json_len,
                                            size_t array_start,
                                            size_t array_end) {
    size_t pos = array_start + 1u;
    int count = 0;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            count++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    return count;
}

static int gltf_preload_prepare_required_images(const char *json,
                                                size_t json_len,
                                                size_t array_start,
                                                size_t array_end,
                                                int *out_image_count,
                                                uint8_t **out_required,
                                                char *error,
                                                size_t error_cap) {
    int image_count = gltf_json_count_objects_in_array(json, json_len, array_start, array_end);
    uint8_t *required = NULL;
    if (out_image_count)
        *out_image_count = image_count;
    if (out_required)
        *out_required = NULL;
    if (image_count <= 0)
        return 1;
    if (!rt_untrusted_count_ok((int64_t)image_count, 1u, json_len)) {
        gltf_preload_set_error(error, error_cap, "failed to stage glTF preload");
        return 0;
    }
    required = (uint8_t *)calloc((size_t)image_count, sizeof(uint8_t));
    if (!required) {
        gltf_preload_set_error(error, error_cap, "failed to stage glTF preload");
        return 0;
    }
    gltf_preload_mark_required_images(json, json_len, required, image_count);
    if (out_required)
        *out_required = required;
    else
        free(required);
    return 1;
}

/// @brief Verify that every texture-referenced data-URI image decodes to a supported format.
/// @details Marks required images (via gltf_preload_mark_required_images), then checks each
///          referenced inline data-URI image is a decodable format, rejecting the model otherwise.
/// @return 1 if all required inline images are valid, 0 if any is unsupported/corrupt.
static int gltf_validate_required_data_uri_images(const char *json, size_t json_len) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    int image_count;
    int index = 0;
    uint8_t *required = NULL;
    if (!gltf_json_find_top_level_array(json, json_len, "images", &array_start, &array_end))
        return 1;

    image_count = gltf_json_count_objects_in_array(json, json_len, array_start, array_end);
    if (image_count <= 0)
        return 1;
    if (!rt_untrusted_count_ok((int64_t)image_count, 1u, json_len))
        return 0;

    required = (uint8_t *)calloc((size_t)image_count, sizeof(uint8_t));
    if (!required)
        return 0;
    gltf_preload_mark_required_images(json, json_len, required, image_count);

    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            char *uri;
            char *mime_type;
            int required_image;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            uri = gltf_json_object_get_string(json, json_len, pos, object_end, "uri");
            mime_type = gltf_json_object_get_string(json, json_len, pos, object_end, "mimeType");
            required_image = index < image_count && required[index] != 0;
            if (required_image && uri && strncmp(uri, "data:", 5) == 0) {
                char parsed_mime[64];
                const char *image_type;
                uint8_t *data = NULL;
                uint8_t *rgba_blob = NULL;
                size_t data_len = 0;
                size_t rgba_len = 0;
                int ok = 1;
                gltf_data_uri_copy_mime(uri, parsed_mime, sizeof(parsed_mime));
                image_type = mime_type ? mime_type : parsed_mime;
                if (gltf_image_is_ktx2(image_type)) {
                    void *ktx_asset = NULL;
                    ok = gltf_parse_data_uri(
                        uri, parsed_mime, sizeof(parsed_mime), &data, &data_len);
                    if (ok)
                        ktx_asset = gltf_decode_ktx2_payload(data, data_len);
                    ok = ktx_asset != NULL;
                    if (ktx_asset && rt_obj_release_check0(ktx_asset))
                        rt_obj_free(ktx_asset);
                    free(data);
                } else if (gltf_preload_image_is_supported_format(image_type)) {
                    ok = gltf_parse_data_uri(
                             uri, parsed_mime, sizeof(parsed_mime), &data, &data_len) &&
                         gltf_decode_image_payload_to_rgba_blob(
                             image_type, data, data_len, &rgba_blob, &rgba_len);
                    free(data);
                    free(rgba_blob);
                }
                if (!ok) {
                    free(uri);
                    free(mime_type);
                    free(required);
                    return 0;
                }
            }
            free(uri);
            free(mime_type);
            index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    free(required);
    return 1;
}

static int gltf_preload_stage_data_uri_image(rt_gltf_preload_bundle *bundle,
                                             int image_index,
                                             const char *mime_type,
                                             const char *uri,
                                             int required_image,
                                             char *error,
                                             size_t error_cap) {
    char parsed_mime[64];
    uint8_t *data = NULL;
    size_t data_len = 0;
    parsed_mime[0] = '\0';
    if (gltf_parse_data_uri(uri, parsed_mime, sizeof(parsed_mime), &data, &data_len))
        return gltf_preload_stage_image_bytes(bundle,
                                              image_index,
                                              mime_type ? mime_type : parsed_mime,
                                              data,
                                              data_len,
                                              required_image,
                                              error,
                                              error_cap);
    if (required_image && gltf_preload_image_is_supported_format(mime_type)) {
        gltf_preload_set_error(error, error_cap, "invalid glTF image payload");
        return 0;
    }
    return 1;
}

static int gltf_preload_stage_image_uri(rt_gltf_preload_bundle *bundle,
                                        const char *model_path,
                                        int image_index,
                                        const char *mime_type,
                                        const char *uri,
                                        int load_assets,
                                        int required_image,
                                        char *error,
                                        size_t error_cap) {
    if (strncmp(uri, "data:", 5) == 0)
        return gltf_preload_stage_data_uri_image(
            bundle, image_index, mime_type, uri, required_image, error, error_cap);
    return gltf_preload_stage_external_image(
        bundle, model_path, uri, load_assets, required_image, error, error_cap);
}

static int gltf_preload_stage_buffer_view_image(rt_gltf_preload_bundle *bundle,
                                                const char *json,
                                                size_t json_len,
                                                int image_index,
                                                size_t object_start,
                                                size_t object_end,
                                                const char *mime_type,
                                                int required_image,
                                                const gltf_preload_buffer_ref_t *buffers,
                                                int buffer_count,
                                                const gltf_preload_buffer_view_ref_t *views,
                                                int view_count,
                                                char *error,
                                                size_t error_cap) {
    int view_index =
        gltf_json_object_get_int(json, json_len, object_start, object_end, "bufferView", -1);
    int staged = 0;
    if (view_index >= 0 && view_index < view_count && views && views[view_index].valid) {
        const gltf_preload_buffer_view_ref_t *view = &views[view_index];
        if (view->buffer >= 0 && view->buffer < buffer_count && buffers &&
            buffers[view->buffer].data) {
            size_t end;
            if (view->byte_length == 0 && required_image) {
                gltf_preload_set_error(error, error_cap, "invalid glTF image payload");
                return 0;
            }
            if (view->byte_length > 0 &&
                gltf_checked_add_size(view->byte_offset, view->byte_length, &end) &&
                end <= buffers[view->buffer].len) {
                uint8_t *copy = (uint8_t *)malloc(view->byte_length);
                if (!copy) {
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    return 0;
                }
                memcpy(copy, buffers[view->buffer].data + view->byte_offset, view->byte_length);
                staged = 1;
                if (!gltf_preload_stage_image_bytes(bundle,
                                                    image_index,
                                                    mime_type,
                                                    copy,
                                                    view->byte_length,
                                                    required_image,
                                                    error,
                                                    error_cap))
                    return 0;
            }
        }
    }
    if (!staged && required_image) {
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    return 1;
}

static int gltf_preload_stage_image_object(rt_gltf_preload_bundle *bundle,
                                           const char *model_path,
                                           const char *json,
                                           size_t json_len,
                                           int image_index,
                                           size_t object_start,
                                           size_t object_end,
                                           int load_assets,
                                           int required_image,
                                           const gltf_preload_buffer_ref_t *buffers,
                                           int buffer_count,
                                           const gltf_preload_buffer_view_ref_t *views,
                                           int view_count,
                                           char *error,
                                           size_t error_cap) {
    char *uri = gltf_json_object_get_string(json, json_len, object_start, object_end, "uri");
    char *mime_type =
        gltf_json_object_get_string(json, json_len, object_start, object_end, "mimeType");
    int ok;
    if (uri) {
        ok = gltf_preload_stage_image_uri(bundle,
                                          model_path,
                                          image_index,
                                          mime_type,
                                          uri,
                                          load_assets,
                                          required_image,
                                          error,
                                          error_cap);
    } else {
        ok = gltf_preload_stage_buffer_view_image(bundle,
                                                  json,
                                                  json_len,
                                                  image_index,
                                                  object_start,
                                                  object_end,
                                                  mime_type,
                                                  required_image,
                                                  buffers,
                                                  buffer_count,
                                                  views,
                                                  view_count,
                                                  error,
                                                  error_cap);
    }
    free(uri);
    free(mime_type);
    return ok;
}

/// @brief Stage every glTF image (inline data-URI, bufferView-embedded, or external file).
/// @details Walks the "images" array, marking texture-referenced images required, and stages each
///          image's bytes (decoding to RGBA when possible) under its image key. A required image
///          that cannot be staged sets @p error and fails; optional images are skipped on error.
static int gltf_preload_stage_images(rt_gltf_preload_bundle *bundle,
                                     const char *model_path,
                                     const char *json,
                                     size_t json_len,
                                     int load_assets,
                                     const gltf_preload_buffer_ref_t *buffers,
                                     int buffer_count,
                                     const gltf_preload_buffer_view_ref_t *views,
                                     int view_count,
                                     char *error,
                                     size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    int image_count;
    int index = 0;
    uint8_t *required = NULL;
    if (!gltf_json_find_top_level_array(json, json_len, "images", &array_start, &array_end))
        return 1;

    if (!gltf_preload_prepare_required_images(
            json, json_len, array_start, array_end, &image_count, &required, error, error_cap))
        return 0;

    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            int required_image;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (index >= image_count) {
                free(required);
                gltf_preload_set_error(error, error_cap, "invalid glTF image list");
                return 0;
            }
            required_image = required ? required[index] != 0 : 0;
            if (!gltf_preload_stage_image_object(bundle,
                                                 model_path,
                                                 json,
                                                 json_len,
                                                 index,
                                                 pos,
                                                 object_end,
                                                 load_assets,
                                                 required_image,
                                                 buffers,
                                                 buffer_count,
                                                 views,
                                                 view_count,
                                                 error,
                                                 error_cap)) {
                free(required);
                return 0;
            }
            index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    free(required);
    return 1;
}

static rt_gltf_preload_bundle *gltf_preload_bundle_alloc_with_root(uint8_t *root_data,
                                                                   size_t root_size,
                                                                   char *error,
                                                                   size_t error_cap) {
    rt_gltf_preload_bundle *bundle = (rt_gltf_preload_bundle *)calloc(1, sizeof(*bundle));
    if (!bundle) {
        gltf_preload_set_error(error, error_cap, "failed to stage glTF preload");
        return NULL;
    }
    bundle->root_data = root_data;
    bundle->root_size = root_size;
    return bundle;
}

static int gltf_preload_stage_bundle_json(rt_gltf_preload_bundle *bundle,
                                          const char *model_path,
                                          char *json,
                                          size_t json_len,
                                          int load_assets,
                                          char *error,
                                          size_t error_cap) {
    gltf_preload_buffer_ref_t *buffer_refs = NULL;
    int buffer_ref_count = 0;
    gltf_preload_buffer_view_ref_t *view_refs = NULL;
    int view_ref_count = 0;
    int ok = gltf_preload_stage_buffers(bundle,
                                        model_path,
                                        json,
                                        json_len,
                                        load_assets,
                                        &buffer_refs,
                                        &buffer_ref_count,
                                        error,
                                        error_cap);
    if (ok) {
        ok = gltf_preload_parse_buffer_views(
            json, json_len, &view_refs, &view_ref_count, error, error_cap);
    }
    if (ok) {
        ok = gltf_preload_validate_accessors(json,
                                             json_len,
                                             buffer_refs,
                                             buffer_ref_count,
                                             view_refs,
                                             view_ref_count,
                                             error,
                                             error_cap);
    }
    if (ok) {
        ok = gltf_preload_stage_meshes(bundle,
                                       json,
                                       json_len,
                                       buffer_refs,
                                       buffer_ref_count,
                                       view_refs,
                                       view_ref_count,
                                       error,
                                       error_cap);
    }
    if (ok) {
        ok = gltf_preload_stage_images(bundle,
                                       model_path,
                                       json,
                                       json_len,
                                       load_assets,
                                       buffer_refs,
                                       buffer_ref_count,
                                       view_refs,
                                       view_ref_count,
                                       error,
                                       error_cap);
    }
    free(buffer_refs);
    free(view_refs);
    return ok;
}

/// @brief Build an off-main-thread preload bundle from a C path and raw glTF/GLB bytes.
/// @details Extracts the JSON, validates accessors, and stages all dependencies so the subsequent
///          rt_gltf_load_preloaded_bundle call can build runtime objects on the main thread with no
///          file I/O or re-parsing. Takes ownership of @p root_data. On failure writes @p error,
///          frees partial work, and returns NULL.
/// @return A populated preload bundle (caller frees via rt_gltf_preload_bundle_free), or NULL.
rt_gltf_preload_bundle *rt_gltf_preload_bundle_create_cstr(const char *model_path,
                                                           uint8_t *root_data,
                                                           size_t root_size,
                                                           int load_assets,
                                                           char *error,
                                                           size_t error_cap) {
    rt_gltf_preload_bundle *bundle;
    char *json = NULL;
    size_t json_len = 0;
    if (error && error_cap > 0)
        error[0] = '\0';
    if (!root_data || root_size == 0 || !model_path) {
        free(root_data);
        gltf_preload_set_error(error, error_cap, "failed to load model");
        return NULL;
    }
    bundle = gltf_preload_bundle_alloc_with_root(root_data, root_size, error, error_cap);
    if (!bundle) {
        free(root_data);
        return NULL;
    }
    json = gltf_json_copy_from_root_bytes(root_data, root_size, &json_len);
    if (json) {
        int ok = gltf_preload_stage_bundle_json(
            bundle, model_path, json, json_len, load_assets, error, error_cap);
        free(json);
        if (!ok) {
            rt_gltf_preload_bundle_free(bundle);
            return NULL;
        }
    }
    return bundle;
}

/// @brief Build an off-main-thread preload bundle from a runtime string path.
/// @details Thin compatibility wrapper around `rt_gltf_preload_bundle_create_cstr`; callers that
/// run
///          on worker threads should prefer the C-string variant with an already-snapshotted path.
rt_gltf_preload_bundle *rt_gltf_preload_bundle_create(rt_string path,
                                                      uint8_t *root_data,
                                                      size_t root_size,
                                                      int load_assets,
                                                      char *error,
                                                      size_t error_cap) {
    return rt_gltf_preload_bundle_create_cstr(
        path ? rt_string_cstr(path) : NULL, root_data, root_size, load_assets, error, error_cap);
}

/// @brief Synthesize a name for an embedded resource (e.g. `inline-image-3.png`).
/// Used by the asset system to give meaningful names to inline base64 buffers.
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
