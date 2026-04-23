//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale_manager.h
// Purpose: Public C API for Viper.Localization.LocaleManager. Owns the
//          process-global registry mapping canonical BCP-47 tags to
//          rt_locale_data_t records; tracks the "current" and "system"
//          locale pointers; exposes search-path configuration and the
//          load/unload/reset lifecycle. Phase 1 ships the registry, the
//          bootstrap/init, and the builtin en-US registration. Phase 2
//          wires in JSON and VPA loaders.
//
// Key invariants:
//   - All mutation is serialized through a single process-global rwlock.
//   - Baked records (arena == NULL) are never freed; JSON/VPA records own
//     their arena and free it on Unload or Reset.
//   - Current and System pointers are Locale handles (refcounted). The
//     registry keeps strong references to them.
//   - Lookup by canonical tag is case-sensitive; callers must canonicalize
//     via Locale.Parse before asking.
//
// Ownership/Lifetime:
//   - Registry entries outlive every formatter/collator that captures
//     their data pointer. Unload traps when the record's formatter_refs
//     counter is non-zero.
//
// Links: src/runtime/localization/rt_locale_manager.c (implementation),
//        src/runtime/localization/rt_locale.h (consumer of lookup_data),
//        src/runtime/localization/rt_locale_platform.h (system detect).
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt.hpp"
#include "rt_locale_data.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//===----------------------------------------------------------------------===//
// Runtime-facing class surface
//===----------------------------------------------------------------------===//

/// @brief Return the current (process-wide) locale as a Locale handle.
/// @details Performs lazy init on first call: registers baked en-US, detects
///          the system locale, seeds current = system if detected-and-loaded
///          or en-US otherwise. Always returns a fresh Locale handle — the
///          caller owns the reference.
void *rt_locale_manager_current(void);

/// @brief Set the current (process-wide) locale.
/// @details Traps when the supplied locale has never been registered with
///          LoadFromJson / LoadFromAsset / LoadBuiltin. NULL traps.
void rt_locale_manager_set_current(void *locale);

/// @brief Return the detected system locale (may not be loaded).
/// @details Determined once during init via the platform adapter and cached
///          for the process. When detection fails the system pointer is the
///          invariant locale.
void *rt_locale_manager_system(void);

/// @brief Enumerate currently registered locale tags as an rt_list of strings.
void *rt_locale_manager_available(void);

/// @brief Check whether the given locale has been loaded into the registry.
int8_t rt_locale_manager_is_loaded(void *locale);

/// @brief Load locale data from a JSON file on the filesystem.
/// @details Phase 1: stub that traps with "not implemented". Phase 2 wires in
///          the JSON loader.
void rt_locale_manager_load_from_json(rt_string path);

/// @brief Soft variant: returns 0 on any failure; 1 on success. Never traps.
int8_t rt_locale_manager_try_load_from_json(rt_string path);

/// @brief Load locale data from a VPA-embedded asset by name.
/// @details Phase 1: stub that traps with "not implemented". Phase 2 wires in
///          the asset loader.
void rt_locale_manager_load_from_asset(rt_string name);

/// @brief Soft variant: returns 0 on any failure; 1 on success.
int8_t rt_locale_manager_try_load_from_asset(rt_string name);

/// @brief Register one of the C-baked locale records.
/// @details v1 only knows "en-US". Calling with anything else traps. Idempotent.
void rt_locale_manager_load_builtin(rt_string tag);

/// @brief High-level load: tries filesystem search paths first, then VPA
///        assets. Returns the registered Locale on success, NULL otherwise.
///        Phase 1 only resolves "en-US" (the baked locale) so the happy path
///        exists for demos; all other tags return NULL.
void *rt_locale_manager_load(rt_string tag);

/// @brief Return the active search path list joined by the platform
///        separator (":" on POSIX, ";" on Windows).
rt_string rt_locale_manager_search_path(void);

/// @brief Append a filesystem directory to the search path.
void rt_locale_manager_add_search_path(rt_string path);

/// @brief Remove a locale from the registry. Returns 0 when unload is
///        refused (locale currently selected, in use by formatter, or baked).
int8_t rt_locale_manager_unload(void *locale);

/// @brief Reset the registry to the initial state (en-US only).
/// @details Traps when any loaded record has live formatters — test code
///          should tear down formatters before calling Reset.
void rt_locale_manager_reset(void);

//===----------------------------------------------------------------------===//
// Internal helpers consumed by rt_locale.c and rt_locale_info.c
//===----------------------------------------------------------------------===//

/// @brief Lookup a locale-data record by canonical tag.
/// @details Read-locked access to the registry. Returns NULL when the tag is
///          not registered. The returned pointer is stable for the lifetime
///          of the registry entry (cleared by Unload/Reset).
const rt_locale_data_t *rt_locale_manager_lookup_data(const char *tag);

/// @brief Increment the live formatter count on a registered locale data
///        record. No-op when @p data is NULL or is the baked invariant.
void rt_locale_manager_retain_data(const rt_locale_data_t *data);

/// @brief Decrement the live formatter count on a registered locale data
///        record. No-op when @p data is NULL or is the baked invariant.
void rt_locale_manager_release_data(const rt_locale_data_t *data);

#ifdef __cplusplus
}
#endif
