typedef void (*vscn_material_texture_setter_fn)(void *material, void *texture);
typedef void (*vscn_material_cubemap_setter_fn)(void *material, void *cubemap);

static void vscn_parse_material_color4(void *material_obj,
                                       const char *key,
                                       double out_rgba[4],
                                       double hi) {
    void *arr = vjson_get(material_obj, key);
    if (arr && vjson_len(arr) >= 4) {
        out_rgba[0] = vscn_clamp_or(vjson_arr_f64(arr, 0, out_rgba[0]), out_rgba[0], 0.0, hi);
        out_rgba[1] = vscn_clamp_or(vjson_arr_f64(arr, 1, out_rgba[1]), out_rgba[1], 0.0, hi);
        out_rgba[2] = vscn_clamp_or(vjson_arr_f64(arr, 2, out_rgba[2]), out_rgba[2], 0.0, hi);
        out_rgba[3] = vscn_clamp_or(vjson_arr_f64(arr, 3, out_rgba[3]), out_rgba[3], 0.0, hi);
    }
}

static void vscn_parse_material_color3(void *material_obj,
                                       const char *key,
                                       double out_rgb[3],
                                       double hi) {
    void *arr = vjson_get(material_obj, key);
    if (arr && vjson_len(arr) >= 3) {
        out_rgb[0] = vscn_clamp_or(vjson_arr_f64(arr, 0, out_rgb[0]), out_rgb[0], 0.0, hi);
        out_rgb[1] = vscn_clamp_or(vjson_arr_f64(arr, 1, out_rgb[1]), out_rgb[1], 0.0, hi);
        out_rgb[2] = vscn_clamp_or(vjson_arr_f64(arr, 2, out_rgb[2]), out_rgb[2], 0.0, hi);
    }
}

static void vscn_parse_material_scalars(void *material_obj, rt_material3d *material) {
    material->shininess = vscn_nonnegative_or(
        vjson_f64(material_obj, "shininess", material->shininess), material->shininess);
    material->workflow = vscn_material_workflow_or(
        vjson_i64(material_obj, "workflow", material->workflow), material->workflow);
    material->alpha =
        vscn_clamp_or(vjson_f64(material_obj, "alpha", material->alpha), material->alpha, 0.0, 1.0);
    material->metallic = vscn_clamp_or(
        vjson_f64(material_obj, "metallic", material->metallic), material->metallic, 0.0, 1.0);
    material->roughness = vscn_clamp_or(
        vjson_f64(material_obj, "roughness", material->roughness), material->roughness, 0.0, 1.0);
    material->ao =
        vscn_clamp_or(vjson_f64(material_obj, "ao", material->ao), material->ao, 0.0, 1.0);
    material->emissive_intensity = vscn_nonnegative_or(
        vjson_f64(material_obj, "emissiveIntensity", material->emissive_intensity),
        material->emissive_intensity);
    material->normal_scale = vscn_nonnegative_or(
        vjson_f64(material_obj, "normalScale", material->normal_scale), material->normal_scale);
    material->alpha_mode = vscn_alpha_mode_or(
        vjson_i64(material_obj, "alphaMode", material->alpha_mode), material->alpha_mode);
    material->alpha_cutoff =
        vscn_clamp_or(vjson_f64(material_obj, "alphaCutoff", material->alpha_cutoff),
                      material->alpha_cutoff,
                      0.0,
                      1.0);
    material->double_sided =
        (int8_t)vjson_bool(material_obj, "doubleSided", material->double_sided);
    material->reflectivity =
        vscn_clamp_or(vjson_f64(material_obj, "reflectivity", material->reflectivity),
                      material->reflectivity,
                      0.0,
                      1.0);
    material->unlit = vjson_bool(material_obj, "unlit", material->unlit);
    material->shading_model =
        (int32_t)vjson_i64(material_obj, "shadingModel", material->shading_model);
    if (material->shading_model < 0 || material->shading_model > 5)
        material->shading_model = 0;
}

static void vscn_parse_material_custom_params(void *material_obj, rt_material3d *material) {
    void *arr = vjson_get(material_obj, "customParams");
    for (int i = 0; i < 8; i++) {
        if (arr && i < vjson_len(arr))
            material->custom_params[i] = vscn_clamp_abs_or(
                vjson_arr_f64(arr, i, material->custom_params[i]), material->custom_params[i]);
    }
}

static int vscn_parse_material_texture_slots(void *material_obj, rt_material3d *material) {
    void *arr = vjson_get(material_obj, "textureSlots");
    if (!arr)
        return 1;
    if (!vjson_is_seq(arr))
        return 0;
    for (int i = 0; i < RT_MATERIAL3D_TEXTURE_SLOT_COUNT && i < vjson_len(arr); i++) {
        void *slot_obj = rt_seq_get(arr, i);
        void *uv_arr = slot_obj ? vjson_get(slot_obj, "uvTransform") : NULL;
        if (!slot_obj || !vjson_is_map(slot_obj) || (uv_arr && !vjson_is_seq(uv_arr)))
            return 0;
        material->texture_slot_uv_set[i] =
            vjson_i64(slot_obj, "uvSet", material->texture_slot_uv_set[i]) > 0 ? 1 : 0;
        material->texture_slot_wrap_s[i] =
            vscn_wrap_or(vjson_i64(slot_obj, "wrapS", material->texture_slot_wrap_s[i]),
                         material->texture_slot_wrap_s[i]);
        material->texture_slot_wrap_t[i] =
            vscn_wrap_or(vjson_i64(slot_obj, "wrapT", material->texture_slot_wrap_t[i]),
                         material->texture_slot_wrap_t[i]);
        {
            int32_t legacy_filter =
                vscn_filter_or(vjson_i64(slot_obj, "filter", material->texture_slot_filter[i]),
                               material->texture_slot_filter[i]);
            material->texture_slot_min_filter[i] =
                vscn_filter_or(vjson_i64(slot_obj, "minFilter", legacy_filter), legacy_filter);
            material->texture_slot_mag_filter[i] =
                vscn_filter_or(vjson_i64(slot_obj, "magFilter", legacy_filter), legacy_filter);
            material->texture_slot_mip_filter[i] = vscn_mip_filter_or(
                vjson_i64(slot_obj, "mipFilter", RT_MATERIAL3D_TEXTURE_MIP_FILTER_NONE),
                RT_MATERIAL3D_TEXTURE_MIP_FILTER_NONE);
            material->texture_slot_filter[i] = material->texture_slot_mag_filter[i];
        }
        for (int j = 0; j < 6; j++) {
            if (uv_arr && j < vjson_len(uv_arr))
                material->texture_slot_uv_transform[i][j] = vscn_clamp_abs_or(
                    vjson_arr_f64(uv_arr, j, material->texture_slot_uv_transform[i][j]),
                    material->texture_slot_uv_transform[i][j]);
        }
    }
    material->texture_wrap_s = material->texture_slot_wrap_s[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
    material->texture_wrap_t = material->texture_slot_wrap_t[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
    material->texture_filter = material->texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
    material->texture_min_filter =
        material->texture_slot_min_filter[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
    material->texture_mag_filter =
        material->texture_slot_mag_filter[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
    material->texture_mip_filter =
        material->texture_slot_mip_filter[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
    return 1;
}

static int vscn_bind_material_texture_ref(void *material_obj,
                                          const char *key,
                                          rt_material3d *material,
                                          rt_pixels_impl **textures,
                                          int tex_count,
                                          vscn_material_texture_setter_fn setter) {
    int64_t index;
    if (!vscn_read_index_ref(material_obj, key, &index) ||
        (index >= 0 && (index >= tex_count || !textures || !textures[index])))
        return 0;
    if (index >= 0 && index < tex_count && textures[index])
        setter(material, textures[index]);
    return 1;
}

static int vscn_bind_material_cubemap_ref(void *material_obj,
                                          const char *key,
                                          rt_material3d *material,
                                          rt_cubemap3d **cubemaps,
                                          int cubemap_count,
                                          vscn_material_cubemap_setter_fn setter) {
    int64_t index;
    if (!vscn_read_index_ref(material_obj, key, &index) ||
        (index >= 0 && (index >= cubemap_count || !cubemaps || !cubemaps[index])))
        return 0;
    if (index >= 0 && index < cubemap_count && cubemaps[index])
        setter(material, cubemaps[index]);
    return 1;
}

static int vscn_bind_material_refs(void *material_obj,
                                   rt_material3d *material,
                                   rt_pixels_impl **textures,
                                   int tex_count,
                                   rt_cubemap3d **cubemaps,
                                   int cubemap_count) {
    return vscn_bind_material_texture_ref(
               material_obj, "texture", material, textures, tex_count, rt_material3d_set_texture) &&
           vscn_bind_material_texture_ref(material_obj,
                                          "normalMap",
                                          material,
                                          textures,
                                          tex_count,
                                          rt_material3d_set_normal_map) &&
           vscn_bind_material_texture_ref(material_obj,
                                          "specularMap",
                                          material,
                                          textures,
                                          tex_count,
                                          rt_material3d_set_specular_map) &&
           vscn_bind_material_texture_ref(material_obj,
                                          "emissiveMap",
                                          material,
                                          textures,
                                          tex_count,
                                          rt_material3d_set_emissive_map) &&
           vscn_bind_material_texture_ref(material_obj,
                                          "metallicRoughnessMap",
                                          material,
                                          textures,
                                          tex_count,
                                          rt_material3d_set_metallic_roughness_map) &&
           vscn_bind_material_texture_ref(
               material_obj, "aoMap", material, textures, tex_count, rt_material3d_set_ao_map) &&
           vscn_bind_material_cubemap_ref(material_obj,
                                          "envMap",
                                          material,
                                          cubemaps,
                                          cubemap_count,
                                          rt_material3d_set_env_map);
}
