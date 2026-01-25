//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file buffer.cpp
 * @brief Text buffer implementation for VEdit.
 *
 * This file implements the Buffer class which provides text storage and
 * manipulation for the VEdit text editor. The buffer uses a line-based
 * storage model with dynamic memory allocation per line.
 *
 * ## Memory Management
 *
 * - Lines are allocated with an initial capacity of 256 bytes
 * - Lines automatically grow when text exceeds capacity
 * - The lines array is pre-allocated to MAX_LINES entries
 * - Each line's text buffer is independently allocated/freed
 *
 * ## File I/O
 *
 * The buffer supports loading and saving files:
 * - load(): Reads file line by line, handles \n and \r\n
 * - save(): Writes all lines with \n terminators
 *
 * ## Line Operations
 *
 * - insertChar(): Insert character at cursor position
 * - deleteChar(): Delete character at cursor position
 * - splitLine(): Split line at cursor (Enter key)
 * - joinLines(): Join line with previous (Backspace at start)
 *
 * @see buffer.hpp for the Buffer class definition
 */
//===----------------------------------------------------------------------===//

#include "../include/buffer.hpp"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace vedit {

Buffer::Buffer() : m_lines(nullptr), m_lineCount(0), m_modified(false) {
    m_filename[0] = '\0';

    m_lines = static_cast<Line *>(malloc(MAX_LINES * sizeof(Line)));
    if (m_lines) {
        memset(m_lines, 0, MAX_LINES * sizeof(Line));
        // Create first empty line
        m_lines[0].capacity = 256;
        m_lines[0].text = static_cast<char *>(malloc(256));
        if (m_lines[0].text) {
            m_lines[0].text[0] = '\0';
            m_lines[0].length = 0;
            m_lineCount = 1;
        }
    }
}

Buffer::~Buffer() {
    if (m_lines) {
        for (int i = 0; i < m_lineCount; i++) {
            free(m_lines[i].text);
        }
        free(m_lines);
    }
}

void Buffer::clear() {
    for (int i = 0; i < m_lineCount; i++) {
        free(m_lines[i].text);
        m_lines[i].text = nullptr;
    }
    m_lineCount = 0;

    // Create first empty line
    m_lines[0].capacity = 256;
    m_lines[0].text = static_cast<char *>(malloc(256));
    if (m_lines[0].text) {
        m_lines[0].text[0] = '\0';
        m_lines[0].length = 0;
        m_lineCount = 1;
    }

    m_modified = false;
    m_filename[0] = '\0';
}

bool Buffer::load(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    // Clear existing content
    for (int i = 0; i < m_lineCount; i++) {
        free(m_lines[i].text);
        m_lines[i].text = nullptr;
    }
    m_lineCount = 0;

    // Read file
    char buf[4096];
    char lineBuf[MAX_LINE_LENGTH];
    int lineLen = 0;

    ssize_t bytesRead;
    while ((bytesRead = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < bytesRead; i++) {
            char c = buf[i];

            if (c == '\n' || c == '\r') {
                if (m_lineCount >= MAX_LINES)
                    break;

                lineBuf[lineLen] = '\0';
                m_lines[m_lineCount].capacity = lineLen + 64;
                m_lines[m_lineCount].text = static_cast<char *>(malloc(m_lines[m_lineCount].capacity));
                if (m_lines[m_lineCount].text) {
                    strcpy(m_lines[m_lineCount].text, lineBuf);
                    m_lines[m_lineCount].length = lineLen;
                    m_lineCount++;
                }
                lineLen = 0;

                // Skip \r\n
                if (c == '\r' && i + 1 < bytesRead && buf[i + 1] == '\n') {
                    i++;
                }
            } else if (lineLen < MAX_LINE_LENGTH - 1) {
                lineBuf[lineLen++] = c;
            }
        }
    }

    // Handle last line without newline
    if (lineLen > 0 && m_lineCount < MAX_LINES) {
        lineBuf[lineLen] = '\0';
        m_lines[m_lineCount].capacity = lineLen + 64;
        m_lines[m_lineCount].text = static_cast<char *>(malloc(m_lines[m_lineCount].capacity));
        if (m_lines[m_lineCount].text) {
            strcpy(m_lines[m_lineCount].text, lineBuf);
            m_lines[m_lineCount].length = lineLen;
            m_lineCount++;
        }
    }

    // Ensure at least one line
    if (m_lineCount == 0) {
        m_lines[0].capacity = 256;
        m_lines[0].text = static_cast<char *>(malloc(256));
        if (m_lines[0].text) {
            m_lines[0].text[0] = '\0';
            m_lines[0].length = 0;
            m_lineCount = 1;
        }
    }

    close(fd);

    strncpy(m_filename, filename, sizeof(m_filename) - 1);
    m_filename[sizeof(m_filename) - 1] = '\0';
    m_modified = false;

    return true;
}

bool Buffer::save(const char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }

    for (int i = 0; i < m_lineCount; i++) {
        if (m_lines[i].text && m_lines[i].length > 0) {
            write(fd, m_lines[i].text, m_lines[i].length);
        }
        write(fd, "\n", 1);
    }

    close(fd);

    strncpy(m_filename, filename, sizeof(m_filename) - 1);
    m_filename[sizeof(m_filename) - 1] = '\0';
    m_modified = false;

    return true;
}

const char *Buffer::lineText(int lineIdx) const {
    if (lineIdx < 0 || lineIdx >= m_lineCount) {
        return "";
    }
    return m_lines[lineIdx].text ? m_lines[lineIdx].text : "";
}

int Buffer::lineLength(int lineIdx) const {
    if (lineIdx < 0 || lineIdx >= m_lineCount) {
        return 0;
    }
    return m_lines[lineIdx].length;
}

bool Buffer::ensureCapacity(int lineIdx, int needed) {
    if (lineIdx < 0 || lineIdx >= m_lineCount) {
        return false;
    }

    Line &line = m_lines[lineIdx];
    if (needed + 1 > line.capacity) {
        int newCap = (needed + 1) * 2;
        char *newText = static_cast<char *>(realloc(line.text, newCap));
        if (!newText) {
            return false;
        }
        line.text = newText;
        line.capacity = newCap;
    }
    return true;
}

bool Buffer::insertLine(int afterLine) {
    if (m_lineCount >= MAX_LINES) {
        return false;
    }

    // Shift lines down
    memmove(&m_lines[afterLine + 2], &m_lines[afterLine + 1],
            (m_lineCount - afterLine - 1) * sizeof(Line));

    // Initialize new line
    int newIdx = afterLine + 1;
    m_lines[newIdx].capacity = 256;
    m_lines[newIdx].text = static_cast<char *>(malloc(256));
    if (!m_lines[newIdx].text) {
        return false;
    }
    m_lines[newIdx].text[0] = '\0';
    m_lines[newIdx].length = 0;

    m_lineCount++;
    return true;
}

void Buffer::insertChar(int line, int col, char c) {
    if (line < 0 || line >= m_lineCount) {
        return;
    }

    Line &ln = m_lines[line];
    if (!ensureCapacity(line, ln.length + 1)) {
        return;
    }

    // Insert character
    memmove(ln.text + col + 1, ln.text + col, ln.length - col + 1);
    ln.text[col] = c;
    ln.length++;
    m_modified = true;
}

void Buffer::insertNewline(int line, int col) {
    if (line < 0 || line >= m_lineCount) {
        return;
    }

    if (!insertLine(line)) {
        return;
    }

    // Move text after cursor to new line
    Line &oldLine = m_lines[line];
    Line &newLine = m_lines[line + 1];

    int remainingLen = oldLine.length - col;
    if (remainingLen > 0) {
        ensureCapacity(line + 1, remainingLen);
        strcpy(newLine.text, oldLine.text + col);
        newLine.length = remainingLen;
        oldLine.text[col] = '\0';
        oldLine.length = col;
    }

    m_modified = true;
}

void Buffer::deleteChar(int line, int col) {
    if (line < 0 || line >= m_lineCount) {
        return;
    }

    Line &ln = m_lines[line];

    if (col < ln.length) {
        // Delete character at position
        memmove(ln.text + col, ln.text + col + 1, ln.length - col);
        ln.length--;
        m_modified = true;
    } else if (line < m_lineCount - 1) {
        // Join with next line
        Line &nextLine = m_lines[line + 1];
        ensureCapacity(line, ln.length + nextLine.length);
        strcat(ln.text, nextLine.text);
        ln.length += nextLine.length;
        deleteLine(line + 1);
        m_modified = true;
    }
}

void Buffer::backspace(int line, int col, int &newLine, int &newCol) {
    newLine = line;
    newCol = col;

    if (col > 0) {
        newCol = col - 1;
        deleteChar(line, newCol);
    } else if (line > 0) {
        newLine = line - 1;
        newCol = m_lines[newLine].length;
        deleteChar(newLine, newCol);
    }
}

void Buffer::deleteLine(int lineIdx) {
    if (m_lineCount <= 1 || lineIdx < 0 || lineIdx >= m_lineCount) {
        return;
    }

    free(m_lines[lineIdx].text);

    // Shift lines up
    memmove(&m_lines[lineIdx], &m_lines[lineIdx + 1],
            (m_lineCount - lineIdx - 1) * sizeof(Line));

    m_lineCount--;
    m_modified = true;
}

} // namespace vedit
