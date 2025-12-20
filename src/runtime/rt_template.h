//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_template.h
// Purpose: Simple string templating with placeholder substitution.
// Key invariants: Placeholders {{key}} replaced with values from Map/Seq.
// Ownership/Lifetime: Returned strings are newly allocated.
// Links: docs/viperlib/text.md
//
// Placeholder syntax:
// - {{key}} - replaced with values.Get(key)
// - {{ key }} - whitespace around key is trimmed
// - {{}} - empty key, left as literal
// - Missing keys: left as-is (not replaced)
//
// Custom delimiters supported via RenderWith().
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Render template with Map values.
    /// @param tmpl Template string with {{key}} placeholders.
    /// @param values Map with string keys and string values.
    /// @return New string with placeholders replaced.
    /// @note Missing keys are left as-is. Traps on null template or values.
    rt_string rt_template_render(rt_string tmpl, void *values);

    /// @brief Render template with Seq values (positional).
    /// @param tmpl Template string with {{0}} {{1}} etc. placeholders.
    /// @param values Seq of strings.
    /// @return New string with placeholders replaced.
    /// @note Out-of-range indices are left as-is. Traps on null template or values.
    rt_string rt_template_render_seq(rt_string tmpl, void *values);

    /// @brief Render template with custom delimiters.
    /// @param tmpl Template string with placeholders.
    /// @param values Map with string keys and string values.
    /// @param prefix Placeholder prefix (e.g., "$" or "{{").
    /// @param suffix Placeholder suffix (e.g., "$" or "}}").
    /// @return New string with placeholders replaced.
    /// @note Traps on null template, values, prefix, or suffix.
    rt_string rt_template_render_with(rt_string tmpl, void *values, rt_string prefix,
                                      rt_string suffix);

    /// @brief Check if template contains a placeholder for key.
    /// @param tmpl Template string.
    /// @param key Placeholder key to check for.
    /// @return true if template contains {{key}}, false otherwise.
    /// @note Uses default {{ }} delimiters.
    bool rt_template_has(rt_string tmpl, rt_string key);

    /// @brief Extract all placeholder keys from template.
    /// @param tmpl Template string.
    /// @return Bag containing all unique placeholder keys.
    /// @note Uses default {{ }} delimiters.
    void *rt_template_keys(rt_string tmpl);

    /// @brief Escape {{ and }} in text for literal output.
    /// @param text Text to escape.
    /// @return New string with {{ escaped as {{{{ and }} escaped as }}}}.
    rt_string rt_template_escape(rt_string text);

#ifdef __cplusplus
}
#endif
