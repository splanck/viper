//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/rt_game3d_asset_load.inc  (#included from rt_game3d.c; not a
// standalone translation unit) Purpose: Game3D async asset loading, model-template instantiation,
// and the public Assets3D API
//===----------------------------------------------------------------------===//
/// @brief Assign a newly-created string into an owned slot, then release the caller's reference.
static void game3d_asset_assign_new_string(rt_string *slot, rt_string value) {
    game3d_assign_ref((void **)slot, value);
    rt_string_unref(value);
}

/// @brief Copy a transient C string into an owned runtime-string slot.
static void game3d_asset_assign_cstr(rt_string *slot, const char *value) {
    game3d_asset_assign_new_string(slot, rt_const_cstr(value ? value : ""));
}

/// @brief Update the cache entry backing @p model_template with a residency priority/distance hint.
/// @details Caller must hold g_game3d_model_cache_lock. Drives victim selection so
/// high-priority/near models stay resident longer under budget.
static int game3d_model_cache_set_template_residency_hint_locked(void *model_template,
                                                                 double priority,
                                                                 double distance) {
    if (!model_template)
        return 0;
    for (int32_t i = 0; i < g_game3d_model_cache_count; ++i) {
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[i];
        if (!entry->loading && entry->model_template == model_template) {
            entry->residency_priority = game3d_model_cache_sanitize_priority(priority);
            entry->residency_distance = game3d_model_cache_sanitize_distance(distance);
            entry->last_used = game3d_model_cache_next_tick();
            return 1;
        }
    }
    return 0;
}

/// @brief Allocate a ModelTemplate retaining the path and loaded model; traps on OOM.
static rt_game3d_model_template *game3d_model_template_new(rt_string path,
                                                           int8_t asset_path,
                                                           void *model) {
    if (!rt_g3d_has_class(model, RT_G3D_MODEL3D_CLASS_ID)) {
        rt_trap("Game3D.ModelTemplate: model must be Model3D");
        return NULL;
    }
    rt_game3d_model_template *model_template = (rt_game3d_model_template *)rt_obj_new_i64(
        RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID, (int64_t)sizeof(*model_template));
    if (!model_template) {
        rt_trap("Game3D.ModelTemplate: allocation failed");
        return NULL;
    }
    memset(model_template, 0, sizeof(*model_template));
    rt_obj_set_finalizer(model_template, game3d_model_template_finalize);
    model_template->asset_path = asset_path ? 1 : 0;
    game3d_assign_ref((void **)&model_template->path, path ? path : rt_const_cstr(""));
    game3d_assign_typed_ref(&model_template->model, model, RT_G3D_MODEL3D_CLASS_ID);
    return model_template;
}

static double game3d_asset_handle_sanitize_progress(double progress);
static double game3d_asset_handle_load_progress(const rt_game3d_asset_handle *handle);
static void game3d_asset_handle_store_progress(rt_game3d_asset_handle *handle, double progress);

typedef void (*rt_game3d_test_async_cache_publish_hook_fn)(void *user_data);
static rt_game3d_test_async_cache_publish_hook_fn g_game3d_test_async_cache_publish_hook = NULL;
static void *g_game3d_test_async_cache_publish_hook_user_data = NULL;

/// @brief Install a test-only callback that runs just before async cache publication.
/// @details The hook pointer and userdata are protected by the model-cache lock because async asset
///          jobs can publish from the main-thread commit queue while tests install or clear hooks
///          from setup/teardown code.
/// @param hook Callback to invoke before publication, or NULL to clear the hook.
/// @param user_data Opaque value passed to @p hook.
void rt_game3d_test_set_async_cache_publish_hook(rt_game3d_test_async_cache_publish_hook_fn hook,
                                                 void *user_data) {
    game3d_model_cache_lock();
    g_game3d_test_async_cache_publish_hook = hook;
    g_game3d_test_async_cache_publish_hook_user_data = user_data;
    game3d_model_cache_unlock();
}

/// @brief Invoke the async cache publication hook after copying it under lock.
/// @details The callback runs outside the cache lock so tests can inspect cache state or trigger
///          additional runtime work without deadlocking the publication path.
static void game3d_asset_async_run_cache_publish_hook(void) {
    rt_game3d_test_async_cache_publish_hook_fn hook;
    void *user_data;
    game3d_model_cache_lock();
    hook = g_game3d_test_async_cache_publish_hook;
    user_data = g_game3d_test_async_cache_publish_hook_user_data;
    game3d_model_cache_unlock();
    if (hook)
        hook(user_data);
}

/// @brief Allocate a terminal AssetHandle3D retaining its result object.
static rt_game3d_asset_handle *game3d_asset_handle_new(
    int8_t ready, double progress, rt_string error, void *entity, void *model_template) {
    rt_game3d_asset_handle *handle = (rt_game3d_asset_handle *)rt_obj_new_i64(
        RT_G3D_GAME3D_ASSET_HANDLE3D_CLASS_ID, (int64_t)sizeof(*handle));
    if (!handle) {
        rt_trap("Game3D.AssetHandle3D: allocation failed");
        return NULL;
    }
    memset(handle, 0, sizeof(*handle));
    rt_obj_set_finalizer(handle, game3d_asset_handle_finalize);
    handle->ready = ready ? 1 : 0;
    game3d_asset_handle_store_progress(handle, progress);
    game3d_assign_ref((void **)&handle->error, error ? error : rt_const_cstr(""));
    game3d_assign_typed_ref(&handle->entity, entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    game3d_assign_typed_ref(
        &handle->model_template, model_template, RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID);
    return handle;
}

/// @brief Create a deferred AssetHandle3D request that starts on first observation.
static rt_game3d_asset_handle *game3d_asset_handle_pending(rt_string path,
                                                           int8_t asset_path,
                                                           int8_t template_request) {
    rt_game3d_asset_handle *handle = game3d_asset_handle_new(0, 0.0, rt_const_cstr(""), NULL, NULL);
    if (!handle)
        return NULL;
    handle->deferred = 1;
    handle->asset_path = asset_path ? 1 : 0;
    handle->template_request = template_request ? 1 : 0;
    game3d_assign_ref((void **)&handle->path, path ? path : rt_const_cstr(""));
    return handle;
}

/// @brief Validate an async asset request up front, returning an error string if it cannot start.
/// @return Empty string if the request is valid, else an owned error message.
static rt_string game3d_asset_handle_preflight_error(rt_game3d_asset_handle *handle) {
    const char *path = handle && handle->path ? rt_string_cstr(handle->path) : NULL;
    if (!path || !*path)
        return rt_const_cstr("invalid path");
    if (!game3d_path_has_model_extension(path))
        return rt_const_cstr("unsupported file extension");
    return rt_const_cstr("");
}

/// @brief Return the load error recorded on an asset handle (NULL if it loaded successfully).
static rt_string game3d_asset_handle_load_error(rt_game3d_asset_handle *handle) {
    if (handle && handle->asset_path)
        return rt_const_cstr("failed to load model asset");
    return rt_const_cstr("failed to load model");
}

/// @brief Normalize async loader diagnostics to the public AssetHandle3D error contract.
/// @details Packed-asset glTF loading can report a verbose dependency failure when the root asset
///          itself is absent. Synchronous Game3D asset APIs expose that condition as the stable
///          `"failed to load model asset"` string, so async handles do the same while preserving
///          genuinely specific parser and texture diagnostics.
static const char *game3d_asset_async_error_or_fallback(rt_game3d_asset_handle *handle,
                                                        const char *message) {
    if (handle && handle->asset_path && message &&
        strstr(message, "failed to load model dependency"))
        return "failed to load model asset";
    if (message && message[0])
        return message;
    return (handle && handle->asset_path) ? "failed to load model asset" : "failed to load model";
}

static int8_t game3d_asset_handle_load_flag(const int8_t *flag) {
    return flag && rt_atomic_load_i8(flag, __ATOMIC_ACQUIRE) ? 1 : 0;
}

static void game3d_asset_handle_store_flag(int8_t *flag, int8_t value) {
    if (flag)
        rt_atomic_store_i8(flag, value ? 1 : 0, __ATOMIC_RELEASE);
}

static double game3d_asset_handle_sanitize_progress(double progress) {
    progress = game3d_finite_or(progress, 0.0);
    if (progress < 0.0)
        return 0.0;
    if (progress > 1.0)
        return 1.0;
    return progress;
}

static double game3d_asset_handle_load_progress(const rt_game3d_asset_handle *handle) {
    double progress = 0.0;
    if (handle)
        rt_atomic_load_f64(&handle->progress, &progress, __ATOMIC_ACQUIRE);
    return game3d_asset_handle_sanitize_progress(progress);
}

static void game3d_asset_handle_store_progress(rt_game3d_asset_handle *handle, double progress) {
    double sanitized = game3d_asset_handle_sanitize_progress(progress);
    if (handle)
        rt_atomic_store_f64(&handle->progress, &sanitized, __ATOMIC_RELEASE);
}

static int game3d_asset_handle_is_ready(const rt_game3d_asset_handle *handle) {
    return handle ? game3d_asset_handle_load_flag(&handle->ready) != 0 : 0;
}

static int game3d_asset_handle_is_cancelled(const rt_game3d_asset_handle *handle) {
    return handle ? game3d_asset_handle_load_flag(&handle->cancelled) != 0 : 0;
}

static int game3d_asset_handle_is_deferred(const rt_game3d_asset_handle *handle) {
    return handle ? game3d_asset_handle_load_flag(&handle->deferred) != 0 : 0;
}

static int game3d_asset_handle_is_async_started(const rt_game3d_asset_handle *handle) {
    return handle ? game3d_asset_handle_load_flag(&handle->async_started) != 0 : 0;
}

/// @brief Mark an async asset handle terminal with an error if it has not already completed.
static void game3d_asset_handle_fail_terminal(rt_game3d_asset_handle *handle, const char *message) {
    if (!handle || game3d_asset_handle_is_ready(handle) || game3d_asset_handle_is_cancelled(handle))
        return;
    game3d_asset_handle_store_flag(&handle->deferred, 0);
    game3d_asset_handle_store_flag(&handle->async_started, 0);
    game3d_asset_handle_store_progress(handle, 1.0);
    game3d_asset_assign_cstr(&handle->error,
                             message && *message ? message : "failed to load model");
    game3d_asset_handle_store_flag(&handle->ready, 1);
}

/// @brief Lazily initialize the shared async-asset runtime (worker thread + commit queue).
/// @return 1 if the runtime is ready, 0 on initialization failure.
static int game3d_asset_async_ensure_runtime(void) {
    int ok = 1;
    game3d_model_cache_lock();
    if (!g_game3d_asset_commit_queue) {
        g_game3d_asset_commit_queue = rt_g3d_commit_queue_new();
        if (!g_game3d_asset_commit_queue)
            ok = 0;
    }
    if (ok && !g_game3d_asset_async_pool) {
        g_game3d_asset_async_pool = rt_threadpool_new(game3d_default_worker_count());
        if (!g_game3d_asset_async_pool)
            ok = 0;
    }
    game3d_model_cache_unlock();
    return ok;
}

/// @brief Run pending main-thread asset commits produced by the async worker (budget-paced).
static void game3d_asset_async_drain_commits(void) {
    if (g_game3d_asset_commit_queue) {
        uint64_t upload_budget =
            rt_atomic_load_u64(&g_game3d_asset_upload_budget_bytes, __ATOMIC_ACQUIRE);
        (void)rt_g3d_commit_queue_drain_budget(
            g_game3d_asset_commit_queue, RT_GAME3D_ASSET_COMMIT_DRAIN_BUDGET, upload_budget);
    }
}

/// @brief Retain and return a cached model template if one is already loaded for the path (else
/// NULL).
static rt_game3d_model_template *game3d_model_cache_try_retain_ready(rt_string cache_path,
                                                                     int8_t asset_path) {
    rt_game3d_model_template *model_template = NULL;
    int32_t index;
    if (!cache_path)
        return NULL;
    game3d_model_cache_lock();
    index = game3d_model_cache_find_index(cache_path, asset_path);
    if (index >= 0) {
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[index];
        rt_game3d_model_template *ready = game3d_model_template_ref(entry->model_template);
        if (!entry->loading && ready) {
            entry->last_used = game3d_model_cache_next_tick();
            rt_obj_retain_maybe(ready);
            model_template = ready;
        }
    }
    game3d_model_cache_unlock();
    return model_template;
}

/// @brief Insert a freshly loaded model template into the cache (evicting a victim if at budget).
static rt_game3d_model_template *game3d_model_cache_store_loaded_template_for_generation(
    rt_string cache_path, int8_t asset_path, void *loaded_model, uint64_t generation, int *stale) {
    if (stale)
        *stale = 0;
    if (!cache_path || !loaded_model)
        return NULL;
    rt_game3d_model_template *model_template =
        game3d_model_template_new(cache_path, asset_path, loaded_model);
    if (!model_template)
        return NULL;

    game3d_model_cache_lock();
    if (g_game3d_model_cache_generation != generation) {
        game3d_model_cache_unlock();
        if (stale)
            *stale = 1;
        if (rt_obj_release_check0(model_template))
            rt_obj_free(model_template);
        return NULL;
    }
    int32_t index = game3d_model_cache_find_index(cache_path, asset_path);
    if (index >= 0) {
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[index];
        rt_game3d_model_template *existing = game3d_model_template_ref(entry->model_template);
        if (!entry->loading && existing) {
            entry->last_used = game3d_model_cache_next_tick();
            rt_obj_retain_maybe(existing);
            game3d_model_cache_unlock();
            if (rt_obj_release_check0(model_template))
                rt_obj_free(model_template);
            return existing;
        }
        game3d_model_cache_remove_at(index);
    }

    game3d_model_cache_evict_if_full();
    if (g_game3d_model_cache_count >= RT_GAME3D_MODEL_CACHE_MAX_ENTRIES ||
        !game3d_model_cache_grow(g_game3d_model_cache_count + 1)) {
        game3d_model_cache_unlock();
        if (rt_obj_release_check0(model_template))
            rt_obj_free(model_template);
        return NULL;
    }

    index = g_game3d_model_cache_count++;
    rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[index];
    memset(entry, 0, sizeof(*entry));
    game3d_model_cache_set_default_residency_hint(entry);
    game3d_assign_ref((void **)&entry->path, cache_path ? cache_path : rt_const_cstr(""));
    entry->asset_path = asset_path ? 1 : 0;
    game3d_assign_typed_ref(
        &entry->model_template, model_template, RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID);
    entry->resident_bytes = game3d_model_template_estimate_resident_bytes(model_template);
    g_game3d_model_resident_bytes =
        game3d_u64_saturating_add(g_game3d_model_resident_bytes, entry->resident_bytes);
    entry->last_used = game3d_model_cache_next_tick();
    game3d_model_cache_evict_to_budget();
    game3d_model_cache_notify_all();
    game3d_model_cache_unlock();
    return model_template;
}

/// @brief Free an async asset job and all of its owned buffers/handles.
static void game3d_asset_async_job_free(rt_game3d_asset_async_job *job) {
    if (!job)
        return;
    rt_gltf_preload_bundle_free(job->preloaded_gltf);
    free(job->preloaded_fbx_data);
    if (job->handle && rt_obj_release_check0(job->handle))
        rt_obj_free(job->handle);
    free(job);
}

/// @brief Commit-queue discard callback for async asset jobs.
/// @details If the queue is freed while a job is pending, the queued callback never runs. This hook
///          releases the retained handle and any staged glTF payload so shutdown cannot leak jobs.
static void game3d_asset_async_job_cancel(void *user_data) {
    game3d_asset_async_job_free((rt_game3d_asset_async_job *)user_data);
}

static int game3d_asset_handle_has_external_waiter(rt_game3d_asset_handle *handle) {
    if (!handle)
        return 0;
    rt_heap_hdr_t *hdr = rt_heap_hdr(handle);
    if (!hdr || hdr->kind != RT_HEAP_OBJECT)
        return 0;
    size_t refcnt = rt_atomic_load_size(&hdr->refcnt, __ATOMIC_ACQUIRE);
    return refcnt > 1u && refcnt < RT_HEAP_IMMORTAL_REFCNT;
}

/// @brief Copy an AssetHandle3D request into worker-safe POD fields on @p job.
/// @details Must run on the main thread before the job is submitted. The worker reads only these
///          snapshotted bytes and flags, never the handle's runtime strings or mutable request
///          state.
/// @return 1 when the request was copied; 0 when the path is absent or too large for the job
/// buffer.
static int game3d_asset_async_snapshot_request(rt_game3d_asset_async_job *job,
                                               const rt_game3d_asset_handle *handle) {
    const char *path;
    size_t len;
    if (!job || !handle || !handle->path)
        return 0;
    path = rt_string_cstr(handle->path);
    if (!path || path[0] == '\0')
        return 0;
    len = strlen(path);
    if (len >= sizeof(job->path))
        return 0;
    memcpy(job->path, path, len + 1u);
    job->asset_path = handle->asset_path ? 1 : 0;
    job->template_request = handle->template_request ? 1 : 0;
    return 1;
}

/// @brief Estimate the main-thread cost (in budget units) of committing an async job's result.
static uint64_t game3d_asset_async_job_commit_cost(rt_game3d_asset_async_job *job) {
    size_t decoded_image_bytes;
    if (!job || job->error[0])
        return 0u;
    if (job->preloaded_fbx_data)
        return 1u;
    if (!job->preloaded_gltf)
        return 1u;
    decoded_image_bytes = rt_gltf_preload_bundle_decoded_image_bytes(job->preloaded_gltf);
    if (decoded_image_bytes > UINT64_MAX)
        return UINT64_MAX;
    return decoded_image_bytes > 0u ? (uint64_t)decoded_image_bytes : 1u;
}

/// @brief Cost of the next incremental upload slice for an async job (for budget-paced streaming).
static uint64_t game3d_asset_async_next_upload_slice_cost(rt_game3d_asset_async_job *job) {
    size_t slice_bytes;
    if (!job || job->error[0] || !job->preloaded_gltf)
        return 0u;
    slice_bytes = rt_gltf_preload_bundle_next_decoded_image_slice_bytes(
        job->preloaded_gltf, (size_t)RT_GAME3D_ASSET_UPLOAD_SLICE_BYTES);
    if (slice_bytes > UINT64_MAX)
        return UINT64_MAX;
    return (uint64_t)slice_bytes;
}

static void game3d_asset_async_commit(void *user_data);
static void game3d_asset_async_prepare_upload_slice(void *user_data);
static void game3d_asset_async_worker(void *user_data);

static int game3d_asset_async_restart_for_current_generation(rt_game3d_asset_async_job *job) {
    rt_game3d_asset_handle *handle = job ? job->handle : NULL;
    if (!handle || game3d_asset_handle_is_ready(handle) ||
        game3d_asset_handle_is_cancelled(handle) || !job->template_request)
        return 0;
    if (!game3d_asset_handle_has_external_waiter(handle))
        return 0;
    if (!game3d_asset_async_ensure_runtime())
        return -1;

    rt_game3d_asset_async_job *next =
        (rt_game3d_asset_async_job *)calloc(1, sizeof(rt_game3d_asset_async_job));
    if (!next)
        return -1;
    if (!game3d_asset_async_snapshot_request(next, handle)) {
        free(next);
        return -1;
    }

    next->handle = handle;
    next->cache_generation = game3d_model_cache_current_generation();
    rt_obj_retain_maybe(handle);
    game3d_asset_handle_store_progress(handle, 0.0);
    if (!rt_threadpool_submit(
            g_game3d_asset_async_pool, game3d_task_fnptr(game3d_asset_async_worker), next)) {
        game3d_asset_async_job_free(next);
        return -1;
    }
    return 1;
}

static void game3d_asset_async_drop_stale_job(rt_game3d_asset_async_job *job,
                                              void **loaded_model_slot) {
    rt_game3d_asset_handle *handle = job ? job->handle : NULL;
    if (loaded_model_slot)
        game3d_release_ref(loaded_model_slot);
    rt_game3d_diag_record_stale_async_load_dropped();
    int restart = game3d_asset_async_restart_for_current_generation(job);
    if (restart < 0) {
        game3d_asset_handle_fail_terminal(handle, "failed to schedule asset load");
    } else if (restart == 0 && handle) {
        game3d_asset_handle_store_flag(&handle->async_started, 0);
    }
    game3d_asset_async_job_free(job);
}

/// @brief Enqueue the next main-thread commit step for an async job onto the commit queue.
/// @return 1 if a commit was enqueued, 0 if the job is finished.
static int game3d_asset_async_enqueue_next_commit(rt_game3d_asset_async_job *job) {
    uint64_t slice_cost = game3d_asset_async_next_upload_slice_cost(job);
    if (slice_cost > 0u) {
        return rt_g3d_commit_queue_enqueue_cost_cancel(g_game3d_asset_commit_queue,
                                                       game3d_asset_async_prepare_upload_slice,
                                                       job,
                                                       slice_cost,
                                                       game3d_asset_async_job_cancel);
    }
    return rt_g3d_commit_queue_enqueue_cost_cancel(g_game3d_asset_commit_queue,
                                                   game3d_asset_async_commit,
                                                   job,
                                                   game3d_asset_async_job_commit_cost(job),
                                                   game3d_asset_async_job_cancel);
}

/// @brief Build the final Model3D on the main thread from an async job's preloaded glTF bundle.
/// @return The loaded model, or NULL on failure (error recorded on the job).
static void *game3d_asset_async_commit_load_model(rt_game3d_asset_async_job *job) {
    rt_game3d_asset_handle *handle = job ? job->handle : NULL;
    void *loaded_model = NULL;
    jmp_buf recovery;
    if (!job || !handle)
        return NULL;

    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        if (job->preloaded_gltf) {
            rt_gltf_preload_bundle *bundle = job->preloaded_gltf;
            loaded_model =
                rt_model3d_load_preloaded_gltf_bundle(handle->path, bundle, handle->asset_path);
            job->preloaded_gltf = NULL;
        } else if (job->preloaded_fbx_data) {
            uint8_t *data = job->preloaded_fbx_data;
            size_t size = job->preloaded_fbx_size;
            loaded_model =
                rt_model3d_load_preloaded_fbx(handle->path, data, size, handle->asset_path);
            job->preloaded_fbx_data = NULL;
            job->preloaded_fbx_size = 0u;
        } else {
            loaded_model = handle->asset_path ? rt_model3d_load_asset(handle->path)
                                              : rt_model3d_load(handle->path);
        }
        if (!loaded_model && !job->error[0]) {
            const char *fallback =
                handle->asset_path ? "failed to load model asset" : "failed to load model";
            snprintf(job->error, sizeof(job->error), "%s", fallback);
        }
    } else {
        const char *msg = rt_trap_get_error();
        snprintf(job->error,
                 sizeof(job->error),
                 "%s",
                 game3d_asset_async_error_or_fallback(handle, msg));
    }
    rt_trap_clear_recovery();
    return loaded_model;
}

/// @brief Commit-queue callback: prepare one budgeted texture-upload slice for an async job.
static void game3d_asset_async_prepare_upload_slice(void *user_data) {
    rt_game3d_asset_async_job *job = (rt_game3d_asset_async_job *)user_data;
    rt_game3d_asset_handle *handle = job ? job->handle : NULL;
    size_t prepared = 0u;
    jmp_buf recovery;
    if (!job || !handle) {
        game3d_asset_async_job_free(job);
        return;
    }

    if (game3d_asset_handle_is_ready(handle) || game3d_asset_handle_is_cancelled(handle)) {
        game3d_asset_handle_store_flag(&handle->async_started, 0);
        game3d_asset_async_job_free(job);
        return;
    }
    if (job->template_request && !game3d_model_cache_generation_matches(job->cache_generation)) {
        game3d_asset_async_drop_stale_job(job, NULL);
        return;
    }

    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        prepared = rt_gltf_preload_bundle_prepare_decoded_image_slice(
            job->preloaded_gltf, (size_t)RT_GAME3D_ASSET_UPLOAD_SLICE_BYTES);
    } else {
        const char *msg = rt_trap_get_error();
        snprintf(job->error,
                 sizeof(job->error),
                 "%s",
                 (msg && msg[0]) ? msg : "failed to prepare texture upload");
    }
    rt_trap_clear_recovery();

    if (!job->error[0]) {
        if (prepared == 0u && game3d_asset_async_next_upload_slice_cost(job) > 0u)
            snprintf(job->error, sizeof(job->error), "%s", "failed to prepare texture upload");
        else {
            job->upload_prepared_bytes =
                game3d_u64_saturating_add(job->upload_prepared_bytes, (uint64_t)prepared);
            if (job->upload_total_bytes > 0u) {
                double ratio = (double)job->upload_prepared_bytes / (double)job->upload_total_bytes;
                if (ratio > 1.0)
                    ratio = 1.0;
                game3d_asset_handle_store_progress(handle, 0.10 + ratio * 0.70);
            } else {
                game3d_asset_handle_store_progress(handle, 0.50);
            }
        }
    }

    if (!game3d_asset_async_enqueue_next_commit(job)) {
        game3d_asset_handle_fail_terminal(handle, "failed to schedule asset commit");
        game3d_asset_async_job_free(job);
    }
}

/// @brief Commit-queue callback: finalize an async job on the main thread (build model, store,
/// notify).
static void game3d_asset_async_commit(void *user_data) {
    rt_game3d_asset_async_job *job = (rt_game3d_asset_async_job *)user_data;
    rt_game3d_asset_handle *handle = job ? job->handle : NULL;
    void *loaded_model = NULL;
    if (!job || !handle) {
        game3d_asset_async_job_free(job);
        return;
    }

    if (!game3d_asset_handle_is_ready(handle) && !game3d_asset_handle_is_cancelled(handle)) {
        if (!job->error[0])
            loaded_model = game3d_asset_async_commit_load_model(job);
        if (loaded_model) {
            if (handle->template_request) {
                rt_string cache_path =
                    game3d_model_cache_key_path(handle->path, handle->asset_path);
                rt_game3d_model_template *model_template = NULL;
                int stale = 0;
                if (!cache_path) {
                    game3d_asset_assign_cstr(&handle->error, "failed to schedule asset load");
                } else if (game3d_model_cache_generation_matches(job->cache_generation)) {
                    game3d_asset_async_run_cache_publish_hook();
                    model_template = game3d_model_cache_store_loaded_template_for_generation(
                        cache_path,
                        handle->asset_path,
                        loaded_model,
                        job->cache_generation,
                        &stale);
                } else {
                    stale = 1;
                }
                if (stale) {
                    game3d_release_ref((void **)&cache_path);
                    game3d_asset_async_drop_stale_job(job, &loaded_model);
                    return;
                }
                if (model_template) {
                    game3d_assign_typed_ref(&handle->model_template,
                                            model_template,
                                            RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID);
                    game3d_release_ref((void **)&model_template);
                } else if (!game3d_string_has_bytes(handle->error)) {
                    game3d_asset_assign_new_string(&handle->error,
                                                   game3d_asset_handle_load_error(handle));
                }
                game3d_release_ref((void **)&cache_path);
            } else {
                rt_game3d_model_template *model_template =
                    game3d_model_template_new(handle->path, handle->asset_path, loaded_model);
                void *entity =
                    model_template ? rt_game3d_model_template_instantiate(model_template) : NULL;
                if (entity) {
                    game3d_assign_typed_ref(&handle->entity, entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
                    game3d_release_ref(&entity);
                } else {
                    game3d_asset_assign_cstr(&handle->error, "failed to load model");
                }
                if (model_template && rt_obj_release_check0(model_template))
                    rt_obj_free(model_template);
            }
        } else {
            if (job->error[0]) {
                rt_string error = rt_string_from_bytes(job->error, strlen(job->error));
                if (error)
                    game3d_assign_ref((void **)&handle->error, error);
                else
                    game3d_asset_assign_new_string(&handle->error,
                                                   game3d_asset_handle_load_error(handle));
                game3d_release_ref((void **)&error);
            } else {
                game3d_asset_assign_new_string(&handle->error,
                                               game3d_asset_handle_load_error(handle));
            }
        }
        game3d_asset_handle_store_flag(&handle->deferred, 0);
        game3d_asset_handle_store_flag(&handle->async_started, 0);
        game3d_asset_handle_store_progress(handle, 1.0);
        game3d_asset_handle_store_flag(&handle->ready, 1);
    } else if (game3d_asset_handle_is_ready(handle) || game3d_asset_handle_is_cancelled(handle)) {
        game3d_asset_handle_store_flag(&handle->async_started, 0);
    }

    game3d_release_ref(&loaded_model);
    game3d_asset_async_job_free(job);
}

/// @brief Worker-thread entry point: stage a model's preload bundle off the main thread, then
/// enqueue commits.
static void game3d_asset_async_worker(void *user_data) {
    rt_game3d_asset_async_job *job = (rt_game3d_asset_async_job *)user_data;
    rt_game3d_asset_handle *handle = job ? job->handle : NULL;
    const char *path = job ? job->path : NULL;
    uint8_t *root_data = NULL;
    size_t root_len = 0;
    if (!job || !handle) {
        game3d_asset_async_job_free(job);
        return;
    }

    if (!job->asset_path && game3d_path_has_gltf_extension(path)) {
        root_data = game3d_asset_read_file_bytes(path, &root_len);
        if (!root_data) {
            snprintf(job->error, sizeof(job->error), "%s", "failed to load model");
        } else if (root_len == 0u) {
            snprintf(job->error, sizeof(job->error), "%s", "empty model file");
            free(root_data);
            root_data = NULL;
        } else {
            job->preloaded_gltf = rt_gltf_preload_bundle_create_cstr(
                path, root_data, root_len, 0, job->error, sizeof(job->error));
            root_data = NULL;
            job->upload_total_bytes =
                (uint64_t)rt_gltf_preload_bundle_decoded_image_bytes(job->preloaded_gltf);
            if (!job->preloaded_gltf && !job->error[0]) {
                snprintf(job->error, sizeof(job->error), "%s", "failed to load model");
            }
        }
    } else if (!job->asset_path && game3d_path_has_fbx_extension(path)) {
        root_data = game3d_asset_read_file_bytes(path, &root_len);
        if (!root_data) {
            snprintf(job->error, sizeof(job->error), "%s", "failed to load model");
        } else if (root_len == 0u) {
            snprintf(job->error, sizeof(job->error), "%s", "empty model file");
            free(root_data);
            root_data = NULL;
        } else {
            job->preloaded_fbx_data = root_data;
            job->preloaded_fbx_size = root_len;
            root_data = NULL;
            game3d_asset_handle_store_progress(handle, 0.10);
        }
    }

    if (!game3d_asset_async_enqueue_next_commit(job)) {
        game3d_asset_handle_store_flag(&handle->deferred, 0);
        game3d_asset_handle_store_flag(&handle->async_started, 0);
        game3d_asset_handle_store_progress(handle, 1.0);
        game3d_asset_handle_store_flag(&handle->ready, 1);
        game3d_asset_async_job_free(job);
    }
}

/// @brief Kick off asynchronous loading for an asset handle (preflight, then dispatch a worker
/// job).
static void game3d_asset_handle_start_async(rt_game3d_asset_handle *handle) {
    if (!handle || game3d_asset_handle_is_ready(handle) ||
        game3d_asset_handle_is_cancelled(handle) || !game3d_asset_handle_is_deferred(handle) ||
        game3d_asset_handle_is_async_started(handle))
        return;

    rt_string preflight_error = game3d_asset_handle_preflight_error(handle);
    if (game3d_string_has_bytes(preflight_error)) {
        game3d_asset_handle_store_flag(&handle->deferred, 0);
        game3d_asset_handle_store_progress(handle, 1.0);
        game3d_assign_ref((void **)&handle->error, preflight_error);
        game3d_asset_handle_store_flag(&handle->ready, 1);
        rt_string_unref(preflight_error);
        return;
    }
    rt_string_unref(preflight_error);

    if (handle->template_request) {
        rt_string cache_path = game3d_model_cache_key_path(handle->path, handle->asset_path);
        if (!cache_path) {
            game3d_asset_handle_store_flag(&handle->deferred, 0);
            game3d_asset_handle_store_progress(handle, 1.0);
            game3d_asset_assign_cstr(&handle->error, "failed to schedule asset load");
            game3d_asset_handle_store_flag(&handle->ready, 1);
            return;
        }
        rt_game3d_model_template *cached =
            game3d_model_cache_try_retain_ready(cache_path, handle->asset_path);
        if (cached) {
            game3d_assign_typed_ref(
                &handle->model_template, cached, RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID);
            game3d_release_ref((void **)&cached);
            game3d_asset_handle_store_flag(&handle->deferred, 0);
            game3d_asset_handle_store_progress(handle, 1.0);
            game3d_release_ref((void **)&cache_path);
            game3d_asset_handle_store_flag(&handle->ready, 1);
            return;
        }
        game3d_release_ref((void **)&cache_path);
    }

    if (!game3d_asset_async_ensure_runtime()) {
        game3d_asset_handle_store_flag(&handle->deferred, 0);
        game3d_asset_handle_store_progress(handle, 1.0);
        game3d_asset_assign_cstr(&handle->error, "failed to schedule asset load");
        game3d_asset_handle_store_flag(&handle->ready, 1);
        return;
    }

    rt_game3d_asset_async_job *job =
        (rt_game3d_asset_async_job *)calloc(1, sizeof(rt_game3d_asset_async_job));
    if (!job) {
        game3d_asset_handle_store_flag(&handle->deferred, 0);
        game3d_asset_handle_store_progress(handle, 1.0);
        game3d_asset_assign_cstr(&handle->error, "failed to schedule asset load");
        game3d_asset_handle_store_flag(&handle->ready, 1);
        return;
    }

    if (!game3d_asset_async_snapshot_request(job, handle)) {
        game3d_asset_handle_fail_terminal(handle, "failed to schedule asset load");
        free(job);
        return;
    }
    job->handle = handle;
    job->cache_generation = game3d_model_cache_current_generation();
    rt_obj_retain_maybe(handle);
    game3d_asset_handle_store_flag(&handle->async_started, 1);
    if (!rt_threadpool_submit(
            g_game3d_asset_async_pool, game3d_task_fnptr(game3d_asset_async_worker), job)) {
        game3d_asset_handle_store_flag(&handle->async_started, 0);
        game3d_asset_handle_store_flag(&handle->deferred, 0);
        game3d_asset_handle_store_progress(handle, 1.0);
        game3d_asset_assign_cstr(&handle->error, "failed to schedule asset load");
        game3d_asset_handle_store_flag(&handle->ready, 1);
        game3d_asset_async_job_free(job);
    }
}

/// @brief Advance an asset handle's async state: drain commits and, if requested, start loading.
static void game3d_asset_handle_service(rt_game3d_asset_handle *handle, int start_if_needed) {
    game3d_asset_async_drain_commits();
    if (start_if_needed)
        game3d_asset_handle_start_async(handle);
}

/// @brief Load a model from disk/asset and wrap it in a new ModelTemplate, bypassing the
///   cache; traps `method` if the model fails to load.
static rt_game3d_model_template *game3d_assets_load_template_uncached_impl(rt_string path,
                                                                           int8_t asset_path,
                                                                           const char *method,
                                                                           int trap_on_fail) {
    void *model = asset_path ? rt_model3d_load_asset(path) : rt_model3d_load(path);
    rt_game3d_model_template *model_template;
    if (!model) {
        if (trap_on_fail)
            rt_trap(method);
        return NULL;
    }
    model_template = game3d_model_template_new(path, asset_path, model);
    game3d_release_ref(&model);
    return model_template;
}

/// @brief Synchronously load a model template from a path, bypassing the cache.
/// @details Reads the root bytes, parses by extension (glTF/vscn/etc.), and wraps the result as a
///          model template. Returns NULL (with an error out-param) on failure.
static rt_game3d_model_template *game3d_assets_load_template_uncached(rt_string path,
                                                                      int8_t asset_path,
                                                                      const char *method) {
    return game3d_assets_load_template_uncached_impl(path, asset_path, method, 1);
}

static void game3d_model_cache_cancel_pending(rt_string cache_path, int8_t asset_path) {
    int32_t pending_index;
    game3d_model_cache_lock();
    pending_index = game3d_model_cache_find_index(cache_path, asset_path);
    if (pending_index >= 0 && g_game3d_model_cache[pending_index].loading) {
        game3d_model_cache_remove_at(pending_index);
        game3d_model_cache_notify_all();
    }
    game3d_model_cache_unlock();
}

static int game3d_model_cache_publish_pending(rt_string cache_path,
                                              int8_t asset_path,
                                              rt_game3d_model_template *model_template) {
    int32_t pending_index;
    rt_game3d_model_cache_entry *entry;
    game3d_model_cache_lock();
    pending_index = game3d_model_cache_find_index(cache_path, asset_path);
    if (pending_index < 0) {
        game3d_model_cache_unlock();
        return 0;
    }
    entry = &g_game3d_model_cache[pending_index];
    game3d_assign_typed_ref(
        &entry->model_template, model_template, RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID);
    entry->resident_bytes = game3d_model_template_estimate_resident_bytes(model_template);
    g_game3d_model_resident_bytes =
        game3d_u64_saturating_add(g_game3d_model_resident_bytes, entry->resident_bytes);
    entry->loading = 0;
    entry->last_used = game3d_model_cache_next_tick();
    game3d_model_cache_evict_to_budget();
    game3d_model_cache_notify_all();
    game3d_model_cache_unlock();
    return 1;
}

/// @brief Return the cached ModelTemplate for (path, asset_path), loading and inserting
///   it on a miss; returns a retained template. Traps on load or cache-grow failure.
static rt_game3d_model_template *game3d_assets_load_template_cached_impl(rt_string path,
                                                                         int8_t asset_path,
                                                                         const char *method,
                                                                         int trap_on_fail) {
    rt_game3d_model_template *model_template;
    rt_string cache_path = game3d_model_cache_key_path(path, asset_path);
    int32_t pending_index = -1;
    if (!cache_path) {
        if (trap_on_fail)
            rt_trap("Game3D.Assets3D: model cache key allocation failed");
        return NULL;
    }
    for (;;) {
        int32_t index;
        game3d_model_cache_lock();
        index = game3d_model_cache_find_index(cache_path, asset_path);
        if (index >= 0) {
            rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[index];
            if (entry->loading) {
                if (!game3d_model_cache_wait_locked_ms(RT_GAME3D_MODEL_CACHE_WAIT_TIMEOUT_MS)) {
                    game3d_model_cache_unlock();
                    game3d_release_ref((void **)&cache_path);
                    if (trap_on_fail)
                        rt_trap("Game3D.Assets3D: timed out waiting for model cache load");
                    return NULL;
                }
                game3d_model_cache_unlock();
                continue;
            }
            rt_game3d_model_template *ready = game3d_model_template_ref(entry->model_template);
            if (ready) {
                rt_obj_retain_maybe(ready);
                entry->last_used = game3d_model_cache_next_tick();
                model_template = ready;
                game3d_model_cache_unlock();
                game3d_release_ref((void **)&cache_path);
                return model_template;
            }
            game3d_model_cache_remove_at(index);
        }
        game3d_model_cache_evict_if_full();
        if (g_game3d_model_cache_count >= RT_GAME3D_MODEL_CACHE_MAX_ENTRIES) {
            if (!game3d_model_cache_wait_locked_ms(RT_GAME3D_MODEL_CACHE_WAIT_TIMEOUT_MS)) {
                game3d_model_cache_unlock();
                game3d_release_ref((void **)&cache_path);
                if (trap_on_fail)
                    rt_trap("Game3D.Assets3D: timed out waiting for model cache");
                return NULL;
            }
            game3d_model_cache_unlock();
            continue;
        }
        if (!game3d_model_cache_grow(g_game3d_model_cache_count + 1)) {
            game3d_model_cache_unlock();
            game3d_release_ref((void **)&cache_path);
            if (trap_on_fail)
                rt_trap("Game3D.Assets3D: model cache allocation failed");
            return NULL;
        }
        pending_index = g_game3d_model_cache_count++;
        rt_game3d_model_cache_entry *entry = &g_game3d_model_cache[pending_index];
        memset(entry, 0, sizeof(*entry));
        game3d_model_cache_set_default_residency_hint(entry);
        game3d_assign_ref((void **)&entry->path, cache_path ? cache_path : rt_const_cstr(""));
        entry->asset_path = asset_path ? 1 : 0;
        entry->loading = 1;
        entry->last_used = game3d_model_cache_next_tick();
        game3d_model_cache_unlock();
        break;
    }

    void *loaded_model = asset_path ? rt_model3d_load_asset(path) : rt_model3d_load(path);
    if (!loaded_model) {
        game3d_model_cache_cancel_pending(cache_path, asset_path);
        game3d_release_ref((void **)&cache_path);
        if (trap_on_fail)
            rt_trap(method);
        return NULL;
    }
    model_template = game3d_model_template_new(cache_path, asset_path, loaded_model);
    game3d_release_ref(&loaded_model);
    if (!model_template) {
        game3d_model_cache_cancel_pending(cache_path, asset_path);
        game3d_release_ref((void **)&cache_path);
        return NULL;
    }

    if (!game3d_model_cache_publish_pending(cache_path, asset_path, model_template)) {
        if (rt_obj_release_check0(model_template))
            rt_obj_free(model_template);
        game3d_release_ref((void **)&cache_path);
        rt_trap("Game3D.Assets3D: pending cache entry was lost");
        return NULL;
    }
    game3d_release_ref((void **)&cache_path);
    return model_template;
}

/// @brief Load a model template through the cache: return a retained ready entry or load-and-store
/// it.
static rt_game3d_model_template *game3d_assets_load_template_cached(rt_string path,
                                                                    int8_t asset_path,
                                                                    const char *method) {
    return game3d_assets_load_template_cached_impl(path, asset_path, method, 1);
}

/// @brief Wrap a loaded model's root node as an entity, attaching its animator if the
///   node carries one. Returns NULL on a NULL root.
static void *game3d_entity_from_model_root(void *root) {
    rt_game3d_entity *entity;
    void *animator;
    if (!rt_g3d_has_class(root, RT_G3D_SCENENODE3D_CLASS_ID))
        return NULL;
    entity = (rt_game3d_entity *)rt_game3d_entity_from_node(root);
    if (!entity)
        return NULL;
    animator = rt_scene_node3d_get_animator(root);
    {
        void *node_animator = rt_scene_node3d_get_node_animator(root);
        if (animator || node_animator) {
            void *game_animator = rt_game3d_animator_new_from_bindings(animator, node_animator);
            if (game_animator) {
                rt_game3d_entity_attach_animator(entity, game_animator);
                game3d_release_ref(&game_animator);
            }
        }
    }
    return entity;
}

/// @brief Get the loaded model backing the template (NULL if invalid).
void *rt_game3d_model_template_get_model(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.get_model: invalid template");
    return game3d_model_template_model_ref(model_template);
}

/// @brief Get the source path the template was loaded from ("" if invalid).
rt_string rt_game3d_model_template_get_path(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.get_path: invalid template");
    return model_template && model_template->path ? model_template->path : rt_const_cstr("");
}

/// @brief True if the template was loaded from a packed asset (0 if invalid).
int8_t rt_game3d_model_template_get_is_asset(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.get_isAsset: invalid template");
    return model_template ? model_template->asset_path : 0;
}

/// @brief Number of immutable scenes on the underlying Model3D. See header.
int64_t rt_game3d_model_template_get_scene_count(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.get_sceneCount: invalid template");
    void *model = game3d_model_template_model_ref(model_template);
    return model ? rt_model3d_get_scene_count(model) : 0;
}

/// @brief Imported scene name lookup. See header.
rt_string rt_game3d_model_template_get_scene_name(void *obj, int64_t index) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.getSceneName: invalid template");
    void *model = game3d_model_template_model_ref(model_template);
    return model ? rt_model3d_get_scene_name(model, index) : rt_const_cstr("");
}

/// @brief Imported camera count per scene. See header.
int64_t rt_game3d_model_template_get_camera_count(void *obj, int64_t scene_index) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.getCameraCount: invalid template");
    void *model = game3d_model_template_model_ref(model_template);
    return model ? rt_model3d_get_camera_count(model, scene_index) : 0;
}

/// @brief Imported camera lookup per scene. See header.
void *rt_game3d_model_template_get_camera(void *obj, int64_t scene_index, int64_t index) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.getCamera: invalid template");
    void *model = game3d_model_template_model_ref(model_template);
    return model ? rt_model3d_get_camera(model, scene_index, index) : NULL;
}

/// @brief Instantiate a fresh entity by cloning the template's model. See header.
void *rt_game3d_model_template_instantiate(void *obj) {
    rt_game3d_model_template *model_template =
        game3d_model_template_checked(obj, "Game3D.ModelTemplate.instantiate: invalid template");
    void *model = game3d_model_template_model_ref(model_template);
    if (!model)
        return NULL;
    void *root = rt_model3d_instantiate(model);
    void *entity = game3d_entity_from_model_root(root);
    game3d_release_ref(&root);
    return entity;
}

/// @brief Instantiate a specific imported scene and wrap its root as a Game3D entity.
void *rt_game3d_model_template_instantiate_scene_at(void *obj, int64_t index) {
    rt_game3d_model_template *model_template = game3d_model_template_checked(
        obj, "Game3D.ModelTemplate.instantiateSceneAt: invalid template");
    void *model = game3d_model_template_model_ref(model_template);
    if (!model)
        return NULL;
    void *scene = rt_model3d_instantiate_scene_at(model, index);
    if (!scene)
        return NULL;
    void *root = rt_scene3d_get_root(scene);
    if (!root) {
        game3d_release_ref(&scene);
        return NULL;
    }
    void *entity = game3d_entity_from_model_root(root);
    game3d_release_ref(&scene);
    return entity;
}

/// @brief Load a filesystem model (uncached) and return a ready entity. See header.
void *rt_game3d_assets_load_model(rt_string path) {
    rt_game3d_model_template *model_template = game3d_assets_load_template_uncached(
        path, 0, "Game3D.Assets3D.LoadModel: failed to load model");
    if (!model_template)
        return NULL;
    void *entity = rt_game3d_model_template_instantiate(model_template);
    if (rt_obj_release_check0(model_template))
        rt_obj_free(model_template);
    return entity;
}

/// @brief Load a packed-asset model (uncached) and return a ready entity. See header.
void *rt_game3d_assets_load_model_asset(rt_string path) {
    rt_game3d_model_template *model_template = game3d_assets_load_template_uncached(
        path, 1, "Game3D.Assets3D.LoadModelAsset: failed to load model asset");
    if (!model_template)
        return NULL;
    void *entity = rt_game3d_model_template_instantiate(model_template);
    if (rt_obj_release_check0(model_template))
        rt_obj_free(model_template);
    return entity;
}

/// @brief Load a skeletal animation clip from a filesystem model.
void *rt_game3d_assets_load_animation(rt_string path, int64_t index) {
    return rt_model3d_load_animation(path, index);
}

/// @brief Load a skeletal animation clip from a packed model asset.
void *rt_game3d_assets_load_animation_asset(rt_string path, int64_t index) {
    return rt_model3d_load_animation_asset(path, index);
}

/// @brief Load a node animation clip from a filesystem model.
void *rt_game3d_assets_load_node_animation(rt_string path, int64_t index) {
    return rt_model3d_load_node_animation(path, index);
}

/// @brief Load a node animation clip from a packed model asset.
void *rt_game3d_assets_load_node_animation_asset(rt_string path, int64_t index) {
    return rt_model3d_load_node_animation_asset(path, index);
}

/// @brief Load a filesystem model as a cached reusable template. See header.
void *rt_game3d_assets_load_model_template(rt_string path) {
    return game3d_assets_load_template_cached(
        path, 0, "Game3D.Assets3D.LoadModelTemplate: failed to load model");
}

/// @brief Load a packed-asset model as a cached reusable template. See header.
void *rt_game3d_assets_load_model_template_asset(rt_string path) {
    return game3d_assets_load_template_cached(
        path, 1, "Game3D.Assets3D.LoadModelTemplateAsset: failed to load model asset");
}

/// @brief Finalize a synchronous (deferred) asset handle whose result was produced inline.
static void game3d_asset_handle_complete_if_deferred(rt_game3d_asset_handle *handle) {
    game3d_asset_handle_service(handle, 1);
}

/// @brief Load a filesystem model and expose the terminal result through AssetHandle3D.
void *rt_game3d_assets_load_model_async(rt_string path) {
    return game3d_asset_handle_pending(path, 0, 0);
}

/// @brief Load a packed-asset model and expose the terminal result through AssetHandle3D.
void *rt_game3d_assets_load_model_asset_async(rt_string path) {
    return game3d_asset_handle_pending(path, 1, 0);
}

/// @brief Load a filesystem ModelTemplate and expose it through AssetHandle3D.
void *rt_game3d_assets_load_model_template_async(rt_string path) {
    return game3d_asset_handle_pending(path, 0, 1);
}

/// @brief Load a packed-asset ModelTemplate and expose it through AssetHandle3D.
void *rt_game3d_assets_load_model_template_asset_async(rt_string path) {
    return game3d_asset_handle_pending(path, 1, 1);
}

/// @brief Set the process-wide ModelTemplate cache budget. Negative means unlimited.
void rt_game3d_assets_set_residency_budget(int64_t bytes) {
    uint64_t budget = bytes < 0 ? UINT64_MAX : (uint64_t)bytes;
    game3d_model_cache_lock();
    g_game3d_model_residency_budget_bytes = budget;
    game3d_model_cache_evict_to_budget();
    game3d_model_cache_notify_all();
    game3d_model_cache_unlock();
}

/// @brief Return estimated bytes currently resident in the shared ModelTemplate cache.
int64_t rt_game3d_assets_get_resident_bytes(void) {
    uint64_t bytes;
    game3d_model_cache_lock();
    bytes = g_game3d_model_resident_bytes;
    game3d_model_cache_unlock();
    return game3d_i64_from_u64_saturating(bytes);
}

/// @brief Annotate a cached ModelTemplate for priority/distance-aware eviction.
void rt_game3d_assets_set_residency_hint(void *model_template_obj,
                                         double priority,
                                         double distance) {
    rt_game3d_model_template *model_template = game3d_model_template_checked(
        model_template_obj, "Game3D.Assets3D.SetResidencyHint: invalid template");
    if (!model_template)
        return;
    game3d_model_cache_lock();
    if (game3d_model_cache_set_template_residency_hint_locked(model_template, priority, distance))
        game3d_model_cache_evict_to_budget();
    game3d_model_cache_notify_all();
    game3d_model_cache_unlock();
}

/// @brief Set the process-wide async asset upload budget. Negative means unlimited.
void rt_game3d_assets_set_upload_budget(int64_t bytes) {
    uint64_t budget = bytes < 0 ? UINT64_MAX : (uint64_t)bytes;
    rt_atomic_store_u64(&g_game3d_asset_upload_budget_bytes, budget, __ATOMIC_RELEASE);
}

/// @brief Evict the cached template backing a ready template AssetHandle3D.
void rt_game3d_assets_evict(void *asset_handle) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(asset_handle, "Game3D.Assets3D.Evict: invalid handle");
    if (!handle || !game3d_asset_handle_is_ready(handle) ||
        game3d_asset_handle_is_cancelled(handle) || game3d_string_has_bytes(handle->error) ||
        !game3d_asset_handle_template_ref(handle))
        return;
    game3d_model_cache_lock();
    if (game3d_model_cache_evict_template(game3d_asset_handle_template_ref(handle)))
        game3d_model_cache_notify_all();
    game3d_model_cache_unlock();
}

/// @brief True once the request has completed or been cancelled. See header.
int8_t rt_game3d_asset_handle_get_ready(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.ready: invalid handle");
    game3d_asset_handle_complete_if_deferred(handle);
    return game3d_asset_handle_is_ready(handle) ? 1 : 0;
}

/// @brief Terminal or in-flight loading progress. See header.
double rt_game3d_asset_handle_get_progress(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.progress: invalid handle");
    game3d_asset_handle_service(handle, 1);
    return game3d_asset_handle_load_progress(handle);
}

/// @brief Return terminal error text, or "" on success / pending work. See header.
rt_string rt_game3d_asset_handle_get_error(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.error: invalid handle");
    game3d_asset_handle_service(handle, 1);
    return handle && handle->error ? handle->error : rt_const_cstr("");
}

/// @brief Cancel a pending request. Current completed handles intentionally no-op.
void rt_game3d_asset_handle_cancel(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.cancel: invalid handle");
    if (!handle || game3d_asset_handle_is_ready(handle))
        return;
    game3d_asset_handle_store_flag(&handle->cancelled, 1);
    game3d_asset_handle_store_flag(&handle->deferred, 0);
    game3d_asset_handle_store_progress(handle, 1.0);
    game3d_asset_assign_cstr(&handle->error, "cancelled");
    game3d_release_typed_ref(&handle->entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    game3d_release_typed_ref(&handle->model_template, RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID);
    game3d_asset_handle_store_flag(&handle->ready, 1);
}

/// @brief Return the entity result for entity-mode requests, or NULL. See header.
void *rt_game3d_asset_handle_get_entity(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.getEntity: invalid handle");
    game3d_asset_handle_complete_if_deferred(handle);
    if (!handle || !game3d_asset_handle_is_ready(handle) ||
        game3d_asset_handle_is_cancelled(handle) || game3d_string_has_bytes(handle->error))
        return NULL;
    return game3d_asset_handle_entity_ref(handle);
}

/// @brief Return the template result for template-mode requests, or NULL. See header.
void *rt_game3d_asset_handle_get_template(void *obj) {
    rt_game3d_asset_handle *handle =
        game3d_asset_handle_checked(obj, "Game3D.AssetHandle3D.getTemplate: invalid handle");
    game3d_asset_handle_complete_if_deferred(handle);
    if (!handle || !game3d_asset_handle_is_ready(handle) ||
        game3d_asset_handle_is_cancelled(handle) || game3d_string_has_bytes(handle->error))
        return NULL;
    return game3d_asset_handle_template_ref(handle);
}

/// @brief Warm the cache by scheduling a filesystem template load. See header.
void rt_game3d_assets_preload(rt_string path) {
    rt_game3d_asset_handle *handle = game3d_asset_handle_pending(path, 0, 1);
    if (!handle)
        return;
    game3d_asset_handle_service(handle, 1);
    game3d_release_ref((void **)&handle);
}

/// @brief Warm the cache by scheduling a packed-asset template load. See header.
void rt_game3d_assets_preload_asset(rt_string path) {
    rt_game3d_asset_handle *handle = game3d_asset_handle_pending(path, 1, 1);
    if (!handle)
        return;
    game3d_asset_handle_service(handle, 1);
    game3d_release_ref((void **)&handle);
}

/// @brief Release every cached model template and reset the cache count. See header.
void rt_game3d_assets_clear_cache(void) {
    rt_game3d_model_cache_entry *entries = NULL;
    int32_t entry_count = 0;
    for (;;) {
        int loading = 0;
        game3d_model_cache_lock();
        for (int32_t i = 0; i < g_game3d_model_cache_count; ++i) {
            if (g_game3d_model_cache[i].loading) {
                loading = 1;
                break;
            }
        }
        if (!loading)
            break;
        if (!game3d_model_cache_wait_locked_ms(RT_GAME3D_MODEL_CACHE_WAIT_TIMEOUT_MS)) {
            game3d_model_cache_unlock();
            rt_trap("Game3D.Assets3D.ClearCache: timed out waiting for in-flight model loads");
            return;
        }
        game3d_model_cache_unlock();
    }
    entries = g_game3d_model_cache;
    entry_count = g_game3d_model_cache_count;
    g_game3d_model_cache = NULL;
    g_game3d_model_cache_count = 0;
    g_game3d_model_cache_capacity = 0;
    g_game3d_model_cache_tick = 0;
    g_game3d_model_resident_bytes = 0;
    free(g_game3d_model_cache_victim_heap);
    g_game3d_model_cache_victim_heap = NULL;
    g_game3d_model_cache_victim_heap_capacity = 0;
    game3d_model_cache_advance_generation_locked();
    game3d_model_cache_notify_all();
    game3d_model_cache_unlock();

    for (int32_t i = 0; i < entry_count; ++i) {
        game3d_release_ref((void **)&entries[i].path);
        game3d_release_typed_ref(&entries[i].model_template, RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID);
    }
    free(entries);
}
