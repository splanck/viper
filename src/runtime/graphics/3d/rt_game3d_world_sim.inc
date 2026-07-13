//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/rt_game3d_world_sim.inc  (#included from rt_game3d.c; not a
// standalone translation unit) Purpose: Game3D environment/debug helpers, per-frame
// controller/animation update, entity spawn/despawn
//===----------------------------------------------------------------------===//
/// @brief GC finalizer for an EnvHandle: release its water, terrain, and world refs.
static void game3d_env_handle_finalize(void *obj) {
    rt_game3d_env_handle *env = (rt_game3d_env_handle *)obj;
    if (!env)
        return;
    game3d_release_ref(&env->water_entity);
    game3d_release_ref(&env->terrain_entity);
    game3d_release_ref(&env->world);
}

/// @brief Allocate an EnvHandle bound to `world`; traps on OOM.
static void *game3d_env_handle_new(rt_game3d_world *world) {
    rt_game3d_env_handle *env = (rt_game3d_env_handle *)rt_obj_new_i64(
        RT_G3D_GAME3D_ENV_HANDLE_CLASS_ID, (int64_t)sizeof(*env));
    if (!env) {
        rt_trap("Game3D.Environment: allocation failed");
        return NULL;
    }
    memset(env, 0, sizeof(*env));
    rt_obj_set_finalizer(env, game3d_env_handle_finalize);
    game3d_assign_ref(&env->world, world);
    return env;
}

/// @brief Resolve and validate the world an EnvHandle targets; traps `method` if absent.
static rt_game3d_world *game3d_env_world(rt_game3d_env_handle *env, const char *method) {
    if (!env || !env->world) {
        rt_trap(method);
        return NULL;
    }
    return game3d_world_checked(env->world, method);
}

/// @brief Fluent: spawn a ground-collider terrain entity (replacing any prior one) into
///   the environment's world and return the handle. See header.
void *rt_game3d_env_handle_with_terrain(void *obj, double size, double height) {
    rt_game3d_env_handle *env =
        game3d_env_handle_checked(obj, "Game3D.EnvHandle.withTerrain: invalid environment");
    rt_game3d_world *world = game3d_env_world(env, "Game3D.EnvHandle.withTerrain: invalid world");
    if (!env || !world)
        return obj;
    if (env->terrain_entity) {
        rt_game3d_entity *old = (rt_game3d_entity *)env->terrain_entity;
        if (old->alive && old->spawned && old->world == world)
            rt_game3d_world_despawn(world, env->terrain_entity);
        game3d_release_ref(&env->terrain_entity);
    }

    double s = game3d_positive_or(size, 80.0);
    double y = game3d_finite_or(height, 0.0);
    void *mat = rt_game3d_materials_rubber(0.30, 0.45, 0.24);
    void *terrain = rt_game3d_prefab_ground(s, mat);
    if (terrain) {
        rt_string name = rt_const_cstr("Environment Terrain");
        rt_game3d_entity_set_name(terrain, name);
        rt_string_unref(name);
        rt_game3d_entity_set_position(terrain, 0.0, y, 0.0);
        void *body = rt_body3d_new_aabb(s * 0.5, 0.001, s * 0.5, 0.0);
        rt_game3d_entity_attach_body(terrain, body);
        rt_game3d_world_spawn(world, terrain);
        game3d_assign_ref(&env->terrain_entity, terrain);
        env->terrain_size = s;
        env->has_terrain_size = 1;
        game3d_release_ref(&body);
    }
    game3d_release_ref(&terrain);
    game3d_release_ref(&mat);
    return obj;
}

/// @brief Fluent: spawn a translucent water plane at `level` (replacing any prior one)
///   and return the handle. See header.
void *rt_game3d_env_handle_with_water(void *obj, double level) {
    rt_game3d_env_handle *env =
        game3d_env_handle_checked(obj, "Game3D.EnvHandle.withWater: invalid environment");
    rt_game3d_world *world = game3d_env_world(env, "Game3D.EnvHandle.withWater: invalid world");
    if (!env || !world)
        return obj;
    if (env->water_entity) {
        rt_game3d_entity *old = (rt_game3d_entity *)env->water_entity;
        if (old->alive && old->spawned && old->world == world)
            rt_game3d_world_despawn(world, env->water_entity);
        game3d_release_ref(&env->water_entity);
    }

    double y = game3d_finite_or(level, 0.0);
    double water_size = env->has_terrain_size ? env->terrain_size : 80.0;
    void *mat = rt_game3d_materials_glass(0.18, 0.42, 0.62, 0.48);
    void *water = rt_game3d_prefab_plane(water_size, water_size, mat);
    if (water) {
        rt_string name = rt_const_cstr("Environment Water");
        rt_game3d_entity_set_name(water, name);
        rt_string_unref(name);
        rt_game3d_entity_set_position(water, 0.0, y, 0.0);
        rt_game3d_world_spawn(world, water);
        game3d_assign_ref(&env->water_entity, water);
    }
    game3d_release_ref(&water);
    game3d_release_ref(&mat);
    return obj;
}

/// @brief Fluent: enable distance fog between near/far planes using the world's clear
///   color and return the handle. See header.
void *rt_game3d_env_handle_with_fog(void *obj, double near_plane, double far_plane) {
    rt_game3d_env_handle *env =
        game3d_env_handle_checked(obj, "Game3D.EnvHandle.withFog: invalid environment");
    rt_game3d_world *world = game3d_env_world(env, "Game3D.EnvHandle.withFog: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_fog(world->canvas,
                            game3d_nonnegative_or(near_plane, 18.0),
                            game3d_positive_or(far_plane, 120.0),
                            world->clear_r,
                            world->clear_g,
                            world->clear_b);
    return obj;
}

/// @brief Fluent: enable exponential height fog on the environment's canvas.
void *rt_game3d_env_handle_with_height_fog(void *obj,
                                           double density,
                                           double height,
                                           double falloff) {
    rt_game3d_env_handle *env =
        game3d_env_handle_checked(obj, "Game3D.EnvHandle.withHeightFog: invalid environment");
    rt_game3d_world *world = game3d_env_world(env, "Game3D.EnvHandle.withHeightFog: invalid world");
    if (world && world->canvas)
        rt_canvas3d_set_height_fog(world->canvas,
                                   game3d_finite_or(height, 0.0),
                                   game3d_positive_or(falloff, 0.1),
                                   game3d_nonnegative_or(density, 0.02),
                                   1.0);
    return obj;
}

/// @brief Build a bright daytime environment (sun lighting, terrain, fog) and return
///   its handle. See header.
void *rt_game3d_environment_outdoor(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Environment.Outdoor: invalid world");
    void *env = game3d_env_handle_new(world);
    void *sun = rt_vec3_new(-0.45, -1.00, -0.22);
    rt_game3d_lighting_outdoor(world, sun);
    rt_game3d_env_handle_with_terrain(env, 96.0, 0.0);
    rt_game3d_env_handle_with_fog(env, 45.0, 220.0);
    game3d_release_ref(&sun);
    return env;
}

/// @brief Build a warm low-sun sunset environment and return its handle. See header.
void *rt_game3d_environment_sunset(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Environment.Sunset: invalid world");
    void *env = game3d_env_handle_new(world);
    void *sun = rt_vec3_new(-0.75, -0.45, -0.20);
    rt_game3d_lighting_outdoor(world, sun);
    if (world && world->canvas) {
        rt_canvas3d_set_ambient(world->canvas, 0.42, 0.30, 0.24);
        game3d_world_set_clear_color(world, 0.90, 0.48, 0.30);
    }
    rt_game3d_env_handle_with_terrain(env, 96.0, 0.0);
    rt_game3d_env_handle_with_fog(env, 28.0, 160.0);
    game3d_release_ref(&sun);
    return env;
}

/// @brief Build a flat, diffuse overcast environment and return its handle. See header.
void *rt_game3d_environment_overcast(void *obj) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.Environment.Overcast: invalid world");
    void *env = game3d_env_handle_new(world);
    rt_game3d_lighting_clear(world);
    if (world && world->canvas) {
        rt_canvas3d_set_ambient(world->canvas, 0.48, 0.50, 0.52);
        game3d_world_set_clear_color(world, 0.56, 0.60, 0.62);
    }
    void *dir = rt_vec3_new(-0.20, -1.0, 0.10);
    void *sun = rt_light3d_new_directional(dir, 0.74, 0.78, 0.82);
    if (sun)
        rt_light3d_set_intensity(sun, 0.70);
    game3d_world_install_light(world, 0, sun);
    rt_game3d_env_handle_with_terrain(env, 96.0, 0.0);
    rt_game3d_env_handle_with_fog(env, 20.0, 110.0);
    game3d_release_ref(&sun);
    game3d_release_ref(&dir);
    return env;
}

/// @brief Build a dark night environment (moonlight, terrain, fog) and return its
///   handle. See header.
void *rt_game3d_environment_night(void *obj) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Environment.Night: invalid world");
    void *env = game3d_env_handle_new(world);
    rt_game3d_lighting_night(world);
    rt_game3d_env_handle_with_terrain(env, 96.0, 0.0);
    rt_game3d_env_handle_with_fog(env, 20.0, 95.0);
    return env;
}

/// @brief Toggle the on-screen debug stats overlay. See header.
void rt_game3d_debug_show_overlay(void *obj, int8_t enabled) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Debug3D.ShowOverlay: invalid world");
    if (world)
        world->debug_overlay_enabled = enabled ? 1 : 0;
}

/// @brief Enable an axis gizmo at the given Vec3 origin and size; traps on a non-Vec3
///   origin. See header.
void rt_game3d_debug_draw_axes(void *obj, void *origin, double size) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Debug3D.DrawAxes: invalid world");
    if (!origin || !rt_g3d_is_vec3(origin)) {
        rt_trap("Game3D.Debug3D.DrawAxes: origin must be Vec3");
        return;
    }
    if (world) {
        game3d_assign_ref(&world->debug_axis_origin, origin);
        world->debug_axis_size = game3d_positive_or(size, 1.0);
        world->debug_axes_enabled = 1;
    }
}

/// @brief Toggle physics collider/contact debug drawing. See header.
void rt_game3d_debug_draw_physics(void *obj, int8_t enabled) {
    rt_game3d_world *world = game3d_world_checked(obj, "Game3D.Debug3D.DrawPhysics: invalid world");
    if (world)
        world->debug_physics_enabled = enabled ? 1 : 0;
}

/// @brief Toggle the on-screen camera info readout. See header.
void rt_game3d_debug_draw_camera_info(void *obj, int8_t enabled) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.Debug3D.DrawCameraInfo: invalid world");
    if (world)
        world->debug_camera_enabled = enabled ? 1 : 0;
}

/// @brief Toggle the backend-capabilities readout overlay. See header.
void rt_game3d_debug_draw_capabilities(void *obj, int8_t enabled) {
    rt_game3d_world *world =
        game3d_world_checked(obj, "Game3D.Debug3D.DrawCapabilities: invalid world");
    if (world)
        world->debug_caps_enabled = enabled ? 1 : 0;
}

/// @brief GC finalizer for a character controller: release its entity and character.

/// @brief Dispatch the per-frame update to the world's installed camera controller by
///   class id; traps if the controller is not a Game3D camera controller.
static void game3d_world_update_controller(rt_game3d_world *world, double dt) {
    if (!world || !world->camera_controller)
        return;
    switch (rt_obj_class_id(world->camera_controller)) {
        case RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID:
            rt_game3d_first_person_controller_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_FREEFLY_CLASS_ID:
            rt_game3d_free_fly_controller_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_ORBIT_CLASS_ID:
            rt_game3d_orbit_controller_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_FOLLOW_CLASS_ID:
            rt_game3d_follow_controller_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_THIRDPERSON_CLASS_ID:
            rt_game3d_thirdperson_controller_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_RAILCAMERA_CLASS_ID:
            rt_game3d_rail_camera_update(world->camera_controller, world, dt);
            break;
        default:
            rt_trap("Game3D.World3D: camera controller must be a Game3D camera controller");
            return;
    }
}

/// @brief Advance every live, non-destroyed entity's animator by `dt`.
typedef struct {
    void **animators;
    int32_t start;
    int32_t end;
    double dt;
} rt_game3d_animation_job;

/// @brief Worker-job entry point: advance a contiguous slice of animators by the frame delta.
static void game3d_animation_job_run(void *arg) {
    rt_game3d_animation_job *job = (rt_game3d_animation_job *)arg;
    if (!job || !job->animators)
        return;
    for (int32_t i = job->start; i < job->end; ++i) {
        void *anim = job->animators[i];
        if (anim)
            rt_game3d_animator_update(anim, job->dt);
    }
}

/// @brief Choose how many parallel jobs to split an animator update across, given the worker count.
static int32_t game3d_animation_job_count(int64_t worker_count, int32_t animator_count) {
    if (worker_count <= 1 || animator_count <= 1)
        return 1;
    int64_t target = worker_count > INT64_MAX / 4 ? (int64_t)animator_count : worker_count * 4;
    if (target < 1)
        target = 1;
    if (target > animator_count)
        target = animator_count;
    return target > INT32_MAX ? INT32_MAX : (int32_t)target;
}

/// @brief Update a list of animators sequentially (the single-threaded fallback path).
static void game3d_update_animator_list_serial(void **animators, int32_t count, double dt) {
    if (!animators || count <= 0)
        return;
    for (int32_t i = 0; i < count; ++i) {
        if (animators[i])
            rt_game3d_animator_update(animators[i], dt);
    }
}

/// @brief Ensure the world's reusable animation animator list can hold @p needed entries.
///
/// @details The animation phase runs every fixed step, so retaining this pointer array avoids
///          allocating one `entity_count`-sized block per tick.
static int game3d_world_ensure_animation_animators(rt_game3d_world *world, int32_t needed) {
    void **grown;
    int32_t new_capacity;
    if (!world || needed < 0)
        return 0;
    if (world->animation_animator_capacity >= needed)
        return 1;
    if (!game3d_compute_capacity(
            world->animation_animator_capacity, needed, 16, sizeof(void *), &new_capacity))
        return 0;
    grown = (void **)realloc(world->animation_animators, (size_t)new_capacity * sizeof(*grown));
    if (!grown)
        return 0;
    world->animation_animators = grown;
    world->animation_animator_capacity = new_capacity;
    return 1;
}

/// @brief Ensure and clear the world's reusable animation dedupe pointer set.
///
/// @details The set uses open addressing and is cleared with memset every tick. Retaining the
///          allocation avoids calloc/free churn while preserving O(n) duplicate detection.
static void **game3d_world_prepare_animation_seen_set(rt_game3d_world *world, size_t capacity) {
    void **grown;
    if (!world || capacity == 0 || capacity > SIZE_MAX / sizeof(void *))
        return NULL;
    if (world->animation_seen_capacity < capacity) {
        grown = (void **)realloc(world->animation_seen_set, capacity * sizeof(*grown));
        if (!grown)
            return NULL;
        world->animation_seen_set = grown;
        world->animation_seen_capacity = capacity;
    }
    memset(world->animation_seen_set, 0, capacity * sizeof(void *));
    return world->animation_seen_set;
}

/// @brief Ensure the reusable parallel-animation task descriptor array has @p needed slots.
///
/// @details Task descriptors are small but allocated from the fixed-step hot path; reusing them
///          keeps parallel animator updates allocation-free after the first growth.
static rt_game3d_animation_job *game3d_world_prepare_animation_jobs(rt_game3d_world *world,
                                                                    int32_t needed) {
    rt_game3d_animation_job *grown;
    int32_t new_capacity;
    if (!world || needed <= 0)
        return NULL;
    if (world->animation_job_capacity < needed) {
        if (!game3d_compute_capacity(world->animation_job_capacity,
                                     needed,
                                     4,
                                     sizeof(rt_game3d_animation_job),
                                     &new_capacity))
            return NULL;
        grown = (rt_game3d_animation_job *)realloc(world->animation_jobs,
                                                   (size_t)new_capacity * sizeof(*grown));
        if (!grown)
            return NULL;
        world->animation_jobs = grown;
        world->animation_job_capacity = new_capacity;
    }
    memset(world->animation_jobs, 0, (size_t)needed * sizeof(rt_game3d_animation_job));
    return (rt_game3d_animation_job *)world->animation_jobs;
}

/// @brief Advance all of the world's animators by @p dt, parallelizing across workers when
/// worthwhile.
static void game3d_world_update_animations(rt_game3d_world *world, double dt) {
    if (!world)
        return;
    int32_t entity_count = game3d_world_safe_entity_count(world);
    int32_t animator_count = 0;
    void **animators;
    if (entity_count <= 0)
        return;
    if (!game3d_world_ensure_animation_animators(world, entity_count))
        return;
    animators = world->animation_animators;
    /* Dedup animators in O(n) via a local open-addressing pointer set rather than the
     * prior O(n^2) inner scan. Dedup is mandatory: the parallel path below partitions
     * `animators` across workers, so a duplicate would let two threads update the same
     * animator concurrently. Capacity >= 2*entity_count keeps a free slot for probing;
     * a calloc failure falls back to the linear scan. */
    size_t seen_cap = 1;
    void **seen;
    while (seen_cap < (size_t)entity_count * 2u && seen_cap < SIZE_MAX / 2u)
        seen_cap <<= 1;
    seen = game3d_world_prepare_animation_seen_set(world, seen_cap);
    for (int32_t i = 0; i < entity_count; ++i) {
        rt_game3d_entity *entity = world->entities[i];
        void *anim = game3d_entity_anim_ref(entity);
        int duplicate = 0;
        if (!entity || !entity->alive || entity->destroyed || !anim ||
            !rt_game3d_animator_needs_game_update(anim))
            continue;
        if (seen) {
            size_t mask = seen_cap - 1;
            size_t slot = (((uintptr_t)anim) >> 4) & mask;
            while (seen[slot]) {
                if (seen[slot] == anim) {
                    duplicate = 1;
                    break;
                }
                slot = (slot + 1) & mask;
            }
            if (!duplicate)
                seen[slot] = anim;
        } else {
            for (int32_t j = 0; j < animator_count; ++j) {
                if (animators[j] == anim) {
                    duplicate = 1;
                    break;
                }
            }
        }
        if (!duplicate)
            animators[animator_count++] = anim;
    }
    if (animator_count <= 0)
        return;
    if (!RT_GAME3D_PARALLEL_ANIMATOR_UPDATES || !world->jobs_enabled || world->worker_count <= 1 ||
        animator_count <= 1 || !game3d_world_ensure_job_pool(world)) {
        game3d_update_animator_list_serial(animators, animator_count, dt);
        return;
    }

    int32_t task_count = game3d_animation_job_count(world->worker_count, animator_count);
    rt_game3d_animation_job *tasks = game3d_world_prepare_animation_jobs(world, task_count);
    if (!tasks || task_count <= 1) {
        game3d_update_animator_list_serial(animators, animator_count, dt);
        return;
    }

    int submit_failed = 0;
    for (int32_t i = 0; i < task_count; ++i) {
        int32_t start = (int32_t)(((int64_t)i * animator_count) / task_count);
        int32_t end = (int32_t)(((int64_t)(i + 1) * animator_count) / task_count);
        tasks[i].animators = animators;
        tasks[i].start = start;
        tasks[i].end = end;
        tasks[i].dt = dt;
        if (!rt_threadpool_submit(
                world->job_pool, game3d_task_fnptr(game3d_animation_job_run), &tasks[i])) {
            submit_failed = 1;
            game3d_animation_job_run(&tasks[i]);
        }
    }
    rt_threadpool_wait(world->job_pool);
    if (submit_failed)
        world->jobs_enabled = 0;
}

/// @brief Dispatch the post-physics late update to the world's camera controller by
///   class id; traps on an unrecognized controller class.
static void game3d_world_late_update_controller(rt_game3d_world *world, double dt) {
    if (!world || !world->camera_controller)
        return;
    switch (rt_obj_class_id(world->camera_controller)) {
        case RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID:
            rt_game3d_first_person_controller_late_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_FREEFLY_CLASS_ID:
            rt_game3d_free_fly_controller_late_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_ORBIT_CLASS_ID:
            rt_game3d_orbit_controller_late_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_FOLLOW_CLASS_ID:
            rt_game3d_follow_controller_late_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_THIRDPERSON_CLASS_ID:
            rt_game3d_thirdperson_controller_late_update(world->camera_controller, world, dt);
            break;
        case RT_G3D_GAME3D_RAILCAMERA_CLASS_ID:
            rt_game3d_rail_camera_late_update(world->camera_controller, world, dt);
            break;
        default:
            rt_trap("Game3D.World3D: camera controller must be a Game3D camera controller");
            return;
    }
}

/// @brief Ensure the world's entity registry can hold `need` entries, doubling capacity;
///   returns 0 on allocation failure.
static int game3d_world_grow_entities(rt_game3d_world *world, int32_t need) {
    int32_t new_cap;
    if (!world)
        return 0;
    if (world->entity_capacity >= need)
        return 1;
    if (!game3d_compute_capacity(
            world->entity_capacity, need, 16, sizeof(rt_game3d_entity *), &new_cap))
        return 0;
    rt_game3d_entity **grown =
        (rt_game3d_entity **)realloc(world->entities, (size_t)new_cap * sizeof(*grown));
    if (!grown)
        return 0;
    world->entities = grown;
    world->entity_capacity = new_cap;
    return 1;
}

/// @brief Linear-search the registry for `entity`; returns its index or -1.
static int32_t game3d_world_find_entity_index(rt_game3d_world *world, rt_game3d_entity *entity) {
    int32_t indexed_slot;
    if (!world || !entity)
        return -1;
    int32_t entity_count = game3d_world_safe_entity_count(world);
    indexed_slot = entity->registry_index;
    if (indexed_slot >= 0 && indexed_slot < entity_count && world->entities[indexed_slot] == entity)
        return indexed_slot;
    for (int32_t i = 0; i < entity_count; ++i) {
        if (world->entities[i] == entity) {
            entity->registry_index = i;
            return i;
        }
    }
    return -1;
}

#include "rt_game3d_indices.inc"

/// @brief Find the spawned entity whose physics body is `body`; NULL if none. Used to
///   map collision events back to entities.
rt_game3d_entity *game3d_world_find_entity_by_body(rt_game3d_world *world, void *body) {
    int found = 0;
    int slot;
    if (!world || !body)
        return NULL;
    if (!game3d_world_body_index_ensure_usable(world))
        return NULL;
    slot = game3d_world_body_index_find_slot(
        world->body_index_entries, world->body_index_capacity, body, &found);
    if (!found || slot < 0)
        return NULL;
    rt_game3d_entity *entity = world->body_index_entries[slot].entity;
    if (!entity || !entity->alive || !entity->spawned || entity->world != world ||
        (world->physics && !rt_world3d_contains_body(world->physics, body))) {
        game3d_world_body_index_remove(world, body);
        return NULL;
    }
    void *entity_body = game3d_entity_body_ref(entity);
    if (entity_body != body) {
        game3d_world_body_index_remove(world, body);
        return NULL;
    }
    return entity;
}

/// @brief Remove `entity` from the registry without rebuilding the name index.
/// @details Used by tree operations that rebuild once after a batch.
static void game3d_world_registry_remove_no_rebuild(rt_game3d_world *world,
                                                    rt_game3d_entity *entity) {
    int32_t index = game3d_world_find_entity_index(world, entity);
    if (index < 0)
        return;
    int32_t entity_count = game3d_world_repair_entity_count(world);
    void *body = game3d_entity_body_ref(entity);
    if (body && game3d_world_find_entity_by_body(world, body) == entity)
        game3d_world_body_index_remove(world, body);
    rt_game3d_entity *owned = world->entities[index];
    int32_t last_index = entity_count - 1;
    rt_game3d_entity *moved = world->entities[last_index];
    if (index != last_index) {
        world->entities[index] = moved;
        if (moved)
            moved->registry_index = index;
    }
    world->entity_count = last_index;
    world->entities[last_index] = NULL;
    if (owned)
        owned->registry_index = -1;
    game3d_release_ref((void **)&owned);
    world->name_index_valid = 0;
}

/// @brief Detach an entity's node from its parent, or from the scene root if it has no
///   parent.
static void game3d_entity_detach_node(rt_game3d_world *world, rt_game3d_entity *entity) {
    void *node = game3d_entity_node_ref(entity);
    if (!world || !node)
        return;
    void *parent = rt_scene_node3d_get_parent(node);
    if (parent)
        rt_scene_node3d_remove_child(parent, node);
    else if (world->scene)
        rt_scene3d_remove(world->scene, node);
}

typedef struct game3d_entity_tree_item {
    rt_game3d_entity *entity;
    int8_t root_link;
    int8_t visited;
} game3d_entity_tree_item;

typedef struct game3d_spawned_entity_list {
    rt_game3d_entity **items;
    int32_t count;
    int32_t capacity;
} game3d_spawned_entity_list;

/// @brief Push an entity-tree traversal item onto the work list, growing it as needed.
static int game3d_entity_tree_push(game3d_entity_tree_item **items,
                                   int32_t *count,
                                   int32_t *capacity,
                                   rt_game3d_entity *entity,
                                   int root_link,
                                   int visited) {
    game3d_entity_tree_item *grown;
    int32_t new_capacity;
    if (!items || !count || !capacity)
        return 0;
    if (*count >= *capacity) {
        if (!game3d_compute_capacity(*capacity, *count + 1, 16, sizeof(**items), &new_capacity))
            return 0;
        grown = (game3d_entity_tree_item *)realloc(*items, (size_t)new_capacity * sizeof(**items));
        if (!grown)
            return 0;
        *items = grown;
        *capacity = new_capacity;
    }
    (*items)[*count].entity = entity;
    (*items)[*count].root_link = root_link ? 1 : 0;
    (*items)[*count].visited = visited ? 1 : 0;
    (*count)++;
    return 1;
}

/// @brief Record one entity spawned during the current tree transaction.
/// @details The list owns only the pointer array; entity lifetimes are held by the world registry.
///          Rollback uses the reverse order so children are removed before parents.
static int game3d_spawned_entity_list_push(game3d_spawned_entity_list *list,
                                           rt_game3d_entity *entity) {
    rt_game3d_entity **grown;
    int32_t new_capacity;
    if (!list || !entity)
        return 0;
    if (list->count >= list->capacity) {
        if (!game3d_compute_capacity(
                list->capacity, list->count + 1, 16, sizeof(*list->items), &new_capacity))
            return 0;
        grown = (rt_game3d_entity **)realloc(list->items, (size_t)new_capacity * sizeof(*grown));
        if (!grown)
            return 0;
        list->items = grown;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = entity;
    return 1;
}

/// @brief Free the pointer storage owned by a spawned-entity rollback list.
static void game3d_spawned_entity_list_free(game3d_spawned_entity_list *list) {
    if (!list)
        return;
    free(list->items);
    memset(list, 0, sizeof(*list));
}

/// @brief Despawn one entity, optionally marking the retained handle destroyed after cleanup.
/// @details Destroy marking happens after physics, scene, and registry removal so cleanup helpers
///          still see a live spawned entity. A temporary retain keeps the handle valid after the
///          registry releases its ownership.
static void game3d_world_despawn_entity_one_internal(rt_game3d_world *world,
                                                     rt_game3d_entity *entity,
                                                     int detach_root,
                                                     int mark_destroyed) {
    if (!world || !entity)
        return;
    if (!entity->spawned || entity->world != world)
        return;
    if (mark_destroyed)
        rt_obj_retain_maybe(entity);
    void *body = game3d_entity_body_ref(entity);
    if (body && world->physics) {
        rt_world3d_remove(world->physics, body);
        game3d_world_body_index_remove(world, body);
    }
    if (detach_root)
        game3d_entity_detach_node(world, entity);
    entity->spawned = 0;
    entity->world = NULL;
    game3d_world_registry_remove_no_rebuild(world, entity);
    if (mark_destroyed) {
        game3d_entity_mark_dead(entity);
        if (rt_obj_release_check0(entity))
            rt_obj_free(entity);
    }
}

/// @brief Despawn a single entity: detach it from physics/scene, unregister it, and release it.
static void game3d_world_despawn_entity_one(rt_game3d_world *world,
                                            rt_game3d_entity *entity,
                                            int detach_root) {
    game3d_world_despawn_entity_one_internal(world, entity, detach_root, 0);
}

/// @brief Roll back entities spawned by a failed tree-spawn transaction.
/// @details Only entities recorded in @p spawned are removed; already-spawned entities in the input
///          tree are left alone. The root scene link is detached only if the recorded entity is the
///          transaction root.
static void game3d_world_rollback_spawned_entities(rt_game3d_world *world,
                                                   game3d_spawned_entity_list *spawned,
                                                   rt_game3d_entity *root,
                                                   int attach_to_scene) {
    if (!world || !spawned)
        return;
    for (int32_t i = spawned->count - 1; i >= 0; --i) {
        rt_game3d_entity *entity = spawned->items[i];
        game3d_world_despawn_entity_one(world, entity, entity == root && attach_to_scene ? 1 : 0);
    }
    game3d_world_rebuild_name_index(world);
}

/// @brief Despawn an entity tree with an explicit heap stack, optionally marking nodes destroyed.
static void game3d_world_despawn_entity_tree_internal(rt_game3d_world *world,
                                                      rt_game3d_entity *entity,
                                                      int detach_root,
                                                      int mark_destroyed) {
    game3d_entity_tree_item *stack = NULL;
    int32_t count = 0;
    int32_t capacity = 0;
    if (!world || !entity)
        return;
    if (!game3d_entity_tree_push(&stack, &count, &capacity, entity, detach_root, 0)) {
        rt_trap("Game3D.World3D.despawn: traversal stack allocation failed");
        return;
    }
    while (count > 0) {
        game3d_entity_tree_item item = stack[--count];
        if (!item.entity)
            continue;
        if (item.visited) {
            game3d_world_despawn_entity_one_internal(
                world, item.entity, item.root_link, mark_destroyed);
            continue;
        }
        if (!game3d_entity_tree_push(&stack, &count, &capacity, item.entity, item.root_link, 1)) {
            free(stack);
            rt_trap("Game3D.World3D.despawn: traversal stack allocation failed");
            return;
        }
        for (int32_t i = game3d_entity_child_count(item.entity) - 1; i >= 0; --i) {
            if (!game3d_entity_tree_push(
                    &stack, &count, &capacity, item.entity->children[i], 0, 0)) {
                free(stack);
                rt_trap("Game3D.World3D.despawn: traversal stack allocation failed");
                return;
            }
        }
    }
    free(stack);
    game3d_world_rebuild_name_index(world);
}

/// @brief Despawn an entity tree with an explicit heap stack, marking handles stale.
static void game3d_world_despawn_entity_tree(rt_game3d_world *world,
                                             rt_game3d_entity *entity,
                                             int detach_root) {
    game3d_world_despawn_entity_tree_internal(world, entity, detach_root, 1);
}

/// @brief Spawn one entity in a tree walk; returns 1 when children should be visited,
///   0 when it was already spawned in this world, and -1 on a trapped failure.
static int game3d_world_spawn_entity_one(rt_game3d_world *world,
                                         rt_game3d_entity *entity,
                                         int attach_to_scene,
                                         int64_t *next_id) {
    if (!world || !entity)
        return 0;
    if (!entity->alive || entity->destroyed) {
        rt_trap("Game3D.World3D.spawn: entity is destroyed");
        return -1;
    }
    if (entity->spawned && entity->world == world)
        return 0;
    if (entity->spawned || (entity->world && entity->world != world)) {
        rt_trap("Game3D.World3D.spawn: entity already belongs to a world");
        return -1;
    }
    game3d_world_repair_entity_count(world);
    if (world->entity_count == INT32_MAX ||
        !game3d_world_grow_entities(world, world->entity_count + 1)) {
        rt_trap("Game3D.World3D.spawn: registry allocation failed");
        return -1;
    }

    entity->spawned = 1;
    entity->world = world;
    rt_obj_retain_maybe(entity);
    entity->registry_index = world->entity_count;
    world->entities[world->entity_count++] = entity;
    if (!game3d_world_name_index_add_entity(world, entity)) {
        entity->spawned = 0;
        entity->world = NULL;
        game3d_world_registry_remove_no_rebuild(world, entity);
        game3d_world_rebuild_name_index(world);
        return -1;
    }

    void *node = game3d_entity_node_ref(entity);
    void *body = game3d_entity_body_ref(entity);
    if (attach_to_scene && world->scene && node && !rt_scene3d_try_add(world->scene, node)) {
        entity->spawned = 0;
        entity->world = NULL;
        game3d_world_registry_remove_no_rebuild(world, entity);
        game3d_world_rebuild_name_index(world);
        rt_trap("Game3D.World3D.spawn: scene attach failed");
        return -1;
    }
    if (body && world->physics) {
        rt_body3d_set_collision_layer(body, entity->layer);
        rt_body3d_set_collision_mask(body, entity->collision_mask_bits);
        rt_game3d_entity *body_owner = game3d_world_find_entity_by_body(world, body);
        if (body_owner && body_owner != entity) {
            if (attach_to_scene && world->scene && node)
                rt_scene3d_remove(world->scene, node);
            entity->spawned = 0;
            entity->world = NULL;
            game3d_world_registry_remove_no_rebuild(world, entity);
            game3d_world_rebuild_name_index(world);
            rt_trap("Game3D.World3D.spawn: body is already attached to another entity");
            return -1;
        }
        if (node) {
            rt_scene_node3d_bind_body(node, body);
            game3d_sync_body_from_entity_node(entity, 1);
        }
        if (!rt_world3d_try_add(world->physics, body)) {
            if (node)
                rt_scene_node3d_clear_body_binding(node);
            if (attach_to_scene && world->scene && node)
                rt_scene3d_remove(world->scene, node);
            entity->spawned = 0;
            entity->world = NULL;
            game3d_world_registry_remove_no_rebuild(world, entity);
            game3d_world_rebuild_name_index(world);
            rt_trap("Game3D.World3D.spawn: physics world add failed");
            return -1;
        }
        if (!game3d_world_body_index_add(world, entity)) {
            rt_world3d_remove(world->physics, body);
            if (node)
                rt_scene_node3d_clear_body_binding(node);
            if (attach_to_scene && world->scene && node)
                rt_scene3d_remove(world->scene, node);
            entity->spawned = 0;
            entity->world = NULL;
            game3d_world_registry_remove_no_rebuild(world, entity);
            game3d_world_rebuild_name_index(world);
            rt_trap("Game3D.World3D.spawn: body index allocation failed");
            return -1;
        }
    }
    if (entity->id == 0 && next_id)
        entity->id = (*next_id)++;
    return 1;
}

/// @brief Iteratively spawn an entity and its children: assign a stable id, add the root
///   node to the scene, register and add bodies to physics, and mark the tree spawned.
/// @details Internal callers pass only live entities; destroyed or cross-world entities trap here.
///   Only the root attaches to the scene (`attach_to_scene`); children remain parented to it.
int game3d_world_spawn_entity_tree(rt_game3d_world *world,
                                   rt_game3d_entity *entity,
                                   int attach_to_scene,
                                   int64_t *next_id) {
    game3d_entity_tree_item *stack = NULL;
    game3d_spawned_entity_list spawned_entities;
    int32_t count = 0;
    int32_t capacity = 0;
    if (!world || !entity)
        return 0;
    memset(&spawned_entities, 0, sizeof(spawned_entities));
    if (!game3d_entity_tree_push(&stack, &count, &capacity, entity, attach_to_scene, 0)) {
        rt_trap("Game3D.World3D.spawn: traversal stack allocation failed");
        return 0;
    }
    while (count > 0) {
        game3d_entity_tree_item item = stack[--count];
        int spawned = game3d_world_spawn_entity_one(world, item.entity, item.root_link, next_id);
        if (spawned < 0) {
            free(stack);
            game3d_world_rollback_spawned_entities(
                world, &spawned_entities, entity, attach_to_scene);
            game3d_spawned_entity_list_free(&spawned_entities);
            return 0;
        }
        if (spawned == 0)
            continue;
        if (!game3d_spawned_entity_list_push(&spawned_entities, item.entity)) {
            game3d_world_despawn_entity_one(world, item.entity, item.root_link);
            free(stack);
            game3d_world_rollback_spawned_entities(
                world, &spawned_entities, entity, attach_to_scene);
            game3d_spawned_entity_list_free(&spawned_entities);
            rt_trap("Game3D.World3D.spawn: rollback list allocation failed");
            return 0;
        }
        for (int32_t i = game3d_entity_child_count(item.entity) - 1; i >= 0; --i) {
            if (!game3d_entity_tree_push(
                    &stack, &count, &capacity, item.entity->children[i], 0, 0)) {
                free(stack);
                game3d_world_rollback_spawned_entities(
                    world, &spawned_entities, entity, attach_to_scene);
                game3d_spawned_entity_list_free(&spawned_entities);
                rt_trap("Game3D.World3D.spawn: traversal stack allocation failed");
                return 0;
            }
        }
    }
    free(stack);
    game3d_spawned_entity_list_free(&spawned_entities);
    return 1;
}

/// @brief Despawn and release every entity (optionally marking them destroyed), then
///   release all owned subsystems. Shared by destroy() and the GC finalizer.
static void game3d_world_release_runtime(rt_game3d_world *world, int mark_entities_destroyed) {
    if (!world)
        return;
    game3d_world_repair_entity_count(world);
    while (world->entity_count > 0) {
        rt_game3d_entity *entity = world->entities[world->entity_count - 1];
        if (entity) {
            game3d_world_despawn_entity_tree_internal(world, entity, 1, mark_entities_destroyed);
        } else {
            world->entity_count--;
        }
    }
    game3d_camera_controller_clear_world_ref_if(world->camera_controller, world);
    game3d_release_ref(&world->camera_controller);
    game3d_release_typed_ref(&world->active_timeline, RT_G3D_GAME3D_TIMELINE_CLASS_ID);
    game3d_release_typed_ref(&world->active_dialogue, RT_G3D_GAME3D_DIALOGUE_CLASS_ID);
    game3d_release_ref(&world->debug_axis_origin);
    if (world->stream) {
        rt_game3d_world_stream *stream = (rt_game3d_world_stream *)rt_g3d_checked_or_null(
            world->stream, RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID);
        if (stream && !stream->retains_world)
            stream->world = NULL;
    }
    game3d_release_ref(&world->stream);
    game3d_release_ref(&world->effects);
    game3d_release_ref(&world->audio);
    game3d_release_ref(&world->input);
    game3d_release_ref(&world->physics);
    game3d_release_ref(&world->scene);
    game3d_release_ref(&world->camera);
    game3d_release_ref(&world->canvas);
    game3d_world_release_job_pool(world);
}

/// @brief Free all of the world's per-subsystem registries (entities, controllers, animators,
/// etc.).
static void game3d_world_free_registries(rt_game3d_world *world) {
    if (!world)
        return;
    game3d_world_clear_combat_events(world);
    game3d_persistence_release(world);
    for (int32_t cl = 0; cl < world->cloth_count; ++cl)
        game3d_release_typed_ref(&world->cloths[cl], RT_G3D_CLOTH3D_CLASS_ID);
    free(world->cloths);
    world->cloths = NULL;
    world->cloth_count = 0;
    world->cloth_capacity = 0;
    free(world->hit_events);
    free(world->damage_events);
    world->hit_events = NULL;
    world->damage_events = NULL;
    world->hit_event_capacity = 0;
    world->damage_event_capacity = 0;
    free(world->combat_scratch);
    world->combat_scratch = NULL;
    free(world->entities);
    free(world->body_index_entries);
    free(world->name_index_hashes);
    free(world->name_index_entities);
    free(world->animation_animators);
    free(world->animation_seen_set);
    free(world->animation_jobs);
    world->entities = NULL;
    world->body_index_entries = NULL;
    world->name_index_hashes = NULL;
    world->name_index_entities = NULL;
    world->animation_animators = NULL;
    world->animation_seen_set = NULL;
    world->animation_jobs = NULL;
    world->entity_count = 0;
    world->entity_capacity = 0;
    world->body_index_count = 0;
    world->body_index_capacity = 0;
    world->name_index_count = 0;
    world->name_index_capacity = 0;
    world->name_index_valid = 0;
    world->animation_animator_capacity = 0;
    world->animation_seen_capacity = 0;
    world->animation_job_capacity = 0;
}

/// @brief GC finalizer for a world: release all runtime state and free the entity array.
static void game3d_world_finalize(void *obj) {
    rt_game3d_world *world = (rt_game3d_world *)obj;
    if (!world)
        return;
    game3d_world_release_runtime(world, 1);
    game3d_world_free_registries(world);
}
