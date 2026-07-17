//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_socket_platform_win.c
// Purpose: WinSock adapter implementation for the runtime networking stack.
//
// Key invariants:
//   - One caller performs each WSAStartup attempt; waiters retry if that attempt
//     fails instead of spinning forever on an abandoned in-progress state.
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
#include <string.h>

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
    for (;;) {
        LONG state = InterlockedCompareExchange(&g_wsa_init_state, 0, 0);
        if (state == 2)
            return;
        if (state == 1) {
            Sleep(0);
            continue;
        }
        if (InterlockedCompareExchange(&g_wsa_init_state, 1, 0) != 0)
            continue;

        WSADATA wsa_data;
        memset(&wsa_data, 0, sizeof(wsa_data));
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0) {
            InterlockedExchange(&g_wsa_init_state, 0);
            rt_trap("Network: WSAStartup failed");
            return;
        }
        if (wsa_data.wVersion != MAKEWORD(2, 2)) {
            WSACleanup();
            InterlockedExchange(&g_wsa_init_state, 0);
            rt_trap("Network: WinSock 2.2 is unavailable");
            return;
        }
        if (atexit(rt_net_cleanup_wsa) != 0) {
            WSACleanup();
            InterlockedExchange(&g_wsa_init_state, 0);
            rt_trap("Network: failed to register WinSock cleanup");
            return;
        }
        InterlockedExchange(&g_wsa_init_state, 2);
        return;
    }
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
/// @return true for WinSock's would-block/already/in-progress connect states.
bool rt_socket_error_is_in_progress(int err) {
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS || err == WSAEALREADY;
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
    return err == WSAENOTSOCK || err == WSAEINVAL || err == WSAEINTR || err == WSAECONNABORTED ||
           err == WSAESHUTDOWN;
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
bool set_socket_timeout(socket_t sock, int timeout_ms, bool is_recv) {
    if (timeout_ms < 0)
        timeout_ms = 0;
    DWORD tv = (DWORD)timeout_ms;
    return setsockopt(sock,
                      SOL_SOCKET,
                      is_recv ? SO_RCVTIMEO : SO_SNDTIMEO,
                      (const char *)&tv,
                      sizeof(tv)) == 0;
}

typedef void(WSAAPI *rt_wsa_set_last_error_fn)(int);

/// @brief Set WinSock's thread-local error without expanding native imports.
static void rt_socket_set_last_error(int error) {
    HMODULE winsock = GetModuleHandleW(L"ws2_32.dll");
    rt_wsa_set_last_error_fn set_error =
        winsock ? (rt_wsa_set_last_error_fn)GetProcAddress(winsock, "WSASetLastError") : NULL;
    if (set_error)
        set_error(error);
    else
        SetLastError((DWORD)error);
}

/// @brief Wait for a WinSock socket to become readable or writable.
/// @param sock Socket handle to wait on.
/// @param timeout_ms Timeout in milliseconds.
/// @param for_write true waits for write readiness; false waits for read readiness.
/// @return Positive when ready, zero on timeout, negative on error.
int wait_socket(socket_t sock, int timeout_ms, bool for_write) {
    if (timeout_ms < 0) {
        rt_socket_set_last_error(WSAEINVAL);
        return -1;
    }
    if (sock == INVALID_SOCK) {
        rt_socket_set_last_error(WSAENOTSOCK);
        return -1;
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (for_write)
        return select(0, NULL, &fds, NULL, &tv);
    return select(0, &fds, NULL, NULL, &tv);
}
