/*
 * File: rt_term.c
 * Purpose: Cross-platform terminal control (CLS, COLOR, LOCATE)
 *          and single-key input (blocking/non-blocking).
 * Behavior:
 *   - Only emits ANSI when stdout is a TTY.
 *   - Windows: enables VT processing, then uses ANSI.
 *   - LOCATE is 1-based (row, col).
 *   - COLOR: fg/bg -1 = leave unchanged; 0..7 normal; 8..15 bright; >=16 uses 256-color SGR.
 *   - GETKEY$ returns 1-char string (blocking).
 *   - INKEY$ returns "" if no key available (non-blocking).
 */

#include "rt.hpp"
#include <stdio.h>
#include <stdint.h>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <conio.h>
  #include <io.h>
  #define isatty _isatty
  #define fileno _fileno
#else
  #include <unistd.h>
  #include <termios.h>
  #include <sys/ioctl.h>
  #include <sys/select.h>
#endif

static int stdout_isatty(void) {
  int fd = fileno(stdout);
  return (fd >= 0) && isatty(fd);
}

#if defined(_WIN32)
static void enable_vt(void) {
  static int once = 0;
  if (once) return;
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
      mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
      SetConsoleMode(h, mode);
    }
  }
  once = 1;
}
#endif

static void out_str(const char* s) {
  if (!s) return;
#if defined(_WIN32)
  enable_vt();
#endif
  fputs(s, stdout);
  fflush(stdout);
}

static void sgr_color(int fg, int bg) {
  if (fg < 0 && bg < 0) {
    return;
  }
  char buf[64];
  int n = 0, wrote = 0;

  buf[n++] = '\x1b';
  buf[n++] = '[';

  if (fg >= 0) {
    if (fg <= 7) {
      n += snprintf(buf + n, sizeof(buf)-n, "%d", 30 + fg);
    } else if (fg <= 15) {
      n += snprintf(buf + n, sizeof(buf)-n, "1;%d", 30 + (fg - 8));
    } else {
      n += snprintf(buf + n, sizeof(buf)-n, "38;5;%d", fg);
    }
    wrote = 1;
  }
  if (bg >= 0) {
    if (wrote) buf[n++] = ';';
    if (bg <= 7) {
      n += snprintf(buf + n, sizeof(buf)-n, "%d", 40 + bg);
    } else if (bg <= 15) {
      n += snprintf(buf + n, sizeof(buf)-n, "%d", 100 + (bg - 8));
    } else {
      n += snprintf(buf + n, sizeof(buf)-n, "48;5;%d", bg);
    }
  }
  buf[n++] = 'm';
  buf[n] = '\0';
  out_str(buf);
}

void rt_term_cls(void) {
  if (!stdout_isatty()) return;
  out_str("\x1b[2J\x1b[H");
}

void rt_term_color_i32(int32_t fg, int32_t bg) {
  if (!stdout_isatty()) return;
  if (fg < -1 || bg < -1) return;
  sgr_color((int)fg, (int)bg);
}

void rt_term_locate_i32(int32_t row, int32_t col) {
  if (!stdout_isatty()) return;
  if (row < 1) row = 1;
  if (col < 1) col = 1;
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (int)row, (int)col);
  out_str(buf);
}

#if defined(_WIN32)
static int readkey_blocking(void) {
  return _getch() & 0xFF;
}
static int readkey_nonblocking(int* out) {
  if (_kbhit()) { *out = _getch() & 0xFF; return 1; }
  return 0;
}
#else
static int readkey_blocking(void) {
  struct termios orig, raw;
  int fd = fileno(stdin);
  if (tcgetattr(fd, &orig) != 0) return 0;
  raw = orig;
  raw.c_lflag &= ~(ICANON | ECHO);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(fd, TCSANOW, &raw) != 0) return 0;
  unsigned char ch = 0;
  ssize_t n = read(fd, &ch, 1);
  tcsetattr(fd, TCSANOW, &orig);
  return (n == 1) ? (int)ch : 0;
}
static int readkey_nonblocking(int* out) {
  struct termios orig, raw;
  int fd = fileno(stdin);
  if (tcgetattr(fd, &orig) != 0) return 0;
  raw = orig;
  raw.c_lflag &= ~(ICANON | ECHO);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(fd, TCSANOW, &raw) != 0) return 0;
  unsigned char ch = 0;
  ssize_t n = read(fd, &ch, 1);
  tcsetattr(fd, TCSANOW, &orig);
  if (n == 1) { *out = (int)ch; return 1; }
  return 0;
}
#endif

// NOTE: Use the existing empty-string constructor in your runtime.
// If rt_const_cstr("") is not available, use the canonical helper already
// used elsewhere to return "" (e.g., rt_empty_string()).

rt_string rt_getkey_str(void) {
  int code = readkey_blocking();
  return rt_chr((int64_t)code);
}

rt_string rt_inkey_str(void) {
  int code = 0;
  int ok = readkey_nonblocking(&code);
  if (ok) return rt_chr((int64_t)code);
  return rt_const_cstr(""); // use your runtime's empty-string helper
}

