//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_csv.c
/// @brief CSV parsing and formatting utilities (RFC 4180 compliant).
///
/// This file implements CSV (Comma-Separated Values) parsing and formatting
/// that complies with RFC 4180. It handles all standard CSV features including
/// quoted fields, escaped quotes, and newlines within quoted fields.
///
/// **CSV Format Rules (RFC 4180):**
///
/// 1. Each record is on a separate line, delimited by a line break (CRLF/LF/CR)
/// 2. Fields are separated by commas (or a custom delimiter)
/// 3. Fields containing special characters must be enclosed in double-quotes:
///    - The delimiter character (`,`)
///    - Double-quote (`"`)
///    - Newline (`\n` or `\r`)
/// 4. Double-quotes within a quoted field are escaped by doubling: `""`
///
/// **Parsing Example:**
/// ```
/// Input:  name,age,"city, state"
/// Result: ["name", "age", "city, state"]
///
/// Input:  "He said ""Hello""",42
/// Result: ["He said \"Hello\"", "42"]
/// ```
///
/// **Formatting Example:**
/// ```
/// Input:  ["name", "city, state", "say \"hi\""]
/// Output: name,"city, state","say ""hi"""
/// ```
///
/// **Data Structure:**
/// - A single row is represented as a Seq of strings
/// - Multiple rows are represented as a Seq of Seqs (Seq<Seq<String>>)
///
/// ```
/// CSV Text                    Viper Structure
/// ─────────────────────       ────────────────────────────────
/// name,age,city               Seq [
/// Alice,30,NYC                  Seq ["name", "age", "city"],
/// Bob,25,LA                     Seq ["Alice", "30", "NYC"],
///                               Seq ["Bob", "25", "LA"]
///                             ]
/// ```
///
/// **Use Cases:**
/// - Importing data from spreadsheets
/// - Exporting data for spreadsheet applications
/// - Data interchange with other applications
/// - Reading configuration files
/// - Parsing log files in CSV format
///
/// **Thread Safety:** All functions are thread-safe (no global mutable state).
///
/// @see rt_seq.c For the Seq container used for rows and fields
///
//===----------------------------------------------------------------------===//

#include "rt_csv.h"

#include "rt_box.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/// Default CSV delimiter.
#define DEFAULT_DELIMITER ','

/// @brief Extract an rt_string from a value that may be a boxed string or raw string.
/// @details Checks if the pointer is a boxed string (tag == RT_BOX_STR) and unboxes it,
///          otherwise treats it as a raw rt_string.
static rt_string csv_extract_string(void *val)
{
    if (!val)
        return NULL;
    // Check if the value is a boxed string (first 8 bytes = tag 0-3)
    int64_t tag = *(int64_t *)val;
    if (tag == RT_BOX_STR)
        return rt_unbox_str(val);
    // Treat as raw rt_string
    return (rt_string)val;
}

/// @brief Get delimiter character from string.
static char get_delim(rt_string delim)
{
    const char *s = rt_string_cstr(delim);
    if (s && s[0] != '\0')
        return s[0];
    return DEFAULT_DELIMITER;
}

/// @brief Check if field needs quoting for CSV output.
static bool needs_quoting(const char *field, size_t len, char delim)
{
    for (size_t i = 0; i < len; i++)
    {
        char c = field[i];
        if (c == delim || c == '"' || c == '\n' || c == '\r')
            return true;
    }
    return false;
}

//=============================================================================
// Parsing Implementation
//=============================================================================

/// @brief Parse state for RFC 4180 CSV parsing.
typedef struct
{
    const char *input; ///< Input string.
    size_t len;        ///< Total length.
    size_t pos;        ///< Current position.
    char delim;        ///< Delimiter character.
} csv_parser;

/// @brief Initialize parser state.
static void parser_init(csv_parser *p, const char *input, size_t len, char delim)
{
    p->input = input;
    p->len = len;
    p->pos = 0;
    p->delim = delim;
}

/// @brief Check if parser is at end of input.
static bool parser_eof(csv_parser *p)
{
    return p->pos >= p->len;
}

/// @brief Peek current character without advancing.
static char parser_peek(csv_parser *p)
{
    if (p->pos >= p->len)
        return '\0';
    return p->input[p->pos];
}

/// @brief Consume current character and advance.
static char parser_consume(csv_parser *p)
{
    if (p->pos >= p->len)
        return '\0';
    return p->input[p->pos++];
}

/// @brief Parse a single field (possibly quoted).
/// @param p Parser state.
/// @param at_line_end Output: set to true if field ends at line boundary.
/// @return Newly allocated field string.
static rt_string parse_field(csv_parser *p, bool *at_line_end)
{
    *at_line_end = false;

    // EOF case - return empty field and signal end of line
    if (parser_eof(p))
    {
        *at_line_end = true;
        return rt_string_from_bytes("", 0);
    }

    // Check for quoted field
    if (parser_peek(p) == '"')
    {
        parser_consume(p); // consume opening quote

        // Build field content with escaped quotes handled
        size_t cap = 64;
        size_t len = 0;
        char *buf = (char *)malloc(cap);
        if (!buf)
            rt_trap("Csv.Parse: memory allocation failed");

        while (!parser_eof(p))
        {
            char c = parser_consume(p);

            if (c == '"')
            {
                // Check for escaped quote
                if (parser_peek(p) == '"')
                {
                    // Escaped quote - consume and add single quote
                    parser_consume(p);
                    if (len + 1 >= cap)
                    {
                        cap *= 2;
                        char *tmp = (char *)realloc(buf, cap);
                        if (!tmp)
                        {
                            free(buf);
                            rt_trap("Csv.Parse: memory allocation failed");
                        }
                        buf = tmp;
                    }
                    buf[len++] = '"';
                }
                else
                {
                    // End of quoted field
                    break;
                }
            }
            else
            {
                // Regular character (including newlines in quoted fields)
                if (len + 1 >= cap)
                {
                    cap *= 2;
                    char *tmp = (char *)realloc(buf, cap);
                    if (!tmp)
                    {
                        free(buf);
                        rt_trap("Csv.Parse: memory allocation failed");
                    }
                    buf = tmp;
                }
                buf[len++] = c;
            }
        }

        buf[len] = '\0';
        rt_string result = rt_string_from_bytes(buf, len);
        free(buf);

        // Skip to delimiter or line end
        if (!parser_eof(p))
        {
            char c = parser_peek(p);
            if (c == p->delim)
            {
                parser_consume(p);
            }
            else if (c == '\r')
            {
                parser_consume(p);
                if (parser_peek(p) == '\n')
                    parser_consume(p);
                *at_line_end = true;
            }
            else if (c == '\n')
            {
                parser_consume(p);
                *at_line_end = true;
            }
        }
        else
        {
            *at_line_end = true;
        }

        return result;
    }
    else
    {
        // Unquoted field - read until delimiter or line end
        size_t start = p->pos;
        while (!parser_eof(p))
        {
            char c = parser_peek(p);
            if (c == p->delim || c == '\r' || c == '\n')
                break;
            parser_consume(p);
        }
        size_t field_len = p->pos - start;
        rt_string result = rt_string_from_bytes(p->input + start, field_len);

        // Handle delimiter or line end
        if (!parser_eof(p))
        {
            char c = parser_peek(p);
            if (c == p->delim)
            {
                parser_consume(p);
            }
            else if (c == '\r')
            {
                parser_consume(p);
                if (parser_peek(p) == '\n')
                    parser_consume(p);
                *at_line_end = true;
            }
            else if (c == '\n')
            {
                parser_consume(p);
                *at_line_end = true;
            }
        }
        else
        {
            *at_line_end = true;
        }

        return result;
    }
}

/// @brief Parse a single row (line) of CSV.
static void *parse_row(csv_parser *p)
{
    void *row = rt_seq_new();
    bool at_line_end = false;

    // Use do-while to ensure we process trailing empty fields after delimiter
    do
    {
        rt_string field = parse_field(p, &at_line_end);
        rt_seq_push(row, (void *)field);
    } while (!at_line_end);

    return row;
}

//=============================================================================
// Formatting Implementation
//=============================================================================

/// @brief Format a single field for CSV output.
/// @param field Field string.
/// @param delim Delimiter character.
/// @param out Output buffer (must have enough space).
/// @return Number of bytes written.
static size_t format_field(const char *field, size_t field_len, char delim, char *out)
{
    if (!needs_quoting(field, field_len, delim))
    {
        // No quoting needed
        memcpy(out, field, field_len);
        return field_len;
    }

    // Need quoting
    size_t o = 0;
    out[o++] = '"';
    for (size_t i = 0; i < field_len; i++)
    {
        char c = field[i];
        if (c == '"')
        {
            out[o++] = '"';
            out[o++] = '"';
        }
        else
        {
            out[o++] = c;
        }
    }
    out[o++] = '"';
    return o;
}

/// @brief Calculate output size for a formatted field.
static size_t calc_field_size(const char *field, size_t field_len, char delim)
{
    if (!needs_quoting(field, field_len, delim))
        return field_len;

    // 2 for quotes + escaped quotes
    size_t size = 2;
    for (size_t i = 0; i < field_len; i++)
    {
        size += (field[i] == '"') ? 2 : 1;
    }
    return size;
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Parses a single line of CSV into a Seq of strings.
///
/// Parses one CSV record (line) using comma as the default delimiter. The
/// result is a Seq where each element is a string representing one field.
///
/// **Parsing rules:**
/// - Commas separate fields
/// - Quoted fields (`"..."`) preserve commas and can contain newlines
/// - Doubled quotes (`""`) within quoted fields become single quotes
///
/// **Example:**
/// ```
/// Dim fields = Csv.ParseLine("Alice,30,NYC")
/// ' fields = Seq ["Alice", "30", "NYC"]
///
/// Dim fields2 = Csv.ParseLine("name,""age"",""New York, NY""")
/// ' fields2 = Seq ["name", "age", "New York, NY"]
/// ```
///
/// @param line The CSV line to parse.
///
/// @return A Seq containing the parsed field strings. Returns an empty Seq
///         if line is NULL. Never returns NULL.
///
/// @note Uses comma (`,`) as the delimiter. For other delimiters, use
///       rt_csv_parse_line_with.
/// @note O(n) time complexity where n is the line length.
///
/// @see rt_csv_parse_line_with For custom delimiters
/// @see rt_csv_parse For parsing multiple lines
/// @see rt_csv_format_line For the inverse operation
void *rt_csv_parse_line(rt_string line)
{
    return rt_csv_parse_line_with(line, rt_const_cstr(","));
}

/// @brief Parses a single line of CSV with a custom delimiter.
///
/// Parses one CSV record using the specified delimiter character. The first
/// character of the delimiter string is used; if empty, defaults to comma.
///
/// **Common delimiters:**
/// | Delimiter | Description           | Use Case              |
/// |-----------|-----------------------|-----------------------|
/// | `,`       | Comma (default)       | Standard CSV          |
/// | `\t`      | Tab                   | TSV files             |
/// | `;`       | Semicolon             | European CSV          |
/// | `|`       | Pipe                  | Log files             |
///
/// **Example:**
/// ```
/// ' Tab-separated values
/// Dim fields = Csv.ParseLineWith("Alice\t30\tNYC", "\t")
/// ' fields = Seq ["Alice", "30", "NYC"]
///
/// ' Semicolon-separated (European format)
/// Dim fields2 = Csv.ParseLineWith("name;age;city", ";")
/// ```
///
/// @param line The CSV line to parse.
/// @param delim String whose first character is the delimiter. If empty
///              or NULL, defaults to comma.
///
/// @return A Seq containing the parsed field strings. Returns an empty Seq
///         if line is NULL. Never returns NULL.
///
/// @note Only the first character of delim is used.
/// @note O(n) time complexity where n is the line length.
///
/// @see rt_csv_parse_line For the default comma delimiter
/// @see rt_csv_parse_with For parsing multiple lines
void *rt_csv_parse_line_with(rt_string line, rt_string delim)
{
    const char *input = rt_string_cstr(line);
    if (!input)
        return rt_seq_new();

    size_t len = strlen(input);
    char d = get_delim(delim);

    csv_parser p;
    parser_init(&p, input, len, d);

    return parse_row(&p);
}

/// @brief Parses multi-line CSV text into a Seq of Seqs.
///
/// Parses complete CSV content containing multiple rows, using comma as the
/// default delimiter. The result is a Seq where each element is itself a Seq
/// of strings representing one row.
///
/// **Example:**
/// ```
/// Dim csv = "name,age,city" & vbLf & "Alice,30,NYC" & vbLf & "Bob,25,LA"
/// Dim rows = Csv.Parse(csv)
///
/// ' rows = Seq [
/// '   Seq ["name", "age", "city"],
/// '   Seq ["Alice", "30", "NYC"],
/// '   Seq ["Bob", "25", "LA"]
/// ' ]
///
/// For Each row In rows
///     For Each field In row
///         Print field & " | ";
///     Next
///     Print
/// Next
/// ```
///
/// **Line ending handling:**
/// - LF (`\n`): Unix/Linux/macOS
/// - CR (`\r`): Classic Mac
/// - CRLF (`\r\n`): Windows
///
/// @param text The CSV text containing one or more rows.
///
/// @return A Seq of Seqs, where each inner Seq contains the fields of one row.
///         Returns an empty Seq if text is NULL or empty. Never returns NULL.
///
/// @note Uses comma (`,`) as the delimiter. For other delimiters, use
///       rt_csv_parse_with.
/// @note O(n) time complexity where n is the total text length.
///
/// @see rt_csv_parse_with For custom delimiters
/// @see rt_csv_parse_line For parsing a single line
/// @see rt_csv_format For the inverse operation
void *rt_csv_parse(rt_string text)
{
    return rt_csv_parse_with(text, rt_const_cstr(","));
}

/// @brief Parses multi-line CSV text with a custom delimiter.
///
/// Parses complete CSV content containing multiple rows, using the specified
/// delimiter character. The result is a Seq where each element is itself a
/// Seq of strings representing one row.
///
/// **Example:**
/// ```
/// ' Tab-separated values file
/// Dim tsv = "name\tage\tcity" & vbLf & "Alice\t30\tNYC"
/// Dim rows = Csv.ParseWith(tsv, "\t")
///
/// ' rows = Seq [
/// '   Seq ["name", "age", "city"],
/// '   Seq ["Alice", "30", "NYC"]
/// ' ]
/// ```
///
/// @param text The CSV text containing one or more rows.
/// @param delim String whose first character is the delimiter. If empty
///              or NULL, defaults to comma.
///
/// @return A Seq of Seqs, where each inner Seq contains the fields of one row.
///         Returns an empty Seq if text is NULL or empty. Never returns NULL.
///
/// @note Only the first character of delim is used.
/// @note O(n) time complexity where n is the total text length.
///
/// @see rt_csv_parse For the default comma delimiter
/// @see rt_csv_parse_line_with For parsing a single line
void *rt_csv_parse_with(rt_string text, rt_string delim)
{
    const char *input = rt_string_cstr(text);
    if (!input)
        return rt_seq_new();

    size_t len = strlen(input);
    if (len == 0)
        return rt_seq_new();

    char d = get_delim(delim);

    csv_parser p;
    parser_init(&p, input, len, d);

    void *rows = rt_seq_new();

    while (!parser_eof(&p))
    {
        void *row = parse_row(&p);
        rt_seq_push(rows, row);
    }

    return rows;
}

/// @brief Formats a Seq of strings as a single CSV line.
///
/// Converts a sequence of field strings into a properly formatted CSV line.
/// Fields containing special characters (commas, quotes, newlines) are
/// automatically quoted, and internal quotes are escaped.
///
/// **Automatic quoting:**
/// A field is quoted if it contains:
/// - The delimiter character (`,`)
/// - Double-quote (`"`)
/// - Newline (`\n`) or carriage return (`\r`)
///
/// **Example:**
/// ```
/// Dim fields = Seq.Of("Alice", "30", "New York, NY")
/// Dim line = Csv.FormatLine(fields)
/// ' line = "Alice,30,\"New York, NY\""
///
/// Dim fields2 = Seq.Of("say \"hi\"", "normal", "with\nnewline")
/// Dim line2 = Csv.FormatLine(fields2)
/// ' line2 = "\"say \"\"hi\"\"\",normal,\"with\nnewline\""
/// ```
///
/// @param fields A Seq of strings to format as CSV.
///
/// @return A CSV-formatted string representing one row. Returns an empty
///         string if fields is NULL or empty.
///
/// @note Uses comma (`,`) as the delimiter. For other delimiters, use
///       rt_csv_format_line_with.
/// @note The returned string does NOT include a trailing newline.
/// @note O(n) time complexity where n is total character count.
///
/// @see rt_csv_format_line_with For custom delimiters
/// @see rt_csv_format For formatting multiple rows
/// @see rt_csv_parse_line For the inverse operation
rt_string rt_csv_format_line(void *fields)
{
    return rt_csv_format_line_with(fields, rt_const_cstr(","));
}

/// @brief Formats a Seq of strings as a CSV line with custom delimiter.
///
/// Converts a sequence of field strings into a properly formatted CSV line
/// using the specified delimiter. Fields containing special characters are
/// automatically quoted.
///
/// **Example:**
/// ```
/// ' Create tab-separated output
/// Dim fields = Seq.Of("Alice", "30", "NYC")
/// Dim line = Csv.FormatLineWith(fields, "\t")
/// ' line = "Alice\t30\tNYC"
///
/// ' Semicolon-separated (European format)
/// Dim line2 = Csv.FormatLineWith(Seq.Of("1.5", "2.5"), ";")
/// ' line2 = "1.5;2.5"
/// ```
///
/// @param fields A Seq of strings to format as CSV.
/// @param delim String whose first character is the delimiter. If empty
///              or NULL, defaults to comma.
///
/// @return A CSV-formatted string representing one row. Returns an empty
///         string if fields is NULL or empty.
///
/// @note Only the first character of delim is used.
/// @note The returned string does NOT include a trailing newline.
/// @note O(n) time complexity where n is total character count.
///
/// @see rt_csv_format_line For the default comma delimiter
/// @see rt_csv_format_with For formatting multiple rows
rt_string rt_csv_format_line_with(void *fields, rt_string delim)
{
    if (!fields)
        return rt_string_from_bytes("", 0);

    char d = get_delim(delim);
    int64_t count = rt_seq_len(fields);

    if (count == 0)
        return rt_string_from_bytes("", 0);

    // Calculate total output size
    size_t total_size = 0;
    for (int64_t i = 0; i < count; i++)
    {
        rt_string field = csv_extract_string(rt_seq_get(fields, i));
        const char *str = rt_string_cstr(field);
        if (!str)
            str = "";
        size_t field_len = strlen(str);
        total_size += calc_field_size(str, field_len, d);
        if (i < count - 1)
            total_size++; // delimiter
    }

    char *out = (char *)malloc(total_size + 1);
    if (!out)
        rt_trap("Csv.FormatLine: memory allocation failed");

    size_t pos = 0;
    for (int64_t i = 0; i < count; i++)
    {
        rt_string field = csv_extract_string(rt_seq_get(fields, i));
        const char *str = rt_string_cstr(field);
        if (!str)
            str = "";
        size_t field_len = strlen(str);
        pos += format_field(str, field_len, d, out + pos);
        if (i < count - 1)
            out[pos++] = d;
    }
    out[pos] = '\0';

    rt_string result = rt_string_from_bytes(out, pos);
    free(out);
    return result;
}

/// @brief Formats a Seq of Seqs as complete CSV text.
///
/// Converts a two-dimensional structure (rows of fields) into properly
/// formatted CSV text. Each row becomes a line in the output, with rows
/// separated by newline characters.
///
/// **Example:**
/// ```
/// Dim rows = Seq.New()
/// rows.Push(Seq.Of("name", "age", "city"))
/// rows.Push(Seq.Of("Alice", "30", "NYC"))
/// rows.Push(Seq.Of("Bob", "25", "LA"))
///
/// Dim csv = Csv.Format(rows)
/// ' csv = "name,age,city\nAlice,30,NYC\nBob,25,LA\n"
///
/// ' Write to file
/// Dim writer = LineWriter.Open("data.csv")
/// writer.Write(csv)
/// writer.Close()
/// ```
///
/// @param rows A Seq of Seqs, where each inner Seq contains the fields of
///             one row.
///
/// @return Complete CSV text with rows separated by newlines. Returns an
///         empty string if rows is NULL or empty.
///
/// @note Uses comma (`,`) as the delimiter. For other delimiters, use
///       rt_csv_format_with.
/// @note Each row ends with a newline character (`\n`).
/// @note O(n) time complexity where n is total character count.
///
/// @see rt_csv_format_with For custom delimiters
/// @see rt_csv_format_line For formatting a single row
/// @see rt_csv_parse For the inverse operation
rt_string rt_csv_format(void *rows)
{
    return rt_csv_format_with(rows, rt_const_cstr(","));
}

/// @brief Formats a Seq of Seqs as CSV text with custom delimiter.
///
/// Converts a two-dimensional structure (rows of fields) into properly
/// formatted CSV text using the specified delimiter.
///
/// **Example:**
/// ```
/// ' Create tab-separated values file
/// Dim rows = Seq.New()
/// rows.Push(Seq.Of("name", "age", "city"))
/// rows.Push(Seq.Of("Alice", "30", "NYC"))
///
/// Dim tsv = Csv.FormatWith(rows, "\t")
/// ' tsv = "name\tage\tcity\nAlice\t30\tNYC\n"
///
/// ' Create semicolon-separated file (European format)
/// Dim euCsv = Csv.FormatWith(rows, ";")
/// ' euCsv = "name;age;city\nAlice;30;NYC\n"
/// ```
///
/// @param rows A Seq of Seqs, where each inner Seq contains the fields of
///             one row.
/// @param delim String whose first character is the delimiter. If empty
///              or NULL, defaults to comma.
///
/// @return Complete CSV text with rows separated by newlines. Returns an
///         empty string if rows is NULL or empty.
///
/// @note Only the first character of delim is used.
/// @note Each row ends with a newline character (`\n`).
/// @note O(n) time complexity where n is total character count.
///
/// @see rt_csv_format For the default comma delimiter
/// @see rt_csv_format_line_with For formatting a single row
/// @see rt_csv_parse_with For the inverse operation
rt_string rt_csv_format_with(void *rows, rt_string delim)
{
    if (!rows)
        return rt_string_from_bytes("", 0);

    char d = get_delim(delim);
    int64_t row_count = rt_seq_len(rows);

    if (row_count == 0)
        return rt_string_from_bytes("", 0);

    // Calculate total output size
    size_t total_size = 0;
    for (int64_t r = 0; r < row_count; r++)
    {
        void *row = rt_seq_get(rows, r);
        if (!row)
            continue;

        int64_t count = rt_seq_len(row);
        for (int64_t i = 0; i < count; i++)
        {
            rt_string field = csv_extract_string(rt_seq_get(row, i));
            const char *str = rt_string_cstr(field);
            if (!str)
                str = "";
            size_t field_len = strlen(str);
            total_size += calc_field_size(str, field_len, d);
            if (i < count - 1)
                total_size++; // delimiter
        }
        total_size++; // newline
    }

    char *out = (char *)malloc(total_size + 1);
    if (!out)
        rt_trap("Csv.Format: memory allocation failed");

    size_t pos = 0;
    for (int64_t r = 0; r < row_count; r++)
    {
        void *row = rt_seq_get(rows, r);
        if (!row)
        {
            out[pos++] = '\n';
            continue;
        }

        int64_t count = rt_seq_len(row);
        for (int64_t i = 0; i < count; i++)
        {
            rt_string field = csv_extract_string(rt_seq_get(row, i));
            const char *str = rt_string_cstr(field);
            if (!str)
                str = "";
            size_t field_len = strlen(str);
            pos += format_field(str, field_len, d, out + pos);
            if (i < count - 1)
                out[pos++] = d;
        }
        out[pos++] = '\n';
    }
    out[pos] = '\0';

    rt_string result = rt_string_from_bytes(out, pos);
    free(out);
    return result;
}
