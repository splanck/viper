//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/physics/rt_physics3d_detect.inc  (#included from rt_physics3d.c; not a standalone translation unit)
// Purpose: Physics3D collision-detection helpers
//===----------------------------------------------------------------------===//
/*==========================================================================
 * Collision detection helpers
 *=========================================================================*/

/// @brief Compute the world-space AABB of a body, writing min/max into `mn`/`mx`.
///
/// If the body has a custom collider, delegates to `rt_collider3d_*` to
/// compute the bounds in the body's pose. Otherwise falls back to the
/// cached primitive shape (AABB / sphere / capsule). This is the broad-
/// phase primitive used by every collision query.
void body_aabb(const rt_body3d *b, double *mn, double *mx) {
    if (!mn || !mx)
        return;
    if (!b) {
        mn[0] = mn[1] = mn[2] = 0.0;
        mx[0] = mx[1] = mx[2] = 0.0;
        return;
    }
    void *collider = body3d_collider_ref(b);
    if (collider) {
        rt_collider_pose pose;
        collider_pose_from_body(b, &pose);
        rt_collider3d_compute_world_aabb_raw(
            collider, pose.position, pose.rotation, pose.scale, mn, mx);
        ph3d_sanitize_aabb(mn, mx);
        return;
    }
    if (b->shape == PH3D_SHAPE_AABB) {
        mn[0] = b->position[0] - b->half_extents[0];
        mn[1] = b->position[1] - b->half_extents[1];
        mn[2] = b->position[2] - b->half_extents[2];
        mx[0] = b->position[0] + b->half_extents[0];
        mx[1] = b->position[1] + b->half_extents[1];
        mx[2] = b->position[2] + b->half_extents[2];
    } else if (b->shape == PH3D_SHAPE_SPHERE) {
        mn[0] = b->position[0] - b->radius;
        mn[1] = b->position[1] - b->radius;
        mn[2] = b->position[2] - b->radius;
        mx[0] = b->position[0] + b->radius;
        mx[1] = b->position[1] + b->radius;
        mx[2] = b->position[2] + b->radius;
    } else /* capsule */
    {
        double a[3], c[3];
        capsule_axis_endpoints(b, a, c);
        mn[0] = (a[0] < c[0] ? a[0] : c[0]) - b->radius;
        mn[1] = (a[1] < c[1] ? a[1] : c[1]) - b->radius;
        mn[2] = (a[2] < c[2] ? a[2] : c[2]) - b->radius;
        mx[0] = (a[0] > c[0] ? a[0] : c[0]) + b->radius;
        mx[1] = (a[1] > c[1] ? a[1] : c[1]) + b->radius;
        mx[2] = (a[2] > c[2] ? a[2] : c[2]) + b->radius;
    }
    ph3d_sanitize_aabb(mn, mx);
}

// Vector / scalar math helpers — internal-only, kept in this TU so the
// optimizer can inline them aggressively. All `double[3]` arguments are
// treated as plain xyz triples (no SIMD layout constraints).

/// @brief Clamp `v` into `[lo, hi]`.
double clampd(double v, double lo, double hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/// @brief 3D dot product `a · b`.
double vec3_dot(const double *a, const double *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/// @brief Squared length `|v|²` — avoids the sqrt when the magnitude isn't needed.
double vec3_len_sq(const double *v) {
    return vec3_dot(v, v);
}

/// @brief Euclidean length `|v|`.
double vec3_len(const double *v) {
    return sqrt(vec3_len_sq(v));
}

/// @brief Copy `src` xyz into `dst`.
void vec3_copy(double *dst, const double *src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

/// @brief Set `dst` to `(x, y, z)`.
void vec3_set(double *dst, double x, double y, double z) {
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
}

/// @brief In-place scalar multiply: `dst *= s`.
static void vec3_scale_in_place(double *dst, double s) {
    dst[0] *= s;
    dst[1] *= s;
    dst[2] *= s;
}

/// @brief Component subtract: `out = a - b`.
void vec3_sub(const double *a, const double *b, double *out) {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

/// @brief Component add: `out = a + b`.
static void vec3_add(const double *a, const double *b, double *out) {
    out[0] = a[0] + b[0];
    out[1] = a[1] + b[1];
    out[2] = a[2] + b[2];
}

/// @brief Cross product: `out = a x b`.
void vec3_cross(const double *a, const double *b, double *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/// @brief Negate: `dst = -src`.
void vec3_negate(const double *src, double *dst) {
    dst[0] = -src[0];
    dst[1] = -src[1];
    dst[2] = -src[2];
}

/// @brief Normalize `v` in place. Returns the original length (0 → no-op).
double vec3_normalize_in_place(double *v) {
    double len = vec3_len(v);
    if (len > 1e-12) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
    return len;
}

/// @brief Copy a vector and normalize it, falling back to +Y for invalid normals.
static void ph3d_vec3_copy_normal_or_up(double *dst, const double *src) {
    ph3d_vec3_copy_state(dst, src);
    if (vec3_normalize_in_place(dst) <= 1e-12)
        vec3_set(dst, 0.0, 1.0, 0.0);
}

/// @brief Sanitize a signed scalar stored in contact/query objects.
static double ph3d_sanitize_contact_scalar(double value) {
    return ph3d_saturate_state_value(value);
}

/// @brief Clamp a query fraction to [0, 1], treating NaN/Inf as 0.
static double ph3d_sanitize_fraction_value(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

// Quaternion helpers — store as `(x, y, z, w)` with `w` last (scalar).
// Used for body orientation and the angular velocity → orientation
// integration step.

/// @brief Set `q` to the identity quaternion (no rotation).
void quat_identity(double *q) {
    q[0] = 0.0;
    q[1] = 0.0;
    q[2] = 0.0;
    q[3] = 1.0;
}

/// @brief Squared norm `|q|²`.
static double quat_len_sq(const double *q) {
    return q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
}

/// @brief Normalize `q` in place. Falls back to identity for near-zero quats.
///
/// Important for stability: numerical drift over many integration steps
/// gradually denormalizes orientations, which causes geometry to skew.
static void quat_normalize(double *q) {
    if (!isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2]) || !isfinite(q[3])) {
        quat_identity(q);
        return;
    }
    double len_sq = quat_len_sq(q);
    if (!isfinite(len_sq) || len_sq < 1e-24) {
        quat_identity(q);
        return;
    }
    double inv_len = 1.0 / sqrt(len_sq);
    q[0] *= inv_len;
    q[1] *= inv_len;
    q[2] *= inv_len;
    q[3] *= inv_len;
}

/// @brief Hamilton product `out = a * b` (composes two rotations).
///
/// Convention: applying `a*b` to a vector first applies `b` then `a`,
/// matching the "right-multiply pre-rotation" convention used in
/// `quat_integrate`.
static void quat_mul(const double *a, const double *b, double *out) {
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

/// @brief Build a unit quaternion from an axis (direction) and angle (radians).
///
/// Standard formula: `q = (sin(θ/2) * axis_unit, cos(θ/2))`. Returns
/// the identity for zero-length axis or zero angle.
static void quat_from_axis_angle(const double *axis, double angle, double *out) {
    double axis_len = vec3_len(axis);
    if (!isfinite(axis_len) || !isfinite(angle) || axis_len < 1e-12 || fabs(angle) < 1e-12) {
        quat_identity(out);
        return;
    }
    double half_angle = angle * 0.5;
    double s = sin(half_angle) / axis_len;
    out[0] = axis[0] * s;
    out[1] = axis[1] * s;
    out[2] = axis[2] * s;
    out[3] = cos(half_angle);
    quat_normalize(out);
}

/// @brief Advance an orientation by the angular velocity over time `dt`.
///
/// Uses the discrete update `q ← Δq * q` where `Δq` is built from
/// `(angular_velocity / |ω|, |ω| * dt)`. Re-normalizes after each step
/// to prevent drift. Skipped when angular velocity or `dt` is zero.
static void quat_integrate(double *orientation, const double *angular_velocity, double dt) {
    double speed = vec3_len(angular_velocity);
    if (!isfinite(speed) || !isfinite(dt) || speed < 1e-12 || dt <= 0.0)
        return;
    double axis[3] = {
        angular_velocity[0] / speed,
        angular_velocity[1] / speed,
        angular_velocity[2] / speed,
    };
    double delta[4], out[4];
    quat_from_axis_angle(axis, speed * dt, delta);
    quat_mul(delta, orientation, out);
    memcpy(orientation, out, sizeof(out));
    quat_normalize(orientation);
}

/// @brief Conjugate `out = q*` — for unit quaternions equivalent to the inverse.
void quat_conjugate(const double *q, double *out) {
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] = q[3];
}

/// @brief Rotate vector `v` by quaternion `q`: `out = q * v * q*`.
///
/// The classic sandwich formula. Allocates two temporaries on the stack.
/// For hot inner loops we'd inline a more efficient form, but this is
/// only called once per pose composition so the cost is irrelevant.
void quat_rotate_vec3(const double *q, const double *v, double *out) {
    double safe_q[4];
    double qv[4];
    double q_conj[4];
    double tmp[4];
    double rotated[4];
    if (!out)
        return;
    if (!q || !v) {
        out[0] = out[1] = out[2] = 0.0;
        return;
    }
    safe_q[0] = q[0];
    safe_q[1] = q[1];
    safe_q[2] = q[2];
    safe_q[3] = q[3];
    quat_normalize(safe_q);
    qv[0] = ph3d_saturate_state_value(v[0]);
    qv[1] = ph3d_saturate_state_value(v[1]);
    qv[2] = ph3d_saturate_state_value(v[2]);
    qv[3] = 0.0;
    quat_conjugate(safe_q, q_conj);
    quat_mul(safe_q, qv, tmp);
    quat_mul(tmp, q_conj, rotated);
    out[0] = ph3d_saturate_state_value(rotated[0]);
    out[1] = ph3d_saturate_state_value(rotated[1]);
    out[2] = ph3d_saturate_state_value(rotated[2]);
}

// Collider pose helpers — pose = position + orientation + scale, used
// when nesting compound colliders or transforming local-space query
// points into world space.

/// @brief Reset a pose to identity (origin, no rotation, unit scale).
void collider_pose_identity(rt_collider_pose *pose) {
    if (!pose)
        return;
    vec3_set(pose->position, 0.0, 0.0, 0.0);
    quat_identity(pose->rotation);
    vec3_set(pose->scale, 1.0, 1.0, 1.0);
}

/// @brief Initialize a pose from a body's transform and collision scale.
///
/// Per-body scale is multiplied with any per-collider child scale during
/// composition (`collider_pose_compose`).
void collider_pose_from_body(const rt_body3d *body, rt_collider_pose *pose) {
    collider_pose_identity(pose);
    if (!body || !pose)
        return;
    ph3d_vec3_copy_state(pose->position, body->position);
    pose->rotation[0] = body->orientation[0];
    pose->rotation[1] = body->orientation[1];
    pose->rotation[2] = body->orientation[2];
    pose->rotation[3] = body->orientation[3];
    quat_normalize(pose->rotation);
    pose->scale[0] = ph3d_sanitize_scale_value(body->scale[0]);
    pose->scale[1] = ph3d_sanitize_scale_value(body->scale[1]);
    pose->scale[2] = ph3d_sanitize_scale_value(body->scale[2]);
}

/// @brief Compose a child transform with a parent pose.
///
/// Order: scale, rotate, translate (TRS). Multiplies parent and child
/// scales component-wise, then rotates the child position by the parent
/// rotation before adding it to the parent position. Child rotation is
/// pre-multiplied into the parent rotation, then re-normalized.
void collider_pose_compose(const rt_collider_pose *parent,
                           const double *child_position,
                           const double *child_rotation,
                           const double *child_scale,
                           rt_collider_pose *out) {
    double scaled[3];
    double rotated[3];
    if (!out)
        return;
    collider_pose_identity(out);
    if (!parent || !child_position || !child_rotation || !child_scale)
        return;
    double parent_scale[3] = {
        ph3d_sanitize_scale_value(parent->scale[0]),
        ph3d_sanitize_scale_value(parent->scale[1]),
        ph3d_sanitize_scale_value(parent->scale[2]),
    };
    double child_scale_safe[3] = {
        ph3d_sanitize_scale_value(child_scale[0]),
        ph3d_sanitize_scale_value(child_scale[1]),
        ph3d_sanitize_scale_value(child_scale[2]),
    };
    double child_rotation_safe[4] = {
        child_rotation[0],
        child_rotation[1],
        child_rotation[2],
        child_rotation[3],
    };
    double parent_rotation_safe[4] = {
        parent->rotation[0],
        parent->rotation[1],
        parent->rotation[2],
        parent->rotation[3],
    };
    quat_normalize(parent_rotation_safe);
    quat_normalize(child_rotation_safe);
    out->scale[0] = ph3d_sanitize_scale_value(parent_scale[0] * child_scale_safe[0]);
    out->scale[1] = ph3d_sanitize_scale_value(parent_scale[1] * child_scale_safe[1]);
    out->scale[2] = ph3d_sanitize_scale_value(parent_scale[2] * child_scale_safe[2]);
    scaled[0] = ph3d_saturate_state_value(child_position[0] * parent_scale[0]);
    scaled[1] = ph3d_saturate_state_value(child_position[1] * parent_scale[1]);
    scaled[2] = ph3d_saturate_state_value(child_position[2] * parent_scale[2]);
    quat_rotate_vec3(parent_rotation_safe, scaled, rotated);
    out->position[0] = ph3d_saturate_state_value(parent->position[0] + rotated[0]);
    out->position[1] = ph3d_saturate_state_value(parent->position[1] + rotated[1]);
    out->position[2] = ph3d_saturate_state_value(parent->position[2] + rotated[2]);
    quat_mul(parent_rotation_safe, child_rotation_safe, out->rotation);
    quat_normalize(out->rotation);
}

/// @brief Transform a local-space point through a pose into world space.
///
/// Applies scale → rotation → translation. Used to lift collision
/// query points (e.g., raycast hits) back into world coordinates.
void transform_point_from_pose(const rt_collider_pose *pose,
                               const double *local_point,
                               double *world_point) {
    double scaled[3];
    double rotated[3];
    if (!pose || !local_point || !world_point)
        return;
    scaled[0] = ph3d_saturate_state_value(local_point[0] * ph3d_sanitize_scale_value(pose->scale[0]));
    scaled[1] = ph3d_saturate_state_value(local_point[1] * ph3d_sanitize_scale_value(pose->scale[1]));
    scaled[2] = ph3d_saturate_state_value(local_point[2] * ph3d_sanitize_scale_value(pose->scale[2]));
    quat_rotate_vec3(pose->rotation, scaled, rotated);
    world_point[0] = ph3d_saturate_state_value(pose->position[0] + rotated[0]);
    world_point[1] = ph3d_saturate_state_value(pose->position[1] + rotated[1]);
    world_point[2] = ph3d_saturate_state_value(pose->position[2] + rotated[2]);
}

/// @brief Inverse of `transform_point_from_pose`: world → local.
///
/// Applies translation → conjugate rotation → inverse scale. Skips
/// the divide-by-scale step on near-zero axes to avoid divide-by-zero
/// when a body has been collapsed along an axis.
void transform_point_to_local(const rt_collider_pose *pose,
                              const double *world_point,
                              double *local_point) {
    double translated[3];
    double inv_rotation[4];
    if (!pose || !world_point || !local_point)
        return;
    translated[0] = ph3d_saturate_state_value(world_point[0] - pose->position[0]);
    translated[1] = ph3d_saturate_state_value(world_point[1] - pose->position[1]);
    translated[2] = ph3d_saturate_state_value(world_point[2] - pose->position[2]);
    quat_conjugate(pose->rotation, inv_rotation);
    quat_rotate_vec3(inv_rotation, translated, local_point);
    if (fabs(ph3d_sanitize_scale_value(pose->scale[0])) > 1e-12)
        local_point[0] = ph3d_saturate_state_value(local_point[0] / ph3d_sanitize_scale_value(pose->scale[0]));
    else
        local_point[0] = 0.0;
    if (fabs(ph3d_sanitize_scale_value(pose->scale[1])) > 1e-12)
        local_point[1] = ph3d_saturate_state_value(local_point[1] / ph3d_sanitize_scale_value(pose->scale[1]));
    else
        local_point[1] = 0.0;
    if (fabs(ph3d_sanitize_scale_value(pose->scale[2])) > 1e-12)
        local_point[2] = ph3d_saturate_state_value(local_point[2] / ph3d_sanitize_scale_value(pose->scale[2]));
    else
        local_point[2] = 0.0;
}

/// @brief Absolute scale factor sanitized for division: non-finite or near-zero → 1.0.
double pose_abs_scale_or_unit(double value) {
    value = fabs(value);
    if (!isfinite(value) || value <= 1e-12)
        return 1.0;
    return value > PH3D_PARAM_ABS_MAX ? PH3D_PARAM_ABS_MAX : value;
}

/// @brief Absolute scale factor with non-finite mapped to 1.0 (zero is preserved).
double pose_abs_scale_or_zero(double value) {
    value = fabs(value);
    if (!isfinite(value))
        return 1.0;
    return value > PH3D_PARAM_ABS_MAX ? PH3D_PARAM_ABS_MAX : value;
}

/// @brief Transform a world-space vector into pose-local space.
/// @details Translation is ignored; inverse rotation and inverse scale are applied.
void transform_vector_to_local(const rt_collider_pose *pose,
                               const double *world_vec,
                               double *local_vec) {
    double inv_rotation[4];
    if (!pose || !world_vec || !local_vec)
        return;
    quat_conjugate(pose->rotation, inv_rotation);
    quat_rotate_vec3(inv_rotation, world_vec, local_vec);
    if (fabs(ph3d_sanitize_scale_value(pose->scale[0])) > 1e-12)
        local_vec[0] = ph3d_saturate_state_value(local_vec[0] / ph3d_sanitize_scale_value(pose->scale[0]));
    else
        local_vec[0] = 0.0;
    if (fabs(ph3d_sanitize_scale_value(pose->scale[1])) > 1e-12)
        local_vec[1] = ph3d_saturate_state_value(local_vec[1] / ph3d_sanitize_scale_value(pose->scale[1]));
    else
        local_vec[1] = 0.0;
    if (fabs(ph3d_sanitize_scale_value(pose->scale[2])) > 1e-12)
        local_vec[2] = ph3d_saturate_state_value(local_vec[2] / ph3d_sanitize_scale_value(pose->scale[2]));
    else
        local_vec[2] = 0.0;
}

/// @brief Transform a local-space normal into world space via inverse-transpose scale.
void transform_normal_from_local(const rt_collider_pose *pose,
                                 const double *local_normal,
                                 double *world_normal) {
    double scaled[3];
    if (!pose || !local_normal || !world_normal)
        return;
    scaled[0] = ph3d_saturate_state_value(local_normal[0]) / pose_abs_scale_or_unit(pose->scale[0]);
    scaled[1] = ph3d_saturate_state_value(local_normal[1]) / pose_abs_scale_or_unit(pose->scale[1]);
    scaled[2] = ph3d_saturate_state_value(local_normal[2]) / pose_abs_scale_or_unit(pose->scale[2]);
    quat_rotate_vec3(pose->rotation, scaled, world_normal);
    ph3d_vec3_sanitize_state(world_normal);
    if (vec3_normalize_in_place(world_normal) <= 1e-12)
        vec3_set(world_normal, 0.0, 1.0, 0.0);
}

/// @brief Choose how many points to sample along a capsule's central axis when
///   approximating it for collision: spacing ≈ radius/2 (min 1e-4), clamped to [1, 256].
int capsule_axis_sample_count(double axis_len, double radius) {
    double spacing;
    int samples;
    if (!isfinite(axis_len) || axis_len <= 1e-9)
        return 1;
    spacing = (isfinite(radius) && radius > 1e-6) ? radius * 0.5 : 0.05;
    if (spacing < 1e-4)
        spacing = 1e-4;
    /* Clamp in double space before the int cast: converting an out-of-range
     * double to int is undefined behavior, and axis_len/spacing can be huge for
     * extreme scales, so the [1,256] clamp must happen before narrowing. */
    double sample_estimate = ceil(axis_len / spacing) + 1.0;
    if (!isfinite(sample_estimate) || sample_estimate >= 256.0)
        return 256;
    if (sample_estimate < 1.0)
        return 1;
    samples = (int)sample_estimate;
    return samples;
}

/// @brief Repair cached primitive shape fields after collider geometry or body scale changes.
static void body3d_sanitize_shape_cache(rt_body3d *body) {
    if (!body)
        return;
    body->half_extents[0] = ph3d_sanitize_extent_value(body->half_extents[0]);
    body->half_extents[1] = ph3d_sanitize_extent_value(body->half_extents[1]);
    body->half_extents[2] = ph3d_sanitize_extent_value(body->half_extents[2]);
    body->radius = ph3d_sanitize_extent_value(body->radius);
    body->height = ph3d_sanitize_extent_value(body->height);
    if (body->shape == PH3D_SHAPE_SPHERE) {
        vec3_set(body->half_extents, body->radius, body->radius, body->radius);
    } else if (body->shape == PH3D_SHAPE_CAPSULE) {
        if (body->height < body->radius * 2.0)
            body->height = body->radius * 2.0;
        vec3_set(body->half_extents,
                 body->radius,
                 fmax(body->height * 0.5, body->radius),
                 body->radius);
    }
}

/// @brief Convert an inertia term to a bounded inverse-inertia component.
static double body3d_inverse_inertia_from(double inertia) {
    if (!isfinite(inertia) || inertia <= 1e-12)
        return 0.0;
    return ph3d_clamp_nonnegative_finite(1.0 / inertia, 0.0);
}

/// @brief Refresh the body's cached primitive-shape fields from its collider.
///
/// Bodies hold a denormalized cache of `(shape, half_extents, radius,
/// height)` so the inner collision loop doesn't have to virtual-dispatch
/// on every comparison. This needs to be re-run whenever the collider's
/// shape parameters change. Falls back to an AABB derived from the
/// collider's local bounds for any non-primitive shape (mesh, hull, etc.).
void body3d_update_shape_cache_from_collider(rt_body3d *body) {
    double local_min[3];
    double local_max[3];
    void *collider;
    double sx;
    double sy;
    double sz;
    int64_t type;
    if (!body) {
        return;
    }
    body->shape = PH3D_SHAPE_AABB;
    vec3_set(body->half_extents, 0.0, 0.0, 0.0);
    body->radius = 0.0;
    body->height = 0.0;
    collider = body3d_collider_ref(body);
    if (!collider)
        return;
    sx = fabs(ph3d_sanitize_scale_value(body->scale[0]));
    sy = fabs(ph3d_sanitize_scale_value(body->scale[1]));
    sz = fabs(ph3d_sanitize_scale_value(body->scale[2]));

    type = rt_collider3d_get_type(collider);
    switch (type) {
        case RT_COLLIDER3D_TYPE_BOX:
            body->shape = PH3D_SHAPE_AABB;
            rt_collider3d_get_box_half_extents_raw(collider, body->half_extents);
            body->half_extents[0] *= sx;
            body->half_extents[1] *= sy;
            body->half_extents[2] *= sz;
            break;
        case RT_COLLIDER3D_TYPE_SPHERE:
            body->shape = PH3D_SHAPE_SPHERE;
            body->radius = rt_collider3d_get_radius_raw(collider);
            body->radius *= fmax(sx, fmax(sy, sz));
            vec3_set(body->half_extents, body->radius, body->radius, body->radius);
            break;
        case RT_COLLIDER3D_TYPE_CAPSULE:
            body->shape = PH3D_SHAPE_CAPSULE;
            {
                double raw_radius = rt_collider3d_get_radius_raw(collider);
                double raw_height = rt_collider3d_get_height_raw(collider);
                body->radius = raw_radius * fmax(sx, sz);
                body->height = fmax(raw_height - 2.0 * raw_radius, 0.0) * sy + 2.0 * body->radius;
            }
            vec3_set(body->half_extents,
                     body->radius,
                     fmax(body->height * 0.5, body->radius),
                     body->radius);
            break;
        default:
            rt_collider3d_get_local_bounds_raw(collider, local_min, local_max);
            body->shape = PH3D_SHAPE_AABB;
            body->half_extents[0] = fabs(local_max[0] - local_min[0]) * 0.5 * sx;
            body->half_extents[1] = fabs(local_max[1] - local_min[1]) * 0.5 * sy;
            body->half_extents[2] = fabs(local_max[2] - local_min[2]) * 0.5 * sz;
            body->radius = body->half_extents[0];
            if (body->half_extents[1] > body->radius)
                body->radius = body->half_extents[1];
            if (body->half_extents[2] > body->radius)
                body->radius = body->half_extents[2];
            body->height = body->half_extents[1] * 2.0;
            break;
    }
    body3d_sanitize_shape_cache(body);
}

/// @brief Compute the diagonal inverse inertia tensor from mass + shape.
///
/// Uses the standard rigid-body formulas:
///   - Box: `I_xx = m(h² + d²)/12`, etc.
///   - Sphere: `I = (2/5) m r²` (uniform on every axis).
///   - Capsule: cylinder approximation, `I_xx = m(3r² + h²)/12`,
///              `I_yy = m r²/2`.
/// Static and kinematic bodies get zero inverse-inertia (they don't
/// rotate from impulses).
static void body3d_compute_inv_inertia(rt_body3d *b) {
    if (!b)
        return;
    vec3_set(b->inv_inertia, 0.0, 0.0, 0.0);
    b->mass = ph3d_clamp_nonnegative_finite(b->mass, 0.0);
    body3d_sanitize_shape_cache(b);
    if (b->mass <= 1e-12 || b->motion_mode != PH3D_MODE_DYNAMIC)
        return;

    if (b->shape == PH3D_SHAPE_AABB) {
        double wx = b->half_extents[0] * 2.0;
        double hy = b->half_extents[1] * 2.0;
        double dz = b->half_extents[2] * 2.0;
        double ixx = b->mass * (hy * hy + dz * dz) / 12.0;
        double iyy = b->mass * (wx * wx + dz * dz) / 12.0;
        double izz = b->mass * (wx * wx + hy * hy) / 12.0;
        b->inv_inertia[0] = body3d_inverse_inertia_from(ixx);
        b->inv_inertia[1] = body3d_inverse_inertia_from(iyy);
        b->inv_inertia[2] = body3d_inverse_inertia_from(izz);
        return;
    }

    if (b->shape == PH3D_SHAPE_SPHERE) {
        double inertia = 0.4 * b->mass * b->radius * b->radius;
        double inv = body3d_inverse_inertia_from(inertia);
        vec3_set(b->inv_inertia, inv, inv, inv);
        return;
    }

    {
        double r2 = b->radius * b->radius;
        double cylinder_height = fmax(b->height - 2.0 * b->radius, 0.0);
        double h2 = cylinder_height * cylinder_height;
        double ixx = b->mass * (3.0 * r2 + h2) / 12.0;
        double iyy = 0.5 * b->mass * r2;
        double izz = ixx;
        b->inv_inertia[0] = body3d_inverse_inertia_from(ixx);
        b->inv_inertia[1] = body3d_inverse_inertia_from(iyy);
        b->inv_inertia[2] = body3d_inverse_inertia_from(izz);
    }
}

/// @brief Synchronize derived motion-mode state with `b->motion_mode`.
///
/// Sets the `is_static`/`is_kinematic` flags, recomputes `inv_mass`
/// (zero for static/kinematic), wipes inverse inertia for non-dynamic
/// bodies, and clears the sleep timer so a mode change doesn't leave a
/// previously-sleeping body inert in the wrong mode.
static void body3d_refresh_motion_mode(rt_body3d *b) {
    if (!b)
        return;
    b->is_static = (b->motion_mode == PH3D_MODE_STATIC) ? 1 : 0;
    b->is_kinematic = (b->motion_mode == PH3D_MODE_KINEMATIC) ? 1 : 0;
    b->mass = ph3d_clamp_nonnegative_finite(b->mass, 0.0);
    if (b->motion_mode == PH3D_MODE_DYNAMIC && b->mass > 1e-12) {
        b->inv_mass = ph3d_clamp_nonnegative_finite(1.0 / b->mass, 0.0);
    } else {
        b->inv_mass = 0.0;
    }
    if (b->motion_mode != PH3D_MODE_DYNAMIC) {
        vec3_set(b->inv_inertia, 0.0, 0.0, 0.0);
        b->is_sleeping = 0;
        b->sleep_time = 0.0;
    } else {
        body3d_compute_inv_inertia(b);
    }
}

/// @brief Validate that a desired motion mode is compatible with the collider.
///
/// Some collider types (e.g., concave triangle meshes) are static-only
/// because the dynamic-collision math doesn't handle them. Traps with
/// `api_name` as the trap message when an invalid combination is
/// requested. Returns 1 when the mode is allowed.
static int body3d_motion_mode_allowed(const rt_body3d *body,
                                      void *collider,
                                      int32_t desired_mode,
                                      const char *api_name) {
    (void)body;
    if (desired_mode == PH3D_MODE_STATIC)
        return 1;
    collider = rt_g3d_checked_or_null(collider, RT_G3D_COLLIDER3D_CLASS_ID);
    if (collider && rt_collider3d_is_static_only_raw(collider)) {
        rt_trap(api_name);
        return 0;
    }
    return 1;
}

/// @brief Wake a sleeping dynamic body (no-op for static/kinematic).
///
/// Called whenever something happens that should re-enter the sim:
/// applied force/torque, teleport, manual velocity change, or contact
/// from a non-sleeping neighbour.
void body3d_wake_if_dynamic(rt_body3d *b) {
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC)
        return;
    b->is_sleeping = 0;
    b->sleep_time = 0.0;
}

/// @brief Apply a world-space angular impulse through the body's local diagonal inertia.
static void body3d_apply_world_angular_impulse(rt_body3d *b, const double *angular_impulse) {
    double inv_rotation[4];
    double local_impulse[3];
    double local_delta[3];
    double world_delta[3];
    double impulse_safe[3];
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC || !angular_impulse)
        return;
    if (!ph3d_vec3_all_finite(angular_impulse))
        return;
    ph3d_vec3_copy_state(impulse_safe, angular_impulse);
    quat_conjugate(b->orientation, inv_rotation);
    quat_rotate_vec3(inv_rotation, impulse_safe, local_impulse);
    local_delta[0] = ph3d_saturate_state_value(local_impulse[0] * b->inv_inertia[0]);
    local_delta[1] = ph3d_saturate_state_value(local_impulse[1] * b->inv_inertia[1]);
    local_delta[2] = ph3d_saturate_state_value(local_impulse[2] * b->inv_inertia[2]);
    quat_rotate_vec3(b->orientation, local_delta, world_delta);
    ph3d_vec3_accumulate_state(b->angular_velocity, world_delta[0], world_delta[1], world_delta[2]);
}

/// @brief Return `I^-1 * v` in world space using the body's local diagonal inertia.
static void body3d_world_inv_inertia_mul(const rt_body3d *b, const double *v, double *out) {
    double inv_rotation[4];
    double local_v[3];
    double local_out[3];
    if (!out)
        return;
    vec3_set(out, 0.0, 0.0, 0.0);
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC || !v || !ph3d_vec3_all_finite(v))
        return;
    quat_conjugate(b->orientation, inv_rotation);
    quat_rotate_vec3(inv_rotation, v, local_v);
    local_out[0] = ph3d_saturate_state_value(local_v[0] * b->inv_inertia[0]);
    local_out[1] = ph3d_saturate_state_value(local_v[1] * b->inv_inertia[1]);
    local_out[2] = ph3d_saturate_state_value(local_v[2] * b->inv_inertia[2]);
    quat_rotate_vec3(b->orientation, local_out, out);
    ph3d_vec3_sanitize_state(out);
}

/// @brief Velocity at a world-space contact point, including angular motion.
void body3d_contact_velocity(const rt_body3d *b, const double *r, double *out) {
    double angular_component[3];
    if (!out)
        return;
    if (!b || !r) {
        vec3_set(out, 0.0, 0.0, 0.0);
        return;
    }
    vec3_cross(b->angular_velocity, r, angular_component);
    vec3_add(b->velocity, angular_component, out);
    ph3d_vec3_sanitize_state(out);
}

/// @brief Effective inverse mass contribution for an impulse direction at offset `r`.
double body3d_contact_impulse_denominator(const rt_body3d *b, const double *r, const double *dir) {
    double r_cross_dir[3];
    double inv_i_term[3];
    double angular_cross[3];
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC || !r || !dir)
        return 0.0;
    vec3_cross(r, dir, r_cross_dir);
    ph3d_vec3_sanitize_state(r_cross_dir);
    body3d_world_inv_inertia_mul(b, r_cross_dir, inv_i_term);
    vec3_cross(inv_i_term, r, angular_cross);
    ph3d_vec3_sanitize_state(angular_cross);
    double denom = b->inv_mass + vec3_dot(angular_cross, dir);
    return isfinite(denom) && denom > 0.0 ? denom : 0.0;
}

/// @brief Apply a world-space linear impulse at a contact offset from the center of mass.
void body3d_apply_contact_impulse(rt_body3d *b, const double *impulse, const double *r) {
    double angular_impulse[3];
    double impulse_safe[3];
    double r_safe[3];
    if (!b || b->motion_mode != PH3D_MODE_DYNAMIC || !impulse || !r)
        return;
    if (!ph3d_vec3_all_finite(impulse))
        return;
    ph3d_vec3_copy_state(impulse_safe, impulse);
    ph3d_vec3_copy_state(r_safe, r);
    ph3d_vec3_accumulate_state(
        b->velocity,
        impulse_safe[0] * b->inv_mass,
        impulse_safe[1] * b->inv_mass,
        impulse_safe[2] * b->inv_mass);
    vec3_cross(r_safe, impulse_safe, angular_impulse);
    ph3d_vec3_sanitize_state(angular_impulse);
    body3d_apply_world_angular_impulse(b, angular_impulse);
}

/// @brief Per-body distance threshold for triggering CCD substeps.
///
/// Returns "half the smallest extent" — a heuristic that matches the
/// fastest a body can move in one step before tunneling becomes a
/// risk. CCD substepping uses this to decide whether the displacement
/// for a step exceeds the body's footprint and needs to be subdivided.
static double body3d_ccd_threshold(const rt_body3d *b) {
    if (!b)
        return 0.0;
    if (b->shape == PH3D_SHAPE_SPHERE || b->shape == PH3D_SHAPE_CAPSULE)
        return b->radius > 1e-6 ? b->radius * 0.5 : 0.0;
    {
        double min_half = b->half_extents[0];
        if (b->half_extents[1] < min_half)
            min_half = b->half_extents[1];
        if (b->half_extents[2] < min_half)
            min_half = b->half_extents[2];
        return min_half > 1e-6 ? min_half * 0.5 : 0.0;
    }
}

// GC finalizers for the boxed query-result objects exposed to Zia.
// Each releases the retained body / collider references so dropping
// the result frees the underlying physics objects when no other
// holder remains.

/// @brief GC finalizer for `PhysicsHit3D` — release referenced body/collider.
static void physics_hit3d_finalizer(void *obj) {
    rt_physics_hit3d_obj *hit = (rt_physics_hit3d_obj *)obj;
    if (!hit)
        return;
    if (hit->body && rt_obj_release_check0(hit->body))
        rt_obj_free(hit->body);
    if (hit->collider && rt_obj_release_check0(hit->collider))
        rt_obj_free(hit->collider);
    hit->body = NULL;
    hit->collider = NULL;
}

/// @brief GC finalizer for `PhysicsHitList3D` — release each item then the array.
static void physics_hit_list3d_finalizer(void *obj) {
    rt_physics_hit_list3d_obj *list = (rt_physics_hit_list3d_obj *)obj;
    if (!list)
        return;
    if (list->items) {
        int64_t count = physics_hit_list3d_count_safe(list);
        for (int64_t i = 0; i < count; ++i) {
            if (list->items[i] && rt_obj_release_check0(list->items[i]))
                rt_obj_free(list->items[i]);
        }
        free(list->items);
        list->items = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

/// @brief GC finalizer for `CollisionEvent3D` — release both bodies and both colliders.
static void collision_event3d_finalizer(void *obj) {
    rt_collision_event3d_obj *event = (rt_collision_event3d_obj *)obj;
    if (!event)
        return;
    if (event->body_a && rt_obj_release_check0(event->body_a))
        rt_obj_free(event->body_a);
    if (event->body_b && rt_obj_release_check0(event->body_b))
        rt_obj_free(event->body_b);
    if (event->collider_a && rt_obj_release_check0(event->collider_a))
        rt_obj_free(event->collider_a);
    if (event->collider_b && rt_obj_release_check0(event->collider_b))
        rt_obj_free(event->collider_b);
    event->body_a = NULL;
    event->body_b = NULL;
    event->collider_a = NULL;
    event->collider_b = NULL;
}

/// @brief Release the body/collider references currently retained by a collision event.
static void collision_event3d_release_refs(rt_collision_event3d_obj *event) {
    if (!event)
        return;
    if (event->body_a && rt_obj_release_check0(event->body_a))
        rt_obj_free(event->body_a);
    if (event->body_b && rt_obj_release_check0(event->body_b))
        rt_obj_free(event->body_b);
    if (event->collider_a && rt_obj_release_check0(event->collider_a))
        rt_obj_free(event->collider_a);
    if (event->collider_b && rt_obj_release_check0(event->collider_b))
        rt_obj_free(event->collider_b);
    event->body_a = NULL;
    event->body_b = NULL;
    event->collider_a = NULL;
    event->collider_b = NULL;
}

/// @brief Refresh an existing boxed collision event from a transient contact snapshot.
static void collision_event3d_assign_from_contact(rt_collision_event3d_obj *event,
                                                  const rt_contact3d *contact) {
    if (!event || !contact)
        return;
    collision_event3d_release_refs(event);
    event->body_a = ph3d_body_ref_or_null(contact->body_a);
    event->body_b = ph3d_body_ref_or_null(contact->body_b);
    event->collider_a = ph3d_collider_ref_or_null(contact->collider_a);
    event->collider_b = ph3d_collider_ref_or_null(contact->collider_b);
    ph3d_vec3_copy_state(event->point, contact->point);
    ph3d_vec3_copy_normal_or_up(event->normal, contact->normal);
    event->separation = ph3d_sanitize_contact_scalar(contact->separation);
    event->contact_count = contact->contact_count;
    if (event->contact_count < 1)
        event->contact_count = 1;
    if (event->contact_count > PH3D_MAX_MANIFOLD_POINTS)
        event->contact_count = PH3D_MAX_MANIFOLD_POINTS;
    for (int32_t i = 0; i < PH3D_MAX_MANIFOLD_POINTS; ++i) {
        ph3d_vec3_copy_state(event->points[i], contact->points[i]);
        ph3d_vec3_copy_normal_or_up(event->normals[i], contact->normals[i]);
        event->separations[i] = ph3d_sanitize_contact_scalar(contact->separations[i]);
    }
    event->relative_speed = ph3d_clamp_nonnegative_finite(contact->relative_speed, 0.0);
    event->normal_impulse = ph3d_clamp_nonnegative_finite(contact->normal_impulse, 0.0);
    event->is_trigger = contact->is_trigger ? 1 : 0;
    if (event->body_a)
        rt_obj_retain_maybe(event->body_a);
    if (event->body_b)
        rt_obj_retain_maybe(event->body_b);
    if (event->collider_a)
        rt_obj_retain_maybe(event->collider_a);
    if (event->collider_b)
        rt_obj_retain_maybe(event->collider_b);
}

/// @brief Box a transient `rt_query_hit3d` into a GC-managed `PhysicsHit3D`.
///
/// Copies all hit fields and retains the body/collider pointers so the
/// hit object can outlive the originating query call.
void *physics_hit3d_new(const rt_query_hit3d *src) {
    rt_physics_hit3d_obj *hit = (rt_physics_hit3d_obj *)rt_obj_new_i64(
        RT_G3D_PHYSICSHIT3D_CLASS_ID, (int64_t)sizeof(rt_physics_hit3d_obj));
    if (!hit) {
        rt_trap("PhysicsHit3D: allocation failed");
        return NULL;
    }
    memset(hit, 0, sizeof(*hit));
    if (src) {
        hit->body = ph3d_body_ref_or_null(src->body);
        hit->collider = ph3d_collider_ref_or_null(src->collider);
        ph3d_vec3_copy_state(hit->point, src->point);
        ph3d_vec3_copy_normal_or_up(hit->normal, src->normal);
        hit->distance = ph3d_clamp_nonnegative_finite(src->distance, 0.0);
        hit->fraction = ph3d_sanitize_fraction_value(src->fraction);
        hit->started_penetrating = src->started_penetrating ? 1 : 0;
        hit->is_trigger = src->is_trigger ? 1 : 0;
        if (hit->body)
            rt_obj_retain_maybe(hit->body);
        if (hit->collider)
            rt_obj_retain_maybe(hit->collider);
    }
    rt_obj_set_finalizer(hit, physics_hit3d_finalizer);
    return hit;
}

/// @brief Build a `PhysicsHitList3D` from a transient hit array.
///
/// Allocates an items array of length `count`, boxes each hit via
/// `physics_hit3d_new`, and returns the GC-managed wrapper. Returns
/// NULL when `count <= 0` so callers can pass empty results back as
/// a NULL Zia value.
void *physics_hit_list3d_new_ex(const rt_query_hit3d *hits,
                                int32_t count,
                                int64_t total_count,
                                int8_t truncated) {
    rt_physics_hit_list3d_obj *list;
    if (count <= 0)
        return NULL;
    list = (rt_physics_hit_list3d_obj *)rt_obj_new_i64(RT_G3D_PHYSICSHITLIST3D_CLASS_ID,
                                                       (int64_t)sizeof(*list));
    if (!list) {
        rt_trap("PhysicsHitList3D: allocation failed");
        return NULL;
    }
    memset(list, 0, sizeof(*list));
    rt_obj_set_finalizer(list, physics_hit_list3d_finalizer);
    list->total_count = total_count >= count ? total_count : count;
    list->truncated = truncated ? 1 : 0;
    list->items = (void **)calloc((size_t)count, sizeof(void *));
    if (!list->items) {
        if (rt_obj_release_check0(list))
            rt_obj_free(list);
        rt_trap("PhysicsHitList3D: allocation failed");
        return NULL;
    }
    list->count = count;
    list->capacity = count;
    for (int32_t i = 0; i < count; ++i) {
        list->items[i] = physics_hit3d_new(&hits[i]);
        if (!list->items[i]) {
            if (rt_obj_release_check0(list))
                rt_obj_free(list);
            rt_trap("PhysicsHitList3D: allocation failed");
            return NULL;
        }
    }
    return list;
}

/// @brief Box a contact's geometric data into a `ContactPoint3D` for Zia.
///
/// Only point/normal/separation are exposed — `body_*` and impulse
/// belong to the higher-level `CollisionEvent3D`.
static void *contact_point3d_new_from_contact(const rt_contact3d *contact) {
    rt_contact_point3d_obj *point;
    if (!contact)
        return NULL;
    point = (rt_contact_point3d_obj *)rt_obj_new_i64(RT_G3D_CONTACTPOINT3D_CLASS_ID,
                                                     (int64_t)sizeof(*point));
    if (!point) {
        rt_trap("ContactPoint3D: allocation failed");
        return NULL;
    }
    memset(point, 0, sizeof(*point));
    ph3d_vec3_copy_state(point->point, contact->point);
    ph3d_vec3_copy_normal_or_up(point->normal, contact->normal);
    point->separation = ph3d_sanitize_contact_scalar(contact->separation);
    return point;
}

/// @brief Box a contact into a `CollisionEvent3D` for OnEnter/Stay/Exit dispatch.
///
/// Includes both bodies, both colliders, contact geometry, relative
/// speed, and the resolved normal impulse so Zia handlers can branch
/// on collision strength, identify the colliding parties, etc. Retains
/// every reference so the event survives past the world step.
static void *collision_event3d_new_from_contact(const rt_contact3d *contact) {
    rt_collision_event3d_obj *event;
    if (!contact)
        return NULL;
    event = (rt_collision_event3d_obj *)rt_obj_new_i64(RT_G3D_COLLISIONEVENT3D_CLASS_ID,
                                                       (int64_t)sizeof(*event));
    if (!event) {
        rt_trap("CollisionEvent3D: allocation failed");
        return NULL;
    }
    memset(event, 0, sizeof(*event));
    rt_obj_set_finalizer(event, collision_event3d_finalizer);
    collision_event3d_assign_from_contact(event, contact);
    return event;
}

/// @brief Release every cached collision-event object in an array and NULL each slot.
/// @details The array itself is kept (capacity unchanged) for reuse next frame; only the pooled
///          event objects are released.
static void world3d_release_event_object_array(void ***array, int32_t *capacity) {
    if (!array || !capacity || !*array)
        return;
    for (int32_t i = 0; i < *capacity; ++i) {
        void *event = (*array)[i];
        if (event && rt_obj_release_check0(event))
            rt_obj_free(event);
        (*array)[i] = NULL;
    }
}

/// @brief Release all four cached collision-event object pools (collision/enter/stay/exit).
static void world3d_release_cached_event_objects(rt_world3d *w) {
    if (!w)
        return;
    world3d_release_event_object_array(&w->collision_event_objects,
                                       &w->collision_event_object_capacity);
    world3d_release_event_object_array(&w->enter_event_objects, &w->enter_event_object_capacity);
    world3d_release_event_object_array(&w->stay_event_objects, &w->stay_event_object_capacity);
    world3d_release_event_object_array(&w->exit_event_objects, &w->exit_event_object_capacity);
}

/// @brief Ensure an event-object pool array holds at least @p needed slots (zero-filled growth).
static int world3d_reserve_event_object_array(void ***array, int32_t *capacity, int32_t needed) {
    void **grown;
    if (!array || !capacity)
        return 0;
    if (needed <= *capacity)
        return 1;
    int32_t new_capacity = ph3d_next_capacity(*capacity, needed, PH3D_INITIAL_CONTACTS);
    if (new_capacity < 0 || (size_t)new_capacity > SIZE_MAX / sizeof(void *))
        return 0;
    grown = (void **)realloc(*array, (size_t)new_capacity * sizeof(void *));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(new_capacity - *capacity) * sizeof(void *));
    *array = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Get (or lazily build) the pooled collision-event object for @p contact at @p index.
/// @details Reuses the slot's existing event object across frames — refreshing its fields from the
///          contact — to avoid per-frame allocation in the collision callback path.
static void *world3d_cached_event_from_contact(void ***array,
                                               int32_t *capacity,
                                               int64_t index,
                                               const rt_contact3d *contact) {
    void *event;
    if (!array || !capacity || index < 0 || index >= INT32_MAX || !contact)
        return NULL;
    if (!world3d_reserve_event_object_array(array, capacity, (int32_t)index + 1))
        return collision_event3d_new_from_contact(contact);
    event = (*array)[index];
    if (event) {
        collision_event3d_assign_from_contact((rt_collision_event3d_obj *)event, contact);
        rt_obj_retain_maybe(event);
        return event;
    }
    event = collision_event3d_new_from_contact(contact);
    if (event) {
        rt_obj_retain_maybe(event);
        (*array)[index] = event;
    }
    return event;
}

/// @brief Return whether a hash-table allocation failure can use the O(n²) diff fallback.
/// @details The fallback is fine for small event sets and preserves behavior under
///   memory pressure, but for large scenes it can cause an unbounded frame spike.
///   This helper caps the fallback by pair comparisons and lets Step fail cleanly
///   when both contact sets are too large to diff without hash tables.
static int world3d_event_diff_fallback_allowed(const rt_world3d *w,
                                               const contact_pair_hash_entry *previous_table,
                                               const contact_pair_hash_entry *current_table) {
    if (!w)
        return 0;
    if (previous_table && current_table)
        return 1;
    if (w->contact_count <= 0 || w->previous_contact_count <= 0)
        return 1;
    int64_t comparisons = (int64_t)w->contact_count * (int64_t)w->previous_contact_count;
    return comparisons <= PH3D_EVENT_DIFF_FALLBACK_PAIR_LIMIT;
}

/// @brief Clear all collision/event counts visible to public query APIs.
/// @details Used by explicit ClearCollisionEvents and by Step failure cleanup so
///   callers never observe stale contacts after a failed allocation or overflow.
static void world3d_clear_visible_collision_events(rt_world3d *w) {
    if (!w)
        return;
    world3d_release_cached_event_objects(w);
    w->contact_count = 0;
    w->previous_contact_count = 0;
    w->enter_event_count = 0;
    w->stay_event_count = 0;
    w->exit_event_count = 0;
}

/// @brief Diff this frame's contacts against the previous frame to fire enter/stay/exit events.
///
/// For each contact that exists this frame:
///   - in previous → `stay`
///   - not in previous → `enter`
/// For each previous contact not present now → `exit`. Then snapshot
/// this frame's contacts as the new "previous" for the next call. The
/// Event arrays grow with the live contact list so large production scenes
/// do not silently drop contacts after the initial capacity.
static int world3d_build_event_buffers(rt_world3d *w) {
    contact_pair_hash_entry *previous_table = NULL;
    contact_pair_hash_entry *current_table = NULL;
    int32_t previous_table_capacity = 0;
    int32_t current_table_capacity = 0;
    int32_t enter_count = 0;
    int32_t stay_count = 0;
    int32_t exit_count = 0;
    int32_t enter_write = 0;
    int32_t stay_write = 0;
    int32_t exit_write = 0;
    if (!w)
        return 0;

    previous_table = contact_pair_table_build(
        w->previous_contacts, w->previous_contact_count, &previous_table_capacity);
    current_table =
        contact_pair_table_build(w->contacts, w->contact_count, &current_table_capacity);
    if (!world3d_event_diff_fallback_allowed(w, previous_table, current_table)) {
        rt_trap("Physics3D.World.Step: collision-event diff allocation failed");
        goto fail;
    }

    for (int32_t i = 0; i < w->contact_count; ++i) {
        int found = previous_table ? contact_pair_table_contains(
                                         previous_table, previous_table_capacity, &w->contacts[i])
                                   : 0;
        if (!previous_table) {
            for (int32_t j = 0; j < w->previous_contact_count; ++j) {
                if (contact_pair_equals(&w->contacts[i], &w->previous_contacts[j])) {
                    found = 1;
                    break;
                }
            }
        }
        if (found) {
            if (!world3d_checked_increment(stay_count, &stay_count))
                goto overflow;
        } else {
            if (!world3d_checked_increment(enter_count, &enter_count))
                goto overflow;
        }
    }

    for (int32_t i = 0; i < w->previous_contact_count; ++i) {
        int found = current_table ? contact_pair_table_contains(current_table,
                                                                current_table_capacity,
                                                                &w->previous_contacts[i])
                                  : 0;
        if (!current_table) {
            for (int32_t j = 0; j < w->contact_count; ++j) {
                if (contact_pair_equals(&w->previous_contacts[i], &w->contacts[j])) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            if (!world3d_checked_increment(exit_count, &exit_count))
                goto overflow;
        }
    }

    if (!world3d_reserve_enter_events(w, enter_count) ||
        !world3d_reserve_stay_events(w, stay_count) ||
        !world3d_reserve_exit_events(w, exit_count) ||
        !world3d_reserve_previous_contacts(w, w->contact_count)) {
        rt_trap("Physics3D.World.Step: event-buffer allocation failed");
        goto fail;
    }

    for (int32_t i = 0; i < w->contact_count; ++i) {
        int found = previous_table ? contact_pair_table_contains(
                                         previous_table, previous_table_capacity, &w->contacts[i])
                                   : 0;
        if (!previous_table) {
            for (int32_t j = 0; j < w->previous_contact_count; ++j) {
                if (contact_pair_equals(&w->contacts[i], &w->previous_contacts[j])) {
                    found = 1;
                    break;
                }
            }
        }
        if (found)
            contact_snapshot_copy(&w->stay_events[stay_write++], &w->contacts[i]);
        else
            contact_snapshot_copy(&w->enter_events[enter_write++], &w->contacts[i]);
    }

    for (int32_t i = 0; i < w->previous_contact_count; ++i) {
        int found = current_table ? contact_pair_table_contains(current_table,
                                                                current_table_capacity,
                                                                &w->previous_contacts[i])
                                  : 0;
        if (!current_table) {
            for (int32_t j = 0; j < w->contact_count; ++j) {
                if (contact_pair_equals(&w->previous_contacts[i], &w->contacts[j])) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found)
            contact_snapshot_copy(&w->exit_events[exit_write++], &w->previous_contacts[i]);
    }

    w->enter_event_count = enter_write;
    w->stay_event_count = stay_write;
    w->exit_event_count = exit_write;
    w->previous_contact_count = w->contact_count;
    for (int32_t i = 0; i < w->contact_count; ++i)
        contact_snapshot_copy(&w->previous_contacts[i], &w->contacts[i]);
    free(previous_table);
    free(current_table);
    return 1;

overflow:
    rt_trap("Physics3D.World.Step: event count overflow");
fail:
    free(previous_table);
    free(current_table);
    return 0;
}
