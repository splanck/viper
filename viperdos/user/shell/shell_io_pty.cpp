//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file shell_io_pty.cpp
 * @brief Channel-based shell I/O for the standalone shell process.
 *
 * Replaces the consoled shell_io.cpp implementation. Instead of writing
 * to an AnsiParser->TextBuffer pipeline, all output is sent as raw bytes
 * (including ANSI escape sequences) over a kernel channel to the terminal
 * emulator (vshell).
 */
//===----------------------------------------------------------------------===//

#include "../syscall.hpp"
#include "shell_io.hpp"

namespace consoled {

static int32_t g_output_channel = -1;

/// Initialize PTY-mode I/O with the output channel to the terminal emulator.
void shell_io_init_pty(int32_t output_ch) {
    g_output_channel = output_ch;
}

TextBuffer *shell_get_buffer() {
    return nullptr; // PTY mode has no TextBuffer
}

void shell_io_flush() {
    // No-op: channel sends are immediate
}

// Declare shell_strlen early so shell_print can use it
// (the full definition is below with the other string helpers)

void shell_print(const char *s) {
    if (!s || g_output_channel < 0)
        return;
    size_t len = shell_strlen(s);
    if (len == 0)
        return;

    // Send in chunks that fit in a channel message (max ~8KB, use 4KB)
    constexpr size_t CHUNK = 4000;
    size_t offset = 0;
    while (offset < len) {
        size_t remaining = len - offset;
        size_t chunk = (remaining < CHUNK) ? remaining : CHUNK;

        int64_t r = sys::channel_send(
            g_output_channel,
            reinterpret_cast<const uint8_t *>(s) + offset,
            chunk, nullptr, 0);

        if (r == VERR_WOULD_BLOCK) {
            // Backpressure - retry with brief sleeps
            for (int retry = 0; retry < 50 && r == VERR_WOULD_BLOCK; retry++) {
                sys::sleep(1);
                r = sys::channel_send(
                    g_output_channel,
                    reinterpret_cast<const uint8_t *>(s) + offset,
                    chunk, nullptr, 0);
            }
            if (r < 0)
                return; // Drop data if terminal is not draining
        } else if (r < 0) {
            return; // Channel error
        }

        offset += chunk;
    }
}

void shell_print_char(char c) {
    if (g_output_channel < 0)
        return;
    int64_t r = sys::channel_send(g_output_channel, &c, 1, nullptr, 0);
    if (r == VERR_WOULD_BLOCK) {
        for (int retry = 0; retry < 20 && r == VERR_WOULD_BLOCK; retry++) {
            sys::sleep(1);
            r = sys::channel_send(g_output_channel, &c, 1, nullptr, 0);
        }
    }
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
        *--p = (digit < 10) ? static_cast<char>('0' + digit)
                            : static_cast<char>('a' + digit - 10);
        n >>= 4;
    } while (n > 0);

    shell_print(p);
}

// =========================================================================
// String Helpers (identical to shell_io.cpp - pure functions)
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
