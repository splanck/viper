//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/physics/rt_physics3d_world.inc  (#included from rt_physics3d.c; not
// a standalone translation unit) Purpose: Physics3D World3D lifecycle, stepping, and integration
//===----------------------------------------------------------------------===//
/*==========================================================================
 * World3D
 *=========================================================================*/

/// @brief GC finalizer for `World3D` — release every body and joint.
///
/// Walks the body and joint arrays releasing each owned reference. The
/// world has strong refs to its bodies/joints so dropping the world
/// frees the entire simulation state.
static void world3d_finalizer(void *obj) {
    rt_world3d *w = (rt_world3d *)obj;
    if (!w)
        return;
    int32_t body_count = world3d_body_count_safe(w);
    int32_t joint_count = world3d_joint_count_safe(w);
    for (int32_t i = 0; i < body_count; i++) {
        rt_body3d *body = w->bodies[i];
        if (body && body->owner_world == w) {
            body->owner_world = NULL;
            body->owner_index = -1;
        }
        if (body && rt_obj_release_check0(body))
            rt_obj_free(body);
        w->bodies[i] = NULL;
    }
    for (int32_t i = 0; i < joint_count; i++) {
        if (w->joints[i] && rt_obj_release_check0(w->joints[i]))
            rt_obj_free(w->joints[i]);
        w->joints[i] = NULL;
    }
    w->body_count = 0;
    w->contact_count = 0;
    w->frame_contact_count = 0;
    w->previous_contact_count = 0;
    w->enter_event_count = 0;
    w->stay_event_count = 0;
    w->exit_event_count = 0;
    w->joint_count = 0;
    world3d_release_cached_event_objects(w);
    free(w->bodies);
    free(w->contacts);
    free(w->frame_contacts);
    free(w->previous_contacts);
    free(w->enter_events);
    free(w->stay_events);
    free(w->exit_events);
    free(w->collision_event_objects);
    free(w->enter_event_objects);
    free(w->stay_event_objects);
    free(w->exit_event_objects);
    free(w->joints);
    free(w->joint_types);
    free(w->broadphase_entries);
    free(w->broadphase_sort_scratch);
    free(w->query_hits_scratch);
    w->query_hits_scratch = NULL;
    if (w->query_sphere_collider && rt_obj_release_check0(w->query_sphere_collider))
        rt_obj_free(w->query_sphere_collider);
    if (w->query_box_collider && rt_obj_release_check0(w->query_box_collider))
        rt_obj_free(w->query_box_collider);
    w->bodies = NULL;
    w->contacts = NULL;
    w->frame_contacts = NULL;
    w->previous_contacts = NULL;
    w->enter_events = NULL;
    w->stay_events = NULL;
    w->exit_events = NULL;
    w->collision_event_objects = NULL;
    w->enter_event_objects = NULL;
    w->stay_event_objects = NULL;
    w->exit_event_objects = NULL;
    w->joints = NULL;
    w->joint_types = NULL;
    w->broadphase_entries = NULL;
    w->broadphase_sort_scratch = NULL;
    w->query_sphere_collider = NULL;
    w->query_box_collider = NULL;
    w->query_hits_scratch = NULL;
    w->max_query_hits = PH3D_MAX_QUERY_HITS;
    w->query_broadphase_count = 0;
    w->query_broadphase_signature = 0;
    w->query_broadphase_mesh_epoch = 0;
    w->query_broadphase_collider_epoch = 0;
    w->query_broadphase_valid = 0;
    w->broadphase_fallback_count = 0;
    w->last_ccd_requested_substeps = 1;
    w->last_ccd_substeps = 1;
    w->ccd_substep_clamped_count = 0;
    w->last_ccd_clamped_body_count = 0;
    w->ccd_substep_clamped_body_count = 0;
    w->ccd_toi_count = 0;
    w->body_capacity = 0;
    w->contact_capacity = 0;
    w->frame_contact_capacity = 0;
    w->previous_contact_capacity = 0;
    w->enter_event_capacity = 0;
    w->stay_event_capacity = 0;
    w->exit_event_capacity = 0;
    w->collision_event_object_capacity = 0;
    w->enter_event_object_capacity = 0;
    w->stay_event_object_capacity = 0;
    w->exit_event_object_capacity = 0;
    w->joint_capacity = 0;
    w->broadphase_capacity = 0;
    w->broadphase_sort_scratch_capacity = 0;
    w->broadphase_world_revision = 1;
    w->query_broadphase_count = 0;
    w->query_broadphase_signature = 0;
    w->query_broadphase_mesh_epoch = 0;
    w->query_broadphase_collider_epoch = 0;
    w->query_broadphase_valid = 0;
}

/// @brief `Physics3D.World.New(gx, gy, gz)` — construct an empty world with gravity.
///
/// Allocates an `rt_world3d`, initializes the gravity vector, and zeros
/// every contact / event / joint table. Body, contact, and joint arrays
/// start at production-sized defaults and grow on demand. Hooks the GC
/// finalizer so dropping the world frees the simulation.
///
/// @param gx,gy,gz Initial world gravity acceleration (m/s²).
/// @return Owned `World3D` handle.
void *rt_world3d_new(double gx, double gy, double gz) {
    rt_world3d *w =
        (rt_world3d *)rt_obj_new_i64(RT_G3D_WORLD3D_CLASS_ID, (int64_t)sizeof(rt_world3d));
    if (!w) {
        rt_trap("Physics3D.World.New: allocation failed");
        return NULL;
    }
    w->vptr = NULL;
    ph3d_vec3_set_finite(w->gravity, gx, gy, gz);
    w->max_query_hits = PH3D_MAX_QUERY_HITS;
    w->body_count = 0;
    w->contact_count = 0;
    w->frame_contact_count = 0;
    w->previous_contact_count = 0;
    w->enter_event_count = 0;
    w->stay_event_count = 0;
    w->exit_event_count = 0;
    w->joint_count = 0;
    w->collision_event_object_capacity = 0;
    w->enter_event_object_capacity = 0;
    w->stay_event_object_capacity = 0;
    w->exit_event_object_capacity = 0;
    w->body_capacity = 0;
    w->contact_capacity = 0;
    w->frame_contact_capacity = 0;
    w->previous_contact_capacity = 0;
    w->enter_event_capacity = 0;
    w->stay_event_capacity = 0;
    w->exit_event_capacity = 0;
    w->joint_capacity = 0;
    w->broadphase_capacity = 0;
    w->broadphase_sort_scratch_capacity = 0;
    w->broadphase_world_revision = 1;
    w->query_broadphase_count = 0;
    w->query_broadphase_signature = 0;
    w->query_broadphase_mesh_epoch = 0;
    w->query_broadphase_collider_epoch = 0;
    w->query_broadphase_valid = 0;
    w->broadphase_fallback_count = 0;
    w->bodies = NULL;
    w->contacts = NULL;
    w->frame_contacts = NULL;
    w->previous_contacts = NULL;
    w->enter_events = NULL;
    w->stay_events = NULL;
    w->exit_events = NULL;
    w->collision_event_objects = NULL;
    w->enter_event_objects = NULL;
    w->stay_event_objects = NULL;
    w->exit_event_objects = NULL;
    w->joints = NULL;
    w->joint_types = NULL;
    w->broadphase_entries = NULL;
    w->broadphase_sort_scratch = NULL;
    w->broadphase_world_revision = 1;
    w->solver_iterations = PH3D_DEFAULT_SOLVER_ITERATIONS;
    w->position_iterations = PH3D_DEFAULT_POSITION_ITERATIONS;
    w->contact_beta = PH3D_DEFAULT_CONTACT_BETA;
    w->restitution_threshold = PH3D_DEFAULT_RESTITUTION_THRESHOLD;
    w->fixed_step_accumulator = 0.0;
    w->fixed_step_alpha = 0.0;
    w->dropped_fixed_steps = 0;
    if (!world3d_reserve_body_capacity(w, PH3D_INITIAL_BODIES) ||
        !world3d_reserve_contacts(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_frame_contacts(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_previous_contacts(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_enter_events(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_stay_events(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_exit_events(w, PH3D_INITIAL_CONTACTS) ||
        !world3d_reserve_joint_capacity(w, PH3D_INITIAL_JOINTS) ||
        !world3d_reserve_broadphase_capacity(w, PH3D_INITIAL_BODIES) ||
        !world3d_reserve_broadphase_sort_scratch(w, PH3D_INITIAL_BODIES)) {
        world3d_finalizer(w);
        if (rt_obj_release_check0(w))
            rt_obj_free(w);
        rt_trap("Physics3D.World.New: storage allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(w, world3d_finalizer);
    return w;
}

/// @brief Decide how many CCD substeps to take this frame.
///
/// Walks every CCD-enabled dynamic body, estimates the maximum
/// distance it will move in `dt` (current speed plus one step of
/// gravity-plus-applied-force acceleration), and computes the number
/// of substeps needed to keep each body's per-substep displacement
/// below its `body3d_ccd_threshold` (≈ half the smallest extent).
/// Clamped to `[1, PH3D_MAX_CCD_SUBSTEPS]`, with clamp diagnostics retained
/// on the world for runtime inspection.
static int world3d_compute_substeps(rt_world3d *w, double dt) {
    int substeps = 1;
    int requested = 1;
    int clamped_body_count = 0;
    if (!w || !isfinite(dt) || dt <= 0.0)
        return substeps;
    w->last_ccd_clamped_body_count = 0;
    for (int32_t i = 0; i < w->body_count; i++) {
        const rt_body3d *b = w->bodies[i];
        if (!b || !b->use_ccd || b->motion_mode == PH3D_MODE_STATIC)
            continue;
        double threshold = body3d_ccd_threshold(b);
        if (threshold <= 1e-6)
            continue;
        double speed = vec3_len(b->velocity);
        if (b->motion_mode == PH3D_MODE_DYNAMIC && b->inv_mass > 0.0) {
            double accel[3] = {
                w->gravity[0] + b->force[0] * b->inv_mass,
                w->gravity[1] + b->force[1] * b->inv_mass,
                w->gravity[2] + b->force[2] * b->inv_mass,
            };
            speed += vec3_len(accel) * dt;
        }
        if (!isfinite(speed))
            continue;
        {
            double needed_f = ceil((speed * dt) / threshold);
            int needed;
            if (!isfinite(needed_f))
                continue;
            if (needed_f > (double)INT_MAX)
                needed = INT_MAX;
            else if (needed_f < 1.0)
                needed = 1;
            else
                needed = (int)needed_f;
            if (needed > PH3D_MAX_CCD_SUBSTEPS)
                clamped_body_count++;
            if (needed > substeps)
                substeps = needed;
        }
    }
    if (substeps < 1)
        substeps = 1;
    requested = substeps;
    if (substeps > PH3D_MAX_CCD_SUBSTEPS)
        substeps = PH3D_MAX_CCD_SUBSTEPS;
    w->last_ccd_requested_substeps = requested;
    w->last_ccd_substeps = substeps;
    if (requested > substeps) {
        w->ccd_substep_clamped_count++;
        w->last_ccd_clamped_body_count = clamped_body_count;
        w->ccd_substep_clamped_body_count += clamped_body_count;
        rt_game3d_diag_record_ccd_clamp(clamped_body_count);
    }
    return substeps;
}

/// @brief Clear one-shot force and torque accumulators from every registered body.
static void world3d_clear_body_accumulators(rt_world3d *w) {
    if (!w)
        return;
    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *b = w->bodies[i];
        if (!b)
            continue;
        b->force[0] = b->force[1] = b->force[2] = 0.0;
        b->torque[0] = b->torque[1] = b->torque[2] = 0.0;
    }
}

/// @brief `World3D.Step(dt)` — advance the simulation by one frame.
///
/// Per-substep:
///   1. **Integration**: forces → velocity (symplectic Euler), apply
///      gravity + linear/angular damping, then advance position +
///      orientation. Sleeping bodies skip the step entirely.
///   2. **Collision detection**: sweep-and-prune broad phase, narrow phase
///      via `test_collision`, append contacts, run `resolve_collision` for
///      non-trigger pairs (with a 6-iteration joint solver per substep).
///   3. **Grounded flag** is set when a contact normal points sufficiently
///      upward (Y > 0.7 ≈ 45° slope), used by character controllers.
/// After the substeps, a unique frame-wide contact set is diffed against
/// the previous frame to fire enter/stay/exit events, and per-step
/// forces/torques are zeroed for the next frame.
///
/// @param obj `World3D` handle.
/// @param dt  Step duration (seconds). No-op for `dt <= 0`.
void rt_world3d_step(void *obj, double dt) {
    rt_world3d *w = world3d_checked(obj);
    if (!w)
        return;
    dt = ph3d_sanitize_step_dt(dt);
    if (dt <= 0.0)
        return;
    w->body_count = world3d_body_count_safe(w);
    w->joint_count = world3d_joint_count_safe(w);
    w->contact_count =
        world3d_contact_count_safe(w->contacts, w->contact_count, w->contact_capacity);
    w->frame_contact_count = world3d_contact_count_safe(
        w->frame_contacts, w->frame_contact_count, w->frame_contact_capacity);
    w->previous_contact_count = world3d_contact_count_safe(
        w->previous_contacts, w->previous_contact_count, w->previous_contact_capacity);
    w->enter_event_count =
        world3d_contact_count_safe(w->enter_events, w->enter_event_count, w->enter_event_capacity);
    w->stay_event_count =
        world3d_contact_count_safe(w->stay_events, w->stay_event_count, w->stay_event_capacity);
    w->exit_event_count =
        world3d_contact_count_safe(w->exit_events, w->exit_event_count, w->exit_event_capacity);
    int step_ok = 0;
    int substeps = world3d_compute_substeps(w, dt);
    double sub_dt = dt / (double)substeps;
    world3d_invalidate_query_broadphase(w);
    world3d_release_cached_event_objects(w);
    w->frame_contact_count = 0;
    w->last_solver_island_count = 0;
    w->last_solver_active_body_count = 0;
    w->last_solver_contact_count = 0;
    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *b = w->bodies[i];
        if (!b)
            continue;
        b->is_grounded = 0;
        vec3_set(b->ground_normal, 0.0, 1.0, 0.0);
    }

    for (int substep = 0; substep < substeps; substep++) {
        /* Phase 1: Integration */
        for (int32_t i = 0; i < w->body_count; i++) {
            rt_body3d *b = w->bodies[i];
            double linear_scale;
            double angular_scale;
            if (!b)
                continue;

            if (b->motion_mode == PH3D_MODE_STATIC) {
                continue;
            }

            if (b->motion_mode == PH3D_MODE_DYNAMIC) {
                if (b->is_sleeping) {
                    continue;
                }

                ph3d_vec3_accumulate_state(b->velocity,
                                           (w->gravity[0] + b->force[0] * b->inv_mass) * sub_dt,
                                           (w->gravity[1] + b->force[1] * b->inv_mass) * sub_dt,
                                           (w->gravity[2] + b->force[2] * b->inv_mass) * sub_dt);
                ph3d_vec3_accumulate_state(b->angular_velocity,
                                           b->torque[0] * b->inv_inertia[0] * sub_dt,
                                           b->torque[1] * b->inv_inertia[1] * sub_dt,
                                           b->torque[2] * b->inv_inertia[2] * sub_dt);

                linear_scale = fmax(0.0, 1.0 - b->linear_damping * sub_dt);
                angular_scale = fmax(0.0, 1.0 - b->angular_damping * sub_dt);
                vec3_scale_in_place(b->velocity, linear_scale);
                vec3_scale_in_place(b->angular_velocity, angular_scale);
                ph3d_vec3_sanitize_state(b->velocity);
                ph3d_vec3_sanitize_state(b->angular_velocity);
                /* Sleep is decided post-solve (world3d_update_sleep), not here:
                 * a body resting under gravity carries -g*dt velocity at this
                 * point and only returns to ~0 once contacts are resolved. */
            }

            /* Swept CCD (time of impact): clip this substep's translation at
             * the first static/kinematic surface so fast bodies can never
             * tunnel regardless of dt. The substep subdivision above keeps
             * displacements small; this pass makes the guarantee exact. The
             * bounding sphere over-approximates non-spherical colliders
             * (conservative — clipping early is safe; the regular contact
             * solve takes over at the surface). */
            if (b->use_ccd && b->motion_mode == PH3D_MODE_DYNAMIC && !b->is_sleeping) {
                double delta[3] = {
                    b->velocity[0] * sub_dt,
                    b->velocity[1] * sub_dt,
                    b->velocity[2] * sub_dt,
                };
                double move_len = vec3_len(delta);
                double threshold = body3d_ccd_threshold(b);
                if (isfinite(move_len) && threshold > 1e-6 && move_len > threshold * 0.5) {
                    double bmin[3];
                    double bmax[3];
                    body_aabb(b, bmin, bmax);
                    double radius =
                        0.5 * fmax(bmax[0] - bmin[0], fmax(bmax[1] - bmin[1], bmax[2] - bmin[2]));
                    double toi = 1.0;
                    double normal[3] = {0.0, 1.0, 0.0};
                    if (radius > 1e-6 &&
                        world3d_ccd_sweep_sphere_raw(
                            w, b->position, radius, delta, b, &toi, normal) &&
                        toi < 1.0) {
                        /* Speculative clip: stop the NORMAL component of the
                         * translation just before the surface, but carry the
                         * full tangential component. A body rolling or sliding
                         * along a surface it is barely separated from sweeps
                         * into it every substep with toi ~= 0; clipping the
                         * whole delta would freeze its tangential motion (the
                         * bowling-ball-stalls-on-the-lane failure). Head-on
                         * impacts have no tangential part, so the thin-wall
                         * anti-tunneling guarantee is unchanged. */
                        double dn =
                            delta[0] * normal[0] + delta[1] * normal[1] + delta[2] * normal[2];
                        double clipped_n = dn * toi;
                        if (dn < 0.0) {
                            /* Small skin keeps the narrowphase from starting
                             * embedded next substep. */
                            clipped_n += 1e-3;
                            if (clipped_n > 0.0)
                                clipped_n = 0.0;
                        }
                        ph3d_vec3_accumulate_state(
                            b->position,
                            (delta[0] - dn * normal[0]) + normal[0] * clipped_n,
                            (delta[1] - dn * normal[1]) + normal[1] * clipped_n,
                            (delta[2] - dn * normal[2]) + normal[2] * clipped_n);
                        /* Reflect the approaching normal component by the
                         * body's restitution (matches what the solver would
                         * do); the tangential component is preserved so the
                         * body slides. If the contact persists next substep,
                         * the solver sees a separating/neutral velocity and
                         * adds no double bounce. */
                        double vn = b->velocity[0] * normal[0] + b->velocity[1] * normal[1] +
                                    b->velocity[2] * normal[2];
                        if (vn < 0.0) {
                            double e = b->restitution;
                            if (!isfinite(e) || e < 0.0)
                                e = 0.0;
                            if (e > 1.0)
                                e = 1.0;
                            double impulse = -(1.0 + e) * vn;
                            ph3d_vec3_accumulate_state(b->velocity,
                                                       normal[0] * impulse,
                                                       normal[1] * impulse,
                                                       normal[2] * impulse);
                        }
                        w->ccd_toi_count++;
                        quat_integrate(b->orientation, b->angular_velocity, sub_dt);
                        ph3d_vec3_sanitize_state(b->position);
                        ph3d_vec3_sanitize_state(b->velocity);
                        quat_normalize(b->orientation);
                        body3d_touch_broadphase(b);
                        continue;
                    }
                }
            }

            ph3d_vec3_accumulate_state(b->position,
                                       b->velocity[0] * sub_dt,
                                       b->velocity[1] * sub_dt,
                                       b->velocity[2] * sub_dt);
            quat_integrate(b->orientation, b->angular_velocity, sub_dt);
            ph3d_vec3_sanitize_state(b->position);
            quat_normalize(b->orientation);
            body3d_touch_broadphase(b);
        }

        /* Phase 2: detect contacts once, then warm-started sequential-impulse solve.
         * Detecting once (rather than re-detecting per iteration) lets per-point
         * impulses accumulate and warm-start across frames, which is what makes
         * stacks rest stably. Joints are interleaved with the velocity iterations. */
        int32_t solver_iterations = ph3d_clamp_solver_iterations(w->solver_iterations);
        int32_t position_iterations = ph3d_clamp_solver_iterations(w->position_iterations);
        ph3d_solver_island_batch solver_batch;
        if (!world3d_detect_contacts(w))
            goto cleanup;
        if (!world3d_build_solver_island_batch(w, &solver_batch))
            goto cleanup;
        if (solver_batch.island_count > w->last_solver_island_count)
            w->last_solver_island_count = solver_batch.island_count;
        if (solver_batch.active_body_count > w->last_solver_active_body_count)
            w->last_solver_active_body_count = solver_batch.active_body_count;
        if (solver_batch.solver_contact_count > w->last_solver_contact_count)
            w->last_solver_contact_count = solver_batch.solver_contact_count;
        world3d_warm_start_solver_islands(w, &solver_batch);
        for (int32_t iter = 0; iter < solver_iterations; iter++) {
            world3d_solve_velocity_solver_islands(&solver_batch, w);
            for (int32_t j = 0; j < w->joint_count; j++) {
                if (w->joints[j])
                    rt_joint3d_solve(w->joints[j], w->joint_types[j], sub_dt);
            }
        }
        for (int32_t iter = 0; iter < position_iterations; iter++)
            world3d_solve_position_solver_islands(&solver_batch, w);
        ph3d_solver_island_batch_free(&solver_batch);
        world3d_finalize_contacts(w);
        world3d_update_sleep(w, sub_dt);
        for (int32_t i = 0; i < w->contact_count; ++i) {
            if (!world3d_append_frame_contact_unique(w, &w->contacts[i]))
                goto cleanup;
        }
    }

    if (!world3d_publish_frame_contacts(w))
        goto cleanup;
    if (!world3d_build_event_buffers(w))
        goto cleanup;
    step_ok = 1;

cleanup:
    if (!step_ok)
        world3d_clear_visible_collision_events(w);
    world3d_invalidate_query_broadphase(w);
    world3d_clear_body_accumulators(w);
}

/// @brief `World3D.StepFixed(dt, fixedDt, maxSteps)` — run accumulated fixed steps.
int64_t rt_world3d_step_fixed(void *obj, double dt, double fixed_dt, int64_t max_steps) {
    rt_world3d *w = world3d_checked(obj);
    double frame_dt = ph3d_sanitize_step_dt(dt);
    double fixed = ph3d_sanitize_fixed_dt(fixed_dt);
    int32_t max_step_count = ph3d_clamp_fixed_step_limit(max_steps);
    int64_t steps = 0;
    if (!w || frame_dt <= 0.0 || fixed <= 0.0 || max_step_count <= 0) {
        ph3d_update_fixed_step_alpha(w, fixed);
        return 0;
    }

    w->fixed_step_accumulator += frame_dt;
    if (!isfinite(w->fixed_step_accumulator) || w->fixed_step_accumulator < 0.0)
        w->fixed_step_accumulator = 0.0;
    {
        double max_accumulator = fixed * (double)max_step_count;
        if (isfinite(max_accumulator) && max_accumulator > 0.0 &&
            w->fixed_step_accumulator > max_accumulator) {
            double dropped_time = w->fixed_step_accumulator - max_accumulator;
            if (dropped_time >= fixed)
                ph3d_add_dropped_fixed_steps(w, (int64_t)floor(dropped_time / fixed));
            w->fixed_step_accumulator = max_accumulator;
        }
    }

    while (w->fixed_step_accumulator + 1e-12 >= fixed && steps < max_step_count) {
        rt_world3d_step(w, fixed);
        w->fixed_step_accumulator -= fixed;
        if (w->fixed_step_accumulator < 0.0 && w->fixed_step_accumulator > -1e-9)
            w->fixed_step_accumulator = 0.0;
        steps++;
    }
    if (steps >= max_step_count && w->fixed_step_accumulator >= fixed) {
        ph3d_add_dropped_fixed_steps(w, (int64_t)floor(w->fixed_step_accumulator / fixed));
        w->fixed_step_accumulator = 0.0;
    }
    ph3d_update_fixed_step_alpha(w, fixed);
    return steps;
}

/// @brief `World3D.FixedStepAlpha` — fixed-step accumulator interpolation fraction.
double rt_world3d_get_fixed_step_alpha(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    if (!w || !isfinite(w->fixed_step_alpha))
        return 0.0;
    if (w->fixed_step_alpha < 0.0)
        return 0.0;
    if (w->fixed_step_alpha >= 1.0)
        return 0.999999;
    return w->fixed_step_alpha;
}

/// @brief `World3D.DroppedFixedSteps` — fixed-step updates discarded by the spiral guard.
int64_t rt_world3d_get_dropped_fixed_steps(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w && w->dropped_fixed_steps > 0 ? w->dropped_fixed_steps : 0;
}

/// @brief Return whether @p body is registered in @p w, using owner-index metadata first.
/// @details Body owner fields make the common membership query O(1). If an index
///   is stale, the helper falls back to a scan and repairs the cached index when
///   it finds the body. This keeps old objects coherent across swap-removes and
///   defensive count clamping.
static int world3d_body_is_registered(rt_world3d *w, rt_body3d *body) {
    if (!w || !body)
        return 0;
    int32_t body_count = world3d_body_count_safe(w);
    w->body_count = body_count;
    if (body->owner_world == w && body->owner_index >= 0 && body->owner_index < body_count &&
        w->bodies[body->owner_index] == body)
        return 1;
    for (int32_t i = 0; i < body_count; ++i) {
        if (w->bodies[i] == body) {
            body->owner_world = w;
            body->owner_index = i;
            return 1;
        }
    }
    if (body->owner_world == w) {
        body->owner_world = NULL;
        body->owner_index = -1;
    }
    return 0;
}

/// @brief Return whether a joint endpoint can be attached to @p w.
/// @details Registered bodies are accepted directly. Bodies that are not owned by
///   any world are also accepted for legacy/manual joint registration, while bodies
///   already owned by a different world are rejected so constraints cannot bridge
///   two independent simulations.
static int world3d_joint_body_allowed(rt_world3d *w, rt_body3d *body) {
    if (!w || !body)
        return 0;
    if (world3d_body_is_registered(w, body))
        return 1;
    return body->owner_world == NULL;
}

/// @brief `World3D.AddJoint(joint, type)` — register a constraint.
///
/// Retains the joint and stores its type tag so `rt_joint3d_solve` can
/// dispatch correctly. Storage grows on demand.
void rt_world3d_add_joint(void *obj, void *joint, int64_t joint_type) {
    rt_world3d *w = world3d_checked(obj);
    void *joint_body_a = NULL;
    void *joint_body_b = NULL;
    if (!w || !joint)
        return;
    if (!joint3d_matches_type(joint, joint_type)) {
        rt_trap("Physics3D.World.AddJoint: joint object does not match joint type");
        return;
    }
    if (!rt_joint3d_get_bodies(joint, (int32_t)joint_type, &joint_body_a, &joint_body_b) ||
        !world3d_joint_body_allowed(w, (rt_body3d *)joint_body_a) ||
        !world3d_joint_body_allowed(w, (rt_body3d *)joint_body_b)) {
        rt_trap("Physics3D.World.AddJoint: joint bodies cannot belong to another world");
        return;
    }
    int32_t joint_count = world3d_joint_count_safe(w);
    w->joint_count = joint_count;
    for (int32_t i = 0; i < joint_count; i++) {
        if (w->joints[i] == joint)
            return;
    }
    if (!world3d_reserve_joint_capacity(w, w->joint_count + 1)) {
        rt_trap("Physics3D: joint storage allocation failed");
        return;
    }
    rt_obj_retain_maybe(joint);
    w->joints[w->joint_count] = joint;
    w->joint_types[w->joint_count] = (int32_t)joint_type;
    w->joint_count++;
}

/// @brief `World3D.RemoveJoint(joint)` — unregister a constraint.
///
/// Linear scan; on hit, swaps in the last entry to compact the array
/// and releases the world's reference. Silent no-op when not found.
void rt_world3d_remove_joint(void *obj, void *joint) {
    rt_world3d *w = world3d_checked(obj);
    if (!w || !joint)
        return;
    int32_t joint_count = world3d_joint_count_safe(w);
    w->joint_count = joint_count;
    for (int32_t i = 0; i < joint_count; i++) {
        if (w->joints[i] == joint) {
            void *removed = w->joints[i];
            w->joints[i] = w->joints[--w->joint_count];
            w->joint_types[i] = w->joint_types[w->joint_count];
            w->joints[w->joint_count] = NULL;
            w->joint_types[w->joint_count] = 0;
            if (rt_obj_release_check0(removed))
                rt_obj_free(removed);
            return;
        }
    }
}

/// @brief Return whether a concrete joint references @p body as either endpoint.
static int world3d_joint_mentions_body(void *joint, int32_t joint_type, rt_body3d *body) {
    void *body_a = NULL;
    void *body_b = NULL;
    return body && rt_joint3d_get_bodies(joint, joint_type, &body_a, &body_b) &&
           (body_a == body || body_b == body);
}

/// @brief Remove and release every joint in @p w that references @p body.
/// @details Called before body removal so a removed body is not retained by stale
///   constraints and the solver never sees a joint endpoint outside the world.
static void world3d_purge_joints_for_body(rt_world3d *w, rt_body3d *body) {
    if (!w || !body)
        return;
    int32_t joint_count = world3d_joint_count_safe(w);
    w->joint_count = joint_count;
    int32_t write = 0;
    for (int32_t read = 0; read < joint_count; ++read) {
        void *joint = w->joints[read];
        int32_t joint_type = w->joint_types[read];
        if (world3d_joint_mentions_body(joint, joint_type, body)) {
            if (joint && rt_obj_release_check0(joint))
                rt_obj_free(joint);
            continue;
        }
        if (write != read) {
            w->joints[write] = w->joints[read];
            w->joint_types[write] = w->joint_types[read];
        }
        write++;
    }
    for (int32_t i = write; i < joint_count; ++i) {
        w->joints[i] = NULL;
        w->joint_types[i] = 0;
    }
    w->joint_count = write;
}

/// @brief `World3D.JointCount` — number of registered joints (0 for NULL).
int64_t rt_world3d_joint_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return world3d_joint_count_safe(w);
}

/// @brief `World3D.SolverIterations` — iterative constraint-solver pass count.
int64_t rt_world3d_get_solver_iterations(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? ph3d_clamp_solver_iterations(w->solver_iterations) : 0;
}

/// @brief `World3D.SolverIterations` setter — tune iterative solver passes.
void rt_world3d_set_solver_iterations(void *obj, int64_t iterations) {
    rt_world3d *w = world3d_checked(obj);
    if (w)
        w->solver_iterations = ph3d_clamp_solver_iterations(iterations);
}

/// @brief `World3D.PositionIterations` — positional correction pass count.
int64_t rt_world3d_get_position_iterations(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? ph3d_clamp_solver_iterations(w->position_iterations) : 0;
}

/// @brief `World3D.PositionIterations` setter — tune positional correction passes.
void rt_world3d_set_position_iterations(void *obj, int64_t iterations) {
    rt_world3d *w = world3d_checked(obj);
    if (w)
        w->position_iterations = ph3d_clamp_solver_iterations(iterations);
}

/// @brief `World3D.ContactBeta` — Baumgarte positional recovery fraction.
double rt_world3d_get_contact_beta(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? ph3d_clamp_unit_finite(w->contact_beta, PH3D_DEFAULT_CONTACT_BETA) : 0.0;
}

/// @brief `World3D.ContactBeta` setter — clamp positional recovery to [0, 1].
void rt_world3d_set_contact_beta(void *obj, double beta) {
    rt_world3d *w = world3d_checked(obj);
    if (w)
        w->contact_beta = ph3d_clamp_unit_finite(beta, 0.0);
}

/// @brief `World3D.RestitutionThreshold` — minimum approach speed that bounces.
double rt_world3d_get_restitution_threshold(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? ph3d_clamp_nonnegative_finite(w->restitution_threshold,
                                             PH3D_DEFAULT_RESTITUTION_THRESHOLD)
             : 0.0;
}

/// @brief `World3D.RestitutionThreshold` setter — clamp to a finite non-negative speed.
void rt_world3d_set_restitution_threshold(void *obj, double threshold) {
    rt_world3d *w = world3d_checked(obj);
    if (w)
        w->restitution_threshold = ph3d_clamp_nonnegative_finite(threshold, 0.0);
}

/// @brief `World3D.LastSolverIslandCount` — max active contact islands in the last Step.
int64_t rt_world3d_get_last_solver_island_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return (w && w->last_solver_island_count > 0) ? w->last_solver_island_count : 0;
}

/// @brief `World3D.LastSolverActiveBodyCount` — max awake bodies scheduled in contact islands.
int64_t rt_world3d_get_last_solver_active_body_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    if (!w || w->last_solver_active_body_count <= 0)
        return 0;
    int32_t body_count = world3d_body_count_safe(w);
    return w->last_solver_active_body_count < body_count ? w->last_solver_active_body_count
                                                         : body_count;
}

/// @brief `World3D.LastSolverContactCount` — max contacts scheduled through islands.
int64_t rt_world3d_get_last_solver_contact_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    if (!w || w->last_solver_contact_count <= 0)
        return 0;
    int32_t contact_count =
        world3d_contact_count_safe(w->contacts, w->contact_count, w->contact_capacity);
    return w->last_solver_contact_count < contact_count ? w->last_solver_contact_count
                                                        : contact_count;
}

/// @brief `World3D.Add(body)` — register a body.
///
/// Retains the body and stores it in the growable bodies array.
int8_t rt_world3d_try_add(void *obj, void *body) {
    rt_world3d *w = world3d_checked(obj);
    rt_body3d *b = body3d_checked(body);
    if (!w || !b)
        return 0;
    if (world3d_body_is_registered(w, b))
        return 1;
    if (b->owner_world && b->owner_world != w) {
        rt_trap("Physics3D.World.Add: body already belongs to another world");
        return 0;
    }
    if (!world3d_reserve_body_capacity(w, w->body_count + 1)) {
        rt_trap("Physics3D: body storage allocation failed");
        return 0;
    }
    rt_obj_retain_maybe(body);
    b->owner_world = w;
    b->owner_index = w->body_count;
    w->bodies[w->body_count++] = b;
    world3d_invalidate_query_broadphase(w);
    return 1;
}

/// @brief Add a body to the world, ignoring the success flag (see rt_world3d_try_add).
void rt_world3d_add(void *obj, void *body) {
    (void)rt_world3d_try_add(obj, body);
}

/// @brief Whether a contact references @p body as either participant.
static int contact_mentions_body(const rt_contact3d *contact, const rt_body3d *body) {
    return contact && body && (contact->body_a == body || contact->body_b == body);
}

/// @brief Remove every contact mentioning @p body from a contact array, compacting it in place.
static void world3d_purge_contact_array_for_body(rt_contact3d *contacts,
                                                 int32_t *count,
                                                 const rt_body3d *body) {
    int32_t write = 0;
    if (!contacts || !count || !body)
        return;
    for (int32_t read = 0; read < *count; ++read) {
        if (contact_mentions_body(&contacts[read], body))
            continue;
        if (write != read)
            contacts[write] = contacts[read];
        write++;
    }
    for (int32_t i = write; i < *count; ++i)
        memset(&contacts[i], 0, sizeof(contacts[i]));
    *count = write;
}

/// @brief Purge all of the world's contact/event arrays of contacts involving @p body.
/// @details Called when a body leaves the world so stale contacts don't fire spurious events.
static void world3d_purge_body_contacts(rt_world3d *w, const rt_body3d *body) {
    if (!w || !body)
        return;
    world3d_release_cached_event_objects(w);
    world3d_purge_contact_array_for_body(w->contacts, &w->contact_count, body);
    world3d_purge_contact_array_for_body(w->frame_contacts, &w->frame_contact_count, body);
    world3d_purge_contact_array_for_body(w->previous_contacts, &w->previous_contact_count, body);
    world3d_purge_contact_array_for_body(w->enter_events, &w->enter_event_count, body);
    world3d_purge_contact_array_for_body(w->stay_events, &w->stay_event_count, body);
    world3d_purge_contact_array_for_body(w->exit_events, &w->exit_event_count, body);
}

/// @brief `World3D.Remove(body)` — unregister a body.
///
/// Same swap-with-last compaction as `RemoveJoint`. Releases the world's
/// reference, which may free the body if no other holder remains.
void rt_world3d_remove(void *obj, void *body) {
    rt_world3d *w = world3d_checked(obj);
    rt_body3d *b = body3d_checked(body);
    if (!w || !b)
        return;
    int32_t body_count = world3d_body_count_safe(w);
    w->body_count = body_count;
    for (int32_t i = 0; i < body_count; i++) {
        if (w->bodies[i] == b) {
            rt_body3d *removed = w->bodies[i];
            world3d_purge_body_contacts(w, b);
            world3d_purge_joints_for_body(w, b);
            if (removed && removed->owner_world == w) {
                removed->owner_world = NULL;
                removed->owner_index = -1;
            }
            w->body_count--;
            w->bodies[i] = w->bodies[w->body_count];
            if (w->bodies[i] && w->bodies[i]->owner_world == w)
                w->bodies[i]->owner_index = i;
            w->bodies[w->body_count] = NULL;
            world3d_invalidate_query_broadphase(w);
            if (removed && rt_obj_release_check0(removed))
                rt_obj_free(removed);
            return;
        }
    }
}

/// @brief `World3D.BodyCount` — number of registered bodies (0 for NULL).
int64_t rt_world3d_body_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return world3d_body_count_safe(w);
}

/// @brief Whether @p body is currently registered in the world.
int8_t rt_world3d_contains_body(void *obj, void *body) {
    rt_world3d *w = world3d_checked(obj);
    rt_body3d *b = body3d_checked(body);
    if (!w || !b)
        return 0;
    return world3d_body_is_registered(w, b) ? 1 : 0;
}

/// @brief `World3D.LastCcdRequestedSubsteps` — unclamped substep demand from the last Step.
int64_t rt_world3d_get_last_ccd_requested_substeps(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    if (!w || w->last_ccd_requested_substeps <= 0)
        return 0;
    return w->last_ccd_requested_substeps == INT32_MAX ? PH3D_MAX_CCD_SUBSTEPS
                                                       : w->last_ccd_requested_substeps;
}

/// @brief `World3D.LastCcdSubsteps` — actual CCD substeps used by the last Step.
int64_t rt_world3d_get_last_ccd_substeps(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    if (!w || w->last_ccd_substeps <= 0)
        return 0;
    return w->last_ccd_substeps > PH3D_MAX_CCD_SUBSTEPS ? PH3D_MAX_CCD_SUBSTEPS
                                                        : w->last_ccd_substeps;
}

/// @brief `World3D.CcdSubstepClampedCount` — number of frames that hit the substep cap.
int64_t rt_world3d_get_ccd_substep_clamped_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return (w && w->ccd_substep_clamped_count > 0) ? w->ccd_substep_clamped_count : 0;
}

/// @brief `World3D.LastCcdClampedBodyCount` — bodies capped by CCD in the last Step.
int64_t rt_world3d_get_last_ccd_clamped_body_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return (w && w->last_ccd_clamped_body_count > 0) ? w->last_ccd_clamped_body_count : 0;
}

/// @brief `World3D.CcdSubstepClampedBodyCount` — total bodies affected by CCD caps.
int64_t rt_world3d_get_ccd_substep_clamped_body_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return (w && w->ccd_substep_clamped_body_count > 0) ? w->ccd_substep_clamped_body_count : 0;
}

/// @brief `Physics3DWorld.SetMaxQueryHits(n)` — configure query result capacity.
/// @details Bounds how many hits `RaycastAll`/`OverlapSphere`/`OverlapAABB`
///          return (lists still report `TotalCount`/`Truncated`). Clamped to
///          16–4096; reallocates the world's scratch buffer on next use.
void rt_world3d_set_max_query_hits(void *obj, int64_t max_hits) {
    rt_world3d *w = world3d_checked(obj);
    if (!w)
        return;
    if (max_hits < PH3D_QUERY_HITS_MIN)
        max_hits = PH3D_QUERY_HITS_MIN;
    if (max_hits > PH3D_QUERY_HITS_MAX)
        max_hits = PH3D_QUERY_HITS_MAX;
    if ((int32_t)max_hits == w->max_query_hits)
        return;
    free(w->query_hits_scratch);
    w->query_hits_scratch = NULL;
    w->max_query_hits = (int32_t)max_hits;
}

/// @brief `Physics3DWorld.MaxQueryHits` — current query result capacity.
int64_t rt_world3d_get_max_query_hits(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return w ? w->max_query_hits : 0;
}

/// @brief `World3D.CcdToiCount` — total swept time-of-impact clips applied.
/// @details Each count is one CCD body whose substep translation was clipped
///          at a static/kinematic surface by the swept pass (i.e. a tunnel
///          that substep subdivision alone would not have prevented exactly).
int64_t rt_world3d_get_ccd_toi_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return (w && w->ccd_toi_count > 0) ? w->ccd_toi_count : 0;
}

/// @brief `World3D.BroadphaseFallbackCount` — brute-force broadphase fallback count.
int64_t rt_world3d_get_broadphase_fallback_count(void *obj) {
    rt_world3d *w = world3d_checked(obj);
    return (w && w->broadphase_fallback_count > 0) ? w->broadphase_fallback_count : 0;
}

/// @brief `World3D.SetGravity(gx, gy, gz)` — change the world gravity vector.
///
/// Takes effect on the next `Step`. Existing in-flight velocities are
/// not modified.
void rt_world3d_set_gravity(void *obj, double gx, double gy, double gz) {
    rt_world3d *w = world3d_checked(obj);
    if (!w)
        return;
    ph3d_vec3_set_finite(w->gravity, gx, gy, gz);
    ph3d_vec3_sanitize_state(w->gravity);
}

/// @brief Shift a contact's cached points by the floating-origin rebase @p delta.
/// @details When the world recenters to keep coordinates near the origin, contact points must move
///          with it; saturates the shifted values to stay finite.
static void contact3d_rebase_origin(rt_contact3d *contact, const double delta[3]) {
    if (!contact || !delta)
        return;
    contact->point[0] = ph3d_saturate_state_value(contact->point[0] - delta[0]);
    contact->point[1] = ph3d_saturate_state_value(contact->point[1] - delta[1]);
    contact->point[2] = ph3d_saturate_state_value(contact->point[2] - delta[2]);
    int32_t point_count = contact->contact_count;
    if (point_count < 0)
        point_count = 0;
    if (point_count > PH3D_MAX_MANIFOLD_POINTS)
        point_count = PH3D_MAX_MANIFOLD_POINTS;
    for (int32_t i = 0; i < point_count; ++i) {
        contact->points[i][0] = ph3d_saturate_state_value(contact->points[i][0] - delta[0]);
        contact->points[i][1] = ph3d_saturate_state_value(contact->points[i][1] - delta[1]);
        contact->points[i][2] = ph3d_saturate_state_value(contact->points[i][2] - delta[2]);
    }
}

/// @brief Apply a floating-origin rebase @p delta to every contact in an array.
static void world3d_rebase_contact_array(rt_contact3d *contacts,
                                         int32_t count,
                                         const double delta[3]) {
    if (!contacts || count <= 0 || !delta)
        return;
    for (int32_t i = 0; i < count; ++i)
        contact3d_rebase_origin(&contacts[i], delta);
}

/// @brief `World3D.RebaseOrigin(dx, dy, dz)` — shift body/contact/query state by -delta.
void rt_world3d_rebase_origin(void *obj, double dx, double dy, double dz) {
    rt_world3d *w = world3d_checked(obj);
    if (!w)
        return;
    double delta[3] = {
        ph3d_saturate_state_value(dx),
        ph3d_saturate_state_value(dy),
        ph3d_saturate_state_value(dz),
    };
    if (delta[0] == 0.0 && delta[1] == 0.0 && delta[2] == 0.0)
        return;

    for (int32_t i = 0; i < w->body_count; ++i) {
        rt_body3d *body = w->bodies[i];
        if (!body)
            continue;
        body->position[0] = ph3d_saturate_state_value(body->position[0] - delta[0]);
        body->position[1] = ph3d_saturate_state_value(body->position[1] - delta[1]);
        body->position[2] = ph3d_saturate_state_value(body->position[2] - delta[2]);
        body3d_touch_broadphase(body);
    }

    world3d_rebase_contact_array(w->contacts, w->contact_count, delta);
    world3d_rebase_contact_array(w->frame_contacts, w->frame_contact_count, delta);
    world3d_rebase_contact_array(w->previous_contacts, w->previous_contact_count, delta);
    world3d_rebase_contact_array(w->enter_events, w->enter_event_count, delta);
    world3d_rebase_contact_array(w->stay_events, w->stay_event_count, delta);
    world3d_rebase_contact_array(w->exit_events, w->exit_event_count, delta);
    world3d_invalidate_query_broadphase(w);
    world3d_release_cached_event_objects(w);
}
