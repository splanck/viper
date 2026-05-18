//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_config.h
// Purpose: Runtime bridge for Viper.Game.Config — a typed configuration loader
//   that parses JSON and exposes values via dotted-path lookups with caller-
//   supplied defaults for missing/mistyped keys.
//
// Key invariants:
//   - Config objects are heap-allocated opaque `void *` handles.
//   - Paths are dotted strings (e.g. "audio.volume"); a missing or wrong-typed
//     key yields the supplied default rather than trapping.
//
// Ownership/Lifetime:
//   - rt_config_load / rt_config_from_string return owned handles (GC-managed).
//   - rt_config_get_str returns a runtime string owned by the caller.
//
// Links: src/runtime/game/rt_config.c (implementation)
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Runtime class ID used to validate Config handles.
#define RT_CONFIG_CLASS_ID INT64_C(-0x510215)

/// @brief Load and parse a JSON config file. @return Config handle, or NULL.
void *rt_config_load(void *path);
/// @brief Parse a JSON string into a Config. @return Config handle, or NULL.
void *rt_config_from_string(void *json_str);
/// @brief Read an integer at dotted @p path, or @p default_val if absent/wrong type.
int64_t rt_config_get_int(void *cfg, void *path, int64_t default_val);
/// @brief Read a string at dotted @p path, or @p default_val if absent/wrong type.
/// @return A caller-owned runtime string.
void *rt_config_get_str(void *cfg, void *path, void *default_val);
/// @brief Read a boolean at dotted @p path, or @p default_val if absent/wrong type.
int8_t rt_config_get_bool(void *cfg, void *path, int8_t default_val);
/// @brief Test whether a value exists at dotted @p path.
int8_t rt_config_has(void *cfg, void *path);

#ifdef __cplusplus
}
#endif
