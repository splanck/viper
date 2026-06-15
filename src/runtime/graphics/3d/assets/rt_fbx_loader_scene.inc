//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_fbx_loader_scene.inc  (#included from rt_fbx_loader.c;
// not a standalone translation unit) Purpose: FBX scene-graph extraction
//===----------------------------------------------------------------------===//
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

/// @brief Parsed FBX `Model` transform properties before matrix composition.
/// @details FBX stores a model transform as a layered stack: local TRS, pre/post rotations,
///          rotation/scaling pivots, offsets, and a geometric transform. Keeping these fields
///          explicit lets the scene-node and skeleton importers share the same interpretation
///          instead of each rebuilding a partial Euler-only path.
typedef struct {
    double translation[3];
    double rotation[3];
    double scale[3];
    double pre_rotation[3];
    double post_rotation[3];
    double rotation_offset[3];
    double rotation_pivot[3];
    double scaling_offset[3];
    double scaling_pivot[3];
    double geometric_translation[3];
    double geometric_rotation[3];
    double geometric_scale[3];
    int rotation_order;
    int rotation_active;
} fbx_transform_components_t;

/// @brief Initialize an FBX transform component record to identity/default values.
/// @param out Destination component record; ignored when NULL.
static void fbx_transform_components_init(fbx_transform_components_t *out) {
    static const fbx_transform_components_t zero_components = {0};
    if (!out)
        return;
    *out = zero_components;
    out->scale[0] = 1.0;
    out->scale[1] = 1.0;
    out->scale[2] = 1.0;
    out->geometric_scale[0] = 1.0;
    out->geometric_scale[1] = 1.0;
    out->geometric_scale[2] = 1.0;
    out->rotation_order = 0;
    out->rotation_active = 1;
}

/// @brief Read a vector-style `Properties70` P-node into a fixed XYZ array.
/// @details FBX vector properties store their numeric payload at property slots 4, 5, and 6.
///          Non-finite lanes are ignored by returning 0 so callers can keep prior defaults.
/// @param p The P-node to read.
/// @param out_xyz Receives X, Y, Z on success.
/// @return Non-zero when all three components were finite and copied.
static int fbx_read_p_xyz(fbx_node_t *p, double out_xyz[3]) {
    double x;
    double y;
    double z;
    if (!p || !out_xyz || p->prop_count < 7)
        return 0;
    x = fbx_prop_f64(p, 4);
    y = fbx_prop_f64(p, 5);
    z = fbx_prop_f64(p, 6);
    if (!isfinite(x) || !isfinite(y) || !isfinite(z))
        return 0;
    out_xyz[0] = x;
    out_xyz[1] = y;
    out_xyz[2] = z;
    return 1;
}

/// @brief Parse every FBX transform-stack property from a `Model` node.
/// @details Reads the common Autodesk stack fields: local TRS, PreRotation, PostRotation,
///          RotationOffset/Pivot, ScalingOffset/Pivot, RotationOrder, RotationActive, and
///          Geometric* values. Unknown properties are ignored, preserving forward compatibility.
/// @param model_node FBX `Model` node.
/// @param out Receives a complete default-filled transform component record.
static void fbx_read_model_transform_components(fbx_node_t *model_node,
                                                fbx_transform_components_t *out) {
    fbx_node_t *p70;
    fbx_transform_components_init(out);
    if (!model_node || !out)
        return;
    p70 = fbx_find_child(model_node, "Properties70");
    if (!p70)
        return;
    for (int32_t pi = 0; pi < p70->child_count; pi++) {
        fbx_node_t *p = &p70->children[pi];
        const char *pn;
        if (strcmp(p->name, "P") != 0 || p->prop_count < 1)
            continue;
        pn = fbx_prop_str(p, 0);
        if (fbx_ascii_equals_i(pn, "Lcl Translation")) {
            (void)fbx_read_p_xyz(p, out->translation);
        } else if (fbx_ascii_equals_i(pn, "Lcl Rotation")) {
            (void)fbx_read_p_xyz(p, out->rotation);
        } else if (fbx_ascii_equals_i(pn, "Lcl Scaling")) {
            (void)fbx_read_p_xyz(p, out->scale);
        } else if (fbx_ascii_equals_i(pn, "PreRotation")) {
            (void)fbx_read_p_xyz(p, out->pre_rotation);
        } else if (fbx_ascii_equals_i(pn, "PostRotation")) {
            (void)fbx_read_p_xyz(p, out->post_rotation);
        } else if (fbx_ascii_equals_i(pn, "RotationOffset")) {
            (void)fbx_read_p_xyz(p, out->rotation_offset);
        } else if (fbx_ascii_equals_i(pn, "RotationPivot")) {
            (void)fbx_read_p_xyz(p, out->rotation_pivot);
        } else if (fbx_ascii_equals_i(pn, "ScalingOffset")) {
            (void)fbx_read_p_xyz(p, out->scaling_offset);
        } else if (fbx_ascii_equals_i(pn, "ScalingPivot")) {
            (void)fbx_read_p_xyz(p, out->scaling_pivot);
        } else if (fbx_ascii_equals_i(pn, "GeometricTranslation")) {
            (void)fbx_read_p_xyz(p, out->geometric_translation);
        } else if (fbx_ascii_equals_i(pn, "GeometricRotation")) {
            (void)fbx_read_p_xyz(p, out->geometric_rotation);
        } else if (fbx_ascii_equals_i(pn, "GeometricScaling")) {
            (void)fbx_read_p_xyz(p, out->geometric_scale);
        } else if (fbx_ascii_equals_i(pn, "RotationOrder") && p->prop_count >= 5) {
            int64_t order = fbx_prop_i64(p, 4);
            if (order >= 0 && order <= 6)
                out->rotation_order = (int)order;
        } else if (fbx_ascii_equals_i(pn, "RotationActive") && p->prop_count >= 5) {
            out->rotation_active = fbx_prop_truthy(p, 4);
        }
    }
    for (int i = 0; i < 3; i++) {
        out->translation[i] = fbx_clamp_abs_or(out->translation[i], 0.0, FBX_NUMERIC_ABS_MAX);
        out->rotation[i] = fbx_sanitize_rotation_degrees(out->rotation[i]);
        out->scale[i] = fbx_scale_or_unit(out->scale[i]);
        out->pre_rotation[i] = fbx_sanitize_rotation_degrees(out->pre_rotation[i]);
        out->post_rotation[i] = fbx_sanitize_rotation_degrees(out->post_rotation[i]);
        out->rotation_offset[i] =
            fbx_clamp_abs_or(out->rotation_offset[i], 0.0, FBX_NUMERIC_ABS_MAX);
        out->rotation_pivot[i] = fbx_clamp_abs_or(out->rotation_pivot[i], 0.0, FBX_NUMERIC_ABS_MAX);
        out->scaling_offset[i] = fbx_clamp_abs_or(out->scaling_offset[i], 0.0, FBX_NUMERIC_ABS_MAX);
        out->scaling_pivot[i] = fbx_clamp_abs_or(out->scaling_pivot[i], 0.0, FBX_NUMERIC_ABS_MAX);
        out->geometric_translation[i] =
            fbx_clamp_abs_or(out->geometric_translation[i], 0.0, FBX_NUMERIC_ABS_MAX);
        out->geometric_rotation[i] = fbx_sanitize_rotation_degrees(out->geometric_rotation[i]);
        out->geometric_scale[i] = fbx_scale_or_unit(out->geometric_scale[i]);
    }
}

/// @brief Write a row-major 4x4 identity matrix into @p out.
/// @param out Sixteen-element row-major matrix buffer.
static void fbx_mat4_identity_local(double out[16]) {
    if (!out)
        return;
    memset(out, 0, 16u * sizeof(double));
    out[0] = 1.0;
    out[5] = 1.0;
    out[10] = 1.0;
    out[15] = 1.0;
}

/// @brief Multiply row-major 4x4 matrices (`out = a * b`), safe for aliased output.
/// @param a Left-hand matrix.
/// @param b Right-hand matrix.
/// @param out Receives the product.
static void fbx_mat4_mul_local(const double a[16], const double b[16], double out[16]) {
    double r[16];
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            r[row * 4 + col] = a[row * 4 + 0] * b[0 * 4 + col] + a[row * 4 + 1] * b[1 * 4 + col] +
                               a[row * 4 + 2] * b[2 * 4 + col] + a[row * 4 + 3] * b[3 * 4 + col];
        }
    }
    memcpy(out, r, sizeof(r));
}

/// @brief Post-multiply @p acc by @p rhs (`acc = acc * rhs`).
/// @param acc Accumulated transform matrix, modified in place.
/// @param rhs Transform matrix appended to the stack.
static void fbx_mat4_append_local(double acc[16], const double rhs[16]) {
    fbx_mat4_mul_local(acc, rhs, acc);
}

/// @brief Build a row-major translation matrix.
/// @param x Translation along X.
/// @param y Translation along Y.
/// @param z Translation along Z.
/// @param out Receives the matrix.
static void fbx_mat4_translate_local(double x, double y, double z, double out[16]) {
    fbx_mat4_identity_local(out);
    out[3] = fbx_clamp_abs_or(x, 0.0, FBX_NUMERIC_ABS_MAX);
    out[7] = fbx_clamp_abs_or(y, 0.0, FBX_NUMERIC_ABS_MAX);
    out[11] = fbx_clamp_abs_or(z, 0.0, FBX_NUMERIC_ABS_MAX);
}

/// @brief Build a row-major non-uniform scale matrix.
/// @param x Scale along X.
/// @param y Scale along Y.
/// @param z Scale along Z.
/// @param out Receives the matrix.
static void fbx_mat4_scale_local(double x, double y, double z, double out[16]) {
    fbx_mat4_identity_local(out);
    out[0] = fbx_scale_or_unit(x);
    out[5] = fbx_scale_or_unit(y);
    out[10] = fbx_scale_or_unit(z);
}

/// @brief Build a row-major single-axis rotation matrix from degrees.
/// @param axis Axis selector: 0 = X, 1 = Y, 2 = Z.
/// @param degrees Rotation angle in degrees.
/// @param out Receives the matrix.
static void fbx_mat4_rotate_axis_degrees(int axis, double degrees, double out[16]) {
    double radians = fbx_sanitize_rotation_degrees(degrees) * 3.14159265358979323846 / 180.0;
    double c = cos(radians);
    double s = sin(radians);
    fbx_mat4_identity_local(out);
    if (!isfinite(c) || !isfinite(s))
        return;
    if (axis == 0) {
        out[5] = c;
        out[6] = -s;
        out[9] = s;
        out[10] = c;
    } else if (axis == 1) {
        out[0] = c;
        out[2] = s;
        out[8] = -s;
        out[10] = c;
    } else {
        out[0] = c;
        out[1] = -s;
        out[4] = s;
        out[5] = c;
    }
}

/// @brief Append one axis rotation to an Euler composition matrix.
/// @param acc Accumulated Euler matrix.
/// @param axis Axis selector: 0 = X, 1 = Y, 2 = Z.
/// @param degrees Axis angle in degrees.
static void fbx_mat4_append_axis_rotation(double acc[16], int axis, double degrees) {
    double r[16];
    fbx_mat4_rotate_axis_degrees(axis, degrees, r);
    fbx_mat4_append_local(acc, r);
}

/// @brief Build an FBX Euler rotation matrix for the requested RotationOrder.
/// @details FBX's order value names the local axis sequence. With Viper's row-major matrices and
///          column vectors, preserving the legacy XYZ result means composing in reverse axis order
///          (`XYZ` becomes `Rz * Ry * Rx`). The same rule is applied to the other FBX orders:
///          0 XYZ, 1 XZY, 2 YZX, 3 YXZ, 4 ZXY, 5 ZYX, 6 SphericXYZ (treated as XYZ).
/// @param xyz_degrees Euler components in degrees.
/// @param rotation_order FBX RotationOrder integer.
/// @param out Receives the rotation matrix.
static void fbx_mat4_euler_degrees(const double xyz_degrees[3],
                                   int rotation_order,
                                   double out[16]) {
    int order = (rotation_order >= 0 && rotation_order <= 6) ? rotation_order : 0;
    fbx_mat4_identity_local(out);
    switch (order) {
        case 1: /* XZY */
            fbx_mat4_append_axis_rotation(out, 1, xyz_degrees[1]);
            fbx_mat4_append_axis_rotation(out, 2, xyz_degrees[2]);
            fbx_mat4_append_axis_rotation(out, 0, xyz_degrees[0]);
            break;
        case 2: /* YZX */
            fbx_mat4_append_axis_rotation(out, 0, xyz_degrees[0]);
            fbx_mat4_append_axis_rotation(out, 2, xyz_degrees[2]);
            fbx_mat4_append_axis_rotation(out, 1, xyz_degrees[1]);
            break;
        case 3: /* YXZ */
            fbx_mat4_append_axis_rotation(out, 2, xyz_degrees[2]);
            fbx_mat4_append_axis_rotation(out, 0, xyz_degrees[0]);
            fbx_mat4_append_axis_rotation(out, 1, xyz_degrees[1]);
            break;
        case 4: /* ZXY */
            fbx_mat4_append_axis_rotation(out, 1, xyz_degrees[1]);
            fbx_mat4_append_axis_rotation(out, 0, xyz_degrees[0]);
            fbx_mat4_append_axis_rotation(out, 2, xyz_degrees[2]);
            break;
        case 5: /* ZYX */
            fbx_mat4_append_axis_rotation(out, 0, xyz_degrees[0]);
            fbx_mat4_append_axis_rotation(out, 1, xyz_degrees[1]);
            fbx_mat4_append_axis_rotation(out, 2, xyz_degrees[2]);
            break;
        case 0: /* XYZ */
        case 6: /* SphericXYZ fallback */
        default:
            fbx_mat4_append_axis_rotation(out, 2, xyz_degrees[2]);
            fbx_mat4_append_axis_rotation(out, 1, xyz_degrees[1]);
            fbx_mat4_append_axis_rotation(out, 0, xyz_degrees[0]);
            break;
    }
}

/// @brief Apply FBX Z-up to Viper Y-up conversion to a full affine matrix.
/// @details Vector correction alone is not enough once pivots and bind matrices are involved.
///          This performs the basis change `C * M * C^-1`, where C maps `(x,y,z)` to
///          `(x,z,-y)`.
/// @param matrix Row-major matrix updated in place.
static void fbx_mat4_apply_zup(double matrix[16]) {
    static const double c[16] = {
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        -1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
    };
    static const double c_inv[16] = {
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        -1.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
    };
    double tmp[16];
    if (!matrix)
        return;
    fbx_mat4_mul_local(c, matrix, tmp);
    fbx_mat4_mul_local(tmp, c_inv, matrix);
}

/// @brief Compose the full FBX model transform stack into one affine matrix.
/// @details The stack follows the FBX SDK transform order for local transforms, including
///          rotation/scaling pivots and offsets. Geometric translation is folded into the initial
///          node translation to preserve Viper's existing scene-node behavior; geometric rotation
///          and scale remain appended because there is no separate per-geometry transform slot.
/// @param c Parsed transform components.
/// @param out Receives a row-major affine matrix.
static void fbx_build_model_transform_matrix(const fbx_transform_components_t *c, double out[16]) {
    double step[16];
    double neg[3];
    double post_inverse[3];
    if (!c || !out) {
        if (out)
            fbx_mat4_identity_local(out);
        return;
    }
    fbx_mat4_identity_local(out);

    fbx_mat4_translate_local(c->translation[0] + c->geometric_translation[0],
                             c->translation[1] + c->geometric_translation[1],
                             c->translation[2] + c->geometric_translation[2],
                             step);
    fbx_mat4_append_local(out, step);

    fbx_mat4_translate_local(
        c->rotation_offset[0], c->rotation_offset[1], c->rotation_offset[2], step);
    fbx_mat4_append_local(out, step);
    fbx_mat4_translate_local(
        c->rotation_pivot[0], c->rotation_pivot[1], c->rotation_pivot[2], step);
    fbx_mat4_append_local(out, step);
    if (c->rotation_active) {
        fbx_mat4_euler_degrees(c->pre_rotation, c->rotation_order, step);
        fbx_mat4_append_local(out, step);
        fbx_mat4_euler_degrees(c->rotation, c->rotation_order, step);
        fbx_mat4_append_local(out, step);
        post_inverse[0] = -c->post_rotation[0];
        post_inverse[1] = -c->post_rotation[1];
        post_inverse[2] = -c->post_rotation[2];
        fbx_mat4_euler_degrees(post_inverse, c->rotation_order, step);
        fbx_mat4_append_local(out, step);
    }
    neg[0] = -c->rotation_pivot[0];
    neg[1] = -c->rotation_pivot[1];
    neg[2] = -c->rotation_pivot[2];
    fbx_mat4_translate_local(neg[0], neg[1], neg[2], step);
    fbx_mat4_append_local(out, step);

    fbx_mat4_translate_local(
        c->scaling_offset[0], c->scaling_offset[1], c->scaling_offset[2], step);
    fbx_mat4_append_local(out, step);
    fbx_mat4_translate_local(c->scaling_pivot[0], c->scaling_pivot[1], c->scaling_pivot[2], step);
    fbx_mat4_append_local(out, step);
    fbx_mat4_scale_local(c->scale[0], c->scale[1], c->scale[2], step);
    fbx_mat4_append_local(out, step);
    neg[0] = -c->scaling_pivot[0];
    neg[1] = -c->scaling_pivot[1];
    neg[2] = -c->scaling_pivot[2];
    fbx_mat4_translate_local(neg[0], neg[1], neg[2], step);
    fbx_mat4_append_local(out, step);

    fbx_mat4_euler_degrees(c->geometric_rotation, c->rotation_order, step);
    fbx_mat4_append_local(out, step);
    fbx_mat4_scale_local(c->geometric_scale[0], c->geometric_scale[1], c->geometric_scale[2], step);
    fbx_mat4_append_local(out, step);
}

/// @brief Invert an affine row-major 4x4 matrix.
/// @details Only the upper-left 3x3 and translation column are used; the bottom row is assumed
///          to be `[0 0 0 1]`, matching FBX transform/bind matrices. Singular matrices return 0.
/// @param m Matrix to invert.
/// @param out Receives the inverse when the function returns non-zero.
/// @return Non-zero on success, zero for singular or malformed matrices.
static int fbx_mat4_inverse_affine(const double m[16], double out[16]) {
    double a00;
    double a01;
    double a02;
    double a10;
    double a11;
    double a12;
    double a20;
    double a21;
    double a22;
    double det;
    double inv_det;
    if (!m || !out)
        return 0;
    a00 = m[0];
    a01 = m[1];
    a02 = m[2];
    a10 = m[4];
    a11 = m[5];
    a12 = m[6];
    a20 = m[8];
    a21 = m[9];
    a22 = m[10];
    det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) +
          a02 * (a10 * a21 - a11 * a20);
    if (!isfinite(det) || fabs(det) < 1e-18)
        return 0;
    inv_det = 1.0 / det;
    out[0] = (a11 * a22 - a12 * a21) * inv_det;
    out[1] = (a02 * a21 - a01 * a22) * inv_det;
    out[2] = (a01 * a12 - a02 * a11) * inv_det;
    out[4] = (a12 * a20 - a10 * a22) * inv_det;
    out[5] = (a00 * a22 - a02 * a20) * inv_det;
    out[6] = (a02 * a10 - a00 * a12) * inv_det;
    out[8] = (a10 * a21 - a11 * a20) * inv_det;
    out[9] = (a01 * a20 - a00 * a21) * inv_det;
    out[10] = (a00 * a11 - a01 * a10) * inv_det;
    out[3] = -(out[0] * m[3] + out[1] * m[7] + out[2] * m[11]);
    out[7] = -(out[4] * m[3] + out[5] * m[7] + out[6] * m[11]);
    out[11] = -(out[8] * m[3] + out[9] * m[7] + out[10] * m[11]);
    out[12] = 0.0;
    out[13] = 0.0;
    out[14] = 0.0;
    out[15] = 1.0;
    return 1;
}

/// @brief Convert a row-major matrix into a runtime Mat4 object.
/// @param m Sixteen-element row-major matrix.
/// @return New Mat4 object, or NULL if allocation fails.
static void *fbx_mat4_to_rt(const double m[16]) {
    if (!m)
        return NULL;
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

/// @brief Read a 16-element matrix child as either double-array, float-array, or scalar props.
/// @details Cluster `Transform` / `TransformLink` nodes are usually array properties, but older
///          ASCII-to-binary conversion tools can emit sixteen scalar properties. Both layouts are
///          accepted so valid bind poses are not discarded due to representation.
/// @param node Parent node containing the matrix child.
/// @param child_name Child node name to read.
/// @param out Receives a row-major matrix on success.
/// @return Non-zero when a finite 16-value matrix was read.
static int fbx_read_child_matrix16(fbx_node_t *node, const char *child_name, double out[16]) {
    fbx_node_t *child;
    if (!node || !child_name || !out)
        return 0;
    child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return 0;
    if ((child->props[0].type == 'd' || child->props[0].type == 'f') &&
        child->props[0].v.array.data && child->props[0].v.array.count >= 16u) {
        if (child->props[0].type == 'd') {
            const double *src = (const double *)child->props[0].v.array.data;
            for (int i = 0; i < 16; i++)
                out[i] = src[i];
        } else {
            const float *src = (const float *)child->props[0].v.array.data;
            for (int i = 0; i < 16; i++)
                out[i] = (double)src[i];
        }
    } else if (child->prop_count >= 16) {
        for (int i = 0; i < 16; i++) {
            fbx_prop_t *p = &child->props[i];
            if (p->type == 'D' || p->type == 'F')
                out[i] = fbx_prop_f64(child, i);
            else if (p->type == 'L' || p->type == 'I' || p->type == 'Y' || p->type == 'C')
                out[i] = (double)fbx_prop_i64(child, i);
            else
                return 0;
        }
    } else {
        return 0;
    }
    for (int i = 0; i < 16; i++) {
        if (!isfinite(out[i]))
            return 0;
        out[i] = fbx_clamp_abs_or(out[i], 0.0, FBX_NUMERIC_ABS_MAX);
    }
    return 1;
}

/// @brief Decompose a row-major affine matrix into position, quaternion, and scale.
/// @details SceneNode3D stores TRS rather than a raw matrix. This extracts translation from the
///          matrix column, scale from basis-vector lengths, and a normalized quaternion from the
///          remaining orthonormalized rotation matrix. Shear is intentionally discarded.
/// @param m Source row-major affine matrix.
/// @param pos Receives XYZ translation when non-NULL.
/// @param quat Receives XYZW quaternion when non-NULL.
/// @param scale Receives XYZ scale when non-NULL.
static void fbx_decompose_matrix_trs(const double m[16], double *pos, double *quat, double *scale) {
    double sx;
    double sy;
    double sz;
    double r00;
    double r01;
    double r02;
    double r10;
    double r11;
    double r12;
    double r20;
    double r21;
    double r22;
    double det;
    double trace;
    double qx = 0.0;
    double qy = 0.0;
    double qz = 0.0;
    double qw = 1.0;
    if (pos) {
        pos[0] = m ? fbx_clamp_abs_or(m[3], 0.0, FBX_NUMERIC_ABS_MAX) : 0.0;
        pos[1] = m ? fbx_clamp_abs_or(m[7], 0.0, FBX_NUMERIC_ABS_MAX) : 0.0;
        pos[2] = m ? fbx_clamp_abs_or(m[11], 0.0, FBX_NUMERIC_ABS_MAX) : 0.0;
    }
    if (!m) {
        if (quat) {
            quat[0] = quat[1] = quat[2] = 0.0;
            quat[3] = 1.0;
        }
        if (scale) {
            scale[0] = scale[1] = scale[2] = 1.0;
        }
        return;
    }
    sx = sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    sy = sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    sz = sqrt(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
    sx = fbx_scale_or_unit(sx);
    sy = fbx_scale_or_unit(sy);
    sz = fbx_scale_or_unit(sz);
    det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
          m[2] * (m[4] * m[9] - m[5] * m[8]);
    if (isfinite(det) && det < 0.0)
        sx = -sx;
    r00 = m[0] / sx;
    r01 = m[1] / sy;
    r02 = m[2] / sz;
    r10 = m[4] / sx;
    r11 = m[5] / sy;
    r12 = m[6] / sz;
    r20 = m[8] / sx;
    r21 = m[9] / sy;
    r22 = m[10] / sz;
    trace = r00 + r11 + r22;
    if (trace > 0.0) {
        double s = sqrt(trace + 1.0) * 2.0;
        if (s > 1e-12) {
            qw = 0.25 * s;
            qx = (r21 - r12) / s;
            qy = (r02 - r20) / s;
            qz = (r10 - r01) / s;
        }
    } else if (r00 > r11 && r00 > r22) {
        double s = sqrt(1.0 + r00 - r11 - r22) * 2.0;
        if (s > 1e-12) {
            qw = (r21 - r12) / s;
            qx = 0.25 * s;
            qy = (r01 + r10) / s;
            qz = (r02 + r20) / s;
        }
    } else if (r11 > r22) {
        double s = sqrt(1.0 + r11 - r00 - r22) * 2.0;
        if (s > 1e-12) {
            qw = (r02 - r20) / s;
            qx = (r01 + r10) / s;
            qy = 0.25 * s;
            qz = (r12 + r21) / s;
        }
    } else {
        double s = sqrt(1.0 + r22 - r00 - r11) * 2.0;
        if (s > 1e-12) {
            qw = (r10 - r01) / s;
            qx = (r02 + r20) / s;
            qy = (r12 + r21) / s;
            qz = 0.25 * s;
        }
    }
    {
        double qlen = sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (isfinite(qlen) && qlen > 1e-12) {
            qx /= qlen;
            qy /= qlen;
            qz /= qlen;
            qw /= qlen;
        } else {
            qx = qy = qz = 0.0;
            qw = 1.0;
        }
    }
    if (quat) {
        quat[0] = qx;
        quat[1] = qy;
        quat[2] = qz;
        quat[3] = qw;
    }
    if (scale) {
        scale[0] = fbx_scale_or_unit(sx);
        scale[1] = fbx_scale_or_unit(sy);
        scale[2] = fbx_scale_or_unit(sz);
    }
}

/// @brief Pull the complete FBX transform stack off a `Model` node and expose it as TRS.
/// @details Unlike the older local-TRS-only path, this honors RotationOrder, pivots, offsets,
///          PreRotation/PostRotation, and geometric transforms before decomposing to the
///          SceneNode3D representation. `z_up` applies a full matrix basis conversion so rotated
///          and pivoted models convert correctly.
static void fbx_extract_model_trs(
    fbx_node_t *model_node, int z_up, double *pos, double *quat, double *scale) {
    fbx_transform_components_t components;
    double matrix[16];
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
    fbx_read_model_transform_components(model_node, &components);
    fbx_build_model_transform_matrix(&components, matrix);
    if (z_up)
        fbx_mat4_apply_zup(matrix);
    fbx_decompose_matrix_trs(matrix, pos, quat, scale);
}

/// @brief Linear-search @p bindings for the entry with the given @p id; NULL if not found.
static const fbx_mesh_binding_t *fbx_lookup_mesh_binding_entry(const fbx_mesh_binding_t *bindings,
                                                               int32_t count,
                                                               int64_t id) {
    for (int32_t i = 0; i < count; i++)
        if (bindings[i].id == id)
            return &bindings[i];
    return NULL;
}

/// @brief Build a new Mesh3D containing only the triangles of @p src assigned to
///   @p material_slot, compacting the referenced vertices through a remap table so the
///   sub-mesh has no unused vertices.
/// @return The new mesh object, or NULL if no triangle uses the slot or on allocation failure.
static void *fbx_clone_mesh_for_material_slot(const rt_mesh3d *src,
                                              const fbx_mesh_material_map_t *map,
                                              int32_t material_slot) {
    void *mesh_obj;
    rt_mesh3d *dst;
    uint32_t selected_triangles = 0;
    uint32_t *vertex_map = NULL;
    if (!src || !map || !map->triangle_slots || material_slot < 0)
        return NULL;
    for (uint32_t tri = 0; tri < map->triangle_count; tri++) {
        if (map->triangle_slots[tri] == material_slot)
            selected_triangles++;
    }
    if (selected_triangles == 0 || selected_triangles > UINT32_MAX / 3u)
        return NULL;
    mesh_obj = rt_mesh3d_new();
    if (!mesh_obj)
        return NULL;
    dst = (rt_mesh3d *)mesh_obj;
    rt_mesh3d_begin_geometry_batch(dst);
    rt_mesh3d_reserve(mesh_obj, (int64_t)selected_triangles * 3, selected_triangles);
    if (dst->build_failed)
        goto fail;
    if (src->positions64 && src->vertex_count > 0) {
        if ((size_t)dst->vertex_capacity > SIZE_MAX / (3u * sizeof(double)))
            goto fail;
        dst->positions64 = (double *)calloc((size_t)dst->vertex_capacity * 3u, sizeof(double));
        if (!dst->positions64)
            goto fail;
    }
    if ((size_t)src->vertex_count > SIZE_MAX / sizeof(*vertex_map))
        goto fail;
    vertex_map = (uint32_t *)malloc((size_t)src->vertex_count * sizeof(*vertex_map));
    if (!vertex_map)
        goto fail;
    for (uint32_t i = 0; i < src->vertex_count; i++)
        vertex_map[i] = UINT32_MAX;
    for (uint32_t tri = 0; tri < map->triangle_count; tri++) {
        if (map->triangle_slots[tri] != material_slot)
            continue;
        for (uint32_t corner = 0; corner < 3u; corner++) {
            uint32_t old_index = src->indices[tri * 3u + corner];
            uint32_t new_index;
            if (old_index >= src->vertex_count)
                goto fail;
            if (vertex_map[old_index] == UINT32_MAX) {
                if (dst->vertex_count >= dst->vertex_capacity)
                    goto fail;
                new_index = dst->vertex_count++;
                vertex_map[old_index] = new_index;
                dst->vertices[new_index] = src->vertices[old_index];
                if (dst->positions64 && src->positions64) {
                    memcpy(&dst->positions64[(size_t)new_index * 3u],
                           &src->positions64[(size_t)old_index * 3u],
                           3u * sizeof(double));
                }
            } else {
                new_index = vertex_map[old_index];
            }
            if (dst->index_count >= dst->index_capacity)
                goto fail;
            dst->indices[dst->index_count++] = new_index;
        }
    }
    dst->bone_count = src->bone_count;
    dst->bounds_dirty = 1;
    rt_mesh3d_end_geometry_batch(dst);
    free(vertex_map);
    return mesh_obj;
fail:
    rt_mesh3d_end_geometry_batch(dst);
    free(vertex_map);
    if (rt_obj_release_check0(mesh_obj))
        rt_obj_free(mesh_obj);
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

/// @brief Apply an FBX Model node's name and TRS (position, rotation quaternion, scale) to a
///        freshly created scene node, honoring the Z-up axis convention.
static void fbx_apply_model_node_trs(fbx_node_t *obj, void *node, int z_up) {
    void *quat_obj;
    double pos[3];
    double quat[4];
    double scale[3];
    fbx_set_clean_object_name(node, fbx_prop_str(obj, 1));
    fbx_extract_model_trs(obj, z_up, pos, quat, scale);
    rt_scene_node3d_set_position(node, pos[0], pos[1], pos[2]);
    quat_obj = rt_quat_new(quat[0], quat[1], quat[2], quat[3]);
    if (quat_obj) {
        rt_scene_node3d_set_rotation(node, quat_obj);
        fbx_release_ref(&quat_obj);
    }
    rt_scene_node3d_set_scale(node, scale[0], scale[1], scale[2]);
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
                                  int z_up,
                                  int *out_failed) {
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
    void *default_material = NULL;
    int failed = 0;

    if (out_failed)
        *out_failed = 0;

    if (!root || !objects)
        return NULL;

    for (int32_t i = 0; i < objects->child_count; i++) {
        fbx_node_t *obj = &objects->children[i];
        const char *type_str;
        void *node;
        int64_t model_id;

        if (strcmp(obj->name, "Model") != 0 || obj->prop_count < 3)
            continue;
        type_str = fbx_prop_str(obj, 2);
        if (strcmp(type_str, "LimbNode") == 0 || strcmp(type_str, "Limb") == 0 ||
            strcmp(type_str, "Root") == 0) {
            continue;
        }

        if (model_count >= model_capacity) {
            if (model_capacity < 0 || model_capacity > INT32_MAX / 2 ||
                (size_t)(model_capacity == 0 ? 16 : model_capacity * 2) >
                    SIZE_MAX / sizeof(*models)) {
                failed = 1;
                break;
            }
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
        fbx_apply_model_node_trs(obj, node, z_up);

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
        default_material = rt_material3d_new();
        if (!default_material)
            failed = 1;
    }

    for (int32_t i = 0; !failed && i < model_count; i++) {
        void *primary_mesh = NULL;
        void *primary_material = NULL;
        int32_t extra_mesh_count = 0;
        void **model_materials = NULL;
        int32_t model_material_count = 0;
        int32_t model_material_capacity = 0;
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
            if (material) {
                if (model_material_count >= model_material_capacity) {
                    int32_t new_capacity =
                        model_material_capacity == 0 ? 4 : model_material_capacity * 2;
                    void **grown;
                    if (model_material_capacity > INT32_MAX / 2 ||
                        (size_t)new_capacity > SIZE_MAX / sizeof(void *)) {
                        failed = 1;
                        break;
                    }
                    grown =
                        (void **)realloc(model_materials, (size_t)new_capacity * sizeof(void *));
                    if (!grown) {
                        failed = 1;
                        break;
                    }
                    model_materials = grown;
                    model_material_capacity = new_capacity;
                }
                model_materials[model_material_count++] = material;
                if (!primary_material)
                    primary_material = material;
            }
        }
        for (int32_t ci = 0; !failed && ci < ct->count; ci++) {
            int64_t child_id = ct->entries[ci].child_id;
            int64_t parent_id = ct->entries[ci].parent_id;
            fbx_node_t *child_obj;
            const fbx_mesh_binding_t *binding;
            void *mesh;
            void *material;
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
            binding = fbx_lookup_mesh_binding_entry(mesh_bindings, mesh_binding_count, child_id);
            if (!binding || !binding->mesh)
                continue;
            mesh = binding->mesh;
            material = primary_material ? primary_material : default_material;
            if (binding->material_map.has_slots && binding->material_map.triangle_count > 0 &&
                binding->material_map.triangle_slots) {
                int32_t slot = binding->material_map.triangle_slots[0];
                if (slot >= 0 && slot < model_material_count && model_materials[slot])
                    material = model_materials[slot];
            }
            if (binding->material_map.has_slots && binding->material_map.slot_count > 1 &&
                model_material_count > 1) {
                for (int32_t slot = 0; slot < binding->material_map.slot_count; slot++) {
                    void *slot_mesh = fbx_clone_mesh_for_material_slot(
                        (const rt_mesh3d *)mesh, &binding->material_map, slot);
                    void *slot_material = slot < model_material_count && model_materials[slot]
                                              ? model_materials[slot]
                                              : default_material;
                    if (!slot_mesh)
                        continue;
                    mesh_node = rt_scene_node3d_new();
                    if (!mesh_node) {
                        fbx_release_ref(&slot_mesh);
                        failed = 1;
                        break;
                    }
                    {
                        char generated_name[96];
                        snprintf(generated_name,
                                 sizeof(generated_name),
                                 "mesh_%d_material_%d",
                                 extra_mesh_count,
                                 slot);
                        rt_scene_node3d_set_name(mesh_node, rt_const_cstr(generated_name));
                    }
                    rt_scene_node3d_set_mesh(mesh_node, slot_mesh);
                    rt_scene_node3d_set_material(mesh_node, slot_material);
                    if (!rt_scene_node3d_try_add_child(models[i].node, mesh_node)) {
                        fbx_release_ref(&mesh_node);
                        fbx_release_ref(&slot_mesh);
                        failed = 1;
                        break;
                    }
                    fbx_release_ref(&mesh_node);
                    fbx_release_ref(&slot_mesh);
                    extra_mesh_count++;
                }
                continue;
            }
            if (!primary_mesh) {
                primary_mesh = mesh;
                primary_material = material;
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
            rt_scene_node3d_set_material(mesh_node, material ? material : default_material);
            if (!rt_scene_node3d_try_add_child(models[i].node, mesh_node)) {
                fbx_release_ref(&mesh_node);
                failed = 1;
                break;
            }
            fbx_release_ref(&mesh_node);
            extra_mesh_count++;
        }
        free(model_materials);
        if (failed)
            break;
        if (primary_mesh) {
            rt_scene_node3d_set_mesh(models[i].node, primary_mesh);
            rt_scene_node3d_set_material(models[i].node,
                                         primary_material ? primary_material : default_material);
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
        if (!rt_scene_node3d_try_add_child(parent, models[i].node)) {
            failed = 1;
            break;
        }
    }

    for (int32_t i = 0; i < model_count; i++)
        fbx_release_ref(&models[i].node);
    free(models);
    fbx_release_ref(&default_material);

    if (failed) {
        if (out_failed)
            *out_failed = 1;
        fbx_release_ref(&scene_root);
        return NULL;
    }
    return scene_root;
}
