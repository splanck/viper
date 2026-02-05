//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_ini.h
// Purpose: INI/config file parsing and formatting.
// Key invariants: Returns Map of Maps (section -> key -> value as string).
//                 Default (sectionless) entries go under empty string key "".
// Ownership/Lifetime: Returned maps are owned by the caller.
// Links: src/il/runtime/classes/RuntimeClasses.inc
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Parse an INI-format string into a Map of Maps.
    /// @param text INI content.
    /// @return Map where keys are section names and values are Maps of key-value pairs.
    ///         Entries without a section header are stored under "".
    void *rt_ini_parse(rt_string text);

    /// @brief Format a Map-of-Maps back to INI string format.
    /// @param ini_map Map of Maps as returned by rt_ini_parse.
    /// @return Formatted INI string.
    rt_string rt_ini_format(void *ini_map);

    /// @brief Get a value from a parsed INI map.
    /// @param ini_map The parsed INI map (Map of Maps).
    /// @param section Section name (use empty string for default section).
    /// @param key Key name.
    /// @return Value string, or empty string if not found.
    rt_string rt_ini_get(void *ini_map, rt_string section, rt_string key);

    /// @brief Set a value in a parsed INI map (creates section if needed).
    /// @param ini_map The parsed INI map (Map of Maps).
    /// @param section Section name.
    /// @param key Key name.
    /// @param value Value string.
    void rt_ini_set(void *ini_map, rt_string section, rt_string key, rt_string value);

    /// @brief Check if a section exists.
    /// @param ini_map The parsed INI map.
    /// @param section Section name.
    /// @return 1 if section exists, 0 otherwise.
    int8_t rt_ini_has_section(void *ini_map, rt_string section);

    /// @brief Get all section names as a Seq.
    /// @param ini_map The parsed INI map.
    /// @return Seq of section name strings.
    void *rt_ini_sections(void *ini_map);

    /// @brief Remove a key from a section.
    /// @param ini_map The parsed INI map.
    /// @param section Section name.
    /// @param key Key name.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_ini_remove(void *ini_map, rt_string section, rt_string key);

#ifdef __cplusplus
}
#endif
