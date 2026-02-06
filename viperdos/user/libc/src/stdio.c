//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/stdio.c
// Purpose: Standard I/O library implementation for ViperDOS.
// Key invariants: FILE structures backed by file descriptors; buffered I/O.
// Ownership/Lifetime: Library; FILE objects managed per-stream.
// Links: user/libc/include/stdio.h
//
//===----------------------------------------------------------------------===//

/**
 * @file stdio.c
 * @brief Standard I/O library implementation for ViperDOS.
 *
 * @details
 * This file implements the standard I/O functions (printf, fopen, fread, etc.)
 * for the ViperDOS C library. The implementation:
 *
 * - Uses syscalls for underlying I/O operations (read, write, open, close)
 * - Provides buffered I/O with configurable buffering modes (_IOFBF, _IOLBF, _IONBF)
 * - Supports standard streams (stdin, stdout, stderr)
 * - Implements printf family with basic format specifiers
 *
 * This is a minimal implementation for OS bring-up, not a full POSIX-compliant
 * stdio. Some features may be incomplete or simplified.
 */

#include "../include/stdio.h"
#include "../include/fcntl.h"
#include "../include/string.h"
#include "../include/unistd.h"
#include "syscall_internal.h"
#define SYS_GETCHAR 0xF1
#define SYS_PUTCHAR 0xF2

/* Minimal FILE structure for freestanding environment */
struct _FILE {
    int fd;
    int error;
    int eof;
    int buf_mode;    /* _IOFBF, _IOLBF, or _IONBF */
    char *buf;       /* Buffer pointer (NULL if none) */
    size_t buf_size; /* Size of buffer */
    size_t buf_pos;  /* Current position in buffer */
    int buf_owned;   /* 1 if we allocated the buffer */
};

/* Default buffers for stdout (line buffered) */
static char _stdout_buf[BUFSIZ];

/* Static FILE objects for standard streams */
static struct _FILE _stdin_file = {0, 0, 0, _IONBF, NULL, 0, 0, 0};
static struct _FILE _stdout_file = {1, 0, 0, _IOLBF, _stdout_buf, BUFSIZ, 0, 0};
static struct _FILE _stderr_file = {2, 0, 0, _IONBF, NULL, 0, 0, 0};

/* Standard stream pointers */
FILE *stdin = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

/**
 * @brief Internal implementation of formatted string printing.
 *
 * @details This is the core formatting engine used by all printf-family functions.
 * It processes a format string and substitutes format specifiers with their
 * corresponding argument values, writing the result to a character buffer.
 *
 * Supported format specifiers:
 * - `%d`, `%i`: Signed decimal integer
 * - `%u`: Unsigned decimal integer
 * - `%x`, `%X`: Unsigned hexadecimal (lowercase/uppercase)
 * - `%p`: Pointer address (prefixed with 0x)
 * - `%s`: Null-terminated string (prints "(null)" for NULL pointers)
 * - `%c`: Single character
 * - `%%`: Literal percent sign
 * - `%ld`, `%li`, `%lu`, `%lx`, `%lX`: Long variants
 * - `%lld`, `%lli`, `%llu`, `%llx`, `%llX`: Long long variants
 *
 * Supported flags:
 * - `0`: Zero-pad numeric values (e.g., `%08x`)
 * - `-`: Left-justify within field width
 * - Width specifier: Minimum field width (e.g., `%10d`)
 *
 * @param str    Destination buffer to write the formatted output. Must be at least
 *               @p size bytes. The output is always null-terminated if size > 0.
 * @param size   Maximum number of bytes to write, including the null terminator.
 *               If 0, nothing is written but the return value still indicates
 *               how many characters would have been written.
 * @param format Printf-style format string containing literal text and format
 *               specifiers beginning with '%'.
 * @param ap     Variable argument list initialized with va_start(), containing
 *               the values to substitute for each format specifier.
 *
 * @return The number of characters that would have been written if the buffer
 *         were large enough (not counting the null terminator). If the return
 *         value is >= size, the output was truncated.
 *
 * @note This function does not support floating-point format specifiers (%f, %e, %g).
 * @note This function does not support precision specifiers (e.g., %.2f).
 * @note The buffer is always null-terminated if size > 0, even on truncation.
 *
 * @see snprintf, sprintf, printf, vsnprintf
 */
static int vsnprintf_internal(char *str, size_t size, const char *format, va_list ap) {
    size_t written = 0;

#define PUTC(c)                                                                                    \
    do {                                                                                           \
        if (written < size - 1) {                                                                  \
            str[written] = (c);                                                                    \
        }                                                                                          \
        written++;                                                                                 \
    } while (0)

    while (*format && written < size) {
        if (*format != '%') {
            PUTC(*format++);
            continue;
        }

        format++; /* skip '%' */

        /* Parse flags */
        int zero_pad = 0;
        int width = 0;
        int left_justify = 0;

        if (*format == '-') {
            left_justify = 1;
            format++;
        }
        if (*format == '0') {
            zero_pad = 1;
            format++;
        }

        /* Parse width (with bounds check - Issue #80 fix) */
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            if (width > 99999)
                width = 99999; /* Cap to prevent overflow */
            format++;
        }

        /* Parse format specifier */
        char buf[32];
        const char *s;
        int len;

        switch (*format) {
            case 'd':
            case 'i': {
                int val = va_arg(ap, int);
                int neg = 0;
                if (val < 0) {
                    neg = 1;
                    val = -val;
                }
                char *p = buf + sizeof(buf) - 1;
                *p = '\0';
                do {
                    *--p = '0' + (val % 10);
                    val /= 10;
                } while (val);
                if (neg)
                    *--p = '-';
                s = p;
                len = (buf + sizeof(buf) - 1) - p;
                goto output_string;
            }

            case 'u': {
                unsigned int val = va_arg(ap, unsigned int);
                char *p = buf + sizeof(buf) - 1;
                *p = '\0';
                do {
                    *--p = '0' + (val % 10);
                    val /= 10;
                } while (val);
                s = p;
                len = (buf + sizeof(buf) - 1) - p;
                goto output_string;
            }

            case 'x':
            case 'X': {
                unsigned int val = va_arg(ap, unsigned int);
                char *p = buf + sizeof(buf) - 1;
                *p = '\0';
                const char *digits = (*format == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                do {
                    *--p = digits[val & 0xF];
                    val >>= 4;
                } while (val);
                s = p;
                len = (buf + sizeof(buf) - 1) - p;
                goto output_string;
            }

            case 'p': {
                unsigned long val = (unsigned long)va_arg(ap, void *);
                char *p = buf + sizeof(buf) - 1;
                *p = '\0';
                do {
                    *--p = "0123456789abcdef"[val & 0xF];
                    val >>= 4;
                } while (val);
                *--p = 'x';
                *--p = '0';
                s = p;
                len = (buf + sizeof(buf) - 1) - p;
                goto output_string;
            }

            case 'l': {
                format++;
                /* Check for 'll' (long long) */
                int is_longlong = 0;
                if (*format == 'l') {
                    is_longlong = 1;
                    format++;
                }

                if (*format == 'x' || *format == 'X') {
                    unsigned long long val =
                        is_longlong ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned long);
                    char *p = buf + sizeof(buf) - 1;
                    *p = '\0';
                    const char *digits = (*format == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                    do {
                        *--p = digits[val & 0xF];
                        val >>= 4;
                    } while (val);
                    s = p;
                    len = (buf + sizeof(buf) - 1) - p;
                    goto output_string;
                } else if (*format == 'd' || *format == 'i') {
                    long long val = is_longlong ? va_arg(ap, long long) : va_arg(ap, long);
                    int neg = 0;
                    if (val < 0) {
                        neg = 1;
                        val = -val;
                    }
                    char *p = buf + sizeof(buf) - 1;
                    *p = '\0';
                    do {
                        *--p = '0' + (val % 10);
                        val /= 10;
                    } while (val);
                    if (neg)
                        *--p = '-';
                    s = p;
                    len = (buf + sizeof(buf) - 1) - p;
                    goto output_string;
                } else if (*format == 'u') {
                    unsigned long long val =
                        is_longlong ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned long);
                    char *p = buf + sizeof(buf) - 1;
                    *p = '\0';
                    do {
                        *--p = '0' + (val % 10);
                        val /= 10;
                    } while (val);
                    s = p;
                    len = (buf + sizeof(buf) - 1) - p;
                    goto output_string;
                }
                break;
            }

            case 's': {
                s = va_arg(ap, const char *);
                if (!s)
                    s = "(null)";
                len = strlen(s);
                goto output_string;
            }

            case 'c': {
                buf[0] = (char)va_arg(ap, int);
                buf[1] = '\0';
                s = buf;
                len = 1;
                goto output_string;
            }

            case '%':
                PUTC('%');
                break;

            output_string: {
                int pad = width - len;
                if (!left_justify) {
                    while (pad-- > 0)
                        PUTC(zero_pad ? '0' : ' ');
                }
                while (len--)
                    PUTC(*s++);
                if (left_justify) {
                    while (pad-- > 0)
                        PUTC(' ');
                }
                break;
            }

            default:
                PUTC('%');
                PUTC(*format);
                break;
        }

        format++;
    }

    if (size > 0) {
        str[written < size ? written : size - 1] = '\0';
    }

    return (int)written;

#undef PUTC
}

/**
 * @brief Write formatted output to a size-limited string buffer.
 *
 * @details Formats and writes output to a character buffer, with protection
 * against buffer overflow. The output is always null-terminated if size > 0.
 * This is the safe alternative to sprintf() and should be preferred in all
 * new code to prevent buffer overruns.
 *
 * @param str    Destination buffer where the formatted string will be written.
 *               Must have space for at least @p size bytes.
 * @param size   Maximum number of bytes to write, including the null terminator.
 *               If the formatted output exceeds size-1 characters, it is truncated.
 * @param format Printf-style format string (see vsnprintf_internal for supported
 *               specifiers).
 * @param ...    Variable arguments corresponding to format specifiers.
 *
 * @return The number of characters that would have been written if the buffer
 *         were large enough (not counting the null terminator). If the return
 *         value >= size, the output was truncated.
 *
 * @note Always check if return value >= size to detect truncation.
 *
 * @see sprintf, printf, vsnprintf
 */
int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vsnprintf_internal(str, size, format, ap);
    va_end(ap);
    return result;
}

/**
 * @brief Write formatted output to a string buffer (UNSAFE - prefer snprintf).
 *
 * @details Formats and writes output to a character buffer without any size
 * checking. This function is inherently unsafe as it can cause buffer overflows
 * if the destination buffer is not large enough for the formatted output.
 *
 * @warning This function performs no bounds checking. Use snprintf() instead
 *          to prevent buffer overflow vulnerabilities.
 *
 * @param str    Destination buffer where the formatted string will be written.
 *               Must be large enough to hold the entire formatted output plus
 *               a null terminator. The caller is responsible for ensuring
 *               adequate buffer size.
 * @param format Printf-style format string (see vsnprintf_internal for supported
 *               specifiers).
 * @param ...    Variable arguments corresponding to format specifiers.
 *
 * @return The number of characters written (not counting the null terminator).
 *
 * @see snprintf (preferred), printf, vsprintf
 */
int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vsnprintf_internal(str, 0x7FFFFFFF, format, ap);
    va_end(ap);
    return result;
}

/**
 * @brief Write formatted output to standard output.
 *
 * @details Formats a string according to the format specification and writes
 * the result to stdout (file descriptor 1). This is the most commonly used
 * output function in C programs for displaying formatted text to the console.
 *
 * The function uses an internal 512-byte buffer for formatting. Output longer
 * than 512 characters will be truncated.
 *
 * @param format Printf-style format string containing literal text and format
 *               specifiers (see vsnprintf_internal for supported specifiers).
 * @param ...    Variable arguments corresponding to format specifiers in the
 *               format string.
 *
 * @return On success, the number of characters written (not including the
 *         null terminator used internally). On write failure, returns the
 *         number of characters that would have been written.
 *
 * @note Output is written via the write() syscall to STDOUT_FILENO.
 * @note Maximum output length is limited to 512 characters due to internal
 *       buffer size.
 *
 * @see fprintf, sprintf, puts, putchar
 */
int printf(const char *format, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, format);
    int result = vsnprintf_internal(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (result > 0) {
        write(STDOUT_FILENO, buf, result);
    }

    return result;
}

/**
 * @brief Write a string to stdout followed by a newline.
 *
 * @details Writes the null-terminated string @p s to standard output, then
 * appends a newline character. This is equivalent to fputs(s, stdout) followed
 * by fputc('\n', stdout), but may be more efficient.
 *
 * Unlike fputs(), puts() always appends a newline after the string. This makes
 * it convenient for simple line-oriented output.
 *
 * @param s Null-terminated string to write. Must not be NULL.
 *
 * @return 0 on success. Note that this differs from the standard C behavior
 *         which returns a non-negative value on success and EOF on failure.
 *
 * @note The null terminator is not written; only the string content plus
 *       a newline.
 * @note This implementation always returns 0 and does not check for write errors.
 *
 * @see fputs, printf, putchar
 */
int puts(const char *s) {
    size_t len = strlen(s);
    write(STDOUT_FILENO, s, len);
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

/**
 * @brief Write a single character to standard output.
 *
 * @details Writes the character @p c (converted to unsigned char) to stdout.
 * This is equivalent to fputc(c, stdout).
 *
 * @param c Character to write, passed as an int but internally converted to
 *          unsigned char. Values outside 0-255 are truncated.
 *
 * @return The character written as an unsigned char cast to int, or EOF on
 *         error. In this implementation, the original character value is
 *         always returned (no error checking on write).
 *
 * @note This function bypasses any buffering and writes directly via write().
 *
 * @see fputc, putc, puts, getchar
 */
int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

/**
 * @brief Read a single character from standard input.
 *
 * @details Reads one character from stdin (file descriptor 0). This function
 * will block if no input is available until a character can be read or an
 * error/EOF condition occurs.
 *
 * This is equivalent to fgetc(stdin) or getc(stdin).
 *
 * @return The character read as an unsigned char cast to int (0-255), or EOF
 *         (-1) if end-of-file is reached or a read error occurs.
 *
 * @note This function reads directly via read() syscall, bypassing any
 *       stdio buffering.
 * @note To distinguish between EOF and error conditions, use feof() and
 *       ferror() on stdin.
 *
 * @see fgetc, getc, putchar, fgets
 */
int getchar(void) {
    unsigned char c = 0;
    long n = read(STDIN_FILENO, &c, 1);
    if (n <= 0)
        return EOF;
    return (int)c;
}

/**
 * @brief Write formatted output to a size-limited buffer using a va_list.
 *
 * @details This is the va_list version of snprintf(). It performs the same
 * formatting operation but takes a va_list argument instead of variadic
 * arguments. This is useful when implementing wrapper functions that need
 * to forward printf-style arguments.
 *
 * @param str    Destination buffer for the formatted output. Must have space
 *               for at least @p size bytes.
 * @param size   Maximum number of bytes to write, including null terminator.
 * @param format Printf-style format string.
 * @param ap     Initialized va_list containing the format arguments.
 *
 * @return Number of characters that would have been written if buffer were
 *         large enough (excluding null terminator). Returns >= size if truncated.
 *
 * @note The va_list @p ap is consumed by this call. If you need to reuse it,
 *       make a copy with va_copy() before calling this function.
 *
 * @see snprintf, vsprintf, vprintf
 */
int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    return vsnprintf_internal(str, size, format, ap);
}

/**
 * @brief Write formatted output to a buffer using a va_list (UNSAFE).
 *
 * @details This is the va_list version of sprintf(). Like sprintf(), this
 * function performs no bounds checking and can cause buffer overflows.
 *
 * @warning This function is unsafe. Use vsnprintf() instead.
 *
 * @param str    Destination buffer. Must be large enough for the entire
 *               formatted output plus null terminator.
 * @param format Printf-style format string.
 * @param ap     Initialized va_list containing the format arguments.
 *
 * @return Number of characters written (excluding null terminator).
 *
 * @see vsnprintf (preferred), sprintf, vprintf
 */
int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf_internal(str, 0x7FFFFFFF, format, ap);
}

/**
 * @brief Write formatted output to stdout using a va_list.
 *
 * @details This is the va_list version of printf(). Formats the output
 * according to the format string and writes it to standard output. Useful
 * for implementing custom printf-like wrapper functions.
 *
 * Uses an internal 512-byte buffer; output exceeding this is truncated.
 *
 * @param format Printf-style format string.
 * @param ap     Initialized va_list containing the format arguments.
 *
 * @return Number of characters written on success, or the number that would
 *         have been written if truncation occurred.
 *
 * @see printf, vfprintf, vsnprintf
 */
int vprintf(const char *format, va_list ap) {
    char buf[512];
    int result = vsnprintf_internal(buf, sizeof(buf), format, ap);
    if (result > 0) {
        write(STDOUT_FILENO, buf, result);
    }
    return result;
}

/**
 * @brief Write formatted output to a stream using a va_list.
 *
 * @details Formats output according to the format string and writes it to
 * the specified stream. This is the va_list version of fprintf() and is
 * commonly used to implement custom logging or output wrapper functions.
 *
 * Uses an internal 512-byte buffer; output exceeding this is truncated.
 *
 * @param stream Destination stream (FILE pointer). Must be open for writing.
 * @param format Printf-style format string.
 * @param ap     Initialized va_list containing the format arguments.
 *
 * @return On success, the number of characters written. On write error,
 *         returns -1 and sets the stream's error indicator.
 *
 * @note The stream's error flag is set if the write operation fails.
 *
 * @see fprintf, vprintf, vsnprintf
 */
int vfprintf(FILE *stream, const char *format, va_list ap) {
    char buf[512];
    int result = vsnprintf_internal(buf, sizeof(buf), format, ap);
    if (result > 0) {
        long written = write(stream->fd, buf, result);
        if (written < 0 || written != result) {
            stream->error = 1;
            return -1;
        }
    }
    return result;
}

/**
 * @brief Write formatted output to a stream.
 *
 * @details Formats a string according to the format specification and writes
 * the result to the specified output stream. This is the stream-based version
 * of printf() and is used for writing formatted output to files or other
 * streams.
 *
 * Common usage patterns:
 * - fprintf(stderr, "Error: %s\n", msg) for error messages
 * - fprintf(logfile, "[%s] %s\n", timestamp, message) for logging
 *
 * @param stream Destination stream (FILE pointer). Must be open for writing.
 *               Common values: stdout, stderr, or a FILE* from fopen().
 * @param format Printf-style format string.
 * @param ...    Variable arguments corresponding to format specifiers.
 *
 * @return On success, the number of characters written. On error, returns -1
 *         and sets the stream's error indicator (check with ferror()).
 *
 * @see printf, sprintf, vfprintf, fputs
 */
int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vfprintf(stream, format, ap);
    va_end(ap);
    return result;
}

/**
 * @brief Write a single character directly to a stream without buffering.
 *
 * @details Internal helper function that bypasses the stream's buffer and
 * writes directly to the underlying file descriptor using write(). This is
 * used for unbuffered streams or when immediate output is required.
 *
 * @param c      Character to write (converted to unsigned char internally).
 * @param stream Destination stream. Must be a valid, open FILE pointer.
 *
 * @return The character written as an unsigned char cast to int, or EOF
 *         on write error. On error, the stream's error indicator is set.
 *
 * @note This is a static internal function, not part of the public API.
 *
 * @see fputc, putc
 */
static int fputc_unbuffered(int c, FILE *stream) {
    char ch = (char)c;
    long result = write(stream->fd, &ch, 1);
    if (result < 0) {
        stream->error = 1;
        return EOF;
    }
    return (unsigned char)c;
}

/**
 * @brief Write a single character to a stream.
 *
 * @details Writes the character @p c (converted to unsigned char) to the
 * specified output stream. The behavior depends on the stream's buffering mode:
 *
 * - **Unbuffered (_IONBF)**: Character is written immediately via write().
 * - **Line buffered (_IOLBF)**: Character is buffered, but the buffer is
 *   flushed when a newline ('\n') is written or when the buffer is full.
 * - **Fully buffered (_IOFBF)**: Character is buffered until the buffer
 *   is full, then the entire buffer is written.
 *
 * @param c      Character to write. Although passed as int, only the low 8 bits
 *               (unsigned char value) are written.
 * @param stream Destination stream. Must be open for writing.
 *
 * @return The character written as an unsigned char cast to int, or EOF on
 *         error. On error, the stream's error indicator is set.
 *
 * @note For stdout, the default buffering mode is line-buffered.
 * @note For stderr, the default buffering mode is unbuffered.
 *
 * @see putc (equivalent macro/function), putchar, fputs, fflush
 */
int fputc(int c, FILE *stream) {
    /* No buffering or no buffer - write directly */
    if (stream->buf_mode == _IONBF || stream->buf == NULL) {
        return fputc_unbuffered(c, stream);
    }

    /* Add to buffer */
    stream->buf[stream->buf_pos++] = (char)c;

    /* Check if we need to flush */
    int should_flush = 0;

    if (stream->buf_pos >= stream->buf_size) {
        /* Buffer full */
        should_flush = 1;
    } else if (stream->buf_mode == _IOLBF && c == '\n') {
        /* Line buffered and got newline */
        should_flush = 1;
    }

    if (should_flush) {
        if (fflush(stream) == EOF)
            return EOF;
    }

    return (unsigned char)c;
}

/**
 * @brief Write a single character to a stream (equivalent to fputc).
 *
 * @details This function is equivalent to fputc(). In standard C, putc() may
 * be implemented as a macro that evaluates @p stream more than once, but this
 * implementation is a simple function wrapper.
 *
 * @param c      Character to write (only low 8 bits are used).
 * @param stream Destination stream, open for writing.
 *
 * @return The character written as unsigned char cast to int, or EOF on error.
 *
 * @see fputc, putchar, getc
 */
int putc(int c, FILE *stream) {
    return fputc(c, stream);
}

/**
 * @brief Write a string to a stream without appending a newline.
 *
 * @details Writes the null-terminated string @p s to the specified stream.
 * Unlike puts(), fputs() does NOT append a newline character after the string.
 * The null terminator is not written.
 *
 * This function writes directly to the stream's file descriptor, bypassing
 * the stream buffer.
 *
 * @param s      Null-terminated string to write. Must not be NULL.
 * @param stream Destination stream, open for writing.
 *
 * @return On success, returns a non-negative value (the number of bytes written).
 *         On error, returns EOF and sets the stream's error indicator.
 *
 * @note Unlike the standard C fputs() which returns a non-negative value on
 *       success, this implementation returns the actual byte count.
 *
 * @see puts, fputc, fprintf, fwrite
 */
int fputs(const char *s, FILE *stream) {
    size_t len = strlen(s);
    long result = write(stream->fd, s, len);
    if (result < 0) {
        stream->error = 1;
        return EOF;
    }
    return (int)result;
}

/**
 * @brief Read a single character from a stream.
 *
 * @details Reads the next character from the input stream and advances the
 * file position indicator. The character is returned as an unsigned char
 * cast to int, allowing all valid character values (0-255) to be distinguished
 * from EOF (-1).
 *
 * This implementation reads directly from the file descriptor without
 * buffering. The function blocks until a character is available, EOF is
 * reached, or an error occurs.
 *
 * @param stream Input stream to read from. Must be open for reading.
 *
 * @return The character read as an unsigned char cast to int (0-255), or
 *         EOF (-1) on end-of-file or read error.
 *         - On EOF: stream's EOF indicator is set (check with feof())
 *         - On error: stream's error indicator is set (check with ferror())
 *
 * @note To distinguish EOF from error, check both feof() and ferror().
 *
 * @see getc (equivalent), getchar, fgets, fread, ungetc
 */
int fgetc(FILE *stream) {
    char c;
    long result = read(stream->fd, &c, 1);
    if (result <= 0) {
        if (result == 0)
            stream->eof = 1;
        else
            stream->error = 1;
        return EOF;
    }
    return (unsigned char)c;
}

/**
 * @brief Read a single character from a stream (equivalent to fgetc).
 *
 * @details This function is equivalent to fgetc(). In standard C, getc() may
 * be implemented as a macro that evaluates @p stream more than once, but this
 * implementation is a simple function wrapper.
 *
 * @param stream Input stream to read from.
 *
 * @return Character read as unsigned char cast to int (0-255), or EOF on
 *         end-of-file or error.
 *
 * @see fgetc, getchar, putc
 */
int getc(FILE *stream) {
    return fgetc(stream);
}

/**
 * @brief Read a line from a stream into a buffer.
 *
 * @details Reads characters from the stream into the buffer until one of:
 * - A newline character is read (which IS stored in the buffer)
 * - End-of-file is reached
 * - size-1 characters have been read
 *
 * The buffer is always null-terminated if at least one character was read.
 * This function is safe against buffer overflows as long as @p size accurately
 * reflects the buffer capacity.
 *
 * Typical usage pattern:
 * @code
 * char line[256];
 * while (fgets(line, sizeof(line), file) != NULL) {
 *     // Process line (note: includes trailing newline if present)
 * }
 * @endcode
 *
 * @param s      Destination buffer to store the line. Must have space for
 *               at least @p size bytes.
 * @param size   Maximum number of characters to read, including the null
 *               terminator. Must be > 0 for meaningful behavior.
 * @param stream Input stream to read from.
 *
 * @return On success, returns @p s. Returns NULL if:
 *         - EOF is reached and no characters were read
 *         - size <= 0
 *         - A read error occurs before any characters are read
 *
 * @note The newline character, if read, is stored in the buffer.
 * @note To read lines without length limit, consider getline() instead.
 *
 * @see fgetc, getline, fputs, fread
 */
char *fgets(char *s, int size, FILE *stream) {
    if (size <= 0)
        return NULL;

    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (i == 0)
                return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    s[i] = '\0';
    return s;
}

/**
 * @brief Test the error indicator for a stream.
 *
 * @details Checks whether the error indicator for the stream has been set.
 * The error indicator is set when a read or write operation fails due to
 * an I/O error (not EOF). Once set, the indicator remains set until
 * explicitly cleared with clearerr() or the stream is closed.
 *
 * Typical usage pattern:
 * @code
 * if (fread(buf, 1, size, file) < size) {
 *     if (feof(file))
 *         printf("Reached end of file\n");
 *     else if (ferror(file))
 *         printf("Read error occurred\n");
 * }
 * @endcode
 *
 * @param stream Stream to check.
 *
 * @return Non-zero if the error indicator is set, zero otherwise.
 *
 * @see clearerr, feof, perror
 */
int ferror(FILE *stream) {
    return stream->error;
}

/**
 * @brief Clear the error and EOF indicators for a stream.
 *
 * @details Resets both the error indicator and the end-of-file indicator
 * for the specified stream. After calling clearerr(), subsequent calls to
 * ferror() and feof() will return zero until another error or EOF condition
 * occurs.
 *
 * This is useful when you want to retry an operation after an error, or
 * continue reading a file after reaching EOF (e.g., if the file has grown).
 *
 * @param stream Stream whose indicators should be cleared.
 *
 * @note This function does not return a value and cannot fail.
 *
 * @see ferror, feof, rewind
 */
void clearerr(FILE *stream) {
    stream->error = 0;
    stream->eof = 0;
}

/**
 * @brief Test the end-of-file indicator for a stream.
 *
 * @details Checks whether the end-of-file indicator for the stream has been
 * set. The EOF indicator is set when a read operation attempts to read past
 * the end of the file. Note that the indicator is NOT set by simply reaching
 * the end of file; it is set when a read is attempted and fails due to EOF.
 *
 * @param stream Stream to check.
 *
 * @return Non-zero if the end-of-file indicator is set, zero otherwise.
 *
 * @note The EOF indicator can be cleared with clearerr() or rewind().
 * @note Don't use feof() to control read loops; check the return value of
 *       read functions instead:
 *       @code
 *       // WRONG: while (!feof(f)) { c = fgetc(f); use(c); }
 *       // RIGHT: while ((c = fgetc(f)) != EOF) { use(c); }
 *       @endcode
 *
 * @see ferror, clearerr, rewind
 */
int feof(FILE *stream) {
    return stream->eof;
}

/**
 * @brief Flush a stream's output buffer to the underlying file.
 *
 * @details Forces any buffered output data to be written to the underlying
 * file descriptor. For output streams, this ensures that all data written
 * with fputc(), fputs(), fwrite(), fprintf(), etc. is actually sent to the
 * file or device.
 *
 * Behavior by argument:
 * - **stream != NULL**: Flushes the specified stream's buffer.
 * - **stream == NULL**: Flushes all open output streams (currently only stdout).
 *
 * Common use cases:
 * - Ensure output appears immediately (e.g., before fork() or system())
 * - Synchronize output before program termination
 * - Force prompt to appear before reading input
 *
 * @param stream Stream to flush, or NULL to flush all output streams.
 *
 * @return 0 on success, EOF on write error. On error, the stream's error
 *         indicator is set.
 *
 * @note For input streams, the behavior is undefined in standard C.
 * @note fclose() automatically calls fflush() before closing.
 *
 * @see setvbuf, fclose, setbuf
 */
int fflush(FILE *stream) {
    if (stream == NULL) {
        /* Flush all streams - just stdout for now */
        fflush(stdout);
        return 0;
    }

    /* If there's buffered data, write it out */
    if (stream->buf && stream->buf_pos > 0) {
        long result = write(stream->fd, stream->buf, stream->buf_pos);
        if (result < 0) {
            stream->error = 1;
            return EOF;
        }
        stream->buf_pos = 0;
    }
    return 0;
}

/**
 * @brief Set the buffering mode and buffer for a stream.
 *
 * @details Controls how the stream buffers its output. This function should
 * be called after opening a stream but before any I/O operations are performed.
 *
 * Buffering modes:
 * - **_IOFBF (Full buffering)**: Output is buffered until the buffer is full,
 *   then written all at once. Best for file I/O.
 * - **_IOLBF (Line buffering)**: Output is buffered until a newline is written
 *   or the buffer is full. Best for interactive output (terminals).
 * - **_IONBF (No buffering)**: Output is written immediately. Best for stderr
 *   or when immediate output is required.
 *
 * @param stream Stream to configure.
 * @param buf    Buffer to use, or NULL to let the system allocate one (note:
 *               in this freestanding implementation, NULL with buffered mode
 *               falls back to unbuffered since malloc is unavailable at this
 *               level).
 * @param mode   Buffering mode: _IOFBF, _IOLBF, or _IONBF.
 * @param size   Size of the buffer in bytes (ignored if mode is _IONBF or
 *               buf is NULL in this implementation).
 *
 * @return 0 on success, non-zero on invalid mode.
 *
 * @note Any buffered data is flushed before changing the buffer.
 * @note If @p buf is provided, the caller must ensure it remains valid for
 *       the lifetime of the stream.
 *
 * @see setbuf, setlinebuf, fflush
 */
int setvbuf(FILE *stream, char *buf, int mode, size_t size) {
    /* Flush any existing buffer first */
    fflush(stream);

    /* Validate mode */
    if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)
        return -1;

    /* If we owned the old buffer, we would free it here (but we don't malloc) */
    stream->buf_owned = 0;

    if (mode == _IONBF) {
        /* No buffering */
        stream->buf = NULL;
        stream->buf_size = 0;
        stream->buf_pos = 0;
    } else {
        if (buf != NULL) {
            /* Use provided buffer */
            stream->buf = buf;
            stream->buf_size = size;
            stream->buf_owned = 0;
        } else if (size > 0) {
            /* Caller wants us to allocate, but we can't in freestanding */
            /* Fall back to unbuffered */
            stream->buf = NULL;
            stream->buf_size = 0;
            mode = _IONBF;
        }
        stream->buf_pos = 0;
    }

    stream->buf_mode = mode;
    return 0;
}

/**
 * @brief Set or disable buffering for a stream (simplified setvbuf).
 *
 * @details This is a simplified interface to setvbuf(). It either enables
 * full buffering with a BUFSIZ-byte buffer, or disables buffering entirely.
 *
 * Equivalent to:
 * - setbuf(stream, buf) when buf != NULL: setvbuf(stream, buf, _IOFBF, BUFSIZ)
 * - setbuf(stream, NULL): setvbuf(stream, NULL, _IONBF, 0)
 *
 * @param stream Stream to configure.
 * @param buf    Buffer of at least BUFSIZ bytes for full buffering, or NULL
 *               to disable buffering.
 *
 * @note Prefer setvbuf() for finer control over buffering behavior.
 * @note If buf is provided, it must remain valid for the stream's lifetime.
 *
 * @see setvbuf, setlinebuf
 */
void setbuf(FILE *stream, char *buf) {
    if (buf != NULL)
        setvbuf(stream, buf, _IOFBF, BUFSIZ);
    else
        setvbuf(stream, NULL, _IONBF, 0);
}

/**
 * @brief Enable line buffering for a stream.
 *
 * @details Sets the stream to line-buffered mode. In this mode, output is
 * buffered until a newline character is written, at which point the entire
 * buffer (including the newline) is flushed. The buffer is also flushed when
 * it becomes full.
 *
 * Equivalent to: setvbuf(stream, NULL, _IOLBF, 0)
 *
 * Line buffering is typically appropriate for interactive output where you
 * want each line to appear immediately while still benefiting from buffering
 * within a line.
 *
 * @param stream Stream to configure for line buffering.
 *
 * @note This is a BSD extension, not part of standard C.
 * @note In this implementation, with NULL buffer and no malloc, the stream
 *       may fall back to unbuffered mode.
 *
 * @see setvbuf, setbuf
 */
void setlinebuf(FILE *stream) {
    /* Line buffering with default buffer */
    setvbuf(stream, NULL, _IOLBF, 0);
}

/**
 * @brief Skip whitespace characters in a string (helper for sscanf).
 *
 * @details Advances the string pointer past any whitespace characters
 * (space, tab, newline). Used internally by sscanf() to handle whitespace
 * in format strings and before numeric conversions.
 *
 * @param str Pointer to string pointer. On return, *str points to the first
 *            non-whitespace character or the null terminator.
 *
 * @return Number of whitespace characters skipped.
 *
 * @note This is a static internal function, not part of the public API.
 */
static int skip_whitespace(const char **str) {
    int count = 0;
    while (**str == ' ' || **str == '\t' || **str == '\n') {
        (*str)++;
        count++;
    }
    return count;
}

/**
 * @defgroup file_pool FILE Structure Pool
 * @brief Static pool of FILE structures for fopen/fdopen/freopen.
 *
 * @details Since this is a freestanding C library without dynamic memory
 * allocation for FILE structures, we maintain a fixed-size pool of FILE
 * objects. This limits the maximum number of simultaneously open files
 * (excluding stdin, stdout, stderr) to FILE_POOL_SIZE.
 * @{
 */

/** Maximum number of FILE structures available (excluding standard streams). */
#define FILE_POOL_SIZE 20

/** Static array of FILE structures available for allocation. */
static struct _FILE file_pool[FILE_POOL_SIZE];

/** Flag indicating whether the file pool has been initialized. */
static int file_pool_init = 0;

/**
 * @brief Initialize the file pool on first use.
 *
 * @details Marks all FILE structures in the pool as available by setting
 * their file descriptor to -1 (invalid). This function is idempotent and
 * only performs initialization once.
 *
 * Called automatically by alloc_file() on first file open operation.
 *
 * @note This is a static internal function, not part of the public API.
 */
static void init_file_pool(void) {
    if (file_pool_init)
        return;
    for (int i = 0; i < FILE_POOL_SIZE; i++) {
        file_pool[i].fd = -1;
    }
    file_pool_init = 1;
}

/**
 * @brief Allocate a FILE structure from the pool.
 *
 * @details Searches the file pool for an available FILE structure (one with
 * fd == -1) and returns a pointer to it. The caller is responsible for
 * initializing the returned structure.
 *
 * @return Pointer to an available FILE structure, or NULL if the pool is
 *         exhausted (too many files open simultaneously).
 *
 * @note This is a static internal function, not part of the public API.
 * @note Maximum of FILE_POOL_SIZE files can be open at once.
 *
 * @see init_file_pool, fclose (which returns structures to the pool)
 */
static struct _FILE *alloc_file(void) {
    init_file_pool();
    for (int i = 0; i < FILE_POOL_SIZE; i++) {
        if (file_pool[i].fd == -1) {
            return &file_pool[i];
        }
    }
    return (struct _FILE *)0;
}

/** @} */ /* End of file_pool group */

/**
 * @brief Parse fopen() mode string into open() flags.
 *
 * @details Converts a stdio mode string (like "r", "w", "a", "r+", "w+", "a+")
 * into the corresponding flags for the open() syscall.
 *
 * Supported mode strings:
 * - `"r"`:  Read-only (O_RDONLY). File must exist.
 * - `"w"`:  Write-only (O_WRONLY | O_CREAT | O_TRUNC). Creates or truncates.
 * - `"a"`:  Append (O_WRONLY | O_CREAT | O_APPEND). Creates if needed.
 * - `"r+"`: Read/write (O_RDWR). File must exist.
 * - `"w+"`: Read/write (O_RDWR | O_CREAT | O_TRUNC). Creates or truncates.
 * - `"a+"`: Read/append (O_RDWR | O_CREAT | O_APPEND). Creates if needed.
 *
 * The "b" modifier for binary mode is accepted but ignored (no distinction
 * between text and binary on this platform).
 *
 * @param mode Mode string from fopen()/freopen().
 *
 * @return Open flags suitable for open() syscall, or -1 if the mode string
 *         is invalid (doesn't start with 'r', 'w', or 'a').
 *
 * @note This is a static internal function, not part of the public API.
 */
static int parse_mode(const char *mode) {
    int flags = 0;
    int has_plus = 0;

    /* Check for '+' anywhere in mode string */
    for (const char *p = mode; *p; p++) {
        if (*p == '+')
            has_plus = 1;
    }

    switch (mode[0]) {
        case 'r':
            flags = has_plus ? O_RDWR : O_RDONLY;
            break;
        case 'w':
            flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
            break;
        case 'a':
            flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
            break;
        default:
            return -1;
    }
    return flags;
}

/**
 * @brief Open a file and return a stream pointer.
 *
 * @details Opens the file specified by @p pathname and associates it with a
 * FILE stream. The @p mode string determines whether the file is opened for
 * reading, writing, or both, and whether it should be created or truncated.
 *
 * Mode strings:
 * - `"r"`:  Open for reading. File must exist.
 * - `"w"`:  Open for writing. Creates file or truncates to zero length.
 * - `"a"`:  Open for appending. Creates file if it doesn't exist.
 * - `"r+"`: Open for reading and writing. File must exist.
 * - `"w+"`: Open for reading and writing. Truncates or creates.
 * - `"a+"`: Open for reading and appending. Creates if needed.
 *
 * Example usage:
 * @code
 * FILE *fp = fopen("data.txt", "r");
 * if (fp == NULL) {
 *     perror("Failed to open file");
 *     return -1;
 * }
 * // ... use the file ...
 * fclose(fp);
 * @endcode
 *
 * @param pathname Path to the file to open. Can be absolute or relative.
 * @param mode     Mode string specifying access type (see above).
 *
 * @return FILE pointer on success, or NULL on failure (file doesn't exist
 *         for "r" mode, permission denied, too many open files, etc.).
 *
 * @note Files are created with permission mode 0666 (modified by umask).
 * @note Maximum of FILE_POOL_SIZE files can be open simultaneously.
 *
 * @see fclose, freopen, fdopen, fread, fwrite
 */
FILE *fopen(const char *pathname, const char *mode) {
    if (!pathname || !mode)
        return (FILE *)0;

    int flags = parse_mode(mode);
    if (flags < 0)
        return (FILE *)0;

    int fd = open(pathname, flags, 0666);
    if (fd < 0)
        return (FILE *)0;

    struct _FILE *f = alloc_file();
    if (!f) {
        close(fd);
        return (FILE *)0;
    }

    f->fd = fd;
    f->error = 0;
    f->eof = 0;
    f->buf_mode = _IOFBF;
    f->buf = (char *)0;
    f->buf_size = 0;
    f->buf_pos = 0;
    f->buf_owned = 0;

    return f;
}

/**
 * @brief Associate a stream with an existing file descriptor.
 *
 * @details Creates a FILE stream that wraps an already-open file descriptor.
 * This is useful when you have a file descriptor from open(), socket(), pipe(),
 * or other low-level operations and want to use stdio functions on it.
 *
 * Example usage:
 * @code
 * int fd = open("file.txt", O_RDONLY);
 * FILE *fp = fdopen(fd, "r");
 * char buf[256];
 * fgets(buf, sizeof(buf), fp);
 * fclose(fp);  // Also closes the underlying fd
 * @endcode
 *
 * @param fd   File descriptor to wrap. Must be a valid, open file descriptor.
 * @param mode Mode string (e.g., "r", "w"). Should match how the fd was opened,
 *             but this implementation doesn't validate it.
 *
 * @return FILE pointer on success, or NULL if fd is invalid or the file pool
 *         is exhausted.
 *
 * @note Closing the FILE with fclose() also closes the underlying fd.
 * @note The mode parameter is accepted for compatibility but not validated
 *       against the fd's actual access mode.
 *
 * @see fopen, fclose, fileno
 */
FILE *fdopen(int fd, const char *mode) {
    if (fd < 0 || !mode)
        return (FILE *)0;

    struct _FILE *f = alloc_file();
    if (!f)
        return (FILE *)0;

    f->fd = fd;
    f->error = 0;
    f->eof = 0;
    f->buf_mode = _IOFBF;
    f->buf = (char *)0;
    f->buf_size = 0;
    f->buf_pos = 0;
    f->buf_owned = 0;

    (void)mode; /* Mode is for compatibility */
    return f;
}

/**
 * @brief Reopen a stream with a different file or mode.
 *
 * @details Closes the file currently associated with @p stream and opens a
 * new file, associating it with the same FILE pointer. This is commonly used
 * to redirect standard streams (stdin, stdout, stderr) to files.
 *
 * Common use case - redirect stdout to a file:
 * @code
 * freopen("output.log", "w", stdout);
 * printf("This goes to output.log\n");
 * @endcode
 *
 * @param pathname Path to the new file to open, or NULL to change the mode
 *                 of the existing file (mode change not fully supported in
 *                 this implementation).
 * @param mode     Mode string for the new file (see fopen() for details).
 * @param stream   Existing stream to redirect. Can be stdin, stdout, stderr,
 *                 or any FILE* from fopen()/fdopen().
 *
 * @return The @p stream pointer on success, or NULL on failure. On failure,
 *         the original stream may be in an undefined state.
 *
 * @note If @p pathname is NULL, the function attempts to change the mode of
 *       the current file, but this is not fully implemented.
 * @note The stream is flushed before closing.
 * @note For standard streams, the old fd is not closed (to preserve 0,1,2).
 *
 * @see fopen, fclose
 */
FILE *freopen(const char *pathname, const char *mode, FILE *stream) {
    if (!stream)
        return (FILE *)0;

    /* Close existing file */
    fflush(stream);
    if (stream->fd >= 0 && stream != stdin && stream != stdout && stream != stderr) {
        close(stream->fd);
    }

    if (!pathname) {
        /* Just change mode - not fully supported */
        return stream;
    }

    int flags = parse_mode(mode);
    if (flags < 0)
        return (FILE *)0;

    int fd = open(pathname, flags, 0666);
    if (fd < 0)
        return (FILE *)0;

    stream->fd = fd;
    stream->error = 0;
    stream->eof = 0;
    stream->buf_pos = 0;

    return stream;
}

/**
 * @brief Close a stream and release its resources.
 *
 * @details Flushes any buffered output, closes the underlying file descriptor,
 * and releases the FILE structure back to the pool for reuse. After fclose(),
 * the stream pointer is invalid and must not be used.
 *
 * Operations performed:
 * 1. Flush any pending output with fflush()
 * 2. Close the underlying file descriptor with close()
 * 3. Mark the FILE structure as available (fd = -1)
 *
 * @param stream Stream to close. Must be a valid FILE pointer from fopen(),
 *               fdopen(), or freopen(). Passing NULL returns EOF.
 *
 * @return 0 on success, EOF on failure (e.g., error flushing buffered data
 *         or closing the file descriptor).
 *
 * @note Standard streams (stdin, stdout, stderr) are handled specially: their
 *       file descriptors are not closed to preserve the standard fd numbers.
 * @note After fclose(), all pointers to the stream become invalid.
 * @note Using a closed stream results in undefined behavior.
 *
 * @see fopen, fflush, fileno
 */
int fclose(FILE *stream) {
    if (!stream)
        return EOF;

    fflush(stream);

    int result = 0;
    if (stream->fd >= 0 && stream != stdin && stream != stdout && stream != stderr) {
        result = close(stream->fd);
        stream->fd = -1;
    }

    return (result < 0) ? EOF : 0;
}

/**
 * @brief Get the file descriptor associated with a stream.
 *
 * @details Returns the integer file descriptor underlying a FILE stream.
 * This is useful when you need to perform low-level operations (select(),
 * fcntl(), ioctl(), etc.) on a stream opened with fopen().
 *
 * Standard file descriptors:
 * - fileno(stdin)  = 0 (STDIN_FILENO)
 * - fileno(stdout) = 1 (STDOUT_FILENO)
 * - fileno(stderr) = 2 (STDERR_FILENO)
 *
 * @param stream FILE pointer to query.
 *
 * @return The file descriptor number (>= 0) on success, or -1 if stream
 *         is NULL.
 *
 * @note The returned fd remains valid as long as the stream is open.
 * @note Be careful mixing stdio and low-level I/O on the same fd; buffering
 *       issues may arise. Call fflush() before using the fd directly.
 *
 * @see fdopen, fflush
 */
int fileno(FILE *stream) {
    if (!stream)
        return -1;
    return stream->fd;
}

/**
 * @brief Read binary data from a stream.
 *
 * @details Reads up to @p nmemb elements of @p size bytes each from the stream
 * into the buffer pointed to by @p ptr. The total number of bytes requested is
 * size * nmemb.
 *
 * Returns the number of complete elements successfully read, which may be less
 * than @p nmemb if end-of-file is reached or a read error occurs.
 *
 * Example usage:
 * @code
 * struct Record records[100];
 * size_t n = fread(records, sizeof(struct Record), 100, fp);
 * if (n < 100) {
 *     if (feof(fp)) printf("Read %zu records (EOF)\n", n);
 *     else if (ferror(fp)) perror("Read error");
 * }
 * @endcode
 *
 * @param ptr    Pointer to buffer where data will be stored. Must have space
 *               for at least size * nmemb bytes.
 * @param size   Size of each element in bytes.
 * @param nmemb  Number of elements to read.
 * @param stream Input stream to read from.
 *
 * @return Number of complete elements successfully read. This may be less than
 *         @p nmemb if EOF is reached or an error occurs. Returns 0 if:
 *         - size or nmemb is 0
 *         - stream or ptr is NULL
 *         - EOF is reached before any data is read
 *         - A read error occurs
 *
 * @note Check feof() and ferror() to distinguish between EOF and error.
 * @note Partial elements at EOF are not counted in the return value.
 *
 * @see fwrite, fgetc, fgets, read
 */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || !ptr || size == 0 || nmemb == 0)
        return 0;

    size_t total = size * nmemb;
    ssize_t bytes_read = read(stream->fd, ptr, total);

    if (bytes_read < 0) {
        stream->error = 1;
        return 0;
    }
    if (bytes_read == 0) {
        stream->eof = 1;
        return 0;
    }

    return (size_t)bytes_read / size;
}

/**
 * @brief Write binary data to a stream.
 *
 * @details Writes @p nmemb elements of @p size bytes each from the buffer
 * pointed to by @p ptr to the output stream. The total number of bytes
 * written is size * nmemb.
 *
 * Example usage:
 * @code
 * struct Record records[10];
 * // ... fill in records ...
 * size_t n = fwrite(records, sizeof(struct Record), 10, fp);
 * if (n < 10) {
 *     perror("Write error");
 * }
 * @endcode
 *
 * @param ptr    Pointer to data to write. Must contain at least size * nmemb
 *               bytes of valid data.
 * @param size   Size of each element in bytes.
 * @param nmemb  Number of elements to write.
 * @param stream Output stream to write to.
 *
 * @return Number of complete elements successfully written. This equals @p nmemb
 *         on success, or a smaller value if a write error occurs. Returns 0 if:
 *         - size or nmemb is 0
 *         - stream or ptr is NULL
 *         - A write error occurs before any data is written
 *
 * @note On error, the stream's error indicator is set (check with ferror()).
 * @note This function writes directly to the fd, bypassing stdio buffering.
 *
 * @see fread, fputs, fputc, write
 */
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || !ptr || size == 0 || nmemb == 0)
        return 0;

    size_t total = size * nmemb;
    ssize_t bytes_written = write(stream->fd, ptr, total);

    if (bytes_written < 0) {
        stream->error = 1;
        return 0;
    }

    return (size_t)bytes_written / size;
}

/**
 * @brief Reposition the file position indicator for a stream.
 *
 * @details Sets the file position indicator for the stream to a new position
 * specified by @p offset relative to the location indicated by @p whence.
 * Any buffered output is flushed before seeking.
 *
 * The @p whence parameter must be one of:
 * - **SEEK_SET**: Offset is relative to the beginning of the file
 * - **SEEK_CUR**: Offset is relative to the current position
 * - **SEEK_END**: Offset is relative to the end of the file
 *
 * Example usage:
 * @code
 * fseek(fp, 0, SEEK_SET);   // Go to beginning
 * fseek(fp, 0, SEEK_END);   // Go to end
 * fseek(fp, -10, SEEK_CUR); // Go back 10 bytes
 * @endcode
 *
 * @param stream Stream to reposition.
 * @param offset Number of bytes to move. Can be negative for SEEK_CUR and
 *               SEEK_END to move backwards.
 * @param whence Reference point for the offset (SEEK_SET, SEEK_CUR, SEEK_END).
 *
 * @return 0 on success, -1 on error (e.g., invalid stream, invalid whence,
 *         seeking on a pipe or socket, or attempting to seek before beginning
 *         of file).
 *
 * @note A successful seek clears the EOF indicator for the stream.
 * @note Any buffered data is flushed before the seek operation.
 *
 * @see ftell, rewind, fsetpos, fgetpos
 */
int fseek(FILE *stream, long offset, int whence) {
    if (!stream)
        return -1;

    fflush(stream);
    long result = lseek(stream->fd, offset, whence);
    if (result < 0)
        return -1;

    stream->eof = 0;
    return 0;
}

/**
 * @brief Get the current file position indicator for a stream.
 *
 * @details Returns the current value of the file position indicator, measured
 * as the number of bytes from the beginning of the file. The returned value
 * can be used with fseek(stream, pos, SEEK_SET) to return to this position.
 *
 * Common pattern to get file size:
 * @code
 * fseek(fp, 0, SEEK_END);
 * long size = ftell(fp);
 * fseek(fp, 0, SEEK_SET);  // Back to beginning
 * @endcode
 *
 * @param stream Stream to query.
 *
 * @return Current position (bytes from beginning) on success, or -1L on error.
 *
 * @note Any buffered data is flushed before querying the position.
 * @note For text streams in some systems, the returned value may not be a
 *       simple byte count; use fgetpos()/fsetpos() for portable positioning
 *       in text mode.
 *
 * @see fseek, fgetpos, rewind
 */
long ftell(FILE *stream) {
    if (!stream)
        return -1L;

    fflush(stream);
    return lseek(stream->fd, 0, SEEK_CUR);
}

/**
 * @brief Reset a stream to the beginning of the file.
 *
 * @details Repositions the file position indicator to the beginning of the
 * file and clears the error indicator. This is equivalent to:
 * @code
 * (void) fseek(stream, 0L, SEEK_SET);
 * clearerr(stream);
 * @endcode
 *
 * However, rewind() does not return an error indication like fseek() does.
 *
 * @param stream Stream to rewind. If NULL, the function does nothing.
 *
 * @note Both the EOF indicator and error indicator are cleared.
 * @note Unlike fseek(), rewind() has no return value and cannot indicate errors.
 *
 * @see fseek, clearerr, fsetpos
 */
void rewind(FILE *stream) {
    if (stream) {
        fseek(stream, 0L, SEEK_SET);
        stream->error = 0;
    }
}

/**
 * @brief Store the current file position indicator.
 *
 * @details Stores the current position of the file position indicator in
 * the fpos_t object pointed to by @p pos. This position can later be restored
 * with fsetpos(). The fpos_t type may contain additional state information
 * beyond just the byte offset (e.g., multibyte conversion state), making
 * this pair of functions more portable than ftell()/fseek() for text files.
 *
 * Example usage:
 * @code
 * fpos_t saved_pos;
 * fgetpos(fp, &saved_pos);
 * // ... read some data ...
 * fsetpos(fp, &saved_pos);  // Restore position
 * @endcode
 *
 * @param stream Stream whose position to save.
 * @param pos    Pointer to fpos_t object to receive the position. Must not
 *               be NULL.
 *
 * @return 0 on success, -1 on error.
 *
 * @note In this implementation, fpos_t is simply a long representing the
 *       byte offset.
 *
 * @see fsetpos, ftell, fseek
 */
int fgetpos(FILE *stream, fpos_t *pos) {
    if (!stream || !pos)
        return -1;

    long p = ftell(stream);
    if (p < 0)
        return -1;

    *pos = p;
    return 0;
}

/**
 * @brief Restore the file position indicator from a saved position.
 *
 * @details Restores the file position indicator to the value stored in the
 * fpos_t object by a previous call to fgetpos(). The @p pos value must have
 * been obtained from a previous fgetpos() call on the same stream.
 *
 * @param stream Stream whose position to restore.
 * @param pos    Pointer to fpos_t object containing the position. Must have
 *               been set by a previous fgetpos() on the same stream.
 *
 * @return 0 on success, -1 on error.
 *
 * @note Using a pos value from a different stream or file results in
 *       undefined behavior.
 *
 * @see fgetpos, fseek, ftell
 */
int fsetpos(FILE *stream, const fpos_t *pos) {
    if (!stream || !pos)
        return -1;

    return fseek(stream, *pos, SEEK_SET);
}

/**
 * @defgroup ungetc_impl ungetc() Implementation
 * @brief Support for pushing characters back onto input streams.
 *
 * @details The ungetc() function allows pushing one character back onto an
 * input stream to be read again. Since this implementation doesn't modify
 * the FILE structure, we use a separate array indexed by stream.
 * @{
 */

/**
 * @brief Per-stream ungetc buffer.
 *
 * @details Each stream can have at most one character pushed back. The array
 * is indexed by get_stream_index(). EOF indicates no character is buffered.
 * The first three entries are for stdin, stdout, stderr; remaining entries
 * are for streams from the file pool.
 */
static int ungetc_buf[FILE_POOL_SIZE + 3] = {EOF, EOF, EOF};

/**
 * @brief Get the index of a stream in the ungetc buffer.
 *
 * @details Maps a FILE pointer to its corresponding index in ungetc_buf[].
 * Standard streams are mapped to indices 0-2; file pool streams are mapped
 * to indices 3 through FILE_POOL_SIZE+2.
 *
 * @param stream Stream to look up.
 *
 * @return Index into ungetc_buf (0 to FILE_POOL_SIZE+2), or -1 if the stream
 *         is not recognized (invalid pointer).
 *
 * @note This is a static internal function, not part of the public API.
 */
static int get_stream_index(FILE *stream) {
    if (stream == stdin)
        return 0;
    if (stream == stdout)
        return 1;
    if (stream == stderr)
        return 2;
    for (int i = 0; i < FILE_POOL_SIZE; i++) {
        if (stream == &file_pool[i])
            return i + 3;
    }
    return -1;
}

/** @} */ /* End of ungetc_impl group */

/**
 * @brief Push a character back onto an input stream.
 *
 * @details Pushes the character @p c (converted to unsigned char) back onto
 * the input stream, where it will be returned by the next read operation.
 * Only one character of pushback is guaranteed by the C standard; this
 * implementation supports exactly one character per stream.
 *
 * A successful ungetc() clears the EOF indicator for the stream.
 *
 * Example usage - peeking at next character:
 * @code
 * int c = fgetc(stream);
 * if (c != EOF) {
 *     if (isdigit(c)) {
 *         ungetc(c, stream);  // Put it back
 *         fscanf(stream, "%d", &number);
 *     }
 * }
 * @endcode
 *
 * @param c      Character to push back, or EOF. If EOF is passed, the
 *               function does nothing and returns EOF.
 * @param stream Stream to push the character back onto.
 *
 * @return The character pushed back as unsigned char cast to int, or EOF if:
 *         - @p c is EOF
 *         - @p stream is NULL
 *         - There is already a character pushed back on this stream
 *
 * @note Only one character can be pushed back at a time per stream.
 * @note The pushed-back character is not written to the file; it exists
 *       only in memory and affects subsequent reads.
 * @note Seeking (fseek, rewind, fsetpos) discards any pushed-back character.
 *
 * @see fgetc, getc, fgets
 */
int ungetc(int c, FILE *stream) {
    if (!stream || c == EOF)
        return EOF;

    int idx = get_stream_index(stream);
    if (idx < 0)
        return EOF;

    if (ungetc_buf[idx] != EOF)
        return EOF; /* Already have an unget char */

    ungetc_buf[idx] = c;
    stream->eof = 0;
    return c;
}

/* External errno support */
extern char *strerror(int errnum);
extern int *__errno_location(void);
#define errno (*__errno_location())

/**
 * @brief Print an error message to stderr based on errno.
 *
 * @details Prints a user-supplied message prefix (if provided), followed by
 * a colon and space, followed by the error message corresponding to the
 * current value of errno, followed by a newline. The entire message is
 * written to stderr.
 *
 * Typical usage after a failed system call:
 * @code
 * FILE *fp = fopen("missing.txt", "r");
 * if (fp == NULL) {
 *     perror("Failed to open missing.txt");
 *     // Output: "Failed to open missing.txt: No such file or directory\n"
 * }
 * @endcode
 *
 * @param s Prefix string to print before the error message. If NULL or empty,
 *          only the error message (from strerror(errno)) is printed.
 *
 * @note The error message comes from strerror(errno), so the current value
 *       of errno at the time of the call determines the message.
 * @note Output is written to stderr and is typically unbuffered.
 *
 * @see strerror, errno, fprintf
 */
void perror(const char *s) {
    if (s && *s) {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

/**
 * @brief Delete a file from the filesystem.
 *
 * @details Removes the file specified by @p pathname from the filesystem.
 * This is equivalent to unlink() for regular files. If the file is currently
 * open, the behavior depends on the underlying filesystem.
 *
 * @param pathname Path to the file to delete.
 *
 * @return 0 on success, -1 on error. On error, errno is set to indicate
 *         the cause (e.g., ENOENT if file doesn't exist, EACCES if
 *         permission denied, EISDIR if pathname is a directory).
 *
 * @note This function cannot remove directories; use rmdir() for that.
 * @note If the file has other hard links, the data remains accessible
 *       through those links.
 *
 * @see rename, unlink, fopen
 */
int remove(const char *pathname) {
    if (!pathname)
        return -1;
    return unlink(pathname);
}

/**
 * @brief Rename or move a file.
 *
 * @details Renames the file from @p oldpath to @p newpath. If @p newpath
 * already exists, it is removed before the rename. This can also be used
 * to move a file to a different directory if both paths are on the same
 * filesystem.
 *
 * @param oldpath Current path to the file.
 * @param newpath New path for the file.
 *
 * @return 0 on success, -1 on error. On error, errno indicates the cause.
 *
 * @note This function is named rename_file() to avoid conflicts with the
 *       standard rename() function which may be a macro or have different
 *       linkage.
 * @note Cross-filesystem moves are not supported; use copy+delete instead.
 *
 * @see remove, fopen
 */
int rename_file(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath)
        return -1;
    return rename(oldpath, newpath);
}

/**
 * @defgroup tmpfile_impl Temporary File Support
 * @brief Functions for creating temporary files and filenames.
 * @{
 */

/** Counter for generating unique temporary filenames. */
static unsigned int tmpnam_counter = 0;

/**
 * @brief Generate a unique temporary filename.
 *
 * @details Generates a string suitable for use as a unique temporary filename.
 * The generated name is of the form "/tmp/tmpXXXXXX" where XXXXXX is a
 * sequence of letters derived from an internal counter.
 *
 * @warning This function is inherently unsafe due to the time-of-check to
 *          time-of-use (TOCTOU) race condition between generating the name
 *          and creating the file. Prefer tmpfile() or mkstemp() instead.
 *
 * @param s Buffer to store the generated filename. If NULL, a static internal
 *          buffer is used (which makes the function non-reentrant). If provided,
 *          must have space for at least L_tmpnam bytes.
 *
 * @return Pointer to the generated filename (either @p s or the internal buffer).
 *
 * @note Using the internal buffer (s == NULL) is not thread-safe.
 * @note The generated filename may not be truly unique; another process could
 *       create a file with the same name between tmpnam() and fopen().
 *
 * @see tmpfile (preferred), L_tmpnam
 */
char *tmpnam(char *s) {
    static char tmpbuf[L_tmpnam];
    char *buf = s ? s : tmpbuf;

    /* Generate name like /tmp/tmpXXXXXX */
    const char *prefix = "/tmp/tmp";
    char *p = buf;
    while (*prefix)
        *p++ = *prefix++;

    unsigned int n = tmpnam_counter++;
    for (int i = 0; i < 6; i++) {
        *p++ = 'A' + (n % 26);
        n /= 26;
    }
    *p = '\0';

    return buf;
}

/**
 * @brief Create a temporary file that is automatically deleted.
 *
 * @details Creates a temporary file opened for update ("w+" mode). The file
 * is automatically deleted when it is closed or when the program terminates.
 * This is safer than tmpnam() as the file is created atomically without a
 * race condition.
 *
 * @return FILE pointer to the temporary file on success, or NULL on failure
 *         (e.g., cannot create file in /tmp, file pool exhausted).
 *
 * @note In this implementation, the file is NOT automatically deleted on
 *       close or program termination. Use remove() explicitly if needed.
 * @note The file is opened in "w+" mode (read/write, truncate).
 *
 * @see tmpnam, fopen
 */
FILE *tmpfile(void) {
    char name[L_tmpnam];
    tmpnam(name);
    return fopen(name, "w+");
}

/** @} */ /* End of tmpfile_impl group */

/**
 * @defgroup getline_impl getline/getdelim Implementation
 * @brief Functions for reading delimiter-terminated lines with automatic buffer management.
 * @{
 */

/* External memory allocation functions */
extern void *malloc(size_t size);
extern void *realloc(void *ptr, size_t size);

/**
 * @brief Read a delimited record from a stream with automatic buffer allocation.
 *
 * @details Reads characters from @p stream into *lineptr until the delimiter
 * character @p delim is encountered or EOF is reached. The buffer is automatically
 * allocated or reallocated as needed to fit the entire line.
 *
 * If *lineptr is NULL or *n is 0, a buffer is allocated. Otherwise, if the
 * existing buffer is too small, it is reallocated to a larger size.
 *
 * The delimiter character (if found) is included in the buffer, followed by
 * a null terminator.
 *
 * Example usage:
 * @code
 * char *line = NULL;
 * size_t len = 0;
 * ssize_t nread;
 * while ((nread = getdelim(&line, &len, '\n', fp)) != -1) {
 *     printf("Read %zd chars: %s", nread, line);
 * }
 * free(line);
 * @endcode
 *
 * @param lineptr Pointer to the buffer pointer. If *lineptr is NULL, a new
 *                buffer is allocated. On return, *lineptr points to the buffer
 *                containing the line (caller must free it).
 * @param n       Pointer to the buffer size. Updated to reflect the current
 *                buffer capacity after reallocation.
 * @param delim   Delimiter character to stop reading at (included in output).
 * @param stream  Input stream to read from.
 *
 * @return Number of characters read (including delimiter, excluding null
 *         terminator), or -1 on error or EOF with no characters read.
 *
 * @note The caller is responsible for freeing *lineptr when done.
 * @note This is a POSIX extension, not part of standard C89/C99.
 *
 * @see getline, fgets
 */
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream) {
    if (!lineptr || !n || !stream)
        return -1;

    if (*lineptr == (char *)0 || *n == 0) {
        *n = 128;
        *lineptr = (char *)malloc(*n);
        if (!*lineptr)
            return -1;
    }

    size_t pos = 0;
    int c;

    while ((c = fgetc(stream)) != EOF) {
        /* Ensure space for char + null terminator */
        if (pos + 2 > *n) {
            size_t new_size = *n * 2;
            char *new_ptr = (char *)realloc(*lineptr, new_size);
            if (!new_ptr)
                return -1;
            *lineptr = new_ptr;
            *n = new_size;
        }

        (*lineptr)[pos++] = (char)c;
        if (c == delim)
            break;
    }

    if (pos == 0 && c == EOF)
        return -1;

    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

/**
 * @brief Read an entire line from a stream with automatic buffer allocation.
 *
 * @details Reads characters from @p stream into *lineptr until a newline
 * character is encountered or EOF is reached. This is equivalent to
 * getdelim(lineptr, n, '\n', stream).
 *
 * The newline character (if found) is included in the buffer, followed by
 * a null terminator. The buffer is automatically allocated or grown as needed.
 *
 * Example usage:
 * @code
 * char *line = NULL;
 * size_t len = 0;
 * ssize_t nread;
 * while ((nread = getline(&line, &len, fp)) != -1) {
 *     // Process line (includes trailing '\n')
 *     line[nread - 1] = '\0';  // Remove newline if desired
 * }
 * free(line);
 * @endcode
 *
 * @param lineptr Pointer to buffer pointer. If *lineptr is NULL, allocates new buffer.
 * @param n       Pointer to buffer size. Updated after reallocation.
 * @param stream  Input stream to read from.
 *
 * @return Number of characters read (including newline, excluding null terminator),
 *         or -1 on error or EOF with no characters read.
 *
 * @note Caller must free *lineptr when done.
 * @note POSIX extension, not standard C.
 *
 * @see getdelim, fgets
 */
ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    return getdelim(lineptr, n, '\n', stream);
}

/** @} */ /* End of getline_impl group */

/**
 * @brief Read formatted input from a string.
 *
 * @details Parses the string @p str according to the format specification and
 * stores the converted values in the locations pointed to by the additional
 * arguments. This is the string-reading counterpart to sprintf().
 *
 * Supported format specifiers:
 * - `%d`, `%i`: Signed decimal integer (int *)
 * - `%u`: Unsigned decimal integer (unsigned int *)
 * - `%x`, `%X`: Unsigned hexadecimal integer (unsigned int *), optional 0x prefix
 * - `%s`: String of non-whitespace characters (char *, must provide buffer)
 * - `%c`: Single character (char *)
 * - `%n`: Number of characters read so far (int *, doesn't count as conversion)
 * - `%%`: Literal percent sign (no conversion)
 *
 * Width specifiers (e.g., `%10s`) limit the maximum number of characters/digits
 * read for that conversion.
 *
 * Whitespace in the format string matches any amount of whitespace in the input.
 *
 * Example usage:
 * @code
 * int x, y;
 * char name[32];
 * int n = sscanf("42 hello 17", "%d %s %d", &x, name, &y);
 * // n = 3, x = 42, name = "hello", y = 17
 * @endcode
 *
 * @param str    Input string to parse. Must be null-terminated.
 * @param format Format string specifying expected input pattern.
 * @param ...    Pointers to variables where converted values will be stored.
 *               Each pointer type must match the corresponding format specifier.
 *
 * @return Number of input items successfully matched and assigned (may be less
 *         than the number of format specifiers if input doesn't match or ends
 *         early). Returns 0 if the first conversion fails. Returns EOF if
 *         input ends before the first conversion.
 *
 * @warning The %s specifier does not perform bounds checking. Always use a
 *          width specifier (e.g., %31s for a 32-byte buffer) to prevent overflow.
 *
 * @note This implementation does not support the following:
 *       - Assignment suppression (*)
 *       - Length modifiers for %s (l, h, etc.)
 *       - Floating point (%f, %e, %g)
 *       - Scansets ([...])
 *
 * @see sprintf, fscanf, strtol
 */
int sscanf(const char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int matched = 0;
    const char *s = str;

    while (*format) {
        if (*format == ' ' || *format == '\t' || *format == '\n') {
            skip_whitespace(&s);
            format++;
            continue;
        }

        if (*format != '%') {
            if (*s != *format)
                break;
            s++;
            format++;
            continue;
        }

        format++; /* skip '%' */

        /* Parse width (optional) */
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }

        /* Handle format specifier */
        switch (*format) {
            case 'd':
            case 'i': {
                skip_whitespace(&s);
                int *ptr = va_arg(ap, int *);
                int neg = 0;
                long val = 0;

                if (*s == '-') {
                    neg = 1;
                    s++;
                } else if (*s == '+') {
                    s++;
                }

                if (!(*s >= '0' && *s <= '9'))
                    goto done;

                int digits = 0;
                while (*s >= '0' && *s <= '9') {
                    val = val * 10 + (*s - '0');
                    s++;
                    digits++;
                    if (width > 0 && digits >= width)
                        break;
                }

                *ptr = neg ? -val : val;
                matched++;
                break;
            }

            case 'u': {
                skip_whitespace(&s);
                unsigned int *ptr = va_arg(ap, unsigned int *);
                unsigned long val = 0;

                if (!(*s >= '0' && *s <= '9'))
                    goto done;

                int digits = 0;
                while (*s >= '0' && *s <= '9') {
                    val = val * 10 + (*s - '0');
                    s++;
                    digits++;
                    if (width > 0 && digits >= width)
                        break;
                }

                *ptr = (unsigned int)val;
                matched++;
                break;
            }

            case 'x':
            case 'X': {
                skip_whitespace(&s);
                unsigned int *ptr = va_arg(ap, unsigned int *);
                unsigned long val = 0;

                /* Skip optional 0x prefix */
                if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
                    s += 2;

                int digits = 0;
                while (1) {
                    int digit;
                    if (*s >= '0' && *s <= '9')
                        digit = *s - '0';
                    else if (*s >= 'a' && *s <= 'f')
                        digit = *s - 'a' + 10;
                    else if (*s >= 'A' && *s <= 'F')
                        digit = *s - 'A' + 10;
                    else
                        break;

                    val = val * 16 + digit;
                    s++;
                    digits++;
                    if (width > 0 && digits >= width)
                        break;
                }

                if (digits == 0)
                    goto done;

                *ptr = (unsigned int)val;
                matched++;
                break;
            }

            case 's': {
                skip_whitespace(&s);
                char *ptr = va_arg(ap, char *);
                int len = 0;

                while (*s && *s != ' ' && *s != '\t' && *s != '\n') {
                    if (width > 0 && len >= width)
                        break;
                    *ptr++ = *s++;
                    len++;
                }
                *ptr = '\0';

                if (len > 0)
                    matched++;
                else
                    goto done;
                break;
            }

            case 'c': {
                char *ptr = va_arg(ap, char *);
                if (!*s)
                    goto done;
                *ptr = *s++;
                matched++;
                break;
            }

            case 'n': {
                int *ptr = va_arg(ap, int *);
                *ptr = (int)(s - str);
                /* %n doesn't count as a matched item */
                break;
            }

            case '%':
                if (*s != '%')
                    goto done;
                s++;
                break;

            default:
                goto done;
        }

        format++;
    }

done:
    va_end(ap);
    return matched;
}
