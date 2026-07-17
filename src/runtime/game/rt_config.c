//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_config.c
// Purpose: Typed game config loader wrapping JSON parse + JsonPath getters.
//
//===----------------------------------------------------------------------===//

#include "rt_config.h"
#include "rt_bytes.h"
#include "rt_file_ext.h"
#include "rt_json.h"
#include "rt_jsonpath.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void *json_root;
} config_impl;

/// @brief Drop one GC reference to @p obj and free it if the count hit zero.
static void config_release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief GC finalizer: release the parsed JSON root when the Config is freed.
static void config_finalizer(void *obj) {
    config_impl *cfg = (config_impl *)obj;
    if (!cfg || !cfg->json_root)
        return;
    config_release_obj(cfg->json_root);
    cfg->json_root = NULL;
}

/// @brief Safe-cast a handle to the Config impl, trapping @p api on a class-id
///        mismatch. @return The impl, or NULL if @p cfg is NULL.
static config_impl *checked_config(void *cfg, const char *api) {
    if (!cfg)
        return NULL;
    if (rt_obj_class_id(cfg) != RT_CONFIG_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (config_impl *)cfg;
}

/// @brief Load a JSON config file and return a queryable config handle.
/// @details Reads the file, parses as JSON, and wraps the root node for typed
///          key lookups via JsonPath queries (e.g., "player.speed").
void *rt_config_load(void *path) {
    if (!path)
        return NULL;

    // rt_file_read_bytes now traps on a missing file (hardened I/O path),
    // but Config.Load's contract is "missing config → NULL so the caller
    // can fall back to defaults." Pre-check existence with the non-trapping
    // helper so a missing config stays a soft failure.
    if (!rt_io_file_exists((rt_string)path))
        return NULL;

    void *bytes = rt_file_read_bytes((rt_string)path);
    if (!bytes)
        return NULL;
    if (rt_bytes_len(bytes) == 0) {
        config_release_obj(bytes);
        return NULL;
    }

    rt_string text = rt_bytes_to_str(bytes);
    config_release_obj(bytes);
    if (!text || rt_str_len(text) == 0) {
        if (text)
            rt_string_unref(text);
        return NULL;
    }

    void *root = rt_json_parse((rt_string)text);
    rt_string_unref(text);
    if (!root)
        return NULL;

    config_impl *cfg =
        (config_impl *)rt_obj_new_i64(RT_CONFIG_CLASS_ID, (int64_t)sizeof(config_impl));
    if (!cfg) {
        config_release_obj(root);
        return NULL;
    }
    memset(cfg, 0, sizeof(config_impl));
    rt_obj_set_finalizer(cfg, config_finalizer);
    cfg->json_root = root;
    return cfg;
}

/// @brief Create a config from a JSON string (no file I/O).
void *rt_config_from_string(void *json_str) {
    if (!json_str)
        return NULL;
    void *root = rt_json_parse((rt_string)json_str);
    if (!root)
        return NULL;

    config_impl *cfg =
        (config_impl *)rt_obj_new_i64(RT_CONFIG_CLASS_ID, (int64_t)sizeof(config_impl));
    if (!cfg) {
        config_release_obj(root);
        return NULL;
    }
    memset(cfg, 0, sizeof(config_impl));
    rt_obj_set_finalizer(cfg, config_finalizer);
    cfg->json_root = root;
    return cfg;
}

/// @brief Get an integer value at a JsonPath, or default_val if missing.
int64_t rt_config_get_int(void *cfg, void *path, int64_t default_val) {
    if (!cfg || !path)
        return default_val;
    config_impl *c = checked_config(cfg, "Config.GetInt: expected Zanna.Game.Config");
    if (!c)
        return default_val;
    if (!c->json_root)
        return default_val;
    // Return the caller's default when the path is absent OR the present value is
    // not int-convertible (object/array/null, or non-numeric string), rather than
    // the coercing helper's 0 (VDOC-245). The try-variant resolves and releases the
    // node internally, so there is no retained-reference leak (VDOC-236).
    int64_t out = default_val;
    return rt_jsonpath_try_get_int(c->json_root, (rt_string)path, &out) ? out : default_val;
}

/// @brief Get a string value at a JsonPath, or default_val if missing.
void *rt_config_get_str(void *cfg, void *path, void *default_val) {
    if (!cfg || !path)
        return default_val;
    config_impl *c = checked_config(cfg, "Config.GetStr: expected Zanna.Game.Config");
    if (!c)
        return default_val;
    if (!c->json_root)
        return default_val;
    // Return the caller's default when the path is absent OR the present value is
    // not string-convertible (object/array/null), rather than the coercing helper's
    // empty string (VDOC-245). On success the try-variant yields a fresh
    // caller-owned string; it resolves/releases the node internally (VDOC-236).
    rt_string out = NULL;
    if (rt_jsonpath_try_get_str(c->json_root, (rt_string)path, &out))
        return out;
    return default_val;
}

/// @brief Get a boolean value at a JsonPath, or default_val if missing.
int8_t rt_config_get_bool(void *cfg, void *path, int8_t default_val) {
    if (!cfg || !path)
        return default_val;
    config_impl *c = checked_config(cfg, "Config.GetBool: expected Zanna.Game.Config");
    if (!c)
        return default_val;
    if (!c->json_root)
        return default_val;
    // Boolean coercion goes through the same int-convertibility check: absent or a
    // non-int-convertible value yields the caller's default rather than false
    // (VDOC-245); a convertible value is truthy when nonzero.
    int64_t out = 0;
    if (rt_jsonpath_try_get_int(c->json_root, (rt_string)path, &out))
        return out != 0;
    return default_val;
}

/// @brief Check whether a JsonPath key exists in the config.
int8_t rt_config_has(void *cfg, void *path) {
    if (!cfg || !path)
        return 0;
    config_impl *c = checked_config(cfg, "Config.Has: expected Zanna.Game.Config");
    if (!c)
        return 0;
    if (!c->json_root)
        return 0;
    // rt_jsonpath_has is the non-retaining existence check; the prior
    // rt_jsonpath_get probe leaked a retained reference on every hit (VDOC-236).
    return rt_jsonpath_has(c->json_root, (rt_string)path);
}

/// @brief Return the borrowed parsed JSON root of a Config (internal/testing).
void *rt_config_json_root(void *cfg) {
    if (!cfg)
        return NULL;
    if (rt_obj_class_id(cfg) != RT_CONFIG_CLASS_ID)
        return NULL;
    return ((config_impl *)cfg)->json_root;
}
