//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_csv.h
// Purpose: CSV parsing and formatting utilities (RFC 4180 compliant).
// Key invariants: Handles quoted fields, escaped quotes, newlines in quotes.
// Ownership/Lifetime: Returned Seq and strings are newly allocated.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Parse a single CSV line into a Seq of strings.
    /// @param line CSV line to parse.
    /// @return Newly allocated Seq containing field strings.
    /// @details Uses comma as default delimiter. Handles RFC 4180 quoting.
    void *rt_csv_parse_line(rt_string line);

    /// @brief Parse a single CSV line with custom delimiter.
    /// @param line CSV line to parse.
    /// @param delim Delimiter string (first character used).
    /// @return Newly allocated Seq containing field strings.
    void *rt_csv_parse_line_with(rt_string line, rt_string delim);

    /// @brief Parse multi-line CSV text into a Seq of Seq of strings.
    /// @param text Multi-line CSV text.
    /// @return Newly allocated Seq of rows, each row a Seq of field strings.
    /// @details Uses comma as default delimiter. Handles RFC 4180 quoting.
    void *rt_csv_parse(rt_string text);

    /// @brief Parse multi-line CSV text with custom delimiter.
    /// @param text Multi-line CSV text.
    /// @param delim Delimiter string (first character used).
    /// @return Newly allocated Seq of rows, each row a Seq of field strings.
    void *rt_csv_parse_with(rt_string text, rt_string delim);

    /// @brief Format a Seq of strings into a CSV line.
    /// @param fields Seq of strings to format.
    /// @return Newly allocated CSV line string.
    /// @details Uses comma as default delimiter. Quotes fields as needed.
    rt_string rt_csv_format_line(void *fields);

    /// @brief Format a Seq of strings into a CSV line with custom delimiter.
    /// @param fields Seq of strings to format.
    /// @param delim Delimiter string (first character used).
    /// @return Newly allocated CSV line string.
    rt_string rt_csv_format_line_with(void *fields, rt_string delim);

    /// @brief Format a Seq of Seq of strings into multi-line CSV text.
    /// @param rows Seq of rows, each row a Seq of field strings.
    /// @return Newly allocated multi-line CSV string.
    /// @details Uses comma as default delimiter. Quotes fields as needed.
    rt_string rt_csv_format(void *rows);

    /// @brief Format a Seq of Seq of strings with custom delimiter.
    /// @param rows Seq of rows, each row a Seq of field strings.
    /// @param delim Delimiter string (first character used).
    /// @return Newly allocated multi-line CSV string.
    rt_string rt_csv_format_with(void *rows, rt_string delim);

#ifdef __cplusplus
}
#endif
