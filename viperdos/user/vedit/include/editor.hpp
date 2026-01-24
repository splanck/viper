#pragma once
//===----------------------------------------------------------------------===//
// Editor state and cursor management
//===----------------------------------------------------------------------===//

#include "buffer.hpp"

namespace vedit {

// Editor configuration
struct Config {
    bool showLineNumbers;
    bool wordWrap;
    int tabWidth;
};

// Editor state
class Editor {
  public:
    Editor();

    // Buffer access
    Buffer &buffer() { return m_buffer; }
    const Buffer &buffer() const { return m_buffer; }

    // Cursor position
    int cursorLine() const { return m_cursorLine; }
    int cursorCol() const { return m_cursorCol; }

    // Scroll position
    int scrollY() const { return m_scrollY; }
    int scrollX() const { return m_scrollX; }

    // Configuration
    Config &config() { return m_config; }
    const Config &config() const { return m_config; }

    // Cursor movement
    void moveCursorLeft();
    void moveCursorRight();
    void moveCursorUp();
    void moveCursorDown();
    void moveCursorHome();
    void moveCursorEnd();
    void moveCursorPageUp(int pageSize);
    void moveCursorPageDown(int pageSize);
    void moveCursorToLine(int line);

    // Editing
    void insertChar(char c);
    void insertNewline();
    void deleteChar();
    void backspace();
    void insertTab();

    // Scrolling
    void ensureCursorVisible(int visibleLines, int visibleCols);
    void scrollTo(int line);

    // File operations
    bool loadFile(const char *filename);
    bool saveFile();
    bool saveFileAs(const char *filename);
    void newFile();

    // Click handling
    void setCursorFromClick(int clickX, int clickY, int textAreaX, int visibleLines);

  private:
    void clampCursor();

    Buffer m_buffer;
    Config m_config;
    int m_cursorLine;
    int m_cursorCol;
    int m_scrollY;
    int m_scrollX;
};

} // namespace vedit
