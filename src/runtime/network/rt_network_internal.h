//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "rt_string.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Platform-Specific Includes and Definitions
//=============================================================================

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")

typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#define SOCK_ERROR SOCKET_ERROR
#define CLOSE_SOCKET(s) closesocket(s)
#define GET_LAST_ERROR() WSAGetLastError()
#define WOULD_BLOCK (WSAGetLastError() == WSAEWOULDBLOCK)
#define EINPROGRESS_VAL WSAEWOULDBLOCK
#define CONN_REFUSED WSAECONNREFUSED
#define ADDR_IN_USE WSAEADDRINUSE
#define PERM_DENIED WSAEACCES

#elif defined(__viperdos__)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef int socket_t;
#define INVALID_SOCK (-1)
#define SOCK_ERROR (-1)
#define CLOSE_SOCKET(s) close(s)
#define GET_LAST_ERROR() errno
#define WOULD_BLOCK (errno == EAGAIN || errno == EWOULDBLOCK)
#define EINPROGRESS_VAL EINPROGRESS
#define CONN_REFUSED ECONNREFUSED
#define ADDR_IN_USE EADDRINUSE
#define PERM_DENIED EACCES

#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef int socket_t;
#define INVALID_SOCK (-1)
#define SOCK_ERROR (-1)
#define CLOSE_SOCKET(s) close(s)
#define GET_LAST_ERROR() errno
#define WOULD_BLOCK (errno == EAGAIN || errno == EWOULDBLOCK)
#define EINPROGRESS_VAL EINPROGRESS
#define CONN_REFUSED ECONNREFUSED
#define ADDR_IN_USE EADDRINUSE
#define PERM_DENIED EACCES
#endif

//=============================================================================
// SIGPIPE Suppression
//=============================================================================

#if defined(__linux__) || defined(__viperdos__)
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

/// @brief Suppress SIGPIPE for a socket (macOS only; no-op elsewhere).
static inline void suppress_sigpipe(socket_t sock) {
#ifdef __APPLE__
    int val = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
    (void)sock;
}

//=============================================================================
// Typed Network Trap
//=============================================================================

extern void rt_trap_net(const char *msg, int err_code);

/// @brief Map platform errno / WSAGetLastError() to an Err_* network code.
static inline int net_classify_errno(void) {
    int e = GET_LAST_ERROR();
#ifdef _WIN32
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

typedef struct {
    int64_t len;
    uint8_t *data;
} bytes_impl;

static inline uint8_t *bytes_data(void *obj) {
    if (!obj)
        return NULL;
    return ((bytes_impl *)obj)->data;
}

static inline int64_t bytes_len(void *obj) {
    if (!obj)
        return 0;
    return ((bytes_impl *)obj)->len;
}

//=============================================================================
// WSA Initialization
//=============================================================================

/// @brief Initialize Windows Sockets (thread-safe, idempotent). No-op on Unix.
void rt_net_init_wsa(void);

//=============================================================================
// Socket Helpers (defined in rt_network.c, used by UDP too)
//=============================================================================

/// @brief Set socket timeout for send or receive.
void set_socket_timeout(socket_t sock, int timeout_ms, bool is_recv);

/// @brief Wait for socket to become readable/writable with timeout.
/// @return 1 if ready, 0 if timeout, -1 on error.
int wait_socket(socket_t sock, int timeout_ms, bool for_write);

/// @brief Access the raw socket owned by a Tcp object.
/// @return Socket descriptor, or INVALID_SOCK for NULL/invalid objects.
socket_t rt_tcp_socket_fd(void *obj);

/// @brief Detach the raw socket from a Tcp object without closing it.
/// @details Marks the Tcp object closed and returns ownership of the socket to the caller.
void rt_tcp_detach_socket(void *obj);
