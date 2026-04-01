//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_config.h
// Purpose: Typed config loader wrapping JSON with dotted path access + defaults.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_config_load(void *path);
void *rt_config_from_string(void *json_str);
int64_t rt_config_get_int(void *cfg, void *path, int64_t default_val);
void *rt_config_get_str(void *cfg, void *path, void *default_val);
int8_t rt_config_get_bool(void *cfg, void *path, int8_t default_val);
int8_t rt_config_has(void *cfg, void *path);

#ifdef __cplusplus
}
#endif
