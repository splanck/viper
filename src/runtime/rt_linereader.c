//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_linereader.c
// Purpose: Implement line-by-line text file reading.
//
// Handles CR, LF, and CRLF line endings:
// - LF (\n): Unix/Linux/macOS
// - CR (\r): Classic Mac
// - CRLF (\r\n): Windows
//
//===----------------------------------------------------------------------===//

#include "rt_linereader.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief LineReader implementation structure.
typedef struct rt_linereader_impl
{
    FILE *fp;       ///< File pointer.
    int8_t eof;     ///< EOF flag.
    int8_t closed;  ///< Closed flag.
    int peeked;     ///< Peeked character (-1 if none, or 0-255).
    int has_peeked; ///< Whether we have a peeked character.
} rt_linereader_impl;

/// @brief Finalizer callback invoked when a LineReader is garbage collected.
///
/// This function is automatically called by Viper's garbage collector when a
/// LineReader object becomes unreachable. It ensures that the underlying
/// operating system file handle is properly closed to prevent resource leaks.
///
/// The finalizer is a safety net - well-written programs should call
/// rt_linereader_close explicitly when done reading. However, if the program
/// forgets to close the reader or an exception occurs, this finalizer ensures
/// the file is eventually closed when the object is collected.
///
/// @param obj Pointer to the LineReader object being finalized. May be NULL (no-op).
///
/// @note This function is idempotent - calling it on an already-closed reader is safe.
/// @note The finalizer does not raise errors; it silently closes the file if open.
///
/// @see rt_linereader_close For explicit closure
/// @see rt_obj_set_finalizer For how finalizers are registered
static void rt_linereader_finalize(void *obj)
{
    if (!obj)
        return;
    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (lr->fp && !lr->closed)
    {
        fclose(lr->fp);
        lr->fp = NULL;
        lr->closed = 1;
    }
}

/// @brief Opens a text file for line-by-line reading.
///
/// Creates a new LineReader object connected to the specified file path. The file
/// is opened in text mode for reading. The returned LineReader provides convenient
/// methods for reading lines, characters, or the entire file content.
///
/// The LineReader is managed by Viper's garbage collector and will automatically
/// close when collected if not explicitly closed.
///
/// **Line ending handling:**
/// The LineReader handles all common line ending formats:
/// - LF (`\n`): Unix/Linux/macOS standard
/// - CR (`\r`): Classic Mac OS
/// - CRLF (`\r\n`): Windows/DOS standard
///
/// When reading lines, the line ending characters are stripped from the result.
///
/// **Usage example:**
/// ```
/// Dim reader = LineReader.Open("data.txt")
/// While Not reader.EOF()
///     Dim line = reader.Read()
///     Print line
/// Wend
/// reader.Close()
/// ```
///
/// @param path Viper string containing the file path. Must not be NULL.
///             Path is interpreted according to the OS (relative or absolute).
///
/// @return A pointer to a new LineReader object on success. On failure, traps
///         with an error message and returns NULL. Failure reasons include:
///         - NULL path string
///         - Invalid path string
///         - File cannot be opened (doesn't exist, no permission, etc.)
///         - Memory allocation failure
///
/// @note The LineReader reads the file sequentially - there is no seek operation.
/// @note Files are opened in text mode, which may affect newline handling on
///       some platforms.
/// @note Thread safety: Not thread-safe. Each thread should have its own LineReader.
///
/// @see rt_linereader_close For closing the reader
/// @see rt_linereader_read For reading lines
/// @see rt_linereader_read_all For reading the entire file
void *rt_linereader_open(rt_string path)
{
    if (!path)
    {
        rt_trap("LineReader.Open: null path");
        return NULL;
    }

    const char *path_str = rt_string_cstr(path);
    if (!path_str)
    {
        rt_trap("LineReader.Open: invalid path");
        return NULL;
    }

    FILE *fp = fopen(path_str, "r");
    if (!fp)
    {
        rt_trap("LineReader.Open: failed to open file");
        return NULL;
    }

    rt_linereader_impl *lr =
        (rt_linereader_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_linereader_impl));
    if (!lr)
    {
        fclose(fp);
        rt_trap("LineReader.Open: memory allocation failed");
        return NULL;
    }

    lr->fp = fp;
    lr->eof = 0;
    lr->closed = 0;
    lr->peeked = -1;
    lr->has_peeked = 0;
    rt_obj_set_finalizer(lr, rt_linereader_finalize);

    return lr;
}

/// @brief Explicitly closes a LineReader, releasing the underlying file handle.
///
/// Closes the file associated with this LineReader object. After calling close,
/// any subsequent read operations on this LineReader will trap with an error.
///
/// It is good practice to explicitly close readers when done, rather than relying
/// on the garbage collector:
/// - Immediate release of OS file handle resources
/// - Avoids potential resource exhaustion with many open files
/// - Makes program behavior more predictable
///
/// @param obj Pointer to a LineReader object. If NULL, this function is a no-op.
///
/// @note This function is idempotent - calling close on an already-closed
///       LineReader does nothing (no error).
/// @note After closing, the LineReader object still exists in memory but is
///       unusable. It will be freed when the garbage collector runs.
/// @note Thread safety: Not thread-safe. Don't close a reader while another
///       thread is using it.
///
/// @see rt_linereader_open For opening files
void rt_linereader_close(void *obj)
{
    if (!obj)
        return;

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (lr->fp && !lr->closed)
    {
        fclose(lr->fp);
        lr->fp = NULL;
        lr->closed = 1;
    }
}

/// @brief Internal helper: gets the next character, consuming any peeked character first.
///
/// This function provides a unified character-reading interface that handles
/// the internal peek buffer. When rt_linereader_peek_char or the CR/LF handling
/// logic needs to "put back" a character, it goes into the peek buffer. This
/// function checks that buffer first before reading from the file.
///
/// @param lr Pointer to the LineReader implementation. Must not be NULL.
///
/// @return The next character (0-255), or EOF if end of file is reached.
///
/// @note This is an internal function, not part of the public API.
static int lr_getc(rt_linereader_impl *lr)
{
    if (lr->has_peeked)
    {
        lr->has_peeked = 0;
        return lr->peeked;
    }
    return fgetc(lr->fp);
}

/// @brief Reads the next line from the file.
///
/// Reads characters from the current file position until a line ending (LF, CR,
/// or CRLF) is encountered or end-of-file is reached. The line ending characters
/// are consumed but NOT included in the returned string.
///
/// **Line ending handling in detail:**
/// - LF (`\n`): Consumed, line ends
/// - CR (`\r`): Consumed, line ends. If followed by LF, that's consumed too.
/// - CRLF (`\r\n`): Both characters consumed as a single line ending
/// - CR + other: CR consumed as line end, other char available for next read
///
/// **Memory management:**
/// The function uses a dynamically growing buffer (starting at 256 bytes) to
/// handle lines of any length. The returned string is a new Viper string that
/// the caller owns.
///
/// **EOF behavior:**
/// - If EOF is reached with content, returns that content and sets EOF flag
/// - If EOF is reached with no content (already at EOF), returns empty string
/// - Use rt_linereader_eof to check if more lines are available
///
/// **Example usage:**
/// ```
/// Dim reader = LineReader.Open("data.txt")
/// While Not reader.EOF()
///     Dim line = reader.Read()
///     If line <> "" Then Print line
/// Wend
/// reader.Close()
/// ```
///
/// @param obj Pointer to a LineReader object. Must not be NULL and reader must be open.
///
/// @return A Viper string containing the line content (without line ending).
///         Returns an empty string if at EOF or on error.
///         Traps if obj is NULL or reader is closed.
///
/// @note Very long lines may cause significant memory allocation.
/// @note Empty lines (just a line ending) return an empty string but EOF is not set.
/// @note Thread safety: Not thread-safe for the same LineReader object.
///
/// @see rt_linereader_read_char For character-by-character reading
/// @see rt_linereader_read_all For reading the entire remaining file
/// @see rt_linereader_eof For checking end-of-file status
rt_string rt_linereader_read(void *obj)
{
    if (!obj)
    {
        rt_trap("LineReader.Read: null reader");
        return rt_string_from_bytes("", 0);
    }

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (!lr->fp || lr->closed)
    {
        rt_trap("LineReader.Read: reader is closed");
        return rt_string_from_bytes("", 0);
    }

    // Dynamic buffer for the line
    size_t cap = 256;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf)
    {
        rt_trap("LineReader.Read: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    int c;
    while ((c = lr_getc(lr)) != EOF)
    {
        if (c == '\n')
        {
            // LF or end of CRLF - line complete
            break;
        }
        else if (c == '\r')
        {
            // CR - check for CRLF
            int next = fgetc(lr->fp);
            if (next != '\n' && next != EOF)
            {
                // Standalone CR, put back the next char
                lr->peeked = next;
                lr->has_peeked = 1;
            }
            // Either way, line is complete
            break;
        }
        else
        {
            // Regular character - add to buffer
            if (len >= cap - 1)
            {
                cap *= 2;
                char *new_buf = (char *)realloc(buf, cap);
                if (!new_buf)
                {
                    free(buf);
                    rt_trap("LineReader.Read: memory allocation failed");
                    return rt_string_from_bytes("", 0);
                }
                buf = new_buf;
            }
            buf[len++] = (char)c;
        }
    }

    if (c == EOF && len == 0)
    {
        // EOF with no content - set EOF flag
        lr->eof = 1;
        free(buf);
        return rt_string_from_bytes("", 0);
    }

    if (c == EOF)
    {
        // Got content but hit EOF
        lr->eof = 1;
    }

    rt_string result = rt_string_from_bytes(buf, len);
    free(buf);
    return result;
}

/// @brief Reads a single character from the file.
///
/// Reads and consumes one character from the current file position, advancing
/// the position by one byte. This provides byte-level access to the file content,
/// useful for implementing custom parsing or reading binary-like data.
///
/// Unlike rt_linereader_read, this function does NOT interpret line endings -
/// CR and LF are returned as their byte values (13 and 10 respectively).
///
/// @param obj Pointer to a LineReader object. Must not be NULL and reader must be open.
///
/// @return The character value (0-255) on success, or -1 if:
///         - End of file is reached (EOF flag is also set)
///         Traps and returns -1 if obj is NULL or reader is closed.
///
/// @note The EOF flag is set when end-of-file is encountered.
/// @note Thread safety: Not thread-safe for the same LineReader object.
///
/// @see rt_linereader_peek_char For reading without consuming
/// @see rt_linereader_read For line-oriented reading
int64_t rt_linereader_read_char(void *obj)
{
    if (!obj)
    {
        rt_trap("LineReader.ReadChar: null reader");
        return -1;
    }

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (!lr->fp || lr->closed)
    {
        rt_trap("LineReader.ReadChar: reader is closed");
        return -1;
    }

    int c = lr_getc(lr);
    if (c == EOF)
    {
        lr->eof = 1;
        return -1;
    }

    return (int64_t)(unsigned char)c;
}

/// @brief Peeks at the next character without consuming it.
///
/// Returns the next character that would be read, but does not advance the
/// file position. The peeked character will be returned again on the next
/// call to rt_linereader_read_char or consumed during rt_linereader_read.
///
/// Multiple calls to PeekChar without intervening reads return the same character.
///
/// **Use cases:**
/// - Look-ahead parsing (check next char before deciding how to proceed)
/// - Conditional reading (peek, then read only if condition is met)
/// - Implementing tokenizers that need to see the next character
///
/// **Example:**
/// ```
/// Dim c = reader.PeekChar()
/// If c = Asc("[") Then
///     ' Start of bracketed section
///     reader.ReadChar()  ' consume the '['
///     ParseBracketedContent(reader)
/// End If
/// ```
///
/// @param obj Pointer to a LineReader object. Must not be NULL and reader must be open.
///
/// @return The next character value (0-255) without consuming it, or -1 if:
///         - End of file is reached (EOF flag is also set)
///         Traps and returns -1 if obj is NULL or reader is closed.
///
/// @note The peeked character is stored internally; peeking multiple times
///       without reading returns the same value.
/// @note Thread safety: Not thread-safe for the same LineReader object.
///
/// @see rt_linereader_read_char For reading and consuming a character
int64_t rt_linereader_peek_char(void *obj)
{
    if (!obj)
    {
        rt_trap("LineReader.PeekChar: null reader");
        return -1;
    }

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (!lr->fp || lr->closed)
    {
        rt_trap("LineReader.PeekChar: reader is closed");
        return -1;
    }

    if (lr->has_peeked)
    {
        return (int64_t)lr->peeked;
    }

    int c = fgetc(lr->fp);
    if (c == EOF)
    {
        lr->eof = 1;
        return -1;
    }

    lr->peeked = c;
    lr->has_peeked = 1;
    return (int64_t)(unsigned char)c;
}

/// @brief Reads the entire remaining file content as a single string.
///
/// Reads all remaining bytes from the current file position to the end of
/// file and returns them as a Viper string. This is useful when you need
/// the complete file content at once, such as for:
/// - Loading configuration files
/// - Reading templates
/// - Processing small text files entirely in memory
///
/// After this call, the EOF flag is always set (the entire file has been consumed).
///
/// **Memory considerations:**
/// The entire remaining content is loaded into memory. For very large files,
/// this may consume significant memory. Consider using line-by-line reading
/// for large files or streaming processing.
///
/// **Example:**
/// ```
/// Dim reader = LineReader.Open("config.json")
/// Dim content = reader.ReadAll()
/// reader.Close()
/// Dim config = JSON.Parse(content)
/// ```
///
/// @param obj Pointer to a LineReader object. Must not be NULL and reader must be open.
///
/// @return A Viper string containing all remaining file content. Returns an
///         empty string if already at EOF or on error.
///         Traps if obj is NULL, reader is closed, or allocation fails.
///
/// @note Any previously peeked character is included at the start of the result.
/// @note The EOF flag is always set after this call.
/// @note Line endings are preserved as-is (no normalization).
/// @note Thread safety: Not thread-safe for the same LineReader object.
///
/// @see rt_linereader_read For line-by-line reading
/// @see rt_linereader_eof For checking end-of-file status
rt_string rt_linereader_read_all(void *obj)
{
    if (!obj)
    {
        rt_trap("LineReader.ReadAll: null reader");
        return rt_string_from_bytes("", 0);
    }

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (!lr->fp || lr->closed)
    {
        rt_trap("LineReader.ReadAll: reader is closed");
        return rt_string_from_bytes("", 0);
    }

    // Get current position and file size
    long pos = ftell(lr->fp);
    if (pos < 0)
        pos = 0;

    fseek(lr->fp, 0, SEEK_END);
    long end = ftell(lr->fp);
    fseek(lr->fp, pos, SEEK_SET);

    size_t remaining = (end > pos) ? (size_t)(end - pos) : 0;

    // Account for any peeked character
    size_t extra = lr->has_peeked ? 1 : 0;
    size_t total = remaining + extra;

    if (total == 0)
    {
        lr->eof = 1;
        return rt_string_from_bytes("", 0);
    }

    char *buf = (char *)malloc(total);
    if (!buf)
    {
        rt_trap("LineReader.ReadAll: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    size_t offset = 0;

    // Add peeked character first
    if (lr->has_peeked)
    {
        buf[offset++] = (char)lr->peeked;
        lr->has_peeked = 0;
    }

    // Read the rest
    size_t read = fread(buf + offset, 1, remaining, lr->fp);
    total = offset + read;

    lr->eof = 1;

    rt_string result = rt_string_from_bytes(buf, total);
    free(buf);
    return result;
}

/// @brief Checks whether the end of file has been reached.
///
/// Returns true if a previous read operation encountered the end of the file.
/// The EOF flag is set when:
/// - rt_linereader_read reaches EOF (either with or without content)
/// - rt_linereader_read_char returns -1
/// - rt_linereader_peek_char returns -1
/// - rt_linereader_read_all completes
///
/// **Typical usage pattern:**
/// ```
/// While Not reader.EOF()
///     Dim line = reader.Read()
///     ProcessLine(line)
/// Wend
/// ```
///
/// @param obj Pointer to a LineReader object. May be NULL.
///
/// @return 1 (true) if EOF has been reached, or if obj is NULL, or if reader
///         is closed. Returns 0 (false) if the reader is open and EOF has not
///         been encountered.
///
/// @note The EOF flag is "sticky" - once set, it remains set.
/// @note Thread safety: Not thread-safe for the same LineReader object.
///
/// @see rt_linereader_read For operations that set the EOF flag
int8_t rt_linereader_eof(void *obj)
{
    if (!obj)
        return 1;

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (!lr->fp || lr->closed)
        return 1;

    return lr->eof;
}
