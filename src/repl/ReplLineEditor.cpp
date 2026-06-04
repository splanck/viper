//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/ReplLineEditor.cpp
// Purpose: Custom line editor implementation using the TUI framework's
//          TerminalSession (raw mode) and InputDecoder (key events).
// Key invariants:
//   - Raw mode is active for the lifetime of the editor.
//   - History ring never exceeds maxHistory entries.
//   - All terminal I/O goes through POSIX read/write (not stdio).
// Ownership/Lifetime:
//   - Impl owns TerminalSession, InputDecoder, and history vector.
// Links: src/repl/ReplLineEditor.hpp
//
//===----------------------------------------------------------------------===//

#include "ReplLineEditor.hpp"

#include "ReplHistoryCodec.hpp"

#include "tui/term/input.hpp"
#include "tui/term/key_event.hpp"
#include "tui/term/session.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cerrno>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#endif

namespace viper::repl {

/// @brief Internal implementation hiding TUI headers from the public interface.
struct ReplLineEditor::Impl {
    viper::tui::TerminalSession session;
    viper::tui::term::InputDecoder decoder;

    std::vector<std::string> history;
    size_t maxHistory{1000};

    CompletionCallback completionCb;

    // Line editing state (reset per readLine call)
    std::string buf;
    size_t cursor{0};
    int historyIndex{-1};
    std::string savedLine; // saved current line when browsing history
    size_t renderedCursorRow{0};

    /// @brief Get terminal width.
    int termWidth() const {
#if defined(__unix__) || defined(__APPLE__)
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
            return ws.ws_col;
#elif defined(_WIN32)
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
            return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#endif
        return 80;
    }

    /// @brief Write raw bytes to stdout.
    void rawWrite(const char *data, size_t len) {
#if defined(__unix__) || defined(__APPLE__)
        while (len > 0) {
            ssize_t n = ::write(STDOUT_FILENO, data, len);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                return;
            }
            if (n == 0)
                return;
            data += static_cast<size_t>(n);
            len -= static_cast<size_t>(n);
        }
#elif defined(_WIN32)
        while (len > 0) {
            DWORD chunk = len > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<DWORD>(len);
            DWORD written = 0;
            if (!WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), data, chunk, &written, nullptr) ||
                written == 0)
                return;
            data += written;
            len -= written;
        }
#endif
    }

    /// @brief Write an owned string buffer to stdout.
    /// @param s Byte string to write exactly as stored.
    void rawWrite(const std::string &s) {
        rawWrite(s.data(), s.size());
    }

    /// @brief Move the terminal cursor up by @p rows rows.
    /// @param rows Number of rows to move.
    void cursorUp(size_t rows) {
        if (rows == 0)
            return;
        char moveBuf[32];
        snprintf(moveBuf, sizeof(moveBuf), "\033[%zuA", rows);
        rawWrite(moveBuf, strlen(moveBuf));
    }

    /// @brief Move the terminal cursor forward by @p cols columns.
    /// @param cols Number of columns to move.
    void cursorForward(size_t cols) {
        if (cols == 0)
            return;
        char moveBuf[32];
        snprintf(moveBuf, sizeof(moveBuf), "\033[%zuC", cols);
        rawWrite(moveBuf, strlen(moveBuf));
    }

    /// @brief Move the terminal cursor down by @p rows rows.
    /// @param rows Number of rows to move.
    void cursorDown(size_t rows) {
        if (rows == 0)
            return;
        char moveBuf[32];
        snprintf(moveBuf, sizeof(moveBuf), "\033[%zuB", rows);
        rawWrite(moveBuf, strlen(moveBuf));
    }

    /// @brief Count visible terminal columns in a string containing ANSI SGR codes.
    /// @details Escape sequences beginning with ESC and ending in an ASCII
    ///          alphabetic final byte are ignored. Other bytes count as one
    ///          column; this is sufficient for stable REPL prompt layout.
    /// @param text Text whose visible width should be measured.
    /// @return Approximate terminal display columns.
    static size_t visibleColumns(const std::string &text) {
        size_t cols = 0;
        bool inEscape = false;
        for (unsigned char c : text) {
            if (inEscape) {
                if (std::isalpha(c))
                    inEscape = false;
                continue;
            }
            if (c == 0x1B) {
                inEscape = true;
                continue;
            }
            ++cols;
        }
        return cols;
    }

    /// @brief Move the cursor to the row where the rendered buffer ends.
    /// @param prompt Prompt currently displayed before @c buf.
    void moveToRenderedEndRow(const std::string &prompt) {
        size_t width = static_cast<size_t>(std::max(1, termWidth()));
        size_t endRow = (visibleColumns(prompt) + buf.size()) / width;
        if (endRow > renderedCursorRow)
            cursorDown(endRow - renderedCursorRow);
        renderedCursorRow = endRow;
    }

    /// @brief Read raw bytes from stdin (blocking with timeout).
    /// @param outBuf Buffer to fill.
    /// @param maxLen Maximum bytes to read.
    /// @return Number of bytes read, 0 on timeout, or -1 on EOF/fatal error.
    int rawRead(char *outBuf, int maxLen) {
#if defined(__unix__) || defined(__APPLE__)
        // Use select with a short timeout for responsive Ctrl-C
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms
        int sel = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
        if (sel <= 0)
            return 0;
        auto n = ::read(STDIN_FILENO, outBuf, static_cast<size_t>(maxLen));
        if (n > 0)
            return static_cast<int>(n);
        if (n == 0)
            return -1;
        return (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
#elif defined(_WIN32)
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        DWORD avail = 0;
        if (!PeekConsoleInput(h, nullptr, 0, &avail))
            return 0;
        DWORD read_count = 0;
        if (!ReadConsoleA(h, outBuf, (DWORD)maxLen, &read_count, nullptr))
            return -1;
        if (read_count == 0)
            return -1;
        return (int)read_count;
#else
            return -1;
#endif
    }

    /// @brief Refresh the displayed prompt and editable buffer.
    /// @details Redraw starts from the top row of the previously rendered logical
    ///          line, clears everything below it, prints prompt plus buffer, then
    ///          restores the terminal cursor to the logical edit cursor. This keeps
    ///          wrapped lines from leaving stale bytes behind.
    /// @param prompt Prompt currently displayed before @c buf.
    void refreshLine(const std::string &prompt) {
        size_t width = static_cast<size_t>(std::max(1, termWidth()));
        size_t promptCols = visibleColumns(prompt);
        size_t cursorCols = promptCols + cursor;
        size_t endCols = promptCols + buf.size();
        size_t cursorRow = cursorCols / width;
        size_t cursorCol = cursorCols % width;
        size_t endRow = endCols / width;

        // Move from the previous cursor row to the top of the rendered line,
        // clear all rows below, then reprint prompt + buffer.
        cursorUp(renderedCursorRow);
        rawWrite("\r\033[J");
        rawWrite(prompt);
        rawWrite(buf);

        cursorUp(endRow - cursorRow);
        rawWrite("\r");
        cursorForward(cursorCol);
        renderedCursorRow = cursorRow;
    }

    /// @brief Invoke the configured completion callback and render matches.
    /// @details A single match replaces the current buffer. Multiple matches are
    ///          printed on a fresh line and the current prompt/buffer are redrawn.
    ///          The callback receives byte offsets, matching the editor's UTF-8
    ///          cursor model.
    /// @param prompt Prompt currently displayed before @c buf.
    void handleTab(const std::string &prompt) {
        if (!completionCb)
            return;

        auto completions = completionCb(buf, cursor);
        if (completions.empty())
            return;

        if (completions.size() == 1) {
            // Single completion: insert it directly
            buf = completions[0];
            cursor = buf.size();
            refreshLine(prompt);
            return;
        }

        // Multiple completions: show them
        rawWrite("\r\n");
        for (size_t i = 0; i < completions.size(); ++i) {
            if (i > 0)
                rawWrite("  ");
            rawWrite(completions[i]);
        }
        rawWrite("\r\n");
        refreshLine(prompt);
    }

    /// @brief Move cursor one word left.
    void wordLeft() {
        while (cursor > 0 && buf[cursor - 1] == ' ')
            --cursor;
        while (cursor > 0 && buf[cursor - 1] != ' ')
            --cursor;
    }

    /// @brief Move cursor one word right.
    void wordRight() {
        while (cursor < buf.size() && buf[cursor] != ' ')
            ++cursor;
        while (cursor < buf.size() && buf[cursor] == ' ')
            ++cursor;
    }

    /// @brief Return true if @p c is a UTF-8 continuation byte.
    /// @param c Byte to inspect.
    /// @return True when the high bits match `10xxxxxx`.
    static bool isUtf8Continuation(unsigned char c) {
        return (c & 0xC0u) == 0x80u;
    }

    /// @brief Find the byte offset of the previous UTF-8 codepoint.
    /// @param text UTF-8 byte buffer.
    /// @param pos Current byte offset.
    /// @return Start offset of the previous codepoint, or zero at buffer start.
    static size_t previousCodepointStart(const std::string &text, size_t pos) {
        if (pos == 0)
            return 0;
        --pos;
        while (pos > 0 && isUtf8Continuation(static_cast<unsigned char>(text[pos])))
            --pos;
        return pos;
    }

    /// @brief Find the byte offset just after the next UTF-8 codepoint.
    /// @param text UTF-8 byte buffer.
    /// @param pos Start offset of the current codepoint.
    /// @return Offset after the current codepoint, clamped to @p text size.
    static size_t nextCodepointEnd(const std::string &text, size_t pos) {
        if (pos >= text.size())
            return text.size();
        ++pos;
        while (pos < text.size() && isUtf8Continuation(static_cast<unsigned char>(text[pos])))
            ++pos;
        return pos;
    }
};

ReplLineEditor::ReplLineEditor(size_t maxHistory) : impl_(new Impl) {
    impl_->maxHistory = maxHistory;
}

ReplLineEditor::~ReplLineEditor() {
    delete impl_;
}

bool ReplLineEditor::isActive() const {
    return impl_->session.active();
}

std::vector<std::string> ReplLineEditor::getHistory() const {
    return impl_->history;
}

void ReplLineEditor::setCompletionCallback(CompletionCallback cb) {
    impl_->completionCb = std::move(cb);
}

void ReplLineEditor::addHistory(const std::string &entry) {
    if (entry.empty())
        return;
    // Skip duplicate of most recent entry
    if (!impl_->history.empty() && impl_->history.back() == entry)
        return;
    impl_->history.push_back(entry);
    if (impl_->history.size() > impl_->maxHistory)
        impl_->history.erase(impl_->history.begin());
}

ReadResult ReplLineEditor::readLine(const std::string &prompt, std::string &line) {
    using Code = viper::tui::term::KeyEvent::Code;
    using Mods = viper::tui::term::KeyEvent::Mods;

    impl_->buf.clear();
    impl_->cursor = 0;
    impl_->historyIndex = -1;
    impl_->savedLine.clear();
    impl_->renderedCursorRow = 0;

    // Print the initial prompt
    impl_->rawWrite(prompt);

    char readBuf[256];

    for (;;) {
        int n = impl_->rawRead(readBuf, sizeof(readBuf));
        if (n < 0) {
            impl_->rawWrite("\r\n");
            return ReadResult::Eof;
        }
        if (n == 0)
            continue;

        impl_->decoder.feed(std::string_view(readBuf, static_cast<size_t>(n)));
        auto events = impl_->decoder.drain();

        for (const auto &ev : events) {
            // --- Ctrl+key shortcuts (detected by codepoint with Ctrl mod) ---

            // Ctrl-C: interrupt
            if (ev.codepoint == 3 || (ev.codepoint == 'c' && (ev.mods & Mods::Ctrl))) {
                impl_->moveToRenderedEndRow(prompt);
                impl_->rawWrite("^C\r\n");
                return ReadResult::Interrupt;
            }

            // Ctrl-D: EOF (only on empty line)
            if (ev.codepoint == 4 || (ev.codepoint == 'd' && (ev.mods & Mods::Ctrl))) {
                if (impl_->buf.empty()) {
                    impl_->rawWrite("\r\n");
                    return ReadResult::Eof;
                }
                // On non-empty line, delete char under cursor (like Delete)
                if (impl_->cursor < impl_->buf.size()) {
                    size_t end = Impl::nextCodepointEnd(impl_->buf, impl_->cursor);
                    impl_->buf.erase(impl_->cursor, end - impl_->cursor);
                    impl_->refreshLine(prompt);
                }
                continue;
            }

            // Ctrl-U: kill line before cursor
            if (ev.codepoint == 21 || (ev.codepoint == 'u' && (ev.mods & Mods::Ctrl))) {
                impl_->buf.erase(0, impl_->cursor);
                impl_->cursor = 0;
                impl_->refreshLine(prompt);
                continue;
            }

            // Ctrl-K: kill line after cursor
            if (ev.codepoint == 11 || (ev.codepoint == 'k' && (ev.mods & Mods::Ctrl))) {
                impl_->buf.erase(impl_->cursor);
                impl_->refreshLine(prompt);
                continue;
            }

            // Ctrl-A: home
            if (ev.codepoint == 1 || (ev.codepoint == 'a' && (ev.mods & Mods::Ctrl))) {
                impl_->cursor = 0;
                impl_->refreshLine(prompt);
                continue;
            }

            // Ctrl-E: end
            if (ev.codepoint == 5 || (ev.codepoint == 'e' && (ev.mods & Mods::Ctrl))) {
                impl_->cursor = impl_->buf.size();
                impl_->refreshLine(prompt);
                continue;
            }

            // Ctrl-W: delete word before cursor
            if (ev.codepoint == 23 || (ev.codepoint == 'w' && (ev.mods & Mods::Ctrl))) {
                size_t start = impl_->cursor;
                impl_->wordLeft();
                impl_->buf.erase(impl_->cursor, start - impl_->cursor);
                impl_->refreshLine(prompt);
                continue;
            }

            // Ctrl-L: clear screen
            if (ev.codepoint == 12 || (ev.codepoint == 'l' && (ev.mods & Mods::Ctrl))) {
                impl_->rawWrite("\033[2J\033[H"); // clear + home
                impl_->refreshLine(prompt);
                continue;
            }

            // --- Special keys ---
            if (ev.code == Code::Enter) {
                impl_->moveToRenderedEndRow(prompt);
                impl_->rawWrite("\r\n");
                line = impl_->buf;
                return ReadResult::Line;
            }

            if (ev.code == Code::Tab) {
                impl_->handleTab(prompt);
                continue;
            }

            if (ev.code == Code::Backspace) {
                if (impl_->cursor > 0) {
                    size_t start = Impl::previousCodepointStart(impl_->buf, impl_->cursor);
                    impl_->buf.erase(start, impl_->cursor - start);
                    impl_->cursor = start;
                    impl_->refreshLine(prompt);
                }
                continue;
            }

            if (ev.code == Code::Delete) {
                if (impl_->cursor < impl_->buf.size()) {
                    size_t end = Impl::nextCodepointEnd(impl_->buf, impl_->cursor);
                    impl_->buf.erase(impl_->cursor, end - impl_->cursor);
                    impl_->refreshLine(prompt);
                }
                continue;
            }

            if (ev.code == Code::Left) {
                if (ev.mods & Mods::Ctrl) {
                    impl_->wordLeft();
                    impl_->refreshLine(prompt);
                } else if (impl_->cursor > 0) {
                    impl_->cursor = Impl::previousCodepointStart(impl_->buf, impl_->cursor);
                    impl_->refreshLine(prompt);
                }
                continue;
            }

            if (ev.code == Code::Right) {
                if (ev.mods & Mods::Ctrl) {
                    impl_->wordRight();
                    impl_->refreshLine(prompt);
                } else if (impl_->cursor < impl_->buf.size()) {
                    impl_->cursor = Impl::nextCodepointEnd(impl_->buf, impl_->cursor);
                    impl_->refreshLine(prompt);
                }
                continue;
            }

            if (ev.code == Code::Home) {
                impl_->cursor = 0;
                impl_->refreshLine(prompt);
                continue;
            }

            if (ev.code == Code::End) {
                impl_->cursor = impl_->buf.size();
                impl_->refreshLine(prompt);
                continue;
            }

            // --- History navigation ---
            if (ev.code == Code::Up) {
                if (impl_->history.empty())
                    continue;
                if (impl_->historyIndex == -1) {
                    impl_->savedLine = impl_->buf;
                    impl_->historyIndex = static_cast<int>(impl_->history.size()) - 1;
                } else if (impl_->historyIndex > 0) {
                    --impl_->historyIndex;
                } else {
                    continue; // Already at oldest
                }
                impl_->buf = impl_->history[static_cast<size_t>(impl_->historyIndex)];
                impl_->cursor = impl_->buf.size();
                impl_->refreshLine(prompt);
                continue;
            }

            if (ev.code == Code::Down) {
                if (impl_->historyIndex == -1)
                    continue;
                if (impl_->historyIndex < static_cast<int>(impl_->history.size()) - 1) {
                    ++impl_->historyIndex;
                    impl_->buf = impl_->history[static_cast<size_t>(impl_->historyIndex)];
                } else {
                    // Return to the saved current line
                    impl_->historyIndex = -1;
                    impl_->buf = impl_->savedLine;
                }
                impl_->cursor = impl_->buf.size();
                impl_->refreshLine(prompt);
                continue;
            }

            // --- Character input ---
            if (ev.code == Code::Unknown && ev.codepoint >= 32) {
                // Insert UTF-8 character at cursor
                // For simplicity, handle ASCII directly; multi-byte later
                if (ev.codepoint < 128) {
                    impl_->buf.insert(impl_->cursor, 1, static_cast<char>(ev.codepoint));
                    ++impl_->cursor;
                } else {
                    // Encode UTF-8
                    char utf8[4];
                    int len = 0;
                    uint32_t cp = ev.codepoint;
                    if (cp < 0x80) {
                        utf8[0] = static_cast<char>(cp);
                        len = 1;
                    } else if (cp < 0x800) {
                        utf8[0] = static_cast<char>(0xC0 | (cp >> 6));
                        utf8[1] = static_cast<char>(0x80 | (cp & 0x3F));
                        len = 2;
                    } else if (cp < 0x10000) {
                        utf8[0] = static_cast<char>(0xE0 | (cp >> 12));
                        utf8[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        utf8[2] = static_cast<char>(0x80 | (cp & 0x3F));
                        len = 3;
                    } else {
                        utf8[0] = static_cast<char>(0xF0 | (cp >> 18));
                        utf8[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        utf8[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        utf8[3] = static_cast<char>(0x80 | (cp & 0x3F));
                        len = 4;
                    }
                    impl_->buf.insert(impl_->cursor, utf8, static_cast<size_t>(len));
                    impl_->cursor += static_cast<size_t>(len);
                }
                impl_->refreshLine(prompt);
                continue;
            }
        }
    }
}

size_t ReplLineEditor::loadHistory(const std::filesystem::path &path) {
    ReplHistoryLoadResult loaded = ReplHistoryCodec::load(path, impl_->maxHistory);
    impl_->history = std::move(loaded.entries);
    return loaded.decodedEntryCount;
}

bool ReplLineEditor::saveHistory(const std::filesystem::path &path) const {
    return ReplHistoryCodec::save(path, impl_->history);
}

} // namespace viper::repl
