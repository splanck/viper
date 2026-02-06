//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/ioctl.c
// Purpose: I/O control operations for ViperDOS libc.
// Key invariants: Currently supports TIOCGWINSZ for terminal size.
// Ownership/Lifetime: Library function.
// Links: user/libc/include/sys/ioctl.h
//
//===----------------------------------------------------------------------===//

/**
 * @file ioctl.c
 * @brief I/O control operations for ViperDOS libc.
 *
 * @details
 * Implements ioctl() with support for:
 * - TIOCGWINSZ: Get terminal window size via SYS_TTY_GET_SIZE syscall
 *
 * SYS_TTY_GET_SIZE returns a packed u64: rows (high 32) | cols (low 32).
 */

#include "../include/sys/ioctl.h"
#include "syscall_internal.h"
#include <stdarg.h>

/* Syscall numbers */
#define SYS_TTY_GET_SIZE 0x124

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);

    int result = -1;

    switch (request) {
        case TIOCGWINSZ: {
            /* Only valid for terminal fds (0, 1, 2) */
            if (fd < 0 || fd > 2) {
                va_end(ap);
                return -1;
            }

            struct winsize *ws = va_arg(ap, struct winsize *);
            if (!ws) {
                va_end(ap);
                return -1;
            }

            long packed = __syscall0(SYS_TTY_GET_SIZE);
            if (packed < 0) {
                ws->ws_col = 80;
                ws->ws_row = 25;
            } else {
                ws->ws_col = (unsigned short)(packed & 0xFFFF);
                ws->ws_row = (unsigned short)((packed >> 32) & 0xFFFF);
            }
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
            result = 0;
            break;
        }
        default:
            result = -1;
            break;
    }

    va_end(ap);
    return result;
}
