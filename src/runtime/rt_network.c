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
    socket_t sock;       // Socket descriptor
    char *host;          // Remote host (allocated)
    int port;            // Remote port
    int local_port;      // Local port
    bool is_open;        // Connection state
    int recv_timeout_ms; // Receive timeout (0 = none)
    int send_timeout_ms; // Send timeout (0 = none)
} rt_tcp_t;

//=============================================================================
// TcpServer Structure
//=============================================================================

typedef struct rt_tcp_server
{
    socket_t sock;     // Listening socket
    char *address;     // Bound address (allocated)
    int port;          // Listening port
    bool is_listening; // Server state
} rt_tcp_server_t;

//=============================================================================
// Windows WSA Initialization
//=============================================================================

#ifdef _WIN32
static bool wsa_initialized = false;

void rt_net_init_wsa(void)
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
void rt_net_init_wsa(void) {}
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
    setsockopt(
        sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, is_recv ? SO_RCVTIMEO : SO_SNDTIMEO, &tv, sizeof(tv));
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
    rt_net_init_wsa();

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
        int sent = send(tcp->sock, (const char *)(buf + total_sent), (int)(len - total_sent), 0);
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
        int received =
            recv(tcp->sock, (char *)(buf + total_received), (int)(count - total_received), 0);
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
    rt_net_init_wsa();

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
    socket_t sock;                     // Socket descriptor
    char *address;                     // Bound address (allocated, or NULL if unbound)
    int port;                          // Bound port (0 if unbound)
    bool is_bound;                     // Whether socket is bound
    bool is_open;                      // Socket state
    char sender_host[INET_ADDRSTRLEN]; // Last sender host
    int sender_port;                   // Last sender port
    int recv_timeout_ms;               // Receive timeout (0 = none)
} rt_udp_t;

//=============================================================================
// Udp - Creation
//=============================================================================

void *rt_udp_new(void)
{
    rt_net_init_wsa();

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
    rt_net_init_wsa();

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

    int sent = sendto(udp->sock,
                      (const char *)buf,
                      (int)len,
                      0,
                      (struct sockaddr *)&dest_addr,
                      sizeof(dest_addr));
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

    int sent =
        sendto(udp->sock, text_ptr, (int)len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
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

    int received = recvfrom(
        udp->sock, (char *)buf, (int)max_bytes, 0, (struct sockaddr *)&sender_addr, &sender_len);

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
    if (setsockopt(udp->sock, SOL_SOCKET, SO_BROADCAST, (const char *)&flag, sizeof(flag)) ==
        SOCK_ERROR)
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

    if (setsockopt(udp->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) ==
        SOCK_ERROR)
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
        return; // Silently ignore if closed

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
    rt_net_init_wsa();

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
    rt_net_init_wsa();

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
    rt_net_init_wsa();

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
    rt_net_init_wsa();

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
    rt_net_init_wsa();

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
    rt_net_init_wsa();

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        rt_trap("Network: failed to get hostname");
    }

    return rt_string_from_bytes(hostname, strlen(hostname));
}

void *rt_dns_local_addrs(void)
{
    rt_net_init_wsa();

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
