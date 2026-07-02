//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale.h
// Purpose: Public C API for the Viper.Localization.Locale class — an immutable
//          reference-counted handle representing a BCP-47 language tag. Holds
//          a non-owning pointer to the locale-data record registered with
//          LocaleManager; the data pointer may be NULL when the locale has
//          been parsed but not registered (info queries then fall through to
//          the invariant defaults).
//
// Key invariants:
//   - Fields `language`, `script`, `region`, `tag` are small fixed-size char
//     buffers so the entire struct is 80 bytes and value-equality is
//     memcmp-friendly for the tag prefix. BCP-47 subtags fit within the
//     allotted capacity per RFC 5646 §2.2.
//   - `language` is always lowercase; `script` is Title-case (Xxxx); `region`
//     is UPPERCASE. Parse-time canonicalization guarantees these invariants.
//   - `data` is either NULL (unregistered) or a pointer to a rt_locale_data_t
//     owned by LocaleManager's registry; never a dangling pointer.
//
// Ownership/Lifetime:
//   - Handles are allocated via rt_obj_new_i64 and refcount-managed by the GC.
//   - Ownership of interned subtag/tag strings is the handle itself; strings
//     are embedded in the struct and freed with the handle.
//
// Links: src/runtime/localization/rt_locale_data.h (data record),
//        src/runtime/localization/rt_locale_manager.h (registry owner),
//        src/runtime/localization/rt_locale.c (implementation).
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

/// @brief In-struct capacity constants for BCP-47 subtag storage.
/// @details Capacities include the NUL terminator. Sized per RFC 5646 limits
///          with small margin: primary language up to 8 chars, script exactly
///          4 chars, region 2 letters or 3 digits, full tag max 35 chars.
#define RT_LOCALE_LANG_CAP 10
#define RT_LOCALE_SCRIPT_CAP 6
#define RT_LOCALE_REGION_CAP 6
#define RT_LOCALE_TAG_CAP 40

/// @brief Locale handle payload. One handle per parsed/loaded tag.
/// @details The runtime distributes instances via rt_obj_new_i64, so this is
///          the exact struct that follows the heap header. Do not copy these
///          bytes directly; always go through the accessor functions.
typedef struct rt_locale {
    char language[RT_LOCALE_LANG_CAP]; ///< lowercased primary subtag
    char script[RT_LOCALE_SCRIPT_CAP]; ///< Title-case script or ""
    char region[RT_LOCALE_REGION_CAP]; ///< UPPERCASE region or ""
    char tag[RT_LOCALE_TAG_CAP];       ///< canonical BCP-47 tag
    const rt_locale_data_t *data;      ///< non-owning; may be NULL
} rt_locale_t;

//===----------------------------------------------------------------------===//
// Constructors (all return a Locale handle with refcount 1)
//===----------------------------------------------------------------------===//

/// @brief Allocate a new default Locale handle (equivalent to Invariant()).
/// @details Used as the zero-arg constructor by the IL class surface; yields
///          the "root" locale with empty language/script/region. Always
///          populates `data` with the baked invariant pointer (en-US's
///          nearest upstream fallback, which is itself en-US in the baked
///          table for now).
/// @return New Locale handle; never NULL (traps on allocation failure).
void *rt_locale_new(void);

/// @brief Parse a BCP-47 tag; traps on invalid input.
/// @details Accepts common user input shapes ("en", "en-US", "EN_us",
///          "en-Latn-US") and normalizes to canonical form.
/// @param tag Input string; NUL-safe but content is validated.
/// @return New Locale handle.
void *rt_locale_parse(rt_string tag);

/// @brief Parse a BCP-47 tag; returns NULL on failure (no trap).
/// @details Use when the caller wants to test candidate tags without a
///          try/catch dance.
/// @param tag Input string; may be NULL.
/// @return New Locale handle or NULL.
void *rt_locale_try_parse(rt_string tag);

/// @brief Parse a BCP-47 tag as an Option.
/// @details Returns `Some(Locale)` for valid tags and `None` for NULL, empty,
///          or invalid input.
/// @param tag Input string; may be NULL.
/// @return Opaque Viper.Option object.
void *rt_locale_try_parse_option(rt_string tag);

/// @brief Build a Locale from pre-split subtag components.
/// @details Each subtag is validated independently; script and region may
///          be empty strings to indicate absence. Canonicalization is
///          applied to each component.
/// @param language Primary language subtag (required).
/// @param script Script subtag or empty string.
/// @param region Region subtag or empty string.
/// @return New Locale handle; traps on invalid input.
void *rt_locale_from_parts(rt_string language, rt_string script, rt_string region);

/// @brief Return the invariant ("root") locale handle.
/// @details Used as the top of every fallback chain; its data pointer refers
///          to the baked en-US record so user-facing queries still produce
///          sensible defaults. Always returns a fresh handle (not a shared
///          singleton) so caller lifetime management is uniform.
void *rt_locale_invariant(void);

//===----------------------------------------------------------------------===//
// Property accessors
//===----------------------------------------------------------------------===//

/// @brief Primary language subtag (lowercased). Empty for invariant locale.
rt_string rt_locale_language(void *locale);

/// @brief Script subtag (Title-case) or empty string when absent.
rt_string rt_locale_script(void *locale);

/// @brief Region subtag (uppercase) or empty string when absent.
rt_string rt_locale_region(void *locale);

/// @brief Canonical BCP-47 tag (e.g. "en-US" / "zh-Hans-CN" / "root").
rt_string rt_locale_tag(void *locale);

//===----------------------------------------------------------------------===//
// Comparisons and derived queries
//===----------------------------------------------------------------------===//

/// @brief Compare two locales by canonical tag.
/// @details NULL-tolerant on both sides; two NULLs compare equal. Script and
///          region emptiness affects the tag and therefore the comparison.
/// @return 1 if equal, 0 otherwise.
int8_t rt_locale_equals(void *a, void *b);

/// @brief Produce the walk-order fallback chain for this locale.
/// @details For `en-Latn-US` returns `[en-Latn-US, en-US, en, root]`;
///          for `en-US` returns `[en-US, en, root]`; for the invariant
///          locale returns `[root]`. The returned List owns strong
///          references to fresh Locale handles — caller is responsible
///          for the List (GC-managed) which in turn owns the handles.
/// @return New rt_list containing obj Locale handles.
void *rt_locale_fallbacks(void *locale);

/// @brief Return the canonical tag as an rt_string. Alias for rt_locale_tag.
/// @details Provided so callers don't need to special-case "ToString".
rt_string rt_locale_to_string(void *locale);

//===----------------------------------------------------------------------===//
// Internal helpers used by LocaleManager / LocaleInfo
//===----------------------------------------------------------------------===//

/// @brief Attach a locale-data record to a handle.
/// @details Called by LocaleManager immediately after registering the data
///          so the Locale's queries short-circuit to the right record. Safe
///          to call with NULL to detach; the record is non-owning.
void rt_locale_bind_data(void *locale, const rt_locale_data_t *data);

/// @brief Internal parse helper that emits structured error codes.
/// @details Used by both rt_locale_parse and rt_locale_try_parse to avoid
///          duplicating the validator. The `strict` flag controls whether
///          the function returns NULL or traps on invalid input.
/// @return New Locale handle on success, NULL on failure (when !strict).
void *rt_locale_parse_internal(rt_string tag, int strict);

/// @brief Find locale data for the given handle, falling back to invariant.
/// @details NULL-safe: returns the invariant data when `locale` is NULL or
///          its `data` pointer has not yet been bound.
const rt_locale_data_t *rt_locale_get_data(void *locale);

#ifdef __cplusplus
}
#endif
