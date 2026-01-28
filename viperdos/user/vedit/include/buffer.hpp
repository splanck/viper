#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file buffer.hpp
 * @brief Text buffer with line-based storage for the VEdit text editor.
 *
 * This file defines the text storage layer for VEdit, providing line-based
 * text management with support for file I/O and editing operations. The buffer
 * uses dynamically allocated lines that grow as needed.
 *
 * ## Architecture
 *
 * The VEdit editor is organized into layers:
 * - **Buffer** (this file): Raw text storage and low-level editing
 * - **Editor**: Cursor management, scrolling, high-level operations
 * - **View**: Rendering and user interface
 *
 * ## Storage Model
 *
 * Text is stored as an array of Line structures, each containing:
 * - A dynamically allocated character buffer
 * - Current text length
 * - Buffer capacity (for efficient appending)
 *
 * This model provides:
 * - O(1) access to any line by index
 * - Efficient insertion/deletion within a line
 * - Simple line splitting and joining for Enter/Backspace
 *
 * ## Memory Management
 *
 * Each line's text buffer is independently allocated and resized:
 * - Initial capacity is 256 bytes for new lines
 * - Capacity doubles when exceeded (amortized O(1) growth)
 * - Buffers are freed when lines are deleted or the buffer is cleared
 *
 * ## Limits
 *
 * The buffer enforces reasonable limits to prevent runaway memory usage:
 * - Maximum 10,000 lines (MAX_LINES)
 * - Maximum 4,096 characters per line (MAX_LINE_LENGTH)
 *
 * @see editor.hpp for cursor and editing operations
 * @see view.hpp for rendering
 */
//===----------------------------------------------------------------------===//

#include <stddef.h>

namespace vedit {

//===----------------------------------------------------------------------===//
// Constants
//===----------------------------------------------------------------------===//

/**
 * @brief Maximum number of lines the buffer can hold.
 *
 * This limit prevents unbounded memory growth when loading or creating
 * very large files. Files exceeding this limit will be truncated on load.
 */
constexpr int MAX_LINES = 10000;

/**
 * @brief Maximum length of a single line in characters.
 *
 * Lines longer than this are truncated during file loading. This limit
 * prevents memory issues with malformed files containing extremely long
 * lines without newlines.
 */
constexpr int MAX_LINE_LENGTH = 4096;

//===----------------------------------------------------------------------===//
// Line Structure
//===----------------------------------------------------------------------===//

/**
 * @brief Represents a single line of text in the buffer.
 *
 * Each line maintains its own dynamically allocated text buffer that
 * can grow as needed. The capacity field tracks the current allocation
 * size to determine when reallocation is needed.
 *
 * ## Memory Layout
 *
 * The text buffer is null-terminated, but `length` stores the actual
 * text length (excluding the null terminator) for efficient operations.
 * The `capacity` is always at least `length + 1` to accommodate the
 * null terminator.
 */
struct Line {
    char *text;   /**< Dynamically allocated text buffer (null-terminated). */
    int length;   /**< Current text length excluding null terminator. */
    int capacity; /**< Allocated buffer size in bytes. */
};

//===----------------------------------------------------------------------===//
// Buffer Class
//===----------------------------------------------------------------------===//

/**
 * @brief Manages a text document as a collection of lines.
 *
 * The Buffer class provides the fundamental text storage for VEdit,
 * handling file I/O, text insertion/deletion, and line management.
 * It tracks the modification state and current filename for save operations.
 *
 * ## Usage
 *
 * @code
 * Buffer buf;
 *
 * // Load a file
 * if (buf.load("/path/to/file.txt")) {
 *     printf("Loaded %d lines\n", buf.lineCount());
 * }
 *
 * // Edit text
 * buf.insertChar(0, 0, 'H');  // Insert 'H' at line 0, column 0
 * buf.insertNewline(0, 1);    // Split line after 'H'
 *
 * // Save changes
 * buf.save("/path/to/file.txt");
 * @endcode
 *
 * ## Invariants
 *
 * - The buffer always contains at least one line (empty if needed)
 * - All line indices passed to methods must be valid (0 to lineCount()-1)
 * - Column indices can range from 0 to lineLength(line) inclusive
 */
class Buffer {
  public:
    /**
     * @brief Constructs an empty buffer with one blank line.
     *
     * Allocates the line array and creates an initial empty line.
     * The buffer starts with no filename and unmodified state.
     */
    Buffer();

    /**
     * @brief Destroys the buffer, freeing all line memory.
     *
     * Frees each line's text buffer, then frees the line array itself.
     */
    ~Buffer();

    //=== File Operations ===//

    /**
     * @brief Loads a text file into the buffer.
     *
     * Replaces the current buffer contents with the contents of the
     * specified file. The buffer is cleared before loading. After a
     * successful load, the modification flag is cleared and the
     * filename is stored for subsequent save operations.
     *
     * ## Line Ending Handling
     *
     * The loader handles both Unix (LF) and Windows (CRLF) line endings:
     * - `\n`: Ends current line, starts new line
     * - `\r\n`: Treated as single line ending
     * - `\r` alone: Treated as line ending
     *
     * ## Limits
     *
     * - Lines exceeding MAX_LINE_LENGTH are truncated
     * - Files exceeding MAX_LINES have extra lines discarded
     * - Files not ending with a newline have their final line preserved
     *
     * @param filename Path to the file to load.
     * @return true if the file was loaded successfully, false on error.
     *
     * @note On failure, the buffer retains a single empty line.
     */
    bool load(const char *filename);

    /**
     * @brief Saves the buffer contents to a file.
     *
     * Writes all lines to the specified file, with Unix-style line endings
     * (LF) between lines. The file is created if it doesn't exist, or
     * truncated if it does.
     *
     * After a successful save, the modification flag is cleared and the
     * filename is updated to the new path.
     *
     * @param filename Path to save the file to.
     * @return true if the file was saved successfully, false on error.
     *
     * @note Empty lines are written as just a newline character.
     */
    bool save(const char *filename);

    /**
     * @brief Clears all buffer contents.
     *
     * Frees all line data and resets the buffer to contain a single
     * empty line. Clears the filename and modification flag.
     *
     * This is called internally by load() before reading a new file,
     * and by Editor::newFile() to create a blank document.
     */
    void clear();

    //=== Line Access ===//

    /**
     * @brief Returns the number of lines in the buffer.
     *
     * The line count is always at least 1, even for an empty buffer.
     *
     * @return Number of lines (1 to MAX_LINES).
     */
    int lineCount() const {
        return m_lineCount;
    }

    /**
     * @brief Returns the text content of a line.
     *
     * @param lineIdx Zero-based line index.
     * @return Pointer to null-terminated line text, or empty string if
     *         the index is out of range.
     *
     * @note The returned pointer is valid until the line is modified.
     */
    const char *lineText(int lineIdx) const;

    /**
     * @brief Returns the length of a line in characters.
     *
     * @param lineIdx Zero-based line index.
     * @return Number of characters in the line (excluding null terminator),
     *         or 0 if the index is out of range.
     */
    int lineLength(int lineIdx) const;

    //=== Editing Operations ===//

    /**
     * @brief Inserts a character at the specified position.
     *
     * Inserts the character at (line, col), shifting subsequent characters
     * right. The line buffer is automatically expanded if needed.
     *
     * @param line Line index (0 to lineCount()-1).
     * @param col  Column index (0 to lineLength(line)).
     * @param c    Character to insert.
     *
     * @note Sets the modified flag on success.
     */
    void insertChar(int line, int col, char c);

    /**
     * @brief Splits a line at the specified position.
     *
     * Creates a new line after the current line, moving text from the
     * split point to the end of the line into the new line.
     *
     * Example:
     * - Before: Line 0 = "Hello World" with split at col 5
     * - After:  Line 0 = "Hello", Line 1 = " World"
     *
     * @param line Line index to split.
     * @param col  Column position where the split occurs.
     *
     * @note Sets the modified flag on success.
     * @note Fails silently if MAX_LINES would be exceeded.
     */
    void insertNewline(int line, int col);

    /**
     * @brief Deletes a character or joins lines.
     *
     * If col is within the line, deletes the character at that position.
     * If col is at the end of the line and there's a next line, joins
     * the next line onto the current line (equivalent to Delete at EOL).
     *
     * @param line Line index.
     * @param col  Column index of character to delete.
     *
     * @note Sets the modified flag on success.
     */
    void deleteChar(int line, int col);

    /**
     * @brief Performs backspace operation at the specified position.
     *
     * If col > 0, deletes the character before the cursor position.
     * If col == 0 and line > 0, joins the current line with the previous line.
     *
     * The new cursor position is returned through the output parameters.
     *
     * @param line    Current line index.
     * @param col     Current column index.
     * @param newLine [out] Line index after backspace.
     * @param newCol  [out] Column index after backspace.
     */
    void backspace(int line, int col, int &newLine, int &newCol);

    /**
     * @brief Deletes an entire line.
     *
     * Removes the specified line and shifts subsequent lines up.
     * Will not delete the last remaining line (buffer always has at
     * least one line).
     *
     * @param lineIdx Index of line to delete.
     *
     * @note Sets the modified flag on success.
     */
    void deleteLine(int lineIdx);

    //=== State ===//

    /**
     * @brief Returns whether the buffer has unsaved changes.
     *
     * @return true if modified since last save/load, false otherwise.
     */
    bool isModified() const {
        return m_modified;
    }

    /**
     * @brief Clears the modification flag.
     *
     * Called after saving to indicate the buffer matches the file.
     */
    void clearModified() {
        m_modified = false;
    }

    /**
     * @brief Returns the current filename.
     *
     * @return Path of the loaded/saved file, or empty string if none.
     */
    const char *filename() const {
        return m_filename;
    }

  private:
    /**
     * @brief Ensures a line's buffer can hold the specified length.
     *
     * Reallocates the line's text buffer if needed to accommodate
     * `needed` characters plus a null terminator.
     *
     * @param lineIdx Line to check/resize.
     * @param needed  Required capacity in characters (excluding null).
     * @return true if capacity is sufficient or reallocation succeeded.
     */
    bool ensureCapacity(int lineIdx, int needed);

    /**
     * @brief Inserts a new empty line after the specified line.
     *
     * Shifts subsequent lines down and initializes the new line with
     * an empty buffer.
     *
     * @param afterLine Index of line after which to insert.
     * @return true on success, false if at MAX_LINES limit.
     */
    bool insertLine(int afterLine);

    Line *m_lines;        /**< Array of line structures. */
    int m_lineCount;      /**< Number of valid lines. */
    bool m_modified;      /**< True if buffer has unsaved changes. */
    char m_filename[256]; /**< Current file path. */
};

} // namespace vedit
