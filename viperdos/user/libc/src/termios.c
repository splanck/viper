//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/termios.c
// Purpose: Terminal I/O control functions for ViperDOS libc.
// Key invariants: Single terminal (stdin/stdout/stderr); in-process state.
// Ownership/Lifetime: Library; terminal settings persist until changed.
// Links: user/libc/include/termios.h
//
//===----------------------------------------------------------------------===//

/**
 * @file termios.c
 * @brief Terminal I/O control functions for ViperDOS libc.
 *
 * @details
 * This file provides minimal termios compatibility:
 *
 * - tcgetattr/tcsetattr: Get/set terminal attributes
 * - cfgetispeed/cfsetispeed: Get/set input baud rate
 * - cfgetospeed/cfsetospeed: Get/set output baud rate
 * - cfmakeraw: Configure raw mode
 * - tcsendbreak/tcdrain/tcflush/tcflow: Terminal control (no-ops)
 * - ttyname: Return terminal name
 *
 * Terminal settings are stored in-process and apply to stdin/stdout/stderr.
 * ViperDOS doesn't have a full TTY subsystem, so some functions are no-ops.
 * The settings are used by read() in unistd.c for line discipline.
 */

#include "../include/termios.h"
#include "../include/string.h"

/* Default terminal settings (cooked mode with echo) */
static struct termios default_termios = {
    .c_iflag = ICRNL | IXON,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = CS8 | CREAD | CLOCAL,
    .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN,
    .c_cc =
        {
            [VINTR] = 0x03,  /* Ctrl+C */
            [VQUIT] = 0x1C,  /* Ctrl+\ */
            [VERASE] = 0x7F, /* Backspace */
            [VKILL] = 0x15,  /* Ctrl+U */
            [VEOF] = 0x04,   /* Ctrl+D */
            [VTIME] = 0,
            [VMIN] = 1,
            [VSTART] = 0x11, /* Ctrl+Q */
            [VSTOP] = 0x13,  /* Ctrl+S */
            [VSUSP] = 0x1A,  /* Ctrl+Z */
        },
    .c_ispeed = B9600,
    .c_ospeed = B9600,
};

/* Current terminal settings for stdin (fd 0) */
static struct termios current_termios;
static int termios_initialized = 0;

static void init_termios(void) {
    if (!termios_initialized) {
        memcpy(&current_termios, &default_termios, sizeof(struct termios));
        termios_initialized = 1;
    }
}

/**
 * @brief Get terminal attributes.
 *
 * @details
 * Retrieves the current terminal settings for the specified file descriptor
 * and stores them in the termios structure. The terminal must be associated
 * with stdin/stdout/stderr (fd 0, 1, or 2) in ViperDOS.
 *
 * The termios structure contains:
 * - c_iflag: Input mode flags (ICRNL, IXON, etc.)
 * - c_oflag: Output mode flags (OPOST, ONLCR, etc.)
 * - c_cflag: Control mode flags (CS8, CREAD, etc.)
 * - c_lflag: Local mode flags (ICANON, ECHO, ISIG, etc.)
 * - c_cc[]: Special characters (VEOF, VERASE, etc.)
 * - c_ispeed/c_ospeed: Input/output baud rates
 *
 * @param fd File descriptor for the terminal (0, 1, or 2).
 * @param termios_p Pointer to termios structure to receive settings.
 * @return 0 on success, -1 on error.
 *
 * @see tcsetattr, cfmakeraw
 */
int tcgetattr(int fd, struct termios *termios_p) {
    if (!termios_p)
        return -1;

    /* Only support stdin for now */
    if (fd < 0 || fd > 2)
        return -1;

    init_termios();
    memcpy(termios_p, &current_termios, sizeof(struct termios));
    return 0;
}

/**
 * @brief Set terminal attributes.
 *
 * @details
 * Sets the terminal parameters associated with the file descriptor.
 * The optional_actions argument specifies when the changes take effect:
 *
 * - TCSANOW: Changes take effect immediately
 * - TCSADRAIN: Changes take effect after all output is transmitted
 * - TCSAFLUSH: Like TCSADRAIN, but also flush pending input
 *
 * ViperDOS applies all changes immediately regardless of optional_actions.
 *
 * @param fd File descriptor for the terminal (0, 1, or 2).
 * @param optional_actions When to apply changes (TCSANOW, etc.; ignored).
 * @param termios_p Pointer to termios structure with new settings.
 * @return 0 on success, -1 on error.
 *
 * @see tcgetattr, cfmakeraw
 */
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
    (void)optional_actions; /* We apply immediately regardless */

    if (!termios_p)
        return -1;

    /* Only support stdin for now */
    if (fd < 0 || fd > 2)
        return -1;

    init_termios();
    memcpy(&current_termios, termios_p, sizeof(struct termios));
    return 0;
}

/**
 * @brief Send a break signal on a serial line.
 *
 * @details
 * Transmits a continuous stream of zero-valued bits for a specified
 * duration. If duration is zero, the break lasts 0.25-0.5 seconds.
 *
 * @note ViperDOS has no serial line support. This function is a no-op.
 *
 * @param fd File descriptor for the terminal.
 * @param duration Duration of break (0 for default).
 * @return 0 on success.
 *
 * @see tcdrain, tcflush, tcflow
 */
int tcsendbreak(int fd, int duration) {
    (void)fd;
    (void)duration;
    /* No-op: break not supported */
    return 0;
}

/**
 * @brief Wait until all output has been transmitted.
 *
 * @details
 * Blocks until all output written to the file descriptor has been
 * transmitted to the terminal. Useful before changing terminal settings
 * to ensure previous output isn't affected.
 *
 * @note ViperDOS has no kernel output buffering. This function is a no-op.
 *
 * @param fd File descriptor for the terminal.
 * @return 0 on success.
 *
 * @see tcsendbreak, tcflush, tcsetattr
 */
int tcdrain(int fd) {
    (void)fd;
    /* No buffering, so nothing to drain */
    return 0;
}

/**
 * @brief Flush pending terminal I/O.
 *
 * @details
 * Discards data according to queue_selector:
 * - TCIFLUSH: Flush pending input (data received but not read)
 * - TCOFLUSH: Flush pending output (data written but not transmitted)
 * - TCIOFLUSH: Flush both input and output
 *
 * @note ViperDOS has no kernel terminal buffers. This function is a no-op.
 *
 * @param fd File descriptor for the terminal.
 * @param queue_selector Which queue to flush.
 * @return 0 on success.
 *
 * @see tcdrain, tcsendbreak
 */
int tcflush(int fd, int queue_selector) {
    (void)fd;
    (void)queue_selector;
    /* No kernel buffers to flush */
    return 0;
}

/**
 * @brief Suspend or restart terminal output/input.
 *
 * @details
 * Controls XON/XOFF flow control:
 * - TCOOFF: Suspend output
 * - TCOON: Restart output
 * - TCIOFF: Transmit XOFF (stop sending)
 * - TCION: Transmit XON (resume sending)
 *
 * @note Flow control is not supported in ViperDOS. This function is a no-op.
 *
 * @param fd File descriptor for the terminal.
 * @param action Flow control action.
 * @return 0 on success.
 *
 * @see tcflush, tcsendbreak
 */
int tcflow(int fd, int action) {
    (void)fd;
    (void)action;
    /* Flow control not supported */
    return 0;
}

/**
 * @brief Get input baud rate from termios structure.
 *
 * @details
 * Extracts and returns the input speed from the termios structure.
 * The returned value is one of the B* constants (B0, B9600, B115200, etc.).
 *
 * @param termios_p Pointer to termios structure.
 * @return Input baud rate, or B0 if termios_p is NULL.
 *
 * @see cfsetispeed, cfgetospeed
 */
speed_t cfgetispeed(const struct termios *termios_p) {
    if (!termios_p)
        return B0;
    return termios_p->c_ispeed;
}

/**
 * @brief Get output baud rate from termios structure.
 *
 * @details
 * Extracts and returns the output speed from the termios structure.
 * The returned value is one of the B* constants (B0, B9600, B115200, etc.).
 *
 * @param termios_p Pointer to termios structure.
 * @return Output baud rate, or B0 if termios_p is NULL.
 *
 * @see cfsetospeed, cfgetispeed
 */
speed_t cfgetospeed(const struct termios *termios_p) {
    if (!termios_p)
        return B0;
    return termios_p->c_ospeed;
}

/**
 * @brief Set input baud rate in termios structure.
 *
 * @details
 * Sets the input speed in the termios structure. The actual baud rate
 * change takes effect when tcsetattr() is called with this structure.
 *
 * Common speed constants: B0, B50, B75, B110, B134, B150, B200, B300,
 * B600, B1200, B1800, B2400, B4800, B9600, B19200, B38400, B57600, B115200.
 *
 * @param termios_p Pointer to termios structure to modify.
 * @param speed Baud rate constant (B9600, B115200, etc.).
 * @return 0 on success, -1 if termios_p is NULL.
 *
 * @see cfgetispeed, cfsetospeed
 */
int cfsetispeed(struct termios *termios_p, speed_t speed) {
    if (!termios_p)
        return -1;
    termios_p->c_ispeed = speed;
    return 0;
}

/**
 * @brief Set output baud rate in termios structure.
 *
 * @details
 * Sets the output speed in the termios structure. The actual baud rate
 * change takes effect when tcsetattr() is called with this structure.
 *
 * @param termios_p Pointer to termios structure to modify.
 * @param speed Baud rate constant (B9600, B115200, etc.).
 * @return 0 on success, -1 if termios_p is NULL.
 *
 * @see cfgetospeed, cfsetispeed
 */
int cfsetospeed(struct termios *termios_p, speed_t speed) {
    if (!termios_p)
        return -1;
    termios_p->c_ospeed = speed;
    return 0;
}

/**
 * @brief Configure termios structure for raw mode.
 *
 * @details
 * Modifies the termios structure for "raw" input mode, where characters
 * are passed through with minimal processing:
 *
 * - Input: No special character processing, no ICRNL, no IXON
 * - Output: No post-processing (OPOST disabled)
 * - Local: No canonical mode, no echo, no signals from special chars
 * - Character size: 8 bits, no parity
 * - Read: Returns immediately with at least 1 character (VMIN=1, VTIME=0)
 *
 * This is useful for applications that need to process every keystroke
 * without buffering or interpretation (e.g., terminal emulators, editors).
 *
 * After calling cfmakeraw(), use tcsetattr() to apply the changes.
 *
 * @param termios_p Pointer to termios structure to modify.
 *
 * @see tcsetattr, tcgetattr
 */
void cfmakeraw(struct termios *termios_p) {
    if (!termios_p)
        return;

    /* Turn off all processing */
    termios_p->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    termios_p->c_oflag &= ~OPOST;
    termios_p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    termios_p->c_cflag &= ~(CSIZE | PARENB);
    termios_p->c_cflag |= CS8;

    /* Set read to return immediately with 1 character minimum */
    termios_p->c_cc[VMIN] = 1;
    termios_p->c_cc[VTIME] = 0;
}

/* Static buffer for ttyname */
static char ttyname_buf[16];

/**
 * @brief Get the name of a terminal.
 *
 * @details
 * Returns the pathname of the terminal device associated with the
 * file descriptor. In ViperDOS, this returns "/dev/tty" for stdin,
 * stdout, and stderr (fd 0, 1, 2).
 *
 * @warning The returned pointer points to static storage that is
 * overwritten by subsequent calls. This function is not thread-safe.
 *
 * @param fd File descriptor to check (must be 0, 1, or 2).
 * @return Pointer to static buffer containing terminal name, or NULL
 *         if fd is not a valid terminal.
 *
 * @see isatty, tcgetattr
 */
char *ttyname(int fd) {
    if (fd < 0 || fd > 2)
        return (char *)0;

    /* Return a generic TTY name */
    memcpy(ttyname_buf, "/dev/tty", 9);
    return ttyname_buf;
}
