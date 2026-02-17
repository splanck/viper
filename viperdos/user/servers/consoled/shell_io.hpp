//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ansi.hpp"
#include "text_buffer.hpp"
#include <gui.h>
#include <stddef.h>
#include <stdint.h>

namespace consoled {

/// Initialize the shell I/O subsystem with parser, buffer, and window pointers.
void shell_io_init(AnsiParser *parser, TextBuffer *buf, gui_window_t *window);

/// Initialize the shell I/O subsystem in PTY mode (output via channel).
void shell_io_init_pty(int32_t output_channel);

/// Get the TextBuffer pointer (for clear/redraw operations).
TextBuffer *shell_get_buffer();

/// Force a present if any characters have been written since last present.
void shell_io_flush();

// Output functions
void shell_print(const char *s);
void shell_print_char(char c);
void shell_put_num(int64_t n);
void shell_put_hex(uint32_t n);

// String helpers (shell_ prefix avoids conflicts with viperlibc/syscall.hpp)
size_t shell_strlen(const char *s);
bool shell_streq(const char *a, const char *b);
bool shell_strstart(const char *s, const char *prefix);
bool shell_strcaseeq(const char *a, const char *b);
bool shell_strcasestart(const char *s, const char *prefix);
void shell_strcpy(char *dst, const char *src, size_t max);

} // namespace consoled
