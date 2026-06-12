//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/common/text/viper_text_buffer.cpp
// Purpose: Implement the C ABI facade for Viper's shared piece-table buffer.
// Key invariants: No C++ exception crosses the exported C ABI; all returned
//                 strings are NUL-terminated copies with explicit byte lengths.
// Ownership/Lifetime: The opaque handle owns a viper::tui::text::TextBuffer.
//                     C callers release handles and strings through this API.
// Links: src/common/text/viper_text_buffer.h,
//        src/tui/include/tui/text/text_buffer.hpp
//
//===----------------------------------------------------------------------===//

#include "viper_text_buffer.h"

#include "tui/text/text_buffer.hpp"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <utility>

/**
 * @brief Concrete storage hidden behind the C ABI handle.
 *
 * @details The struct intentionally lives only in this translation unit so C
 * users cannot observe, allocate, or depend on the underlying C++ text buffer
 * layout.
 */
struct viper_text_buffer {
    viper::tui::text::TextBuffer buffer; ///< Owned shared text engine.
};

/**
 * @brief Copy a C++ string into an allocator-stable C string.
 *
 * @details The returned buffer is allocated with malloc and always contains one
 * trailing NUL byte after the copied payload. The payload itself may contain
 * embedded NUL bytes, so callers must use the returned length when binary-safe
 * handling matters.
 *
 * @param value String payload to copy.
 * @param out_len Optional output for the payload byte length.
 * @return Caller-owned C allocation, or NULL on allocation failure.
 */
static char *duplicate_string_for_c(const std::string &value, size_t *out_len) {
    if (out_len) {
        *out_len = 0;
    }
    if (value.size() == static_cast<size_t>(-1)) {
        return nullptr;
    }

    char *copy = static_cast<char *>(std::malloc(value.size() + 1));
    if (!copy) {
        return nullptr;
    }
    if (!value.empty()) {
        std::memcpy(copy, value.data(), value.size());
    }
    copy[value.size()] = '\0';
    if (out_len) {
        *out_len = value.size();
    }
    return copy;
}

viper_text_buffer_t *viper_text_buffer_new(void) {
    try {
        return new viper_text_buffer();
    } catch (...) {
        return nullptr;
    }
}

void viper_text_buffer_free(viper_text_buffer_t *buffer) {
    delete buffer;
}

bool viper_text_buffer_load_bytes(viper_text_buffer_t *buffer, const char *bytes, size_t len) {
    if (!buffer || (!bytes && len != 0)) {
        return false;
    }
    try {
        std::string text;
        if (len != 0) {
            text.assign(bytes, len);
        }
        buffer->buffer.load(std::move(text));
        return true;
    } catch (...) {
        return false;
    }
}

bool viper_text_buffer_insert_bytes(viper_text_buffer_t *buffer,
                                    size_t pos,
                                    const char *bytes,
                                    size_t len) {
    if (!buffer || (!bytes && len != 0)) {
        return false;
    }
    try {
        const std::string_view text(bytes ? bytes : "", len);
        buffer->buffer.insert(pos, text);
        return true;
    } catch (...) {
        return false;
    }
}

bool viper_text_buffer_erase(viper_text_buffer_t *buffer, size_t pos, size_t len) {
    if (!buffer) {
        return false;
    }
    try {
        buffer->buffer.erase(pos, len);
        return true;
    } catch (...) {
        return false;
    }
}

void viper_text_buffer_begin_transaction(viper_text_buffer_t *buffer) {
    if (!buffer) {
        return;
    }
    try {
        buffer->buffer.beginTxn();
    } catch (...) {
    }
}

void viper_text_buffer_end_transaction(viper_text_buffer_t *buffer) {
    if (!buffer) {
        return;
    }
    try {
        buffer->buffer.endTxn();
    } catch (...) {
    }
}

bool viper_text_buffer_undo(viper_text_buffer_t *buffer) {
    if (!buffer) {
        return false;
    }
    try {
        return buffer->buffer.undo();
    } catch (...) {
        return false;
    }
}

bool viper_text_buffer_redo(viper_text_buffer_t *buffer) {
    if (!buffer) {
        return false;
    }
    try {
        return buffer->buffer.redo();
    } catch (...) {
        return false;
    }
}

size_t viper_text_buffer_size(const viper_text_buffer_t *buffer) {
    return buffer ? buffer->buffer.size() : 0;
}

size_t viper_text_buffer_line_count(const viper_text_buffer_t *buffer) {
    return buffer ? buffer->buffer.lineCount() : 0;
}

size_t viper_text_buffer_line_start(const viper_text_buffer_t *buffer, size_t line_no) {
    return buffer ? buffer->buffer.lineStart(line_no) : 0;
}

size_t viper_text_buffer_line_end(const viper_text_buffer_t *buffer, size_t line_no) {
    return buffer ? buffer->buffer.lineEnd(line_no) : 0;
}

size_t viper_text_buffer_line_length(const viper_text_buffer_t *buffer, size_t line_no) {
    return buffer ? buffer->buffer.lineLength(line_no) : 0;
}

char *viper_text_buffer_text_dup(const viper_text_buffer_t *buffer, size_t *out_len) {
    if (out_len) {
        *out_len = 0;
    }
    if (!buffer) {
        return nullptr;
    }
    try {
        return duplicate_string_for_c(buffer->buffer.str(), out_len);
    } catch (...) {
        return nullptr;
    }
}

char *viper_text_buffer_line_dup(const viper_text_buffer_t *buffer,
                                 size_t line_no,
                                 size_t *out_len) {
    if (out_len) {
        *out_len = 0;
    }
    if (!buffer) {
        return nullptr;
    }
    try {
        return duplicate_string_for_c(buffer->buffer.getLine(line_no), out_len);
    } catch (...) {
        return nullptr;
    }
}

void viper_text_buffer_free_string(char *text) {
    std::free(text);
}
