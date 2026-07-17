//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_network_internal.h
// Purpose: Shared platform-specific definitions for the networking runtime.
//   Provides socket type abstractions, SIGPIPE suppression, error classification,
//   and socket helper declarations used by TCP, UDP, and DNS modules.
//
// Key invariants:
//   - _DARWIN_C_SOURCE / _GNU_SOURCE must be defined BEFORE including this header.
//     Each .c file defines them at the top of the file before any includes.
//   - socket_t is SOCKET on Windows, int on Unix.
//   - All platform-specific socket includes are handled here.
//   - WSA initialization (Windows) must be called before any socket operation.
//
// Ownership/Lifetime:
//   - Socket handles are owned by the creating module (TCP, UDP).
//   - This header does not allocate or free any resources.
//
// Links: src/runtime/network/rt_network.c (TCP client + server),
//        src/runtime/network/rt_network_udp.c (UDP sockets),
//        src/runtime/network/rt_network_dns.c (DNS resolution),
//        src/runtime/network/rt_network.h (public API)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_network.h"

#include "rt_bytes.h"
#include "rt_error.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_socket_platform.h"
#include "rt_string.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Typed Network Trap
//=============================================================================

extern void rt_trap_net(const char *msg, int err_code);

/// @brief Map platform errno / WSAGetLastError() to an Err_* network code.
static inline int net_classify_errno(void) {
    int e = GET_LAST_ERROR();
#if RT_PLATFORM_WINDOWS
    switch (e) {
        case WSAECONNREFUSED:
            return Err_ConnectionRefused;
        case WSAECONNRESET:
        case WSAECONNABORTED:
            return Err_ConnectionReset;
        case WSAETIMEDOUT:
            return Err_Timeout;
        case WSAENETUNREACH:
        case WSAEHOSTUNREACH:
            return Err_NetworkError;
        case WSAESHUTDOWN:
        case WSAENOTCONN:
            return Err_ConnectionClosed;
        default:
            return Err_NetworkError;
    }
#else
    switch (e) {
        case ECONNREFUSED:
            return Err_ConnectionRefused;
        case ECONNRESET:
        case EPIPE:
            return Err_ConnectionReset;
        case ETIMEDOUT:
            return Err_Timeout;
        case ENETUNREACH:
        case EHOSTUNREACH:
            return Err_NetworkError;
        case ENOTCONN:
            return Err_ConnectionClosed;
        default:
            return Err_NetworkError;
    }
#endif
}

//=============================================================================
// Internal Bytes Access
//=============================================================================

static inline uint8_t *bytes_data(void *obj) {
    uint8_t *(*bytes_data_fn)(void *) = rt_bytes_data;
    uint8_t *data = bytes_data_fn(obj);
    return data;
}

static inline int64_t bytes_len(void *obj) {
    int64_t (*bytes_len_fn)(void *) = rt_bytes_len;
    int64_t len = bytes_len_fn(obj);
    return len;
}

//=============================================================================
// Runtime String Helpers
//=============================================================================

/// @brief Borrow an rt_string as a C string for OS networking APIs.
/// @details Runtime strings may contain embedded NUL bytes, while getaddrinfo,
/// inet_pton, and similar APIs stop at the first NUL. Return 0 for NULL,
/// invalid, oversized, or embedded-NUL strings so public wrappers can reject
/// them before they reach the OS.
int rt_net_cstr_no_embedded_nul(rt_string value, const char **out, size_t *len_out);

//=============================================================================
// Socket Helpers (defined in rt_network.c, used by UDP too)
//=============================================================================

/// @brief Convert a public int64 millisecond timeout to the socket helper range.
/// @return 1 on success, 0 if negative or larger than INT_MAX.
int rt_net_timeout_ms_to_int(int64_t timeout_ms, int *out_timeout_ms);

/// @brief Convert a positive byte count to an int-sized socket API length.
/// @return 1 on success, 0 if the count is larger than INT_MAX.
int rt_net_i64_len_to_int(int64_t byte_count, int *out_len);

/// @brief Wait for socket to become readable/writable with timeout.
/// @return 1 if ready, 0 if timeout, -1 on error.
int wait_socket(socket_t sock, int timeout_ms, bool for_write);

/// @brief Access the raw socket owned by a Tcp object.
/// @return Socket descriptor, or INVALID_SOCK for NULL/invalid objects.
socket_t rt_tcp_socket_fd(void *obj);

/// @brief Detach the raw socket from a Tcp object without closing it.
/// @details Marks the Tcp object closed and returns ownership of the socket to the caller.
void rt_tcp_detach_socket(void *obj);
