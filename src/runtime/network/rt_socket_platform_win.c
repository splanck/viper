//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_socket_platform_win.c
// Purpose: WinSock adapter implementation for the runtime networking stack.
//
// Key invariants:
//   - WSAStartup runs at most once and all concurrent callers observe the same
//     completed initialization before socket work continues.
//   - Socket readiness waits use select(), matching existing Windows behavior.
//
// Ownership/Lifetime:
//   - The adapter does not own sockets except during rt_socket_close().
//
// Links: src/runtime/network/rt_socket_platform.h
//
//===----------------------------------------------------------------------===//

#include "rt_socket_platform.h"

#include "rt_internal.h"

#include <errno.h>
#include <stdlib.h>

static volatile LONG g_wsa_init_state = 0; // 0=uninit, 1=in-progress, 2=done

/// @brief atexit handler that releases the process WinSock startup reference.
/// @details Registered only after a successful WSAStartup call.
static void rt_net_cleanup_wsa(void) {
    WSACleanup();
}

/// @brief Initialize WinSock once for the process.
/// @details Uses a three-state atomic flag so concurrent callers either perform
///          WSAStartup exactly once or wait until the winning caller finishes.
///          A successful startup registers rt_net_cleanup_wsa() for process exit.
void rt_net_init_wsa(void) {
    if (g_wsa_init_state == 2)
        return;

    LONG prev = InterlockedCompareExchange(&g_wsa_init_state, 1, 0);
    if (prev == 2)
        return;
    if (prev == 1) {
        while (g_wsa_init_state != 2)
            Sleep(0);
        return;
    }

    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        InterlockedExchange(&g_wsa_init_state, 0);
        rt_trap("Network: WSAStartup failed");
        return;
    }
    atexit(rt_net_cleanup_wsa);
    InterlockedExchange(&g_wsa_init_state, 2);
}

/// @brief Close a WinSock socket handle.
/// @param sock Socket handle returned by socket().
/// @return Native closesocket() result.
int rt_socket_close(socket_t sock) {
    return closesocket(sock);
}

/// @brief Return the calling thread's last WinSock error.
/// @return WSAGetLastError() value.
int rt_socket_last_error(void) {
    return WSAGetLastError();
}

/// @brief Classify an interrupted WinSock operation.
/// @param err Native WinSock error code to test.
/// @return true when @p err is WSAEINTR.
bool rt_socket_error_is_interrupted(int err) {
    return err == WSAEINTR;
}

/// @brief Classify non-blocking WinSock retry conditions.
/// @param err Native WinSock error code to test.
/// @return true when @p err is WSAEWOULDBLOCK.
bool rt_socket_error_is_would_block(int err) {
    return err == WSAEWOULDBLOCK;
}

/// @brief Classify an in-progress non-blocking WinSock connect().
/// @param err Native WinSock error code to test.
/// @return true when @p err is WSAEWOULDBLOCK.
bool rt_socket_error_is_in_progress(int err) {
    return err == WSAEWOULDBLOCK;
}

/// @brief Classify a WinSock socket timeout error.
/// @param err Native WinSock error code to test.
/// @return true when @p err is WSAETIMEDOUT.
bool rt_socket_error_is_timeout(int err) {
    return err == WSAETIMEDOUT || err == ETIMEDOUT;
}

/// @brief Classify the last WinSock receive error as timeout or would-block.
/// @return true when the last WinSock error is WSAETIMEDOUT or WSAEWOULDBLOCK.
bool rt_socket_recv_timed_out(void) {
    int err = WSAGetLastError();
    return err == WSAETIMEDOUT || err == WSAEWOULDBLOCK;
}

/// @brief Detect accept() failures caused by listener shutdown races.
/// @param err Native WinSock error code returned after accept() failed.
/// @return true for expected close/interruption errors during shutdown.
bool rt_socket_accept_interrupted_by_close(int err) {
    return err == WSAENOTSOCK || err == WSAEINVAL || err == WSAEINTR || err == WSAECONNABORTED;
}

/// @brief No-op SIGPIPE suppression on Windows.
/// @param sock Socket handle accepted for API symmetry.
void suppress_sigpipe(socket_t sock) {
    (void)sock;
}

/// @brief Toggle non-blocking mode on a WinSock socket.
/// @param sock Socket handle to update.
/// @param nonblocking true enables non-blocking mode, false restores blocking mode.
/// @return true when ioctlsocket(FIONBIO) succeeds.
bool rt_socket_set_nonblocking(socket_t sock, bool nonblocking) {
    u_long mode = nonblocking ? 1u : 0u;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
}

/// @brief Query queued read bytes for a WinSock socket.
/// @param sock Socket handle to inspect.
/// @param bytes_out Receives the queued byte count on success.
/// @return true when ioctlsocket(FIONREAD) succeeds.
bool rt_socket_available_bytes(socket_t sock, int64_t *bytes_out) {
    if (!bytes_out)
        return false;
    *bytes_out = 0;
    u_long bytes_available = 0;
    if (ioctlsocket(sock, FIONREAD, &bytes_available) != 0)
        return false;
    *bytes_out = (int64_t)bytes_available;
    return true;
}

/// @brief Apply SO_RCVTIMEO or SO_SNDTIMEO to a WinSock socket.
/// @param sock Socket handle to configure.
/// @param timeout_ms Timeout in milliseconds; negative values are treated as zero.
/// @param is_recv true selects SO_RCVTIMEO, false selects SO_SNDTIMEO.
void set_socket_timeout(socket_t sock, int timeout_ms, bool is_recv) {
    if (timeout_ms < 0)
        timeout_ms = 0;
    DWORD tv = (DWORD)timeout_ms;
    setsockopt(
        sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
}

/// @brief Wait for a WinSock socket to become readable or writable.
/// @param sock Socket handle to wait on.
/// @param timeout_ms Timeout in milliseconds.
/// @param for_write true waits for write readiness; false waits for read readiness.
/// @return Positive when ready, zero on timeout, negative on error.
int wait_socket(socket_t sock, int timeout_ms, bool for_write) {
    if (timeout_ms < 0 || sock == INVALID_SOCK)
        return -1;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (for_write)
        return select((int)(sock + 1), NULL, &fds, NULL, &tv);
    return select((int)(sock + 1), &fds, NULL, NULL, &tv);
}
