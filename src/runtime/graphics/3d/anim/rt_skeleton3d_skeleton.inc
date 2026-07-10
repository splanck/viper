//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/anim/rt_skeleton3d_skeleton.inc  (#included from rt_skeleton3d.c; not a standalone translation unit)
// Purpose: Skeleton3D bone hierarchy implementation
//===----------------------------------------------------------------------===//
/*==========================================================================
 * Skeleton3D implementation
 *=========================================================================*/

/// @brief GC finalizer for a Skeleton3D — releases the bone array and any name strings.
static void rt_skeleton3d_finalize(void *obj) {
    rt_skeleton3d *s = (rt_skeleton3d *)obj;
    free(s->bones);
    s->bones = NULL;
    s->bone_count = 0;
    s->bone_capacity = 0;
    free(s->aliases);
    s->aliases = NULL;
    s->alias_count = 0;
    s->alias_capacity = 0;
}

/// @brief Copy a runtime string into the fixed bone-name storage used by GPU payloads.
static int skeleton3d_copy_canonical_name(rt_string name, char out[64]) {
    const char *cstr;
    size_t len;
    if (!out)
        return 0;
    out[0] = '\0';
    if (!name)
        return 1;
    cstr = rt_string_cstr(name);
    if (!cstr)
        return 0;
    len = strlen(cstr);
    if (len > 63)
        len = 63;
    memcpy(out, cstr, len);
    out[len] = '\0';
    return 1;
}

/// @brief Allocate an empty Skeleton3D — no bones until `add_bone` is called.
void *rt_skeleton3d_new(void) {
    rt_skeleton3d *s =
        (rt_skeleton3d *)rt_obj_new_i64(RT_G3D_SKELETON3D_CLASS_ID, (int64_t)sizeof(rt_skeleton3d));
    if (!s) {
        rt_trap("Skeleton3D.New: memory allocation failed");
        return NULL;
    }
    s->vptr = NULL;
    s->bones = NULL;
    s->bone_count = 0;
    s->bone_capacity = 0;
    s->frozen = 0;
    s->aliases = NULL;
    s->alias_count = 0;
    s->alias_capacity = 0;
    rt_obj_set_finalizer(s, rt_skeleton3d_finalize);
    return s;
}

/// @brief Append one bone to the skeleton with its bind-pose matrix and parent reference.
///
/// `parent_index` of -1 marks a root bone. `bind_mat4` is the
/// bone-to-model transform in the rest pose; the inverse-bind
/// (model-to-bone) is computed lazily by
/// `rt_skeleton3d_compute_inverse_bind`.
/// @return The newly-assigned bone index (0-based), or -1 on failure.
int64_t rt_skeleton3d_add_bone(void *obj, rt_string name, int64_t parent_index, void *bind_mat4) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!s)
        return -1;
    if (s->frozen) {
        rt_trap("Skeleton3D.AddBone: skeleton is already bound to an animation runtime");
        return -1;
    }
    s->bone_count = skeleton3d_safe_bone_count(s);
    if (!s->bones)
        s->bone_capacity = 0;
    if (s->bone_capacity < s->bone_count || s->bone_capacity > VGFX3D_MAX_SKELETON_BONES)
        s->bone_capacity = s->bone_count;
    if (s->bone_count >= VGFX3D_MAX_SKELETON_BONES) {
        rt_trap("Skeleton3D.AddBone: max 1024 bones exceeded");
        return -1;
    }
    // parent_index == -1 denotes a root bone. Any other negative value is
    // invalid — previously only the upper bound was checked, so callers could
    // pass -2, -100, etc. which survived validation and corrupted the
    // hierarchy in downstream code.
    if (parent_index < -1 || parent_index >= s->bone_count) {
        rt_trap("Skeleton3D.AddBone: parent_index must be -1 (root) or a valid bone index");
        return -1;
    }

    if (s->bone_count >= s->bone_capacity) {
        int32_t new_capacity = s->bone_capacity > 0 ? s->bone_capacity * 2 : 4;
        vgfx3d_bone_t *nb;
        if (new_capacity < s->bone_count + 1)
            new_capacity = s->bone_count + 1;
        if (new_capacity > VGFX3D_MAX_SKELETON_BONES)
            new_capacity = VGFX3D_MAX_SKELETON_BONES;
        nb = (vgfx3d_bone_t *)realloc(s->bones, (size_t)new_capacity * sizeof(vgfx3d_bone_t));
        if (!nb)
            return -1;
        if (new_capacity > s->bone_capacity) {
            memset(
                nb + s->bone_capacity, 0, (size_t)(new_capacity - s->bone_capacity) * sizeof(*nb));
        }
        s->bones = nb;
        s->bone_capacity = new_capacity;
    }

    vgfx3d_bone_t *bone = &s->bones[s->bone_count];
    memset(bone, 0, sizeof(vgfx3d_bone_t));

    if (!skeleton3d_copy_canonical_name(name, bone->name))
        bone->name[0] = '\0';

    bone->parent_index = (int32_t)parent_index;

    /* Copy bind pose from Mat4 (double → float). Malformed matrices fall back to identity. */
    mat4_impl *bind = skeleton3d_mat4_checked(bind_mat4);
    if (bind) {
        int valid = 1;
        float local[16];
        for (int i = 0; i < 16; i++) {
            double value = bind->m[i];
            if (!skeleton3d_value_fits_float(value)) {
                valid = 0;
                break;
            }
            local[i] = (float)value;
        }
        if (valid)
            memcpy(bone->bind_pose_local, local, sizeof(local));
        else
            mat4f_identity(bone->bind_pose_local);
    } else {
        mat4f_identity(bone->bind_pose_local);
    }
    mat4f_identity(bone->inverse_bind);

    int64_t idx = s->bone_count;
    s->bone_count++;
    return idx;
}

/// @brief Precompute the inverse-bind matrix for every bone (model-space → bone-space).
///
/// Skinning multiplies each bone's animated world matrix by its
/// inverse-bind to displace each vertex from rest space into the
/// animated pose. Call this once after all bones are added.
void rt_skeleton3d_compute_inverse_bind(void *obj) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    float *bind_locals;
    float *globals;
    if (!s)
        return;
    s->bone_count = skeleton3d_safe_bone_count(s);
    if (s->bone_count <= 0)
        return;

    /* Heap-allocated: 1024-bone skeletons would need 128 KB of stack here. */
    bind_locals = (float *)malloc((size_t)s->bone_count * 16u * sizeof(float) * 2u);
    if (!bind_locals)
        return;
    globals = bind_locals + (size_t)s->bone_count * 16u;
    for (int32_t i = 0; i < s->bone_count; i++)
        memcpy(&bind_locals[i * 16], s->bones[i].bind_pose_local, 16 * sizeof(float));
    skeleton3d_compute_globals_from_locals(s, bind_locals, globals, s->bone_count);

    for (int32_t i = 0; i < s->bone_count; i++) {
        if (mat4f_invert(&globals[i * 16], s->bones[i].inverse_bind) != 0)
            mat4f_identity(s->bones[i].inverse_bind);
    }
    free(bind_locals);
}

/// @brief Number of bones in the skeleton (0 for NULL).
int64_t rt_skeleton3d_get_bone_count(void *obj) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!s)
        return 0;
    s->bone_count = skeleton3d_safe_bone_count(s);
    return s->bone_count;
}

/// @brief Linear search for a bone by name; returns its index or -1 if not found.
int64_t rt_skeleton3d_find_bone(void *obj, rt_string name) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!s || !name)
        return -1;
    s->bone_count = skeleton3d_safe_bone_count(s);
    char target[64];
    if (!skeleton3d_copy_canonical_name(name, target))
        return -1;
    for (int32_t i = 0; i < s->bone_count; i++)
        if (strcmp(s->bones[i].name, target) == 0)
            return i;
    return -1;
}

/// @brief Search for a bone by name and wrap the optional index in an Option.
/// @details This preserves `rt_skeleton3d_find_bone` exactly, but turns the
///          legacy `-1` sentinel into `None` for callers that want explicit
///          absence handling.
/// @param obj Skeleton3D handle.
/// @param name Exact bone name to search for.
/// @return `Some(index)` for a matching bone, otherwise `None`.
void *rt_skeleton3d_find_bone_option(void *obj, rt_string name) {
    int64_t index = rt_skeleton3d_find_bone(obj, name);
    return index >= 0 ? rt_option_some_i64(index) : rt_option_none();
}

/// @brief Read the name of bone at `index`. Empty string for out-of-range or NULL.
rt_string rt_skeleton3d_get_bone_name(void *obj, int64_t index) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!s)
        return rt_const_cstr("");
    s->bone_count = skeleton3d_safe_bone_count(s);
    if (index < 0 || index >= s->bone_count)
        return rt_const_cstr("");
    return rt_const_cstr(s->bones[index].name);
}

/// @brief Read the bind-pose Mat4 of bone at `index` (identity for out-of-range / NULL).
void *rt_skeleton3d_get_bone_bind_pose(void *obj, int64_t index) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!s)
        return NULL;
    s->bone_count = skeleton3d_safe_bone_count(s);
    if (index < 0 || index >= s->bone_count)
        return NULL;
    const float *m = s->bones[index].bind_pose_local;
    return rt_mat4_new(m[0],
                       m[1],
                       m[2],
                       m[3],
                       m[4],
                       m[5],
                       m[6],
                       m[7],
                       m[8],
                       m[9],
                       m[10],
                       m[11],
                       m[12],
                       m[13],
                       m[14],
                       m[15]);
}

/// @brief Register (or remove) an external bone-name alias for retargeting.
///
/// `external` is the name a foreign rig uses; `local` is this skeleton's bone
/// it should map to. Retargeting consults these aliases before falling back
/// to humanoid-role inference — the deterministic escape hatch for arbitrary
/// downloaded rigs (e.g. `mixamorig:Hips` -> `hips`). Re-registering an
/// external name replaces the mapping; an empty/NULL `local` removes it.
/// The alias table is not consumed by skinning or pose math, so it may be
/// edited freely even on a frozen skeleton.
void rt_skeleton3d_set_bone_alias(void *obj, rt_string external, rt_string local) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    char ext_buf[64];
    char local_buf[64];
    if (!s)
        return;
    if (!skeleton3d_copy_canonical_name(external, ext_buf) || ext_buf[0] == '\0') {
        rt_trap("Skeleton3D.SetBoneAlias: external name must be non-empty");
        return;
    }
    if (!skeleton3d_copy_canonical_name(local, local_buf))
        local_buf[0] = '\0';

    int32_t found = -1;
    for (int32_t i = 0; i < s->alias_count; i++) {
        if (strcmp(s->aliases[i].external, ext_buf) == 0) {
            found = i;
            break;
        }
    }
    if (local_buf[0] == '\0') {
        /* Removal. */
        if (found >= 0) {
            s->aliases[found] = s->aliases[s->alias_count - 1];
            s->alias_count--;
        }
        return;
    }
    if (found < 0) {
        if (s->alias_count >= s->alias_capacity) {
            int32_t cap = s->alias_capacity > 0 ? s->alias_capacity * 2 : 8;
            rt_skeleton3d_alias *grown =
                (rt_skeleton3d_alias *)realloc(s->aliases, (size_t)cap * sizeof(*grown));
            if (!grown)
                return;
            s->aliases = grown;
            s->alias_capacity = cap;
        }
        found = s->alias_count++;
    }
    memcpy(s->aliases[found].external, ext_buf, sizeof(ext_buf));
    memcpy(s->aliases[found].local, local_buf, sizeof(local_buf));
}

/// @brief Number of registered bone-name aliases.
int64_t rt_skeleton3d_get_alias_count(void *obj) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    return s ? s->alias_count : 0;
}

/// @brief Resolve an external bone name through the alias table.
/// @return The mapped local bone name, or NULL when no alias matches.
const char *skeleton3d_alias_lookup(const rt_skeleton3d *s, const char *external) {
    if (!s || !external || !external[0])
        return NULL;
    for (int32_t i = 0; i < s->alias_count; i++) {
        if (strcmp(s->aliases[i].external, external) == 0)
            return s->aliases[i].local;
    }
    return NULL;
}
