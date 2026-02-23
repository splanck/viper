//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_template.h
// Purpose: Simple string templating with {{key}} placeholder substitution from a Map or sequential Seq, supporting custom delimiters via RenderWith.
//
// Key invariants:
//   - Placeholders have the form {{key}}; whitespace around key is trimmed.
//   - Missing keys are left as literal placeholder text (not replaced).
//   - Empty placeholders {{}} are left as-is.
//   - Custom delimiters can be configured via rt_template_render_with.
//
// Ownership/Lifetime:
//   - Returned strings are newly allocated; caller must release.
//   - Input template and value strings are borrowed for the duration of rendering.
//
// Links: src/runtime/text/rt_template.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

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
    rt_string rt_template_render_with(rt_string tmpl,
                                      void *values,
                                      rt_string prefix,
                                      rt_string suffix);

    /// @brief Check if template contains a placeholder for key.
    /// @param tmpl Template string.
    /// @param key Placeholder key to check for.
    /// @return 1 if template contains {{key}}, 0 otherwise.
    /// @note Uses default {{ }} delimiters.
    int8_t rt_template_has(rt_string tmpl, rt_string key);

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
