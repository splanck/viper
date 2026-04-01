//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_config.c
// Purpose: Typed game config loader wrapping JSON parse + JsonPath getters.
//
//===----------------------------------------------------------------------===//

#include "rt_config.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// JSON parse and path query externs
extern void *rt_json_parse(rt_string text);
extern void *rt_jsonpath_get(void *root, rt_string path);
extern void *rt_jsonpath_get_str(void *root, rt_string path);
extern int64_t rt_jsonpath_get_int(void *root, rt_string path);
extern rt_string rt_const_cstr(const char *s);
extern rt_string rt_string_from_bytes(const char *data, size_t len);
extern rt_string rt_io_file_read_all_text(rt_string path);

typedef struct {
    void *json_root;
} config_impl;

static config_impl *get(void *cfg) {
    return (config_impl *)cfg;
}

void *rt_config_load(void *path) {
    if (!path)
        return NULL;
    rt_string text = rt_io_file_read_all_text((rt_string)path);
    if (!text)
        return NULL;
    void *root = rt_json_parse((rt_string)text);
    if (!root)
        return NULL;

    config_impl *cfg = (config_impl *)rt_obj_new_i64(0, (int64_t)sizeof(config_impl));
    if (!cfg)
        return NULL;
    memset(cfg, 0, sizeof(config_impl));
    cfg->json_root = root;
    return cfg;
}

void *rt_config_from_string(void *json_str) {
    if (!json_str)
        return NULL;
    void *root = rt_json_parse((rt_string)json_str);
    if (!root)
        return NULL;

    config_impl *cfg = (config_impl *)rt_obj_new_i64(0, (int64_t)sizeof(config_impl));
    if (!cfg)
        return NULL;
    memset(cfg, 0, sizeof(config_impl));
    cfg->json_root = root;
    return cfg;
}

int64_t rt_config_get_int(void *cfg, void *path, int64_t default_val) {
    if (!cfg || !path)
        return default_val;
    config_impl *c = get(cfg);
    if (!c->json_root)
        return default_val;
    void *val = rt_jsonpath_get(c->json_root, (rt_string)path);
    if (!val)
        return default_val;
    return rt_jsonpath_get_int(c->json_root, (rt_string)path);
}

void *rt_config_get_str(void *cfg, void *path, void *default_val) {
    if (!cfg || !path)
        return default_val;
    config_impl *c = get(cfg);
    if (!c->json_root)
        return default_val;
    void *val = rt_jsonpath_get_str(c->json_root, (rt_string)path);
    if (!val)
        return default_val;
    return val;
}

int8_t rt_config_get_bool(void *cfg, void *path, int8_t default_val) {
    if (!cfg || !path)
        return default_val;
    config_impl *c = get(cfg);
    if (!c->json_root)
        return default_val;
    void *val = rt_jsonpath_get(c->json_root, (rt_string)path);
    if (!val)
        return default_val;
    return rt_jsonpath_get_int(c->json_root, (rt_string)path) != 0;
}

int8_t rt_config_has(void *cfg, void *path) {
    if (!cfg || !path)
        return 0;
    config_impl *c = get(cfg);
    if (!c->json_root)
        return 0;
    return rt_jsonpath_get(c->json_root, (rt_string)path) != NULL;
}
