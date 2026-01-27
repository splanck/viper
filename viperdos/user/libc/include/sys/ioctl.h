#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sys/ioctl.h
 * @brief I/O control operations for ViperDOS.
 *
 * @details
 * Provides ioctl() for device-specific control operations.
 * Currently supports terminal window size queries (TIOCGWINSZ).
 */

/** @brief Terminal window size structure. */
struct winsize {
    unsigned short ws_row;    /**< Number of rows (characters). */
    unsigned short ws_col;    /**< Number of columns (characters). */
    unsigned short ws_xpixel; /**< Horizontal size in pixels (unused). */
    unsigned short ws_ypixel; /**< Vertical size in pixels (unused). */
};

/** @brief Get terminal window size. */
#define TIOCGWINSZ 0x5413

/** @brief Set terminal window size. */
#define TIOCSWINSZ 0x5414

/**
 * @brief Perform device-specific I/O control operation.
 *
 * @param fd File descriptor.
 * @param request Request code (e.g., TIOCGWINSZ).
 * @param ... Request-specific arguments.
 * @return 0 on success, -1 on error.
 */
int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IOCTL_H */
