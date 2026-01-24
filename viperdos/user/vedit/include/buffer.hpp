#pragma once
//===----------------------------------------------------------------------===//
// Text buffer with line-based storage
//===----------------------------------------------------------------------===//

#include <stddef.h>

namespace vedit {

constexpr int MAX_LINES = 10000;
constexpr int MAX_LINE_LENGTH = 4096;

// Single line of text
struct Line {
    char *text;
    int length;
    int capacity;
};

// Text buffer
class Buffer {
  public:
    Buffer();
    ~Buffer();

    // File operations
    bool load(const char *filename);
    bool save(const char *filename);
    void clear();

    // Line access
    int lineCount() const { return m_lineCount; }
    const char *lineText(int lineIdx) const;
    int lineLength(int lineIdx) const;

    // Editing
    void insertChar(int line, int col, char c);
    void insertNewline(int line, int col);
    void deleteChar(int line, int col);
    void backspace(int line, int col, int &newLine, int &newCol);
    void deleteLine(int lineIdx);

    // State
    bool isModified() const { return m_modified; }
    void clearModified() { m_modified = false; }
    const char *filename() const { return m_filename; }

  private:
    bool ensureCapacity(int lineIdx, int needed);
    bool insertLine(int afterLine);

    Line *m_lines;
    int m_lineCount;
    bool m_modified;
    char m_filename[256];
};

} // namespace vedit
