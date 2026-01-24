//===----------------------------------------------------------------------===//
// Editor implementation
//===----------------------------------------------------------------------===//

#include "../include/editor.hpp"

namespace vedit {

Editor::Editor() : m_cursorLine(0), m_cursorCol(0), m_scrollY(0), m_scrollX(0) {
    m_config.showLineNumbers = true;
    m_config.wordWrap = false;
    m_config.tabWidth = 4;
}

void Editor::clampCursor() {
    if (m_cursorLine < 0) {
        m_cursorLine = 0;
    }
    if (m_cursorLine >= m_buffer.lineCount()) {
        m_cursorLine = m_buffer.lineCount() - 1;
    }
    if (m_cursorCol < 0) {
        m_cursorCol = 0;
    }
    if (m_cursorCol > m_buffer.lineLength(m_cursorLine)) {
        m_cursorCol = m_buffer.lineLength(m_cursorLine);
    }
}

void Editor::moveCursorLeft() {
    if (m_cursorCol > 0) {
        m_cursorCol--;
    } else if (m_cursorLine > 0) {
        m_cursorLine--;
        m_cursorCol = m_buffer.lineLength(m_cursorLine);
    }
}

void Editor::moveCursorRight() {
    if (m_cursorCol < m_buffer.lineLength(m_cursorLine)) {
        m_cursorCol++;
    } else if (m_cursorLine < m_buffer.lineCount() - 1) {
        m_cursorLine++;
        m_cursorCol = 0;
    }
}

void Editor::moveCursorUp() {
    if (m_cursorLine > 0) {
        m_cursorLine--;
        clampCursor();
    }
}

void Editor::moveCursorDown() {
    if (m_cursorLine < m_buffer.lineCount() - 1) {
        m_cursorLine++;
        clampCursor();
    }
}

void Editor::moveCursorHome() {
    m_cursorCol = 0;
}

void Editor::moveCursorEnd() {
    m_cursorCol = m_buffer.lineLength(m_cursorLine);
}

void Editor::moveCursorPageUp(int pageSize) {
    m_cursorLine -= pageSize;
    clampCursor();
}

void Editor::moveCursorPageDown(int pageSize) {
    m_cursorLine += pageSize;
    clampCursor();
}

void Editor::moveCursorToLine(int line) {
    m_cursorLine = line;
    clampCursor();
}

void Editor::insertChar(char c) {
    m_buffer.insertChar(m_cursorLine, m_cursorCol, c);
    m_cursorCol++;
}

void Editor::insertNewline() {
    m_buffer.insertNewline(m_cursorLine, m_cursorCol);
    m_cursorLine++;
    m_cursorCol = 0;
}

void Editor::deleteChar() {
    m_buffer.deleteChar(m_cursorLine, m_cursorCol);
}

void Editor::backspace() {
    int newLine, newCol;
    m_buffer.backspace(m_cursorLine, m_cursorCol, newLine, newCol);
    m_cursorLine = newLine;
    m_cursorCol = newCol;
}

void Editor::insertTab() {
    for (int i = 0; i < m_config.tabWidth; i++) {
        insertChar(' ');
    }
}

void Editor::ensureCursorVisible(int visibleLines, int visibleCols) {
    // Vertical scroll
    if (m_cursorLine < m_scrollY) {
        m_scrollY = m_cursorLine;
    } else if (m_cursorLine >= m_scrollY + visibleLines) {
        m_scrollY = m_cursorLine - visibleLines + 1;
    }

    // Horizontal scroll
    if (m_cursorCol < m_scrollX) {
        m_scrollX = m_cursorCol;
    } else if (m_cursorCol >= m_scrollX + visibleCols) {
        m_scrollX = m_cursorCol - visibleCols + 1;
    }
}

void Editor::scrollTo(int line) {
    m_scrollY = line;
    if (m_scrollY < 0) {
        m_scrollY = 0;
    }
    if (m_scrollY > m_buffer.lineCount() - 1) {
        m_scrollY = m_buffer.lineCount() - 1;
    }
}

bool Editor::loadFile(const char *filename) {
    bool result = m_buffer.load(filename);
    if (result) {
        m_cursorLine = 0;
        m_cursorCol = 0;
        m_scrollY = 0;
        m_scrollX = 0;
    }
    return result;
}

bool Editor::saveFile() {
    const char *fname = m_buffer.filename();
    if (fname[0] == '\0') {
        return false;
    }
    return m_buffer.save(fname);
}

bool Editor::saveFileAs(const char *filename) {
    return m_buffer.save(filename);
}

void Editor::newFile() {
    m_buffer.clear();
    m_cursorLine = 0;
    m_cursorCol = 0;
    m_scrollY = 0;
    m_scrollX = 0;
}

void Editor::setCursorFromClick(int clickX, int clickY, int textAreaX, int visibleLines) {
    (void)visibleLines;

    // Calculate line from Y position (assuming LINE_HEIGHT = 14)
    int lineHeight = 14;
    int clickLine = m_scrollY + clickY / lineHeight;

    // Calculate column from X position (assuming CHAR_WIDTH = 8)
    int charWidth = 8;
    int clickCol = m_scrollX + (clickX - textAreaX - 4) / charWidth;

    m_cursorLine = clickLine;
    m_cursorCol = clickCol;
    clampCursor();
}

} // namespace vedit
