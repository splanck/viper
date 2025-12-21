//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

// Enable multicast support (ip_mreq, IP_ADD_MEMBERSHIP, etc.)
#ifndef _WIN32
#define _DARWIN_C_SOURCE 1 // macOS: expose BSD extensions
#define _GNU_SOURCE 1      // Linux: expose ip_mreq
#endif

///
/// @file rt_network.c
/// @brief TCP networking support for Viper.Network.Tcp and TcpServer.
///
/// Implements cross-platform TCP client and server functionality using
/// Berkeley sockets API. Features:
/// - Blocking I/O with configurable timeouts
/// - TCP_NODELAY enabled by default (low latency)
/// - Platform-specific handling for Windows (WSA) and Unix
///
/// **Thread Safety:** Each connection is independent and can be used from
/// a single thread. Sharing connections across threads requires external
/// synchronization.
///
//===----------------------------------------------------------------------===//

#include "rt_network.h"

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

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
// Internal Bytes Access
//=============================================================================

typedef struct
{
    int64_t len;
    uint8_t *data;
} bytes_impl;

static inline uint8_t *bytes_data(void *obj)
{
    if (!obj)
        return NULL;
    return ((bytes_impl *)obj)->data;
}

static inline int64_t bytes_len(void *obj)
{
    if (!obj)
        return 0;
    return ((bytes_impl *)obj)->len;
}

//=============================================================================
// Tcp Connection Structure
//=============================================================================

typedef struct rt_tcp
{
    socket_t sock;        // Socket descriptor
    char *host;           // Remote host (allocated)
    int port;             // Remote port
    int local_port;       // Local port
    bool is_open;         // Connection state
    int recv_timeout_ms;  // Receive timeout (0 = none)
    int send_timeout_ms;  // Send timeout (0 = none)
} rt_tcp_t;

//=============================================================================
// TcpServer Structure
//=============================================================================

typedef struct rt_tcp_server
{
    socket_t sock;        // Listening socket
    char *address;        // Bound address (allocated)
    int port;             // Listening port
    bool is_listening;    // Server state
} rt_tcp_server_t;

//=============================================================================
// Windows WSA Initialization
//=============================================================================

#ifdef _WIN32
static bool wsa_initialized = false;

static void init_wsa(void)
{
    if (wsa_initialized)
        return;

    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0)
    {
        rt_trap("Network: WSAStartup failed");
    }
    wsa_initialized = true;
}
#else
#define init_wsa() ((void)0)
#endif

//=============================================================================
// Socket Helpers
//=============================================================================

/// @brief Set socket to non-blocking mode.
static void set_nonblocking(socket_t sock, bool nonblocking)
{
#ifdef _WIN32
    u_long mode = nonblocking ? 1 : 0;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (nonblocking)
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    else
        fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
#endif
}

/// @brief Enable TCP_NODELAY on socket.
static void set_nodelay(socket_t sock)
{
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
}

/// @brief Set socket timeout.
static void set_socket_timeout(socket_t sock, int timeout_ms, bool is_recv)
{
#ifdef _WIN32
    DWORD tv = (DWORD)timeout_ms;
    setsockopt(sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO,
               (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO,
               &tv, sizeof(tv));
#endif
}

/// @brief Wait for socket to become readable/writable with timeout.
/// @return 1 if ready, 0 if timeout, -1 on error.
static int wait_socket(socket_t sock, int timeout_ms, bool for_write)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int result;
    if (for_write)
        result = select((int)(sock + 1), NULL, &fds, NULL, &tv);
    else
        result = select((int)(sock + 1), &fds, NULL, NULL, &tv);

    return result;
}

/// @brief Get local port from socket.
static int get_local_port(socket_t sock)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(sock, (struct sockaddr *)&addr, &len) == 0)
    {
        return ntohs(addr.sin_port);
    }
    return 0;
}

//=============================================================================
// Tcp Client - Connection Creation
//=============================================================================

void *rt_tcp_connect(rt_string host, int64_t port)
{
    return rt_tcp_connect_for(host, port, 0);
}

void *rt_tcp_connect_for(rt_string host, int64_t port, int64_t timeout_ms)
{
    init_wsa();

    const char *host_ptr = rt_string_cstr(host);
    if (!host_ptr || *host_ptr == '\0')
    {
        rt_trap("Network: invalid host");
    }

    if (port < 1 || port > 65535)
    {
        rt_trap("Network: invalid port number");
    }

    // Copy host string
    size_t host_len = strlen(host_ptr);
    char *host_cstr = (char *)malloc(host_len + 1);
    if (!host_cstr)
    {
        rt_trap("Network: memory allocation failed");
    }
    memcpy(host_cstr, host_ptr, host_len + 1);

    // Resolve hostname
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", (int)port);

    int status = getaddrinfo(host_cstr, port_str, &hints, &res);
    if (status != 0)
    {
        free(host_cstr);
        rt_trap("Network: host not found");
    }

    // Create socket
    socket_t sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCK)
    {
        freeaddrinfo(res);
        free(host_cstr);
        rt_trap("Network: failed to create socket");
    }

    // Connect with optional timeout
    int connect_result;
    if (timeout_ms > 0)
    {
        // Non-blocking connect with timeout
        set_nonblocking(sock, true);

        connect_result = connect(sock, res->ai_addr, (int)res->ai_addrlen);

        if (connect_result == SOCK_ERROR)
        {
            int err = GET_LAST_ERROR();
#ifdef _WIN32
            if (err == WSAEWOULDBLOCK)
#else
            if (err == EINPROGRESS)
#endif
            {
                // Wait for connection to complete
                int ready = wait_socket(sock, (int)timeout_ms, true);
                if (ready <= 0)
                {
                    CLOSE_SOCKET(sock);
                    freeaddrinfo(res);
                    free(host_cstr);
                    rt_trap("Network: connection timeout");
                }

                // Check if connection succeeded
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);
                if (so_error != 0)
                {
                    CLOSE_SOCKET(sock);
                    freeaddrinfo(res);
                    free(host_cstr);
                    if (so_error == CONN_REFUSED)
                        rt_trap("Network: connection refused");
                    rt_trap("Network: connection failed");
                }
            }
            else
            {
                CLOSE_SOCKET(sock);
                freeaddrinfo(res);
                free(host_cstr);
                if (err == CONN_REFUSED)
                    rt_trap("Network: connection refused");
                rt_trap("Network: connection failed");
            }
        }

        // Switch back to blocking mode
        set_nonblocking(sock, false);
    }
    else
    {
        // Blocking connect
        connect_result = connect(sock, res->ai_addr, (int)res->ai_addrlen);
        if (connect_result == SOCK_ERROR)
        {
            int err = GET_LAST_ERROR();
            CLOSE_SOCKET(sock);
            freeaddrinfo(res);
            free(host_cstr);
            if (err == CONN_REFUSED)
                rt_trap("Network: connection refused");
            rt_trap("Network: connection failed");
        }
    }

    freeaddrinfo(res);

    // Enable TCP_NODELAY
    set_nodelay(sock);

    // Create connection object
    rt_tcp_t *tcp = (rt_tcp_t *)calloc(1, sizeof(rt_tcp_t));
    if (!tcp)
    {
        CLOSE_SOCKET(sock);
        free(host_cstr);
        rt_trap("Network: memory allocation failed");
    }

    tcp->sock = sock;
    tcp->host = host_cstr;
    tcp->port = (int)port;
    tcp->local_port = get_local_port(sock);
    tcp->is_open = true;
    tcp->recv_timeout_ms = 0;
    tcp->send_timeout_ms = 0;

    return tcp;
}

//=============================================================================
// Tcp Client - Properties
//=============================================================================

rt_string rt_tcp_host(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return rt_const_cstr(tcp->host);
}

int64_t rt_tcp_port(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return tcp->port;
}

int64_t rt_tcp_local_port(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return tcp->local_port;
}

int8_t rt_tcp_is_open(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    return tcp->is_open ? 1 : 0;
}

int64_t rt_tcp_available(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        return 0;

#ifdef _WIN32
    u_long bytes_available = 0;
    ioctlsocket(tcp->sock, FIONREAD, &bytes_available);
    return (int64_t)bytes_available;
#else
    int bytes_available = 0;
    ioctl(tcp->sock, FIONREAD, &bytes_available);
    return (int64_t)bytes_available;
#endif
}

//=============================================================================
// Tcp Client - Send Methods
//=============================================================================

int64_t rt_tcp_send(void *obj, void *data)
{
    if (!obj)
        rt_trap("Network: NULL connection");
    if (!data)
        rt_trap("Network: NULL data");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap("Network: connection closed");

    int64_t len = bytes_len(data);
    uint8_t *buf = bytes_data(data);

    if (len == 0)
        return 0;

    int sent = send(tcp->sock, (const char *)buf, (int)len, 0);
    if (sent == SOCK_ERROR)
    {
        tcp->is_open = false;
        rt_trap("Network: send failed");
    }

    return sent;
}

int64_t rt_tcp_send_str(void *obj, rt_string text)
{
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap("Network: connection closed");

    const char *text_ptr = rt_string_cstr(text);
    size_t len = strlen(text_ptr);
    if (len == 0)
        return 0;

    int sent = send(tcp->sock, text_ptr, (int)len, 0);
    if (sent == SOCK_ERROR)
    {
        tcp->is_open = false;
        rt_trap("Network: send failed");
    }

    return sent;
}

void rt_tcp_send_all(void *obj, void *data)
{
    if (!obj)
        rt_trap("Network: NULL connection");
    if (!data)
        rt_trap("Network: NULL data");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap("Network: connection closed");

    int64_t len = bytes_len(data);
    uint8_t *buf = bytes_data(data);

    int64_t total_sent = 0;
    while (total_sent < len)
    {
        int sent = send(tcp->sock, (const char *)(buf + total_sent),
                        (int)(len - total_sent), 0);
        if (sent == SOCK_ERROR)
        {
            tcp->is_open = false;
            rt_trap("Network: send failed");
        }
        if (sent == 0)
        {
            tcp->is_open = false;
            rt_trap("Network: connection closed by peer");
        }
        total_sent += sent;
    }
}

//=============================================================================
// Tcp Client - Receive Methods
//=============================================================================

void *rt_tcp_recv(void *obj, int64_t max_bytes)
{
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap("Network: connection closed");

    if (max_bytes <= 0)
        return rt_bytes_new(0);

    // Allocate receive buffer
    void *result = rt_bytes_new(max_bytes);
    uint8_t *buf = bytes_data(result);

    int received = recv(tcp->sock, (char *)buf, (int)max_bytes, 0);
    if (received == SOCK_ERROR)
    {
        // Check if it's a timeout
#ifdef _WIN32
        if (WSAGetLastError() == WSAETIMEDOUT)
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
#endif
        {
            // Timeout - return empty bytes
            return rt_bytes_new(0);
        }
        tcp->is_open = false;
        rt_trap("Network: receive failed");
    }

    if (received == 0)
    {
        // Connection closed by peer
        tcp->is_open = false;
        return rt_bytes_new(0);
    }

    // Return exact size received
    if (received < max_bytes)
    {
        void *exact = rt_bytes_new(received);
        memcpy(bytes_data(exact), buf, received);
        return exact;
    }

    return result;
}

rt_string rt_tcp_recv_str(void *obj, int64_t max_bytes)
{
    void *bytes = rt_tcp_recv(obj, max_bytes);
    return rt_bytes_to_str(bytes);
}

void *rt_tcp_recv_exact(void *obj, int64_t count)
{
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap("Network: connection closed");

    if (count <= 0)
        return rt_bytes_new(0);

    void *result = rt_bytes_new(count);
    uint8_t *buf = bytes_data(result);

    int64_t total_received = 0;
    while (total_received < count)
    {
        int received = recv(tcp->sock, (char *)(buf + total_received),
                            (int)(count - total_received), 0);
        if (received == SOCK_ERROR)
        {
            tcp->is_open = false;
            rt_trap("Network: receive failed");
        }
        if (received == 0)
        {
            tcp->is_open = false;
            rt_trap("Network: connection closed before receiving all data");
        }
        total_received += received;
    }

    return result;
}

rt_string rt_tcp_recv_line(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (!tcp->is_open)
        rt_trap("Network: connection closed");

    // Line buffer with initial capacity
    size_t cap = 256;
    size_t len = 0;
    char *line = (char *)malloc(cap);
    if (!line)
        rt_trap("Network: memory allocation failed");

    while (1)
    {
        char c;
        int received = recv(tcp->sock, &c, 1, 0);
        if (received == SOCK_ERROR)
        {
            free(line);
            tcp->is_open = false;
            rt_trap("Network: receive failed");
        }
        if (received == 0)
        {
            free(line);
            tcp->is_open = false;
            rt_trap("Network: connection closed before end of line");
        }

        if (c == '\n')
        {
            // Strip trailing CR if present
            if (len > 0 && line[len - 1] == '\r')
            {
                len--;
            }
            break;
        }

        // Add character to buffer
        if (len >= cap)
        {
            cap *= 2;
            char *new_line = (char *)realloc(line, cap);
            if (!new_line)
            {
                free(line);
                rt_trap("Network: memory allocation failed");
            }
            line = new_line;
        }
        line[len++] = c;
    }

    // Create string result
    rt_string result = rt_string_from_bytes(line, len);
    free(line);
    return result;
}

//=============================================================================
// Tcp Client - Timeout and Close
//=============================================================================

void rt_tcp_set_recv_timeout(void *obj, int64_t timeout_ms)
{
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    tcp->recv_timeout_ms = (int)timeout_ms;
    set_socket_timeout(tcp->sock, (int)timeout_ms, true);
}

void rt_tcp_set_send_timeout(void *obj, int64_t timeout_ms)
{
    if (!obj)
        rt_trap("Network: NULL connection");

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    tcp->send_timeout_ms = (int)timeout_ms;
    set_socket_timeout(tcp->sock, (int)timeout_ms, false);
}

void rt_tcp_close(void *obj)
{
    if (!obj)
        return;

    rt_tcp_t *tcp = (rt_tcp_t *)obj;
    if (tcp->is_open)
    {
        CLOSE_SOCKET(tcp->sock);
        tcp->is_open = false;
    }
}

//=============================================================================
// TcpServer - Creation
//=============================================================================

void *rt_tcp_server_listen(int64_t port)
{
    return rt_tcp_server_listen_at(rt_const_cstr("0.0.0.0"), port);
}

void *rt_tcp_server_listen_at(rt_string address, int64_t port)
{
    init_wsa();

    if (port < 1 || port > 65535)
    {
        rt_trap("Network: invalid port number");
    }

    // Get address string
    const char *addr_ptr = rt_string_cstr(address);
    size_t addr_len = strlen(addr_ptr);
    char *addr_cstr = (char *)malloc(addr_len + 1);
    if (!addr_cstr)
    {
        rt_trap("Network: memory allocation failed");
    }
    memcpy(addr_cstr, addr_ptr, addr_len + 1);

    // Create socket
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK)
    {
        free(addr_cstr);
        rt_trap("Network: failed to create socket");
    }

    // Enable address reuse
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    // Bind to address
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, addr_cstr, &bind_addr.sin_addr) != 1)
    {
        CLOSE_SOCKET(sock);
        free(addr_cstr);
        rt_trap("Network: invalid address");
    }

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == SOCK_ERROR)
    {
        int err = GET_LAST_ERROR();
        CLOSE_SOCKET(sock);
        free(addr_cstr);
        if (err == ADDR_IN_USE)
            rt_trap("Network: port already in use");
        if (err == PERM_DENIED)
            rt_trap("Network: permission denied (port < 1024?)");
        rt_trap("Network: bind failed");
    }

    // Start listening
    if (listen(sock, SOMAXCONN) == SOCK_ERROR)
    {
        CLOSE_SOCKET(sock);
        free(addr_cstr);
        rt_trap("Network: listen failed");
    }

    // Create server object
    rt_tcp_server_t *server = (rt_tcp_server_t *)calloc(1, sizeof(rt_tcp_server_t));
    if (!server)
    {
        CLOSE_SOCKET(sock);
        free(addr_cstr);
        rt_trap("Network: memory allocation failed");
    }

    server->sock = sock;
    server->address = addr_cstr;
    server->port = (int)port;
    server->is_listening = true;

    return server;
}

//=============================================================================
// TcpServer - Properties
//=============================================================================

int64_t rt_tcp_server_port(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL server");

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    return server->port;
}

rt_string rt_tcp_server_address(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL server");

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    return rt_const_cstr(server->address);
}

int8_t rt_tcp_server_is_listening(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL server");

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    return server->is_listening ? 1 : 0;
}

//=============================================================================
// TcpServer - Accept and Close
//=============================================================================

void *rt_tcp_server_accept(void *obj)
{
    return rt_tcp_server_accept_for(obj, 0);
}

void *rt_tcp_server_accept_for(void *obj, int64_t timeout_ms)
{
    if (!obj)
        rt_trap("Network: NULL server");

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    if (!server->is_listening)
        rt_trap("Network: server not listening");

    // Use select for timeout
    if (timeout_ms > 0)
    {
        int ready = wait_socket(server->sock, (int)timeout_ms, false);
        if (ready == 0)
        {
            // Timeout - return NULL
            return NULL;
        }
        if (ready < 0)
        {
            rt_trap("Network: accept failed");
        }
    }

    // Accept connection
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    socket_t client_sock = accept(server->sock, (struct sockaddr *)&client_addr, &client_len);
    if (client_sock == INVALID_SOCK)
    {
        // Check if server was closed
        if (!server->is_listening)
            return NULL;
        rt_trap("Network: accept failed");
    }

    // Enable TCP_NODELAY
    set_nodelay(client_sock);

    // Get client info
    char host_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, host_buf, sizeof(host_buf));

    char *host_cstr = (char *)malloc(strlen(host_buf) + 1);
    if (!host_cstr)
    {
        CLOSE_SOCKET(client_sock);
        rt_trap("Network: memory allocation failed");
    }
    strcpy(host_cstr, host_buf);

    // Create connection object
    rt_tcp_t *tcp = (rt_tcp_t *)calloc(1, sizeof(rt_tcp_t));
    if (!tcp)
    {
        CLOSE_SOCKET(client_sock);
        free(host_cstr);
        rt_trap("Network: memory allocation failed");
    }

    tcp->sock = client_sock;
    tcp->host = host_cstr;
    tcp->port = ntohs(client_addr.sin_port);
    tcp->local_port = get_local_port(client_sock);
    tcp->is_open = true;
    tcp->recv_timeout_ms = 0;
    tcp->send_timeout_ms = 0;

    return tcp;
}

void rt_tcp_server_close(void *obj)
{
    if (!obj)
        return;

    rt_tcp_server_t *server = (rt_tcp_server_t *)obj;
    if (server->is_listening)
    {
        CLOSE_SOCKET(server->sock);
        server->is_listening = false;
    }
}

//=============================================================================
// Udp Socket Structure
//=============================================================================

typedef struct rt_udp
{
    socket_t sock;           // Socket descriptor
    char *address;           // Bound address (allocated, or NULL if unbound)
    int port;                // Bound port (0 if unbound)
    bool is_bound;           // Whether socket is bound
    bool is_open;            // Socket state
    char sender_host[INET_ADDRSTRLEN]; // Last sender host
    int sender_port;         // Last sender port
    int recv_timeout_ms;     // Receive timeout (0 = none)
} rt_udp_t;

//=============================================================================
// Udp - Creation
//=============================================================================

void *rt_udp_new(void)
{
    init_wsa();

    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCK)
    {
        rt_trap("Network: failed to create UDP socket");
    }

    rt_udp_t *udp = (rt_udp_t *)calloc(1, sizeof(rt_udp_t));
    if (!udp)
    {
        CLOSE_SOCKET(sock);
        rt_trap("Network: memory allocation failed");
    }

    udp->sock = sock;
    udp->address = NULL;
    udp->port = 0;
    udp->is_bound = false;
    udp->is_open = true;
    udp->sender_host[0] = '\0';
    udp->sender_port = 0;
    udp->recv_timeout_ms = 0;

    return udp;
}

void *rt_udp_bind(int64_t port)
{
    return rt_udp_bind_at(rt_const_cstr("0.0.0.0"), port);
}

void *rt_udp_bind_at(rt_string address, int64_t port)
{
    init_wsa();

    if (port < 0 || port > 65535)
    {
        rt_trap("Network: invalid port number");
    }

    const char *addr_ptr = rt_string_cstr(address);
    if (!addr_ptr || *addr_ptr == '\0')
    {
        rt_trap("Network: invalid address");
    }

    // Create socket
    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCK)
    {
        rt_trap("Network: failed to create UDP socket");
    }

    // Enable address reuse
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    // Bind to address
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, addr_ptr, &bind_addr.sin_addr) != 1)
    {
        CLOSE_SOCKET(sock);
        rt_trap("Network: invalid address");
    }

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == SOCK_ERROR)
    {
        int err = GET_LAST_ERROR();
        CLOSE_SOCKET(sock);
        if (err == ADDR_IN_USE)
            rt_trap("Network: port already in use");
        if (err == PERM_DENIED)
            rt_trap("Network: permission denied (port < 1024?)");
        rt_trap("Network: bind failed");
    }

    // Get actual port if 0 was specified
    int actual_port = (int)port;
    if (port == 0)
    {
        struct sockaddr_in bound_addr;
        socklen_t len = sizeof(bound_addr);
        if (getsockname(sock, (struct sockaddr *)&bound_addr, &len) == 0)
        {
            actual_port = ntohs(bound_addr.sin_port);
        }
    }

    // Copy address string
    size_t addr_len = strlen(addr_ptr);
    char *addr_cstr = (char *)malloc(addr_len + 1);
    if (!addr_cstr)
    {
        CLOSE_SOCKET(sock);
        rt_trap("Network: memory allocation failed");
    }
    memcpy(addr_cstr, addr_ptr, addr_len + 1);

    // Create UDP object
    rt_udp_t *udp = (rt_udp_t *)calloc(1, sizeof(rt_udp_t));
    if (!udp)
    {
        CLOSE_SOCKET(sock);
        free(addr_cstr);
        rt_trap("Network: memory allocation failed");
    }

    udp->sock = sock;
    udp->address = addr_cstr;
    udp->port = actual_port;
    udp->is_bound = true;
    udp->is_open = true;
    udp->sender_host[0] = '\0';
    udp->sender_port = 0;
    udp->recv_timeout_ms = 0;

    return udp;
}

//=============================================================================
// Udp - Properties
//=============================================================================

int64_t rt_udp_port(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    return udp->port;
}

rt_string rt_udp_address(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (udp->address)
        return rt_const_cstr(udp->address);
    return rt_str_empty();
}

int8_t rt_udp_is_bound(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    return udp->is_bound ? 1 : 0;
}

//=============================================================================
// Udp - Send Methods
//=============================================================================

/// @brief Helper to resolve hostname and create sockaddr_in.
static int resolve_host(const char *host, int port, struct sockaddr_in *addr)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((uint16_t)port);

    // Try parsing as IP address first
    if (inet_pton(AF_INET, host, &addr->sin_addr) == 1)
    {
        return 0;
    }

    // Resolve hostname
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(host, NULL, &hints, &res) != 0)
    {
        return -1;
    }

    struct sockaddr_in *resolved = (struct sockaddr_in *)res->ai_addr;
    addr->sin_addr = resolved->sin_addr;
    freeaddrinfo(res);

    return 0;
}

int64_t rt_udp_send_to(void *obj, rt_string host, int64_t port, void *data)
{
    if (!obj)
        rt_trap("Network: NULL socket");
    if (!data)
        rt_trap("Network: NULL data");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap("Network: socket closed");

    const char *host_ptr = rt_string_cstr(host);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: invalid host");

    if (port < 1 || port > 65535)
        rt_trap("Network: invalid port number");

    int64_t len = bytes_len(data);
    uint8_t *buf = bytes_data(data);

    if (len == 0)
        return 0;

    // Check packet size
    if (len > 65507)
        rt_trap("Network: message too large (max 65507 bytes for UDP)");

    // Resolve destination
    struct sockaddr_in dest_addr;
    if (resolve_host(host_ptr, (int)port, &dest_addr) != 0)
        rt_trap("Network: host not found");

    int sent = sendto(udp->sock, (const char *)buf, (int)len, 0,
                      (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (sent == SOCK_ERROR)
    {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEMSGSIZE)
            rt_trap("Network: message too large");
#else
        if (errno == EMSGSIZE)
            rt_trap("Network: message too large");
#endif
        rt_trap("Network: send failed");
    }

    return sent;
}

int64_t rt_udp_send_to_str(void *obj, rt_string host, int64_t port, rt_string text)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap("Network: socket closed");

    const char *host_ptr = rt_string_cstr(host);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: invalid host");

    if (port < 1 || port > 65535)
        rt_trap("Network: invalid port number");

    const char *text_ptr = rt_string_cstr(text);
    size_t len = strlen(text_ptr);

    if (len == 0)
        return 0;

    if (len > 65507)
        rt_trap("Network: message too large (max 65507 bytes for UDP)");

    // Resolve destination
    struct sockaddr_in dest_addr;
    if (resolve_host(host_ptr, (int)port, &dest_addr) != 0)
        rt_trap("Network: host not found");

    int sent = sendto(udp->sock, text_ptr, (int)len, 0,
                      (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (sent == SOCK_ERROR)
    {
        rt_trap("Network: send failed");
    }

    return sent;
}

//=============================================================================
// Udp - Receive Methods
//=============================================================================

void *rt_udp_recv(void *obj, int64_t max_bytes)
{
    return rt_udp_recv_from(obj, max_bytes);
}

void *rt_udp_recv_from(void *obj, int64_t max_bytes)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap("Network: socket closed");

    if (max_bytes <= 0)
        return rt_bytes_new(0);

    // Allocate receive buffer
    void *result = rt_bytes_new(max_bytes);
    uint8_t *buf = bytes_data(result);

    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    int received = recvfrom(udp->sock, (char *)buf, (int)max_bytes, 0,
                            (struct sockaddr *)&sender_addr, &sender_len);

    if (received == SOCK_ERROR)
    {
        // Check for timeout
#ifdef _WIN32
        if (WSAGetLastError() == WSAETIMEDOUT)
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
#endif
        {
            // Return empty bytes on timeout
            return rt_bytes_new(0);
        }
        rt_trap("Network: receive failed");
    }

    // Store sender info
    inet_ntop(AF_INET, &sender_addr.sin_addr, udp->sender_host, sizeof(udp->sender_host));
    udp->sender_port = ntohs(sender_addr.sin_port);

    // Return exact size received
    if (received < max_bytes)
    {
        void *exact = rt_bytes_new(received);
        memcpy(bytes_data(exact), buf, received);
        return exact;
    }

    return result;
}

void *rt_udp_recv_for(void *obj, int64_t max_bytes, int64_t timeout_ms)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap("Network: socket closed");

    // Use select for timeout
    if (timeout_ms > 0)
    {
        int ready = wait_socket(udp->sock, (int)timeout_ms, false);
        if (ready == 0)
        {
            // Timeout - return NULL
            return NULL;
        }
        if (ready < 0)
        {
            rt_trap("Network: receive failed");
        }
    }

    return rt_udp_recv_from(obj, max_bytes);
}

rt_string rt_udp_sender_host(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    return rt_const_cstr(udp->sender_host);
}

int64_t rt_udp_sender_port(void *obj)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    return udp->sender_port;
}

//=============================================================================
// Udp - Options and Close
//=============================================================================

void rt_udp_set_broadcast(void *obj, int8_t enable)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap("Network: socket closed");

    int flag = enable ? 1 : 0;
    if (setsockopt(udp->sock, SOL_SOCKET, SO_BROADCAST, (const char *)&flag, sizeof(flag)) == SOCK_ERROR)
    {
        rt_trap("Network: failed to set broadcast option");
    }
}

void rt_udp_join_group(void *obj, rt_string group_addr)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        rt_trap("Network: socket closed");

    const char *addr_ptr = rt_string_cstr(group_addr);
    if (!addr_ptr || *addr_ptr == '\0')
        rt_trap("Network: invalid multicast address");

    // Validate multicast address (224.0.0.0 - 239.255.255.255)
    struct in_addr mcast_addr;
    if (inet_pton(AF_INET, addr_ptr, &mcast_addr) != 1)
    {
        rt_trap("Network: invalid multicast address");
    }

    uint32_t addr_val = ntohl(mcast_addr.s_addr);
    if ((addr_val & 0xF0000000) != 0xE0000000)
    {
        rt_trap("Network: invalid multicast address (must be 224.0.0.0 - 239.255.255.255)");
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr = mcast_addr;
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(udp->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) == SOCK_ERROR)
    {
        rt_trap("Network: failed to join multicast group");
    }
}

void rt_udp_leave_group(void *obj, rt_string group_addr)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (!udp->is_open)
        return;  // Silently ignore if closed

    const char *addr_ptr = rt_string_cstr(group_addr);
    if (!addr_ptr || *addr_ptr == '\0')
        return;

    struct in_addr mcast_addr;
    if (inet_pton(AF_INET, addr_ptr, &mcast_addr) != 1)
        return;

    struct ip_mreq mreq;
    mreq.imr_multiaddr = mcast_addr;
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    setsockopt(udp->sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const char *)&mreq, sizeof(mreq));
}

void rt_udp_set_recv_timeout(void *obj, int64_t timeout_ms)
{
    if (!obj)
        rt_trap("Network: NULL socket");

    rt_udp_t *udp = (rt_udp_t *)obj;
    udp->recv_timeout_ms = (int)timeout_ms;
    set_socket_timeout(udp->sock, (int)timeout_ms, true);
}

void rt_udp_close(void *obj)
{
    if (!obj)
        return;

    rt_udp_t *udp = (rt_udp_t *)obj;
    if (udp->is_open)
    {
        CLOSE_SOCKET(udp->sock);
        udp->is_open = false;
        udp->is_bound = false;
    }
}

//=============================================================================
// DNS Resolution - Static Utility Functions
//=============================================================================

/// @brief Check if a string is a valid IPv4 address (without DNS lookup).
/// @details Parses dotted decimal format: four octets 0-255 separated by dots.
static bool parse_ipv4(const char *addr)
{
    if (!addr || !*addr)
        return false;

    int parts = 0;
    int value = 0;
    bool has_digit = false;

    for (const char *p = addr; *p; p++)
    {
        if (*p >= '0' && *p <= '9')
        {
            value = value * 10 + (*p - '0');
            if (value > 255)
                return false;
            has_digit = true;
        }
        else if (*p == '.')
        {
            if (!has_digit || parts >= 3)
                return false;
            parts++;
            value = 0;
            has_digit = false;
        }
        else
        {
            return false;
        }
    }

    return has_digit && parts == 3;
}

/// @brief Check if a string is a valid IPv6 address (without DNS lookup).
/// @details Parses colon hex format with :: for zero compression.
static bool parse_ipv6(const char *addr)
{
    if (!addr || !*addr)
        return false;

    // Use inet_pton for validation - it's portable and handles all cases
    struct in6_addr result;
    return inet_pton(AF_INET6, addr, &result) == 1;
}

rt_string rt_dns_resolve(rt_string hostname)
{
    init_wsa();

    const char *host_ptr = rt_string_cstr(hostname);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: NULL hostname");

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET; // IPv4 only for Resolve()
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    int ret = getaddrinfo(host_ptr, NULL, &hints, &result);
    if (ret != 0 || !result)
    {
        if (result)
            freeaddrinfo(result);
        rt_trap("Network: hostname not found");
    }

    char ip_str[INET_ADDRSTRLEN];
    struct sockaddr_in *addr = (struct sockaddr_in *)result->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));

    freeaddrinfo(result);
    return rt_string_from_bytes(ip_str, strlen(ip_str));
}

void *rt_dns_resolve_all(rt_string hostname)
{
    init_wsa();

    const char *host_ptr = rt_string_cstr(hostname);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: NULL hostname");

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC; // Both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    int ret = getaddrinfo(host_ptr, NULL, &hints, &result);
    if (ret != 0 || !result)
    {
        if (result)
            freeaddrinfo(result);
        rt_trap("Network: hostname not found");
    }

    void *seq = rt_seq_new();

    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next)
    {
        char ip_str[INET6_ADDRSTRLEN];

        if (rp->ai_family == AF_INET)
        {
            struct sockaddr_in *addr = (struct sockaddr_in *)rp->ai_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
        }
        else if (rp->ai_family == AF_INET6)
        {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *)rp->ai_addr;
            inet_ntop(AF_INET6, &addr->sin6_addr, ip_str, sizeof(ip_str));
        }
        else
        {
            continue;
        }

        rt_string addr_str = rt_string_from_bytes(ip_str, strlen(ip_str));
        rt_seq_push(seq, (void *)addr_str);
    }

    freeaddrinfo(result);
    return seq;
}

rt_string rt_dns_resolve4(rt_string hostname)
{
    init_wsa();

    const char *host_ptr = rt_string_cstr(hostname);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: NULL hostname");

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET; // IPv4 only
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    int ret = getaddrinfo(host_ptr, NULL, &hints, &result);
    if (ret != 0 || !result)
    {
        if (result)
            freeaddrinfo(result);
        rt_trap("Network: no IPv4 address found");
    }

    char ip_str[INET_ADDRSTRLEN];
    struct sockaddr_in *addr = (struct sockaddr_in *)result->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));

    freeaddrinfo(result);
    return rt_string_from_bytes(ip_str, strlen(ip_str));
}

rt_string rt_dns_resolve6(rt_string hostname)
{
    init_wsa();

    const char *host_ptr = rt_string_cstr(hostname);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: NULL hostname");

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET6; // IPv6 only
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    int ret = getaddrinfo(host_ptr, NULL, &hints, &result);
    if (ret != 0 || !result)
    {
        if (result)
            freeaddrinfo(result);
        rt_trap("Network: no IPv6 address found");
    }

    char ip_str[INET6_ADDRSTRLEN];
    struct sockaddr_in6 *addr = (struct sockaddr_in6 *)result->ai_addr;
    inet_ntop(AF_INET6, &addr->sin6_addr, ip_str, sizeof(ip_str));

    freeaddrinfo(result);
    return rt_string_from_bytes(ip_str, strlen(ip_str));
}

rt_string rt_dns_reverse(rt_string ip_address)
{
    init_wsa();

    const char *addr_ptr = rt_string_cstr(ip_address);
    if (!addr_ptr || *addr_ptr == '\0')
        rt_trap("Network: NULL address");

    // Try IPv4 first
    struct sockaddr_in sa4;
    struct sockaddr_in6 sa6;
    struct sockaddr *sa;
    socklen_t sa_len;

    if (inet_pton(AF_INET, addr_ptr, &sa4.sin_addr) == 1)
    {
        sa4.sin_family = AF_INET;
        sa4.sin_port = 0;
        sa = (struct sockaddr *)&sa4;
        sa_len = sizeof(sa4);
    }
    else if (inet_pton(AF_INET6, addr_ptr, &sa6.sin6_addr) == 1)
    {
        sa6.sin6_family = AF_INET6;
        sa6.sin6_port = 0;
        sa6.sin6_flowinfo = 0;
        sa6.sin6_scope_id = 0;
        sa = (struct sockaddr *)&sa6;
        sa_len = sizeof(sa6);
    }
    else
    {
        rt_trap("Network: invalid IP address");
    }

    char host[NI_MAXHOST];
    int ret = getnameinfo(sa, sa_len, host, sizeof(host), NULL, 0, NI_NAMEREQD);
    if (ret != 0)
    {
        rt_trap("Network: reverse lookup failed");
    }

    return rt_string_from_bytes(host, strlen(host));
}

int8_t rt_dns_is_ipv4(rt_string address)
{
    const char *addr_ptr = rt_string_cstr(address);
    if (!addr_ptr || *addr_ptr == '\0')
        return 0;

    return parse_ipv4(addr_ptr) ? 1 : 0;
}

int8_t rt_dns_is_ipv6(rt_string address)
{
    const char *addr_ptr = rt_string_cstr(address);
    if (!addr_ptr || *addr_ptr == '\0')
        return 0;

    return parse_ipv6(addr_ptr) ? 1 : 0;
}

int8_t rt_dns_is_ip(rt_string address)
{
    return rt_dns_is_ipv4(address) || rt_dns_is_ipv6(address);
}

rt_string rt_dns_local_host(void)
{
    init_wsa();

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        rt_trap("Network: failed to get hostname");
    }

    return rt_string_from_bytes(hostname, strlen(hostname));
}

void *rt_dns_local_addrs(void)
{
    init_wsa();

    void *seq = rt_seq_new();

#ifdef _WIN32
    // Windows: use gethostname + getaddrinfo for local addresses
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
        return seq;

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    if (getaddrinfo(hostname, NULL, &hints, &result) != 0)
        return seq;

    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next)
    {
        char ip_str[INET6_ADDRSTRLEN];

        if (rp->ai_family == AF_INET)
        {
            struct sockaddr_in *addr = (struct sockaddr_in *)rp->ai_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
        }
        else if (rp->ai_family == AF_INET6)
        {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *)rp->ai_addr;
            inet_ntop(AF_INET6, &addr->sin6_addr, ip_str, sizeof(ip_str));
        }
        else
        {
            continue;
        }

        rt_string addr_str = rt_string_from_bytes(ip_str, strlen(ip_str));
        rt_seq_push(seq, (void *)addr_str);
    }

    freeaddrinfo(result);
#else
    // Unix: use getifaddrs for local addresses
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1)
        return seq;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        char ip_str[INET6_ADDRSTRLEN];
        int family = ifa->ifa_addr->sa_family;

        if (family == AF_INET)
        {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
        }
        else if (family == AF_INET6)
        {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ifa->ifa_addr;
            inet_ntop(AF_INET6, &addr->sin6_addr, ip_str, sizeof(ip_str));
        }
        else
        {
            continue;
        }

        rt_string addr_str = rt_string_from_bytes(ip_str, strlen(ip_str));
        rt_seq_push(seq, (void *)addr_str);
    }

    freeifaddrs(ifaddr);
#endif

    return seq;
}

//=============================================================================
// HTTP Client Implementation
//=============================================================================

/// @brief Maximum number of redirects to follow.
#define HTTP_MAX_REDIRECTS 5

/// @brief Default timeout for HTTP requests (30 seconds).
#define HTTP_DEFAULT_TIMEOUT_MS 30000

/// @brief Initial buffer size for reading responses.
#define HTTP_BUFFER_SIZE 4096

/// @brief Parsed URL structure.
typedef struct parsed_url
{
    char *host;     // Allocated hostname
    int port;       // Port number (default 80)
    char *path;     // Path including query string (allocated)
} parsed_url_t;

/// @brief HTTP header entry.
typedef struct http_header
{
    char *name;
    char *value;
    struct http_header *next;
} http_header_t;

/// @brief HTTP request structure.
typedef struct rt_http_req
{
    char *method;           // HTTP method
    parsed_url_t url;       // Parsed URL
    http_header_t *headers; // Linked list of headers
    uint8_t *body;          // Request body
    size_t body_len;        // Body length
    int timeout_ms;         // Timeout in milliseconds
} rt_http_req_t;

/// @brief HTTP response structure.
typedef struct rt_http_res
{
    int status;             // HTTP status code
    char *status_text;      // Status text (allocated)
    void *headers;          // Map of headers
    uint8_t *body;          // Response body
    size_t body_len;        // Body length
} rt_http_res_t;

/// @brief Free parsed URL.
static void free_parsed_url(parsed_url_t *url)
{
    if (url->host)
        free(url->host);
    if (url->path)
        free(url->path);
    url->host = NULL;
    url->path = NULL;
}

/// @brief Parse URL into components.
/// @return 0 on success, -1 on error.
static int parse_url(const char *url_str, parsed_url_t *result)
{
    memset(result, 0, sizeof(*result));
    result->port = 80;

    // Check for http:// prefix
    if (strncmp(url_str, "http://", 7) == 0)
    {
        url_str += 7;
    }
    else if (strncmp(url_str, "https://", 8) == 0)
    {
        // HTTPS not supported
        return -1;
    }

    // Find end of host (either ':', '/', or end of string)
    const char *host_end = url_str;
    while (*host_end && *host_end != ':' && *host_end != '/')
        host_end++;

    size_t host_len = host_end - url_str;
    if (host_len == 0)
        return -1;

    result->host = (char *)malloc(host_len + 1);
    if (!result->host)
        return -1;
    memcpy(result->host, url_str, host_len);
    result->host[host_len] = '\0';

    const char *p = host_end;

    // Parse port if present
    if (*p == ':')
    {
        p++;
        result->port = 0;
        while (*p >= '0' && *p <= '9')
        {
            result->port = result->port * 10 + (*p - '0');
            p++;
        }
        if (result->port <= 0 || result->port > 65535)
        {
            free_parsed_url(result);
            return -1;
        }
    }

    // Parse path (default to "/")
    if (*p == '/')
    {
        size_t path_len = strlen(p);
        result->path = (char *)malloc(path_len + 1);
        if (!result->path)
        {
            free_parsed_url(result);
            return -1;
        }
        memcpy(result->path, p, path_len + 1);
    }
    else
    {
        result->path = (char *)malloc(2);
        if (!result->path)
        {
            free_parsed_url(result);
            return -1;
        }
        result->path[0] = '/';
        result->path[1] = '\0';
    }

    return 0;
}

/// @brief Free header list.
static void free_headers(http_header_t *headers)
{
    while (headers)
    {
        http_header_t *next = headers->next;
        free(headers->name);
        free(headers->value);
        free(headers);
        headers = next;
    }
}

/// @brief Add header to request.
static void add_header(rt_http_req_t *req, const char *name, const char *value)
{
    http_header_t *h = (http_header_t *)malloc(sizeof(http_header_t));
    if (!h)
        return;
    h->name = strdup(name);
    h->value = strdup(value);
    h->next = req->headers;
    req->headers = h;
}

/// @brief Check if header exists (case-insensitive).
static bool has_header(rt_http_req_t *req, const char *name)
{
    for (http_header_t *h = req->headers; h; h = h->next)
    {
        if (strcasecmp(h->name, name) == 0)
            return true;
    }
    return false;
}

/// @brief Build HTTP request string.
/// @return Allocated string, caller must free.
static char *build_request(rt_http_req_t *req)
{
    // Calculate total size
    size_t size = strlen(req->method) + 1 + strlen(req->url.path) + 11; // "METHOD PATH HTTP/1.1\r\n"

    // Host header
    char host_header[300];
    if (req->url.port != 80)
        snprintf(host_header, sizeof(host_header), "Host: %s:%d\r\n", req->url.host, req->url.port);
    else
        snprintf(host_header, sizeof(host_header), "Host: %s\r\n", req->url.host);
    size += strlen(host_header);

    // Content-Length if body
    char content_len_header[64] = "";
    if (req->body && req->body_len > 0)
    {
        snprintf(content_len_header, sizeof(content_len_header), "Content-Length: %zu\r\n", req->body_len);
        size += strlen(content_len_header);
    }

    // Connection header
    size += 19; // "Connection: close\r\n"

    // User headers
    for (http_header_t *h = req->headers; h; h = h->next)
    {
        size += strlen(h->name) + 2 + strlen(h->value) + 2; // "Name: Value\r\n"
    }

    size += 2; // Final CRLF
    size += req->body_len;
    size += 1; // Null terminator

    char *request = (char *)malloc(size);
    if (!request)
        return NULL;

    char *p = request;
    p += sprintf(p, "%s %s HTTP/1.1\r\n", req->method, req->url.path);
    p += sprintf(p, "%s", host_header);

    if (content_len_header[0])
        p += sprintf(p, "%s", content_len_header);

    p += sprintf(p, "Connection: close\r\n");

    // User headers
    for (http_header_t *h = req->headers; h; h = h->next)
    {
        p += sprintf(p, "%s: %s\r\n", h->name, h->value);
    }

    p += sprintf(p, "\r\n");

    // Body
    if (req->body && req->body_len > 0)
    {
        memcpy(p, req->body, req->body_len);
        p += req->body_len;
    }

    *p = '\0';
    return request;
}

/// @brief Read a line from socket (up to CRLF).
/// @return Allocated line without CRLF, or NULL on error.
static char *read_line(void *tcp)
{
    char *line = NULL;
    size_t len = 0;
    size_t cap = 256;
    line = (char *)malloc(cap);
    if (!line)
        return NULL;

    while (1)
    {
        void *data = rt_tcp_recv(tcp, 1);
        if (rt_bytes_len(data) == 0)
        {
            // Connection closed
            if (len == 0)
            {
                free(line);
                return NULL;
            }
            break;
        }

        uint8_t *byte_ptr = bytes_data(data);
        char c = (char)byte_ptr[0];

        if (c == '\n')
        {
            // Remove trailing CR if present
            if (len > 0 && line[len - 1] == '\r')
                len--;
            break;
        }

        if (len + 1 >= cap)
        {
            cap *= 2;
            char *new_line = (char *)realloc(line, cap);
            if (!new_line)
            {
                free(line);
                return NULL;
            }
            line = new_line;
        }
        line[len++] = c;
    }

    line[len] = '\0';
    return line;
}

/// @brief Parse HTTP response status line.
/// @return Status code, or -1 on error.
static int parse_status_line(const char *line, char **status_text_out)
{
    // Format: HTTP/1.x STATUS_CODE STATUS_TEXT
    if (strncmp(line, "HTTP/1.", 7) != 0)
        return -1;

    const char *p = line + 7;
    // Skip version digit
    if (*p != '0' && *p != '1')
        return -1;
    p++;

    // Skip space
    if (*p != ' ')
        return -1;
    p++;

    // Parse status code
    int status = 0;
    while (*p >= '0' && *p <= '9')
    {
        status = status * 10 + (*p - '0');
        p++;
    }

    if (status < 100 || status > 599)
        return -1;

    // Skip space and get status text
    if (*p == ' ')
        p++;

    if (status_text_out)
        *status_text_out = strdup(p);

    return status;
}

/// @brief Parse header line into name and value.
static void parse_header_line(const char *line, void *headers_map)
{
    const char *colon = strchr(line, ':');
    if (!colon)
        return;

    size_t name_len = colon - line;
    char *name = (char *)malloc(name_len + 1);
    if (!name)
        return;
    memcpy(name, line, name_len);
    name[name_len] = '\0';

    // Skip colon and whitespace
    const char *value = colon + 1;
    while (*value == ' ' || *value == '\t')
        value++;

    // Convert name to lowercase for case-insensitive lookup
    for (char *p = name; *p; p++)
    {
        if (*p >= 'A' && *p <= 'Z')
            *p = *p + ('a' - 'A');
    }

    rt_string name_str = rt_string_from_bytes(name, strlen(name));
    rt_string value_str = rt_string_from_bytes(value, strlen(value));
    rt_map_set(headers_map, name_str, (void *)value_str);
    free(name);
}

/// @brief Read response body with Content-Length.
static uint8_t *read_body_content_length(void *tcp, size_t content_length, size_t *out_len)
{
    uint8_t *body = (uint8_t *)malloc(content_length);
    if (!body)
        return NULL;

    size_t total_read = 0;
    while (total_read < content_length)
    {
        size_t remaining = content_length - total_read;
        size_t chunk_size = remaining < HTTP_BUFFER_SIZE ? remaining : HTTP_BUFFER_SIZE;

        void *data = rt_tcp_recv(tcp, (int64_t)chunk_size);
        int64_t len = rt_bytes_len(data);
        if (len == 0)
            break;

        uint8_t *data_ptr = bytes_data(data);
        memcpy(body + total_read, data_ptr, len);
        total_read += len;
    }

    *out_len = total_read;
    return body;
}

/// @brief Read chunked transfer encoding body.
static uint8_t *read_body_chunked(void *tcp, size_t *out_len)
{
    size_t body_cap = HTTP_BUFFER_SIZE;
    size_t body_len = 0;
    uint8_t *body = (uint8_t *)malloc(body_cap);
    if (!body)
        return NULL;

    while (1)
    {
        // Read chunk size line
        char *size_line = read_line(tcp);
        if (!size_line)
            break;

        // Parse hex chunk size
        size_t chunk_size = 0;
        for (char *p = size_line; *p; p++)
        {
            char c = *p;
            if (c >= '0' && c <= '9')
                chunk_size = chunk_size * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f')
                chunk_size = chunk_size * 16 + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                chunk_size = chunk_size * 16 + (c - 'A' + 10);
            else
                break;
        }
        free(size_line);

        if (chunk_size == 0)
        {
            // Last chunk - read trailing CRLF
            char *trailer = read_line(tcp);
            free(trailer);
            break;
        }

        // Expand body buffer if needed
        while (body_len + chunk_size > body_cap)
        {
            body_cap *= 2;
            uint8_t *new_body = (uint8_t *)realloc(body, body_cap);
            if (!new_body)
            {
                free(body);
                return NULL;
            }
            body = new_body;
        }

        // Read chunk data
        size_t read = 0;
        while (read < chunk_size)
        {
            size_t remaining = chunk_size - read;
            size_t to_read = remaining < HTTP_BUFFER_SIZE ? remaining : HTTP_BUFFER_SIZE;

            void *data = rt_tcp_recv(tcp, (int64_t)to_read);
            int64_t len = rt_bytes_len(data);
            if (len == 0)
            {
                *out_len = body_len;
                return body;
            }

            uint8_t *data_ptr = bytes_data(data);
            memcpy(body + body_len, data_ptr, len);
            body_len += len;
            read += len;
        }

        // Read trailing CRLF after chunk
        char *chunk_end = read_line(tcp);
        free(chunk_end);
    }

    *out_len = body_len;
    return body;
}

/// @brief Read response body until connection closes.
static uint8_t *read_body_until_close(void *tcp, size_t *out_len)
{
    size_t body_cap = HTTP_BUFFER_SIZE;
    size_t body_len = 0;
    uint8_t *body = (uint8_t *)malloc(body_cap);
    if (!body)
        return NULL;

    while (1)
    {
        void *data = rt_tcp_recv(tcp, HTTP_BUFFER_SIZE);
        int64_t len = rt_bytes_len(data);
        if (len == 0)
            break;

        // Expand buffer if needed
        while (body_len + len > body_cap)
        {
            body_cap *= 2;
            uint8_t *new_body = (uint8_t *)realloc(body, body_cap);
            if (!new_body)
            {
                free(body);
                return NULL;
            }
            body = new_body;
        }

        uint8_t *data_ptr = bytes_data(data);
        memcpy(body + body_len, data_ptr, len);
        body_len += len;
    }

    *out_len = body_len;
    return body;
}

/// @brief Perform HTTP request and return response.
static rt_http_res_t *do_http_request(rt_http_req_t *req, int redirects_remaining)
{
    init_wsa();

    if (redirects_remaining <= 0)
    {
        rt_trap("HTTP: too many redirects");
        return NULL;
    }

    // Connect to server
    rt_string host = rt_string_from_bytes(req->url.host, strlen(req->url.host));
    void *tcp = req->timeout_ms > 0
                    ? rt_tcp_connect_for(host, req->url.port, req->timeout_ms)
                    : rt_tcp_connect(host, req->url.port);

    if (!tcp || !rt_tcp_is_open(tcp))
    {
        rt_trap("HTTP: connection failed");
        return NULL;
    }

    // Set socket timeout
    if (req->timeout_ms > 0)
    {
        rt_tcp_set_recv_timeout(tcp, req->timeout_ms);
        rt_tcp_set_send_timeout(tcp, req->timeout_ms);
    }

    // Build and send request
    char *request_str = build_request(req);
    if (!request_str)
    {
        rt_tcp_close(tcp);
        rt_trap("HTTP: failed to build request");
        return NULL;
    }

    size_t request_len = strlen(request_str) + (req->body ? req->body_len : 0);
    void *request_bytes = rt_bytes_new((int64_t)request_len);
    uint8_t *request_ptr = bytes_data(request_bytes);

    size_t header_len = strlen(request_str);
    memcpy(request_ptr, request_str, header_len);
    if (req->body && req->body_len > 0)
        memcpy(request_ptr + header_len, req->body, req->body_len);

    free(request_str);
    rt_tcp_send_all(tcp, request_bytes);

    // Read status line
    char *status_line = read_line(tcp);
    if (!status_line)
    {
        rt_tcp_close(tcp);
        rt_trap("HTTP: invalid response");
        return NULL;
    }

    char *status_text = NULL;
    int status = parse_status_line(status_line, &status_text);
    free(status_line);

    if (status < 0)
    {
        rt_tcp_close(tcp);
        rt_trap("HTTP: invalid status line");
        return NULL;
    }

    // Read headers
    void *headers_map = rt_map_new();
    char *redirect_location = NULL;

    while (1)
    {
        char *line = read_line(tcp);
        if (!line || line[0] == '\0')
        {
            free(line);
            break;
        }

        // Check for Location header (for redirects)
        if (strncasecmp(line, "location:", 9) == 0)
        {
            const char *loc = line + 9;
            while (*loc == ' ')
                loc++;
            redirect_location = strdup(loc);
        }

        parse_header_line(line, headers_map);
        free(line);
    }

    // Handle redirects (3xx with Location)
    if ((status == 301 || status == 302 || status == 307 || status == 308) && redirect_location)
    {
        rt_tcp_close(tcp);
        free(status_text);

        // Parse new URL
        parsed_url_t new_url;
        if (parse_url(redirect_location, &new_url) < 0)
        {
            // Relative URL - use same host
            if (redirect_location[0] == '/')
            {
                free(req->url.path);
                req->url.path = redirect_location;
            }
            else
            {
                free(redirect_location);
                rt_trap("HTTP: invalid redirect URL");
                return NULL;
            }
        }
        else
        {
            free_parsed_url(&req->url);
            req->url = new_url;
            free(redirect_location);
        }

        // Follow redirect
        return do_http_request(req, redirects_remaining - 1);
    }
    free(redirect_location);

    // Determine how to read body
    size_t body_len = 0;
    uint8_t *body = NULL;

    // Check for Content-Length
    rt_string content_length_key = rt_string_from_bytes("content-length", 14);
    rt_string content_length_val = (rt_string)rt_map_get(headers_map, content_length_key);

    // Check for Transfer-Encoding: chunked
    rt_string transfer_encoding_key = rt_string_from_bytes("transfer-encoding", 17);
    rt_string transfer_encoding_val = (rt_string)rt_map_get(headers_map, transfer_encoding_key);

    bool is_head = strcmp(req->method, "HEAD") == 0;

    if (is_head)
    {
        // HEAD requests have no body
        body = NULL;
        body_len = 0;
    }
    else if (transfer_encoding_val && strstr(rt_string_cstr(transfer_encoding_val), "chunked"))
    {
        body = read_body_chunked(tcp, &body_len);
    }
    else if (content_length_val)
    {
        size_t content_len = (size_t)atoll(rt_string_cstr(content_length_val));
        body = read_body_content_length(tcp, content_len, &body_len);
    }
    else
    {
        // Read until connection closes
        body = read_body_until_close(tcp, &body_len);
    }

    rt_tcp_close(tcp);

    // Create response object (must use rt_obj_new_i64 for GC management)
    rt_http_res_t *res = (rt_http_res_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_res_t));
    if (!res)
    {
        free(body);
        free(status_text);
        rt_trap("HTTP: memory allocation failed");
        return NULL;
    }

    res->status = status;
    res->status_text = status_text;
    res->headers = headers_map;
    res->body = body;
    res->body_len = body_len;

    return res;
}

//=============================================================================
// Http Static Class Implementation
//=============================================================================

rt_string rt_http_get(rt_string url)
{
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    // Create request
    rt_http_req_t req = {0};
    req.method = "GET";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap("HTTP: invalid URL format");

    // Execute request
    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap("HTTP: request failed");

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    // Note: res is GC-managed (allocated with rt_obj_new_i64), don't free it
    // But body and status_text are malloc'd, so free them
    free(res->body);
    free(res->status_text);
    res->body = NULL;
    res->status_text = NULL;

    return result;
}

void *rt_http_get_bytes(rt_string url)
{
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    rt_http_req_t req = {0};
    req.method = "GET";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap("HTTP: invalid URL format");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap("HTTP: request failed");

    void *result = rt_bytes_new((int64_t)res->body_len);
    uint8_t *result_ptr = bytes_data(result);
    if (res->body && res->body_len > 0)
        memcpy(result_ptr, res->body, res->body_len);

    free(res->body);
    free(res->status_text);
    // res is GC-managed, don't free

    return result;
}

rt_string rt_http_post(rt_string url, rt_string body)
{
    const char *url_str = rt_string_cstr(url);
    const char *body_str = rt_string_cstr(body);

    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    rt_http_req_t req = {0};
    req.method = "POST";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap("HTTP: invalid URL format");

    if (body_str)
    {
        req.body = (uint8_t *)body_str;
        req.body_len = strlen(body_str);
    }

    // Add Content-Type if not empty body
    if (req.body_len > 0)
        add_header(&req, "Content-Type", "text/plain; charset=utf-8");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res)
        rt_trap("HTTP: request failed");

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    free(res->body);
    free(res->status_text);
    // res is GC-managed, don't free

    return result;
}

void *rt_http_post_bytes(rt_string url, void *body)
{
    const char *url_str = rt_string_cstr(url);

    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    rt_http_req_t req = {0};
    req.method = "POST";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap("HTTP: invalid URL format");

    if (body)
    {
        int64_t body_len = rt_bytes_len(body);
        uint8_t *body_ptr = bytes_data(body);
        req.body = body_ptr;
        req.body_len = (size_t)body_len;
    }

    if (req.body_len > 0)
        add_header(&req, "Content-Type", "application/octet-stream");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res)
        rt_trap("HTTP: request failed");

    void *result = rt_bytes_new((int64_t)res->body_len);
    uint8_t *result_ptr = bytes_data(result);
    if (res->body && res->body_len > 0)
        memcpy(result_ptr, res->body, res->body_len);

    free(res->body);
    free(res->status_text);
    // res is GC-managed, don't free

    return result;
}

int8_t rt_http_download(rt_string url, rt_string dest_path)
{
    const char *url_str = rt_string_cstr(url);
    const char *path_str = rt_string_cstr(dest_path);

    if (!url_str || *url_str == '\0')
        return 0;
    if (!path_str || *path_str == '\0')
        return 0;

    rt_http_req_t req = {0};
    req.method = "GET";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        return 0;

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);

    if (!res)
        return 0;

    if (res->status < 200 || res->status >= 300)
    {
        free(res->body);
        free(res->status_text);
        // res is GC-managed, don't free
        return 0;
    }

    // Write to file
    FILE *f = fopen(path_str, "wb");
    if (!f)
    {
        free(res->body);
        free(res->status_text);
        // res is GC-managed, don't free
        return 0;
    }

    size_t written = fwrite(res->body, 1, res->body_len, f);
    fclose(f);

    free(res->body);
    free(res->status_text);
    // res is GC-managed, don't free

    return written == res->body_len ? 1 : 0;
}

void *rt_http_head(rt_string url)
{
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    rt_http_req_t req = {0};
    req.method = "HEAD";
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req.url) < 0)
        rt_trap("HTTP: invalid URL format");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free_parsed_url(&req.url);

    if (!res)
        rt_trap("HTTP: request failed");

    void *headers = res->headers;
    free(res->body);
    free(res->status_text);
    // res is GC-managed, don't free

    return headers;
}

//=============================================================================
// HttpReq Instance Class Implementation
//=============================================================================

void *rt_http_req_new(rt_string method, rt_string url)
{
    const char *method_str = rt_string_cstr(method);
    const char *url_str = rt_string_cstr(url);

    if (!method_str || *method_str == '\0')
        rt_trap("HTTP: invalid method");
    if (!url_str || *url_str == '\0')
        rt_trap("HTTP: invalid URL");

    // Must use rt_obj_new_i64 for GC management
    rt_http_req_t *req = (rt_http_req_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_req_t));
    if (!req)
        rt_trap("HTTP: memory allocation failed");

    memset(req, 0, sizeof(*req));
    req->method = strdup(method_str);
    req->timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;

    if (parse_url(url_str, &req->url) < 0)
    {
        free(req->method);
        // Note: GC-managed object, so we don't free it directly
        rt_trap("HTTP: invalid URL format");
    }

    return req;
}

void *rt_http_req_set_header(void *obj, rt_string name, rt_string value)
{
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    const char *name_str = rt_string_cstr(name);
    const char *value_str = rt_string_cstr(value);

    if (name_str && value_str)
        add_header(req, name_str, value_str);

    return obj;
}

void *rt_http_req_set_body(void *obj, void *data)
{
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;

    if (data)
    {
        int64_t len = rt_bytes_len(data);
        uint8_t *ptr = bytes_data(data);

        // Make a copy of the body
        req->body = (uint8_t *)malloc(len);
        if (req->body)
        {
            memcpy(req->body, ptr, len);
            req->body_len = len;
        }
    }

    return obj;
}

void *rt_http_req_set_body_str(void *obj, rt_string text)
{
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    const char *text_str = rt_string_cstr(text);

    if (text_str)
    {
        size_t len = strlen(text_str);
        req->body = (uint8_t *)malloc(len);
        if (req->body)
        {
            memcpy(req->body, text_str, len);
            req->body_len = len;
        }
    }

    return obj;
}

void *rt_http_req_set_timeout(void *obj, int64_t timeout_ms)
{
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->timeout_ms = (int)timeout_ms;

    return obj;
}

void *rt_http_req_send(void *obj)
{
    if (!obj)
        rt_trap("HTTP: NULL request");

    rt_http_req_t *req = (rt_http_req_t *)obj;

    // Add Content-Type for POST with body if not set
    if (req->body && req->body_len > 0 && !has_header(req, "Content-Type"))
    {
        add_header(req, "Content-Type", "application/octet-stream");
    }

    rt_http_res_t *res = do_http_request(req, HTTP_MAX_REDIRECTS);

    // Cleanup request
    free(req->method);
    free_parsed_url(&req->url);
    free_headers(req->headers);
    free(req->body);
    free(req);

    return res;
}

//=============================================================================
// HttpRes Instance Class Implementation
//=============================================================================

int64_t rt_http_res_status(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_http_res_t *)obj)->status;
}

rt_string rt_http_res_status_text(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    if (!res->status_text)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(res->status_text, strlen(res->status_text));
}

void *rt_http_res_headers(void *obj)
{
    if (!obj)
        return rt_map_new();
    return ((rt_http_res_t *)obj)->headers;
}

void *rt_http_res_body(void *obj)
{
    if (!obj)
        return rt_bytes_new(0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    void *bytes = rt_bytes_new((int64_t)res->body_len);
    if (res->body && res->body_len > 0)
    {
        uint8_t *ptr = bytes_data(bytes);
        memcpy(ptr, res->body, res->body_len);
    }
    return bytes;
}

rt_string rt_http_res_body_str(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    if (!res->body || res->body_len == 0)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes((const char *)res->body, res->body_len);
}

rt_string rt_http_res_header(void *obj, rt_string name)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_http_res_t *res = (rt_http_res_t *)obj;

    // Convert name to lowercase for lookup
    const char *name_str = rt_string_cstr(name);
    if (!name_str)
        return rt_string_from_bytes("", 0);

    size_t len = strlen(name_str);
    char *lower_name = (char *)malloc(len + 1);
    if (!lower_name)
        return rt_string_from_bytes("", 0);

    for (size_t i = 0; i <= len; i++)
    {
        char c = name_str[i];
        if (c >= 'A' && c <= 'Z')
            lower_name[i] = c + ('a' - 'A');
        else
            lower_name[i] = c;
    }

    rt_string lower_key = rt_string_from_bytes(lower_name, len);
    free(lower_name);

    rt_string value = (rt_string)rt_map_get(res->headers, lower_key);
    if (!value)
        return rt_string_from_bytes("", 0);

    return value;
}

int8_t rt_http_res_is_ok(void *obj)
{
    if (!obj)
        return 0;

    int status = ((rt_http_res_t *)obj)->status;
    return (status >= 200 && status < 300) ? 1 : 0;
}

//=============================================================================
// URL Parsing and Construction Implementation
//=============================================================================

/// @brief URL structure.
typedef struct rt_url
{
    char *scheme;   // URL scheme (e.g., "http", "https")
    char *user;     // Username (optional)
    char *pass;     // Password (optional)
    char *host;     // Hostname
    int64_t port;   // Port number (0 = not specified)
    char *path;     // Path component
    char *query;    // Query string (without leading ?)
    char *fragment; // Fragment (without leading #)
} rt_url_t;

/// @brief Get default port for a scheme.
/// @return Default port or 0 if unknown.
static int64_t default_port_for_scheme(const char *scheme)
{
    if (!scheme)
        return 0;
    if (strcmp(scheme, "http") == 0)
        return 80;
    if (strcmp(scheme, "https") == 0)
        return 443;
    if (strcmp(scheme, "ftp") == 0)
        return 21;
    if (strcmp(scheme, "ssh") == 0)
        return 22;
    if (strcmp(scheme, "telnet") == 0)
        return 23;
    if (strcmp(scheme, "smtp") == 0)
        return 25;
    if (strcmp(scheme, "dns") == 0)
        return 53;
    if (strcmp(scheme, "pop3") == 0)
        return 110;
    if (strcmp(scheme, "imap") == 0)
        return 143;
    if (strcmp(scheme, "ldap") == 0)
        return 389;
    if (strcmp(scheme, "ws") == 0)
        return 80;
    if (strcmp(scheme, "wss") == 0)
        return 443;
    return 0;
}

/// @brief Check if character is unreserved (RFC 3986).
static bool is_unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

/// @brief Convert hex character to value.
/// @return -1 if invalid.
static int hex_char_to_int(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/// @brief Percent-encode a string.
/// @return Allocated string, caller must free.
static char *percent_encode(const char *str, bool encode_slash)
{
    if (!str)
        return strdup("");

    size_t len = strlen(str);
    // Worst case: every char becomes %XX
    char *result = (char *)malloc(len * 3 + 1);
    if (!result)
        return NULL;

    char *p = result;
    for (size_t i = 0; i < len; i++)
    {
        char c = str[i];
        if (is_unreserved(c) || (!encode_slash && c == '/'))
        {
            *p++ = c;
        }
        else
        {
            *p++ = '%';
            *p++ = "0123456789ABCDEF"[(unsigned char)c >> 4];
            *p++ = "0123456789ABCDEF"[(unsigned char)c & 0x0F];
        }
    }
    *p = '\0';
    return result;
}

/// @brief Percent-decode a string.
/// @return Allocated string, caller must free.
static char *percent_decode(const char *str)
{
    if (!str)
        return strdup("");

    size_t len = strlen(str);
    char *result = (char *)malloc(len + 1);
    if (!result)
        return NULL;

    char *p = result;
    for (size_t i = 0; i < len; i++)
    {
        if (str[i] == '%' && i + 2 < len)
        {
            int high = hex_char_to_int(str[i + 1]);
            int low = hex_char_to_int(str[i + 2]);
            if (high >= 0 && low >= 0)
            {
                *p++ = (char)((high << 4) | low);
                i += 2;
                continue;
            }
        }
        else if (str[i] == '+')
        {
            // Plus is space in query strings
            *p++ = ' ';
            continue;
        }
        *p++ = str[i];
    }
    *p = '\0';
    return result;
}

/// @brief Duplicate a string safely (handles NULL).
static char *safe_strdup(const char *str)
{
    return str ? strdup(str) : NULL;
}

/// @brief Internal URL parsing.
/// @return 0 on success, -1 on error.
static int parse_url_full(const char *url_str, rt_url_t *result)
{
    memset(result, 0, sizeof(*result));

    if (!url_str || *url_str == '\0')
        return -1;

    const char *p = url_str;

    // Parse scheme (if present)
    const char *scheme_end = strstr(p, "://");
    bool has_authority = false;
    if (scheme_end)
    {
        size_t scheme_len = scheme_end - p;
        result->scheme = (char *)malloc(scheme_len + 1);
        if (!result->scheme)
            return -1;
        memcpy(result->scheme, p, scheme_len);
        result->scheme[scheme_len] = '\0';

        // Convert scheme to lowercase
        for (char *s = result->scheme; *s; s++)
        {
            if (*s >= 'A' && *s <= 'Z')
                *s = *s + ('a' - 'A');
        }

        p = scheme_end + 3; // Skip "://"
        has_authority = true;
    }
    else if (p[0] == '/' && p[1] == '/')
    {
        // Network-path reference (starts with //)
        p += 2;
        has_authority = true;
    }

    // Parse authority (userinfo@host:port) - only if we have a scheme or //
    if (has_authority && *p && *p != '/' && *p != '?' && *p != '#')
    {
        // Find end of authority
        const char *auth_end = p;
        while (*auth_end && *auth_end != '/' && *auth_end != '?' && *auth_end != '#')
            auth_end++;

        // Check for userinfo (@)
        const char *at_sign = NULL;
        for (const char *s = p; s < auth_end; s++)
        {
            if (*s == '@')
            {
                at_sign = s;
                break;
            }
        }

        const char *host_start = p;
        if (at_sign)
        {
            // Parse userinfo
            const char *colon = NULL;
            for (const char *s = p; s < at_sign; s++)
            {
                if (*s == ':')
                {
                    colon = s;
                    break;
                }
            }

            if (colon)
            {
                // user:pass
                size_t user_len = colon - p;
                result->user = (char *)malloc(user_len + 1);
                if (result->user)
                {
                    memcpy(result->user, p, user_len);
                    result->user[user_len] = '\0';
                }

                size_t pass_len = at_sign - colon - 1;
                result->pass = (char *)malloc(pass_len + 1);
                if (result->pass)
                {
                    memcpy(result->pass, colon + 1, pass_len);
                    result->pass[pass_len] = '\0';
                }
            }
            else
            {
                // Just user
                size_t user_len = at_sign - p;
                result->user = (char *)malloc(user_len + 1);
                if (result->user)
                {
                    memcpy(result->user, p, user_len);
                    result->user[user_len] = '\0';
                }
            }
            host_start = at_sign + 1;
        }

        // Parse host:port
        // Check for IPv6 literal [...]
        const char *port_colon = NULL;
        if (*host_start == '[')
        {
            // IPv6 literal
            const char *bracket_end = strchr(host_start, ']');
            if (bracket_end && bracket_end < auth_end)
            {
                size_t host_len = bracket_end - host_start + 1;
                result->host = (char *)malloc(host_len + 1);
                if (result->host)
                {
                    memcpy(result->host, host_start, host_len);
                    result->host[host_len] = '\0';
                }
                if (bracket_end + 1 < auth_end && *(bracket_end + 1) == ':')
                    port_colon = bracket_end + 1;
            }
        }
        else
        {
            // Regular host
            for (const char *s = host_start; s < auth_end; s++)
            {
                if (*s == ':')
                {
                    port_colon = s;
                    break;
                }
            }

            const char *host_end = port_colon ? port_colon : auth_end;
            size_t host_len = host_end - host_start;
            result->host = (char *)malloc(host_len + 1);
            if (result->host)
            {
                memcpy(result->host, host_start, host_len);
                result->host[host_len] = '\0';
            }
        }

        // Parse port
        if (port_colon && port_colon + 1 < auth_end)
        {
            result->port = 0;
            for (const char *s = port_colon + 1; s < auth_end && *s >= '0' && *s <= '9'; s++)
            {
                result->port = result->port * 10 + (*s - '0');
            }
        }

        p = auth_end;
    }

    // Parse path
    const char *path_start = p;
    const char *path_end = p;
    while (*path_end && *path_end != '?' && *path_end != '#')
        path_end++;

    if (path_end > path_start)
    {
        size_t path_len = path_end - path_start;
        result->path = (char *)malloc(path_len + 1);
        if (result->path)
        {
            memcpy(result->path, path_start, path_len);
            result->path[path_len] = '\0';
        }
    }

    p = path_end;

    // Parse query
    if (*p == '?')
    {
        p++;
        const char *query_end = p;
        while (*query_end && *query_end != '#')
            query_end++;

        size_t query_len = query_end - p;
        result->query = (char *)malloc(query_len + 1);
        if (result->query)
        {
            memcpy(result->query, p, query_len);
            result->query[query_len] = '\0';
        }

        p = query_end;
    }

    // Parse fragment
    if (*p == '#')
    {
        p++;
        size_t frag_len = strlen(p);
        result->fragment = (char *)malloc(frag_len + 1);
        if (result->fragment)
        {
            memcpy(result->fragment, p, frag_len);
            result->fragment[frag_len] = '\0';
        }
    }

    return 0;
}

/// @brief Free URL structure contents.
static void free_url(rt_url_t *url)
{
    if (url->scheme)
        free(url->scheme);
    if (url->user)
        free(url->user);
    if (url->pass)
        free(url->pass);
    if (url->host)
        free(url->host);
    if (url->path)
        free(url->path);
    if (url->query)
        free(url->query);
    if (url->fragment)
        free(url->fragment);
    memset(url, 0, sizeof(*url));
}

void *rt_url_parse(rt_string url_str)
{
    const char *str = rt_string_cstr(url_str);
    if (!str)
        rt_trap("URL: Invalid URL string");

    rt_url_t *url = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!url)
        rt_trap("URL: Memory allocation failed");

    memset(url, 0, sizeof(*url));

    if (parse_url_full(str, url) != 0)
    {
        rt_trap("URL: Failed to parse URL");
    }

    return url;
}

void *rt_url_new(void)
{
    rt_url_t *url = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!url)
        rt_trap("URL: Memory allocation failed");

    memset(url, 0, sizeof(*url));
    return url;
}

rt_string rt_url_scheme(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->scheme)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->scheme, strlen(url->scheme));
}

void rt_url_set_scheme(void *obj, rt_string scheme)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->scheme)
        free(url->scheme);

    const char *str = rt_string_cstr(scheme);
    url->scheme = str ? strdup(str) : NULL;

    // Convert to lowercase
    if (url->scheme)
    {
        for (char *p = url->scheme; *p; p++)
        {
            if (*p >= 'A' && *p <= 'Z')
                *p = *p + ('a' - 'A');
        }
    }
}

rt_string rt_url_host(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->host)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->host, strlen(url->host));
}

void rt_url_set_host(void *obj, rt_string host)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->host)
        free(url->host);

    const char *str = rt_string_cstr(host);
    url->host = str ? strdup(str) : NULL;
}

int64_t rt_url_port(void *obj)
{
    if (!obj)
        return 0;

    return ((rt_url_t *)obj)->port;
}

void rt_url_set_port(void *obj, int64_t port)
{
    if (!obj)
        return;

    ((rt_url_t *)obj)->port = port;
}

rt_string rt_url_path(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->path)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->path, strlen(url->path));
}

void rt_url_set_path(void *obj, rt_string path)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->path)
        free(url->path);

    const char *str = rt_string_cstr(path);
    url->path = str ? strdup(str) : NULL;
}

rt_string rt_url_query(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->query, strlen(url->query));
}

void rt_url_set_query(void *obj, rt_string query)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->query)
        free(url->query);

    const char *str = rt_string_cstr(query);
    url->query = str ? strdup(str) : NULL;
}

rt_string rt_url_fragment(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->fragment)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->fragment, strlen(url->fragment));
}

void rt_url_set_fragment(void *obj, rt_string fragment)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->fragment)
        free(url->fragment);

    const char *str = rt_string_cstr(fragment);
    url->fragment = str ? strdup(str) : NULL;
}

rt_string rt_url_user(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->user)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->user, strlen(url->user));
}

void rt_url_set_user(void *obj, rt_string user)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->user)
        free(url->user);

    const char *str = rt_string_cstr(user);
    url->user = str ? strdup(str) : NULL;
}

rt_string rt_url_pass(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->pass)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(url->pass, strlen(url->pass));
}

void rt_url_set_pass(void *obj, rt_string pass)
{
    if (!obj)
        return;

    rt_url_t *url = (rt_url_t *)obj;
    if (url->pass)
        free(url->pass);

    const char *str = rt_string_cstr(pass);
    url->pass = str ? strdup(str) : NULL;
}

rt_string rt_url_authority(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;

    // Calculate size: user:pass@host:port
    size_t size = 0;
    if (url->user)
    {
        size += strlen(url->user);
        if (url->pass)
            size += 1 + strlen(url->pass); // :pass
        size += 1;                          // @
    }
    if (url->host)
        size += strlen(url->host);
    if (url->port > 0)
        size += 8; // :65535

    if (size == 0)
        return rt_string_from_bytes("", 0);

    char *result = (char *)malloc(size + 1);
    if (!result)
        return rt_string_from_bytes("", 0);

    char *p = result;
    if (url->user)
    {
        p += sprintf(p, "%s", url->user);
        if (url->pass)
            p += sprintf(p, ":%s", url->pass);
        *p++ = '@';
    }
    if (url->host)
        p += sprintf(p, "%s", url->host);
    if (url->port > 0)
        p += sprintf(p, ":%lld", (long long)url->port);

    rt_string str = rt_string_from_bytes(result, p - result);
    free(result);
    return str;
}

rt_string rt_url_host_port(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->host)
        return rt_string_from_bytes("", 0);

    // Check if port is default for scheme
    int64_t default_port = default_port_for_scheme(url->scheme);
    bool show_port = url->port > 0 && url->port != default_port;

    size_t size = strlen(url->host) + (show_port ? 8 : 0);
    char *result = (char *)malloc(size + 1);
    if (!result)
        return rt_string_from_bytes("", 0);

    if (show_port)
        sprintf(result, "%s:%lld", url->host, (long long)url->port);
    else
        strcpy(result, url->host);

    rt_string str = rt_string_from_bytes(result, strlen(result));
    free(result);
    return str;
}

rt_string rt_url_full(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;

    // Calculate total size
    size_t size = 0;
    if (url->scheme)
        size += strlen(url->scheme) + 3; // scheme://
    if (url->user)
    {
        size += strlen(url->user);
        if (url->pass)
            size += 1 + strlen(url->pass);
        size += 1; // @
    }
    if (url->host)
        size += strlen(url->host);
    if (url->port > 0)
        size += 8; // :65535
    if (url->path)
        size += strlen(url->path);
    if (url->query)
        size += 1 + strlen(url->query); // ?query
    if (url->fragment)
        size += 1 + strlen(url->fragment); // #fragment

    if (size == 0)
        return rt_string_from_bytes("", 0);

    char *result = (char *)malloc(size + 1);
    if (!result)
        return rt_string_from_bytes("", 0);

    char *p = result;
    if (url->scheme)
        p += sprintf(p, "%s://", url->scheme);
    if (url->user)
    {
        p += sprintf(p, "%s", url->user);
        if (url->pass)
            p += sprintf(p, ":%s", url->pass);
        *p++ = '@';
    }
    if (url->host)
        p += sprintf(p, "%s", url->host);
    if (url->port > 0)
    {
        int64_t default_port = default_port_for_scheme(url->scheme);
        if (url->port != default_port)
            p += sprintf(p, ":%lld", (long long)url->port);
    }
    if (url->path)
        p += sprintf(p, "%s", url->path);
    if (url->query && url->query[0])
        p += sprintf(p, "?%s", url->query);
    if (url->fragment && url->fragment[0])
        p += sprintf(p, "#%s", url->fragment);

    rt_string str = rt_string_from_bytes(result, p - result);
    free(result);
    return str;
}

void *rt_url_set_query_param(void *obj, rt_string name, rt_string value)
{
    if (!obj)
        return obj;

    rt_url_t *url = (rt_url_t *)obj;
    const char *name_str = rt_string_cstr(name);
    const char *value_str = rt_string_cstr(value);

    if (!name_str)
        return obj;

    // Encode name and value
    char *enc_name = percent_encode(name_str, true);
    char *enc_value = value_str ? percent_encode(value_str, true) : strdup("");

    if (!enc_name || !enc_value)
    {
        free(enc_name);
        free(enc_value);
        return obj;
    }

    // Parse existing query into map
    void *map = rt_url_decode_query(rt_string_from_bytes(url->query ? url->query : "", url->query ? strlen(url->query) : 0));

    // Set the new param
    rt_map_set(map, name, (void *)value);

    // Rebuild query string
    rt_string new_query = rt_url_encode_query(map);

    if (url->query)
        free(url->query);
    url->query = strdup(rt_string_cstr(new_query));

    free(enc_name);
    free(enc_value);
    return obj;
}

rt_string rt_url_get_query_param(void *obj, rt_string name)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return rt_string_from_bytes("", 0);

    void *map = rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
    rt_string value = (rt_string)rt_map_get(map, name);

    if (!value)
        return rt_string_from_bytes("", 0);

    return value;
}

int8_t rt_url_has_query_param(void *obj, rt_string name)
{
    if (!obj)
        return 0;

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return 0;

    void *map = rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
    return rt_map_has(map, name);
}

void *rt_url_del_query_param(void *obj, rt_string name)
{
    if (!obj)
        return obj;

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return obj;

    void *map = rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
    rt_map_remove(map, name);

    rt_string new_query = rt_url_encode_query(map);

    if (url->query)
        free(url->query);

    const char *query_str = rt_string_cstr(new_query);
    url->query = query_str && *query_str ? strdup(query_str) : NULL;

    return obj;
}

void *rt_url_query_map(void *obj)
{
    if (!obj)
        return rt_map_new();

    rt_url_t *url = (rt_url_t *)obj;
    if (!url->query)
        return rt_map_new();

    return rt_url_decode_query(rt_string_from_bytes(url->query, strlen(url->query)));
}

void *rt_url_resolve(void *obj, rt_string relative)
{
    if (!obj)
        rt_trap("URL: NULL base URL");

    rt_url_t *base = (rt_url_t *)obj;
    const char *rel_str = rt_string_cstr(relative);

    if (!rel_str || *rel_str == '\0')
        return rt_url_clone(obj);

    // Parse relative URL
    rt_url_t rel;
    memset(&rel, 0, sizeof(rel));
    parse_url_full(rel_str, &rel);

    // Create new URL
    rt_url_t *result = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!result)
        rt_trap("URL: Memory allocation failed");
    memset(result, 0, sizeof(*result));

    // RFC 3986 resolution algorithm
    if (rel.scheme)
    {
        // Relative has scheme - use as-is
        result->scheme = safe_strdup(rel.scheme);
        result->user = safe_strdup(rel.user);
        result->pass = safe_strdup(rel.pass);
        result->host = safe_strdup(rel.host);
        result->port = rel.port;
        result->path = safe_strdup(rel.path);
        result->query = safe_strdup(rel.query);
    }
    else
    {
        if (rel.host)
        {
            // Relative has authority
            result->scheme = safe_strdup(base->scheme);
            result->user = safe_strdup(rel.user);
            result->pass = safe_strdup(rel.pass);
            result->host = safe_strdup(rel.host);
            result->port = rel.port;
            result->path = safe_strdup(rel.path);
            result->query = safe_strdup(rel.query);
        }
        else
        {
            result->scheme = safe_strdup(base->scheme);
            result->user = safe_strdup(base->user);
            result->pass = safe_strdup(base->pass);
            result->host = safe_strdup(base->host);
            result->port = base->port;

            if (!rel.path || *rel.path == '\0')
            {
                result->path = safe_strdup(base->path);
                if (rel.query)
                    result->query = safe_strdup(rel.query);
                else
                    result->query = safe_strdup(base->query);
            }
            else
            {
                if (rel.path[0] == '/')
                {
                    result->path = safe_strdup(rel.path);
                }
                else
                {
                    // Merge paths
                    if (!base->host || !base->path || *base->path == '\0')
                    {
                        // No base authority or empty base path
                        size_t len = strlen(rel.path) + 2;
                        result->path = (char *)malloc(len);
                        if (result->path)
                            sprintf(result->path, "/%s", rel.path);
                    }
                    else
                    {
                        // Remove last segment of base path
                        const char *last_slash = strrchr(base->path, '/');
                        if (last_slash)
                        {
                            size_t base_len = last_slash - base->path + 1;
                            size_t len = base_len + strlen(rel.path) + 1;
                            result->path = (char *)malloc(len);
                            if (result->path)
                            {
                                memcpy(result->path, base->path, base_len);
                                strcpy(result->path + base_len, rel.path);
                            }
                        }
                        else
                        {
                            result->path = safe_strdup(rel.path);
                        }
                    }
                }
                result->query = safe_strdup(rel.query);
            }
        }
    }

    result->fragment = safe_strdup(rel.fragment);

    // Clean up relative URL
    free_url(&rel);

    return result;
}

void *rt_url_clone(void *obj)
{
    if (!obj)
        return rt_url_new();

    rt_url_t *url = (rt_url_t *)obj;
    rt_url_t *clone = (rt_url_t *)rt_obj_new_i64(0, sizeof(rt_url_t));
    if (!clone)
        rt_trap("URL: Memory allocation failed");

    clone->scheme = safe_strdup(url->scheme);
    clone->user = safe_strdup(url->user);
    clone->pass = safe_strdup(url->pass);
    clone->host = safe_strdup(url->host);
    clone->port = url->port;
    clone->path = safe_strdup(url->path);
    clone->query = safe_strdup(url->query);
    clone->fragment = safe_strdup(url->fragment);

    return clone;
}

rt_string rt_url_encode(rt_string text)
{
    const char *str = rt_string_cstr(text);
    char *encoded = percent_encode(str, true);
    if (!encoded)
        return rt_string_from_bytes("", 0);

    rt_string result = rt_string_from_bytes(encoded, strlen(encoded));
    free(encoded);
    return result;
}

rt_string rt_url_decode(rt_string text)
{
    const char *str = rt_string_cstr(text);
    char *decoded = percent_decode(str);
    if (!decoded)
        return rt_string_from_bytes("", 0);

    rt_string result = rt_string_from_bytes(decoded, strlen(decoded));
    free(decoded);
    return result;
}

rt_string rt_url_encode_query(void *map)
{
    if (!map)
        return rt_string_from_bytes("", 0);

    void *keys = rt_map_keys(map);
    int64_t len = rt_seq_len(keys);

    if (len == 0)
        return rt_string_from_bytes("", 0);

    // Build query string
    size_t cap = 256;
    char *result = (char *)malloc(cap);
    if (!result)
        return rt_string_from_bytes("", 0);

    size_t pos = 0;
    for (int64_t i = 0; i < len; i++)
    {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        rt_string value = (rt_string)rt_map_get(map, key);

        const char *key_str = rt_string_cstr(key);
        const char *value_str = rt_string_cstr(value);

        char *enc_key = percent_encode(key_str, true);
        char *enc_value = value_str ? percent_encode(value_str, true) : strdup("");

        if (!enc_key || !enc_value)
        {
            free(enc_key);
            free(enc_value);
            continue;
        }

        size_t needed = strlen(enc_key) + 1 + strlen(enc_value) + 2; // key=value&
        if (pos + needed >= cap)
        {
            cap = (pos + needed) * 2;
            char *new_result = (char *)realloc(result, cap);
            if (!new_result)
            {
                free(enc_key);
                free(enc_value);
                break;
            }
            result = new_result;
        }

        if (i > 0)
            result[pos++] = '&';
        pos += sprintf(result + pos, "%s=%s", enc_key, enc_value);

        free(enc_key);
        free(enc_value);
    }

    result[pos] = '\0';
    rt_string str = rt_string_from_bytes(result, pos);
    free(result);
    return str;
}

void *rt_url_decode_query(rt_string query)
{
    void *map = rt_map_new();
    const char *str = rt_string_cstr(query);

    if (!str || *str == '\0')
        return map;

    const char *p = str;
    while (*p)
    {
        // Find end of key
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');

        if (!eq || (amp && amp < eq))
        {
            // Key without value
            const char *end = amp ? amp : p + strlen(p);
            if (end > p)
            {
                char *key = (char *)malloc(end - p + 1);
                if (key)
                {
                    memcpy(key, p, end - p);
                    key[end - p] = '\0';
                    char *dec_key = percent_decode(key);
                    if (dec_key)
                    {
                        rt_string key_str = rt_string_from_bytes(dec_key, strlen(dec_key));
                        rt_map_set(map, key_str, (void *)rt_string_from_bytes("", 0));
                        free(dec_key);
                    }
                    free(key);
                }
            }
            p = amp ? amp + 1 : p + strlen(p);
        }
        else
        {
            // Key=Value
            size_t key_len = eq - p;
            const char *val_start = eq + 1;
            const char *val_end = amp ? amp : val_start + strlen(val_start);

            char *key = (char *)malloc(key_len + 1);
            char *val = (char *)malloc(val_end - val_start + 1);

            if (key && val)
            {
                memcpy(key, p, key_len);
                key[key_len] = '\0';
                memcpy(val, val_start, val_end - val_start);
                val[val_end - val_start] = '\0';

                char *dec_key = percent_decode(key);
                char *dec_val = percent_decode(val);

                if (dec_key && dec_val)
                {
                    rt_string key_str = rt_string_from_bytes(dec_key, strlen(dec_key));
                    rt_string val_str = rt_string_from_bytes(dec_val, strlen(dec_val));
                    rt_map_set(map, key_str, (void *)val_str);
                }

                free(dec_key);
                free(dec_val);
            }

            free(key);
            free(val);
            p = amp ? amp + 1 : val_end;
        }
    }

    return map;
}

int8_t rt_url_is_valid(rt_string url_str)
{
    const char *str = rt_string_cstr(url_str);
    if (!str || *str == '\0')
        return 0;

    rt_url_t url;
    memset(&url, 0, sizeof(url));

    int result = parse_url_full(str, &url);
    free_url(&url);

    return result == 0 ? 1 : 0;
}
