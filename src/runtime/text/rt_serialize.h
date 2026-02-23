//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_serialize.h
// Purpose: Unified serialization interface providing format-agnostic dispatch (JSON, TOML, YAML, INI, XML) via a format enum or auto-detection from file extension.
//
// Key invariants:
//   - Format is selected by enum or auto-detected from file extension.
//   - All serialize operations produce a string; deserialize produces a value.
//   - rt_serialize_auto detects the format from the file extension.
//   - Format-specific errors return NULL; no trapping on bad input.
//
// Ownership/Lifetime:
//   - Returned values and strings are newly allocated; caller must release.
//   - Input strings are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_serialize.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Format enumeration
    //=========================================================================

    /// @brief Supported serialization formats.
    typedef enum
    {
        RT_FORMAT_JSON = 0, ///< JSON (RFC 8259)
        RT_FORMAT_XML = 1,  ///< XML (subset)
        RT_FORMAT_YAML = 2, ///< YAML (1.2 subset)
        RT_FORMAT_TOML = 3, ///< TOML (v1.0)
        RT_FORMAT_CSV = 4   ///< CSV (RFC 4180)
    } rt_format_t;

    //=========================================================================
    // Unified Parse / Format
    //=========================================================================

    /// @brief Parse text in the specified format into a Viper value.
    /// @param text Input text.
    /// @param format Format enum (RT_FORMAT_JSON, etc.).
    /// @return Parsed value (Map, Seq, String, boxed primitive).
    ///         Returns NULL on parse error (check rt_serialize_error()).
    void *rt_serialize_parse(rt_string text, int64_t format);

    /// @brief Format a Viper value as text in the specified format.
    /// @param obj Value to format.
    /// @param format Format enum.
    /// @return Formatted string.
    rt_string rt_serialize_format(void *obj, int64_t format);

    /// @brief Format a Viper value as pretty-printed text.
    /// @param obj Value to format.
    /// @param format Format enum.
    /// @param indent Spaces per indentation level (ignored for CSV).
    /// @return Pretty-printed string.
    rt_string rt_serialize_format_pretty(void *obj, int64_t format, int64_t indent);

    /// @brief Check if text is valid for the specified format.
    /// @param text Text to validate.
    /// @param format Format enum.
    /// @return 1 if valid, 0 otherwise.
    int8_t rt_serialize_is_valid(rt_string text, int64_t format);

    //=========================================================================
    // Format Auto-Detection
    //=========================================================================

    /// @brief Detect the format of a text string.
    /// @details Heuristically detects the format based on content:
    ///          - Starts with '{' or '[' → JSON
    ///          - Starts with '<' → XML
    ///          - Contains '---' at start → YAML
    ///          - Contains '[section]' or 'key = value' → TOML
    ///          - Contains commas with no special markers → CSV
    /// @param text Text to detect.
    /// @return Format enum, or -1 if unrecognized.
    int64_t rt_serialize_detect(rt_string text);

    /// @brief Parse text by auto-detecting the format.
    /// @param text Input text.
    /// @return Parsed value, or NULL on error.
    void *rt_serialize_auto_parse(rt_string text);

    //=========================================================================
    // Round-Trip Conversion
    //=========================================================================

    /// @brief Convert between formats.
    /// @details Parses from source format and formats to target format.
    /// @param text Input text.
    /// @param from_format Source format enum.
    /// @param to_format Target format enum.
    /// @return Converted text in target format.
    rt_string rt_serialize_convert(rt_string text, int64_t from_format, int64_t to_format);

    //=========================================================================
    // Format Metadata
    //=========================================================================

    /// @brief Get the name of a format.
    /// @param format Format enum.
    /// @return Format name string ("json", "xml", "yaml", "toml", "csv").
    rt_string rt_serialize_format_name(int64_t format);

    /// @brief Get the MIME type for a format.
    /// @param format Format enum.
    /// @return MIME type string (e.g., "application/json").
    rt_string rt_serialize_mime_type(int64_t format);

    /// @brief Look up format by name (case-insensitive).
    /// @param name Format name ("json", "xml", "yaml", "toml", "csv").
    /// @return Format enum, or -1 if unrecognized.
    int64_t rt_serialize_format_from_name(rt_string name);

    //=========================================================================
    // Error Handling
    //=========================================================================

    /// @brief Get the last serialization error message.
    /// @return Error string, or empty string if no error.
    rt_string rt_serialize_error(void);

#ifdef __cplusplus
}
#endif
