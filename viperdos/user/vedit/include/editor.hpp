#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file editor.hpp
 * @brief Editor state and cursor management for VEdit.
 *
 * This file defines the Editor class which manages the editing state for VEdit,
 * including cursor position, scrolling, and configuration. The Editor sits
 * between the low-level Buffer and the high-level View, coordinating text
 * operations with cursor updates.
 *
 * ## Architecture
 *
 * ```
 * +-------+    +--------+    +--------+
 * | View  | -> | Editor | -> | Buffer |
 * +-------+    +--------+    +--------+
 *   UI/Input   Cursor/State   Text Data
 * ```
 *
 * The Editor class:
 * - Owns the Buffer (text storage)
 * - Maintains cursor position (line, column)
 * - Manages scroll position (viewport into document)
 * - Holds configuration settings
 *
 * ## Cursor Model
 *
 * The cursor position is represented as (line, column) where:
 * - `line`: 0-based line index into the buffer
 * - `col`: 0-based character offset within the line
 *
 * The cursor can be positioned at the end of a line (col == lineLength)
 * for appending text, but cannot exceed that position.
 *
 * ## Scroll Position
 *
 * The scroll position defines the top-left corner of the visible area:
 * - `scrollY`: First visible line index
 * - `scrollX`: First visible column (for horizontal scrolling)
 *
 * The View uses these values to determine which portion of the document
 * to render, and the Editor updates them to keep the cursor visible.
 *
 * @see buffer.hpp for text storage
 * @see view.hpp for rendering
 */
//===----------------------------------------------------------------------===//

#include "buffer.hpp"

namespace vedit {

//===----------------------------------------------------------------------===//
// Configuration
//===----------------------------------------------------------------------===//

/**
 * @brief Editor configuration settings.
 *
 * These settings control editor behavior and can be toggled through
 * the View menu. Changes take effect immediately on the next render.
 */
struct Config {
    /**
     * @brief Whether to display line numbers in the gutter.
     *
     * When true, a gutter area is drawn on the left side of the text
     * area showing line numbers. This reduces the available width for
     * text by LINE_NUMBER_WIDTH pixels.
     */
    bool showLineNumbers;

    /**
     * @brief Whether to wrap long lines.
     *
     * When true, lines longer than the visible width are wrapped to
     * multiple display lines. When false, horizontal scrolling is
     * used to view long lines.
     *
     * @note Currently not implemented; horizontal scrolling is always used.
     */
    bool wordWrap;

    /**
     * @brief Number of spaces to insert for Tab key.
     *
     * Controls how many space characters are inserted when the user
     * presses Tab. Typical values are 2, 4, or 8.
     */
    int tabWidth;
};

//===----------------------------------------------------------------------===//
// Editor Class
//===----------------------------------------------------------------------===//

/**
 * @brief Manages editing state including cursor, scroll, and buffer.
 *
 * The Editor class is the central coordinator for VEdit, connecting
 * user input (from main.cpp) with text storage (Buffer) and providing
 * state information for rendering (View).
 *
 * ## Usage
 *
 * @code
 * Editor editor;
 *
 * // Load a file
 * editor.loadFile("/path/to/file.txt");
 *
 * // Process user input
 * editor.moveCursorDown();
 * editor.insertChar('H');
 * editor.insertNewline();
 *
 * // Keep cursor visible after changes
 * editor.ensureCursorVisible(visibleLines, visibleCols);
 *
 * // Save changes
 * editor.saveFile();
 * @endcode
 *
 * ## Default Settings
 *
 * - Line numbers: enabled
 * - Word wrap: disabled
 * - Tab width: 4 spaces
 * - Cursor: line 0, column 0
 * - Scroll: (0, 0) - top-left of document
 */
class Editor {
  public:
    /**
     * @brief Constructs an editor with default settings.
     *
     * Initializes configuration, creates an empty buffer, and positions
     * the cursor at the origin (0, 0).
     */
    Editor();

    //=== Buffer Access ===//

    /**
     * @brief Returns a mutable reference to the text buffer.
     *
     * @return Reference to the Buffer for direct manipulation.
     */
    Buffer &buffer() {
        return m_buffer;
    }

    /**
     * @brief Returns a const reference to the text buffer.
     *
     * @return Const reference to the Buffer for read-only access.
     */
    const Buffer &buffer() const {
        return m_buffer;
    }

    //=== Cursor Position ===//

    /**
     * @brief Returns the current cursor line.
     *
     * @return Zero-based line index (0 to buffer.lineCount()-1).
     */
    int cursorLine() const {
        return m_cursorLine;
    }

    /**
     * @brief Returns the current cursor column.
     *
     * @return Zero-based column offset (0 to lineLength).
     */
    int cursorCol() const {
        return m_cursorCol;
    }

    //=== Scroll Position ===//

    /**
     * @brief Returns the vertical scroll offset.
     *
     * @return Index of the first visible line.
     */
    int scrollY() const {
        return m_scrollY;
    }

    /**
     * @brief Returns the horizontal scroll offset.
     *
     * @return Column offset of the first visible character.
     */
    int scrollX() const {
        return m_scrollX;
    }

    //=== Configuration ===//

    /**
     * @brief Returns a mutable reference to the configuration.
     *
     * @return Reference to Config for reading/modifying settings.
     */
    Config &config() {
        return m_config;
    }

    /**
     * @brief Returns a const reference to the configuration.
     *
     * @return Const reference to Config for read-only access.
     */
    const Config &config() const {
        return m_config;
    }

    //=== Cursor Movement ===//

    /**
     * @brief Moves the cursor one character left.
     *
     * If the cursor is at the beginning of a line (column 0) and not
     * on the first line, moves to the end of the previous line.
     */
    void moveCursorLeft();

    /**
     * @brief Moves the cursor one character right.
     *
     * If the cursor is at the end of a line and not on the last line,
     * moves to the beginning of the next line.
     */
    void moveCursorRight();

    /**
     * @brief Moves the cursor one line up.
     *
     * Clamps the column if the new line is shorter than the current
     * cursor column position.
     */
    void moveCursorUp();

    /**
     * @brief Moves the cursor one line down.
     *
     * Clamps the column if the new line is shorter than the current
     * cursor column position.
     */
    void moveCursorDown();

    /**
     * @brief Moves the cursor to the beginning of the current line.
     *
     * Sets column to 0, line remains unchanged.
     */
    void moveCursorHome();

    /**
     * @brief Moves the cursor to the end of the current line.
     *
     * Sets column to the line length, line remains unchanged.
     */
    void moveCursorEnd();

    /**
     * @brief Moves the cursor up by one page.
     *
     * @param pageSize Number of lines in a page (typically visibleLines).
     */
    void moveCursorPageUp(int pageSize);

    /**
     * @brief Moves the cursor down by one page.
     *
     * @param pageSize Number of lines in a page (typically visibleLines).
     */
    void moveCursorPageDown(int pageSize);

    /**
     * @brief Moves the cursor to a specific line.
     *
     * @param line Target line index (clamped to valid range).
     */
    void moveCursorToLine(int line);

    //=== Editing Operations ===//

    /**
     * @brief Inserts a character at the cursor position.
     *
     * The cursor advances one position after insertion.
     *
     * @param c Character to insert.
     */
    void insertChar(char c);

    /**
     * @brief Inserts a newline at the cursor position.
     *
     * Splits the current line at the cursor, creating a new line below.
     * The cursor moves to the beginning of the new line.
     */
    void insertNewline();

    /**
     * @brief Deletes the character at the cursor position (Delete key).
     *
     * If at the end of a line, joins with the next line.
     */
    void deleteChar();

    /**
     * @brief Deletes the character before the cursor (Backspace key).
     *
     * If at the beginning of a line, joins with the previous line.
     * The cursor moves to the deletion point.
     */
    void backspace();

    /**
     * @brief Inserts spaces equivalent to a tab.
     *
     * Inserts `config.tabWidth` space characters at the cursor position.
     */
    void insertTab();

    //=== Scrolling ===//

    /**
     * @brief Adjusts scroll position to keep cursor visible.
     *
     * Called after cursor movement or editing to ensure the cursor
     * remains within the visible area of the text region.
     *
     * @param visibleLines Number of lines that fit in the text area.
     * @param visibleCols  Number of columns that fit in the text area.
     */
    void ensureCursorVisible(int visibleLines, int visibleCols);

    /**
     * @brief Scrolls to show a specific line at the top.
     *
     * @param line Line index to scroll to (clamped to valid range).
     */
    void scrollTo(int line);

    //=== File Operations ===//

    /**
     * @brief Loads a file into the buffer.
     *
     * Replaces buffer contents with the file and resets cursor/scroll
     * to the origin.
     *
     * @param filename Path to the file to load.
     * @return true on success, false on error.
     */
    bool loadFile(const char *filename);

    /**
     * @brief Saves the buffer to its current filename.
     *
     * @return true on success, false if no filename is set or on error.
     */
    bool saveFile();

    /**
     * @brief Saves the buffer to a new filename.
     *
     * @param filename Path to save the file to.
     * @return true on success, false on error.
     */
    bool saveFileAs(const char *filename);

    /**
     * @brief Clears the buffer and starts a new document.
     *
     * Resets buffer, cursor, and scroll position to initial state.
     */
    void newFile();

    //=== Click Handling ===//

    /**
     * @brief Positions the cursor based on a mouse click.
     *
     * Converts pixel coordinates in the text area to a (line, column)
     * position, accounting for scroll offset and line number gutter.
     *
     * @param clickX      X pixel coordinate relative to window.
     * @param clickY      Y pixel coordinate relative to text area top.
     * @param textAreaX   X offset where text area begins.
     * @param visibleLines Number of visible lines (unused).
     */
    void setCursorFromClick(int clickX, int clickY, int textAreaX, int visibleLines);

  private:
    /**
     * @brief Clamps cursor to valid range.
     *
     * Ensures the cursor line is within the buffer and the cursor
     * column doesn't exceed the current line's length.
     */
    void clampCursor();

    Buffer m_buffer;  /**< Text storage. */
    Config m_config;  /**< Editor settings. */
    int m_cursorLine; /**< Current cursor line (0-based). */
    int m_cursorCol;  /**< Current cursor column (0-based). */
    int m_scrollY;    /**< First visible line. */
    int m_scrollX;    /**< First visible column. */
};

} // namespace vedit
