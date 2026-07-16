//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_socket_platform_posix.c
// Purpose: POSIX socket adapter implementation for the runtime networking stack.
//
// Key invariants:
//   - WinSock initialization is a no-op on POSIX-style platforms.
//   - poll() is preferred when available; ViperDOS falls back to select().
//   - macOS SIGPIPE suppression uses SO_NOSIGPIPE once per socket, while any
//     platform exposing MSG_NOSIGNAL also receives per-send suppression.
//
// Ownership/Lifetime:
//   - The adapter does not own sockets except during rt_socket_close().
//
// Links: src/runtime/network/rt_socket_platform.h
//
//===----------------------------------------------------------------------===//

#include "rt_socket_platform.h"

#include <limits.h>
#include <string.h>
#include <time.h>

/// @brief No-op WinSock initializer for POSIX-style socket stacks.
/// @details Keeps call sites platform-neutral; only the Windows adapter has
///          process-level socket startup work to perform.
void rt_net_init_wsa(void) {}

/// @brief Close a POSIX socket descriptor.
/// @param sock File descriptor returned by socket().
/// @return Native close() result.
int rt_socket_close(socket_t sock) {
    return close(sock);
}

/// @brief Return the current POSIX socket error indicator.
/// @return The calling thread's errno value.
int rt_socket_last_error(void) {
    return errno;
}

/// @brief Classify an interrupted POSIX socket operation.
/// @param err Native errno value to test.
/// @return true when @p err is EINTR.
bool rt_socket_error_is_interrupted(int err) {
    return err == EINTR;
}

/// @brief Classify non-blocking POSIX retry conditions.
/// @param err Native errno value to test.
/// @return true when @p err is EAGAIN or EWOULDBLOCK.
bool rt_socket_error_is_would_block(int err) {
    return err == EAGAIN || err == EWOULDBLOCK;
}

/// @brief Classify an in-progress non-blocking POSIX connect().
/// @param err Native errno value to test.
/// @return true when @p err is EINPROGRESS.
bool rt_socket_error_is_in_progress(int err) {
    return err == EINPROGRESS;
}

/// @brief Classify a POSIX socket timeout error.
/// @param err Native errno value to test.
/// @return true when @p err is ETIMEDOUT.
bool rt_socket_error_is_timeout(int err) {
    return err == ETIMEDOUT;
}

/// @brief Classify the last POSIX receive error as timeout or would-block.
/// @return true when errno is EAGAIN, EWOULDBLOCK, or ETIMEDOUT.
bool rt_socket_recv_timed_out(void) {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT;
}

/// @brief Detect accept() failures caused by listener shutdown races.
/// @param err Native errno value returned after accept() failed.
/// @return true for expected close/interruption errors during shutdown.
bool rt_socket_accept_interrupted_by_close(int err) {
    return err == EBADF || err == EINVAL || err == EINTR || err == ECONNABORTED;
}

/// @brief Suppress SIGPIPE for sockets on platforms with per-socket support.
/// @details macOS accepts SO_NOSIGPIPE; platforms exposing MSG_NOSIGNAL use
///          SEND_FLAGS on send() as a second line of defense.
/// @param sock Socket descriptor to configure.
void suppress_sigpipe(socket_t sock) {
#if RT_PLATFORM_MACOS && defined(SO_NOSIGPIPE)
    int val = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
    (void)sock;
}

/// @brief Toggle O_NONBLOCK on a POSIX socket.
/// @param sock Socket descriptor to update.
/// @param nonblocking true to enable non-blocking mode, false to restore blocking mode.
/// @return true when both fcntl calls succeed, false otherwise.
bool rt_socket_set_nonblocking(socket_t sock, bool nonblocking) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0)
        return false;
    int new_flags = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(sock, F_SETFL, new_flags) == 0;
}

/// @brief Query queued read bytes for a POSIX socket.
/// @details Uses FIONREAD where the platform exposes ioctl(); ViperDOS reports
///          unsupported so callers retain their historical zero-on-unknown behavior.
/// @param sock Socket descriptor to inspect.
/// @param bytes_out Receives the queued byte count on success.
/// @return true if the query succeeded, false otherwise.
bool rt_socket_available_bytes(socket_t sock, int64_t *bytes_out) {
    if (!bytes_out)
        return false;
    *bytes_out = 0;
#if RT_PLATFORM_VIPERDOS
    (void)sock;
    return false;
#else
    int bytes_available = 0;
    if (ioctl(sock, FIONREAD, &bytes_available) != 0)
        return false;
    if (bytes_available < 0)
        return false;
    *bytes_out = (int64_t)bytes_available;
    return true;
#endif
}

/// @brief Apply SO_RCVTIMEO or SO_SNDTIMEO to a POSIX socket.
/// @param sock Socket descriptor to configure.
/// @param timeout_ms Timeout in milliseconds; negative values are treated as zero.
/// @param is_recv true selects SO_RCVTIMEO, false selects SO_SNDTIMEO.
bool set_socket_timeout(socket_t sock, int timeout_ms, bool is_recv) {
    if (timeout_ms < 0)
        timeout_ms = 0;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

/// @brief Wait for a POSIX socket to become readable or writable.
/// @details Uses poll() where available, retrying after EINTR, and falls back to
///          select() on ViperDOS.
/// @param sock Socket descriptor to wait on.
/// @param timeout_ms Timeout in milliseconds.
/// @param for_write true waits for POLLOUT/write readiness; false waits for read readiness.
/// @return Positive when ready, zero on timeout, negative on error.
int wait_socket(socket_t sock, int timeout_ms, bool for_write) {
    if (timeout_ms < 0)
        return -1;
#if !RT_PLATFORM_VIPERDOS
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = for_write ? POLLOUT : POLLIN;
    pfd.revents = 0;
    int result;
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
        return -1;
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    int remaining_ms = timeout_ms;
    do {
        result = poll(&pfd, 1, remaining_ms);
        if (result >= 0 || errno != EINTR)
            break;
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            return -1;
        time_t seconds = deadline.tv_sec - now.tv_sec;
        long nanoseconds = deadline.tv_nsec - now.tv_nsec;
        if (nanoseconds < 0) {
            seconds--;
            nanoseconds += 1000000000L;
        }
        if (seconds < 0 || (seconds == 0 && nanoseconds == 0))
            return 0;
        if (seconds > INT_MAX / 1000)
            remaining_ms = INT_MAX;
        else {
            int64_t rounded_ms = (int64_t)seconds * 1000 + (nanoseconds + 999999L) / 1000000L;
            remaining_ms = rounded_ms > INT_MAX ? INT_MAX : (int)rounded_ms;
        }
    } while (1);
    if (result > 0 && (pfd.revents & POLLNVAL)) {
        errno = EBADF;
        return -1;
    }
    if (result > 0 && (pfd.revents & (POLLERR | POLLHUP)) && !(pfd.revents & (POLLIN | POLLOUT)))
        return -1;
    return result;
#else
    fd_set fds;
    FD_ZERO(&fds);
    if (sock == INVALID_SOCK || sock >= FD_SETSIZE) {
        errno = EINVAL;
        return -1;
    }
    FD_SET(sock, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (for_write)
        return select(sock + 1, NULL, &fds, NULL, &tv);
    return select(sock + 1, &fds, NULL, NULL, &tv);
#endif
}
