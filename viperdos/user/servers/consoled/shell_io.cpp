//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "shell_io.hpp"

namespace consoled {

static AnsiParser *g_parser = nullptr;
static TextBuffer *g_buffer = nullptr;
static gui_window_t *g_window = nullptr;

void shell_io_init(AnsiParser *parser, TextBuffer *buf, gui_window_t *window) {
    g_parser = parser;
    g_buffer = buf;
    g_window = window;
}

TextBuffer *shell_get_buffer() {
    return g_buffer;
}

void shell_io_flush() {
    // No-op: presentation is handled by the main event loop (synchronous gui_present).
}

void shell_print(const char *s) {
    if (!g_parser || !s)
        return;
    size_t len = shell_strlen(s);
    if (len > 0) {
        g_parser->write(s, len);
    }
}

void shell_print_char(char c) {
    if (!g_parser)
        return;
    g_parser->write(&c, 1);
}

void shell_put_num(int64_t n) {
    char buf[32];
    char *p = buf + 31;
    *p = '\0';

    bool neg = false;
    if (n < 0) {
        neg = true;
        n = -n;
    }

    do {
        *--p = '0' + static_cast<char>(n % 10);
        n /= 10;
    } while (n > 0);

    if (neg)
        *--p = '-';

    shell_print(p);
}

void shell_put_hex(uint32_t n) {
    shell_print("0x");
    char buf[16];
    char *p = buf + 15;
    *p = '\0';

    do {
        int digit = n & 0xF;
        *--p = (digit < 10) ? static_cast<char>('0' + digit) : static_cast<char>('a' + digit - 10);
        n >>= 4;
    } while (n > 0);

    shell_print(p);
}

// =========================================================================
// String Helpers
// =========================================================================

size_t shell_strlen(const char *s) {
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}

bool shell_streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool shell_strstart(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix)
            return false;
        s++;
        prefix++;
    }
    return true;
}

static char shell_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

bool shell_strcaseeq(const char *a, const char *b) {
    while (*a && *b) {
        if (shell_tolower(*a) != shell_tolower(*b))
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool shell_strcasestart(const char *s, const char *prefix) {
    while (*prefix) {
        if (shell_tolower(*s) != shell_tolower(*prefix))
            return false;
        s++;
        prefix++;
    }
    return true;
}

void shell_strcpy(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

} // namespace consoled
