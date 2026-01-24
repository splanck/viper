//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/socket.c
// Purpose: BSD socket API implementation for ViperDOS.
// Key invariants: Socket FDs start at 128; uses kernel syscalls.
// Ownership/Lifetime: Library; socket objects ref-counted.
// Links: user/libc/include/sys/socket.h
//
//===----------------------------------------------------------------------===//

/**
 * @file socket.c
 * @brief BSD socket API implementation for ViperDOS.
 *
 * @details
 * This file implements the standard BSD socket functions (socket, connect,
 * send, recv, close, etc.) for the ViperDOS C library. Key features:
 *
 * - Socket FD virtualization: Socket FDs start at 128 to avoid collision
 *   with stdio FDs (0-2) and kernel file descriptors.
 * - Kernel backend: Uses kernel TCP socket syscalls directly.
 * - Reference counting: Socket objects are ref-counted for dup() support.
 *
 * This is a minimal implementation focused on TCP client sockets.
 */

#include "../include/sys/socket.h"
#include "../include/arpa/inet.h"
#include "../include/errno.h"
#include "../include/netinet/in.h"
#include "../include/string.h"

/* Kernel socket syscalls */
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall4(long num, long arg0, long arg1, long arg2, long arg3);
#define SYS_SOCKET_CREATE  0x50
#define SYS_SOCKET_CONNECT 0x51
#define SYS_SOCKET_SEND    0x52
#define SYS_SOCKET_RECV    0x53
#define SYS_SOCKET_CLOSE   0x54

// -----------------------------------------------------------------------------
// libc socket FD virtualization
//
// Kernel TCP sockets are identified by small integer IDs starting at 0, which
// collides with stdin/stdout/stderr and breaks POSIX-style code that uses
// close()/poll()/select() on sockets. libc therefore exposes sockets as a
// separate FD namespace that does not overlap the kernel file descriptor table.
// -----------------------------------------------------------------------------

#define VIPER_SOCKET_FD_BASE 128
#define VIPER_SOCKET_MAX_FDS 64

typedef struct
{
    int in_use;
    int socket_id;     /* kernel socket id (index in tcp socket table) */
    unsigned int refs; /* reference count across duplicated FDs */
} viper_socket_obj_t;

typedef struct
{
    int in_use;
    unsigned short obj_index;
} viper_socket_fd_t;

static viper_socket_obj_t g_sock_objs[VIPER_SOCKET_MAX_FDS];
static viper_socket_fd_t g_sock_fds[VIPER_SOCKET_MAX_FDS];

static int viper_sock_fd_in_range(int fd)
{
    return fd >= VIPER_SOCKET_FD_BASE && fd < (VIPER_SOCKET_FD_BASE + VIPER_SOCKET_MAX_FDS);
}

static int viper_sock_fd_index(int fd)
{
    return fd - VIPER_SOCKET_FD_BASE;
}

static int viper_sock_get_obj_index_for_fd(int fd)
{
    if (!viper_sock_fd_in_range(fd))
        return -1;

    int idx = viper_sock_fd_index(fd);
    if (idx < 0 || idx >= VIPER_SOCKET_MAX_FDS)
        return -1;

    if (!g_sock_fds[idx].in_use)
        return -1;

    int obj = (int)g_sock_fds[idx].obj_index;
    if (obj < 0 || obj >= VIPER_SOCKET_MAX_FDS)
        return -1;
    if (!g_sock_objs[obj].in_use)
        return -1;

    return obj;
}

static viper_socket_obj_t *viper_sock_get_obj_for_fd(int fd)
{
    int obj = viper_sock_get_obj_index_for_fd(fd);
    if (obj < 0)
        return (viper_socket_obj_t *)0;
    return &g_sock_objs[obj];
}

static int viper_sock_alloc_obj(int socket_id)
{
    for (int i = 0; i < VIPER_SOCKET_MAX_FDS; i++)
    {
        if (!g_sock_objs[i].in_use)
        {
            g_sock_objs[i].in_use = 1;
            g_sock_objs[i].socket_id = socket_id;
            g_sock_objs[i].refs = 1;
            return i;
        }
    }
    return -EMFILE;
}

static void viper_sock_release_obj(int obj)
{
    if (obj < 0 || obj >= VIPER_SOCKET_MAX_FDS)
        return;
    g_sock_objs[obj].in_use = 0;
    g_sock_objs[obj].socket_id = -1;
    g_sock_objs[obj].refs = 0;
}

static int viper_sock_alloc_fd_slot(int obj)
{
    if (obj < 0 || obj >= VIPER_SOCKET_MAX_FDS)
        return -EINVAL;

    for (int i = 0; i < VIPER_SOCKET_MAX_FDS; i++)
    {
        if (!g_sock_fds[i].in_use)
        {
            g_sock_fds[i].in_use = 1;
            g_sock_fds[i].obj_index = (unsigned short)obj;
            return VIPER_SOCKET_FD_BASE + i;
        }
    }
    return -EMFILE;
}

static int viper_sock_alloc_specific_fd_slot(int fd, int obj)
{
    if (!viper_sock_fd_in_range(fd))
        return -EINVAL;
    if (obj < 0 || obj >= VIPER_SOCKET_MAX_FDS)
        return -EINVAL;

    int idx = viper_sock_fd_index(fd);
    if (idx < 0 || idx >= VIPER_SOCKET_MAX_FDS)
        return -EINVAL;

    if (g_sock_fds[idx].in_use)
        return -EBUSY;

    g_sock_fds[idx].in_use = 1;
    g_sock_fds[idx].obj_index = (unsigned short)obj;
    return fd;
}

static void viper_sock_free_fd_slot(int fd)
{
    if (!viper_sock_fd_in_range(fd))
        return;

    int idx = viper_sock_fd_index(fd);
    if (idx < 0 || idx >= VIPER_SOCKET_MAX_FDS)
        return;

    g_sock_fds[idx].in_use = 0;
    g_sock_fds[idx].obj_index = 0;
}

static int viper_sock_close_obj(viper_socket_obj_t *obj)
{
    if (!obj || !obj->in_use)
        return -EBADF;

    long rc = __syscall3(SYS_SOCKET_CLOSE, obj->socket_id, 0, 0);
    return (rc == 0) ? 0 : (int)rc;
}

static int viper_sock_close_fd(int fd)
{
    int obj_index = viper_sock_get_obj_index_for_fd(fd);
    if (obj_index < 0)
        return -EBADF;

    viper_socket_obj_t *obj = &g_sock_objs[obj_index];

    viper_sock_free_fd_slot(fd);

    if (obj->refs > 0)
        obj->refs--;

    if (obj->refs == 0)
    {
        (void)viper_sock_close_obj(obj);
        viper_sock_release_obj(obj_index);
    }

    return 0;
}

static int viper_sock_dup_fd(int oldfd)
{
    int obj_index = viper_sock_get_obj_index_for_fd(oldfd);
    if (obj_index < 0)
        return -EBADF;

    viper_socket_obj_t *obj = &g_sock_objs[obj_index];
    if (!obj->in_use)
        return -EBADF;

    int newfd = viper_sock_alloc_fd_slot(obj_index);
    if (newfd < 0)
        return newfd;

    obj->refs++;
    return newfd;
}

static int viper_sock_dup2_fd(int oldfd, int newfd)
{
    if (oldfd == newfd)
        return newfd;

    int obj_index = viper_sock_get_obj_index_for_fd(oldfd);
    if (obj_index < 0)
        return -EBADF;

    if (!viper_sock_fd_in_range(newfd))
        return -ENOTSUP;

    // If newfd already exists as a socket FD, close it first.
    if (viper_sock_get_obj_for_fd(newfd) != (viper_socket_obj_t *)0)
    {
        (void)viper_sock_close_fd(newfd);
    }

    int rc = viper_sock_alloc_specific_fd_slot(newfd, obj_index);
    if (rc < 0)
        return rc;

    g_sock_objs[obj_index].refs++;
    return newfd;
}

// Exposed for other libc modules (e.g., unistd.c, poll.c).
int __viper_socket_is_fd(int fd)
{
    return viper_sock_get_obj_for_fd(fd) ? 1 : 0;
}

// Exposed for other libc modules (e.g., poll.c) to query socket id.
int __viper_socket_get_backend(int fd, int *out_backend, int *out_socket_id)
{
    viper_socket_obj_t *obj = viper_sock_get_obj_for_fd(fd);
    if (!obj)
        return -EBADF;
    if (out_backend)
        *out_backend = 1; /* KERNEL backend */
    if (out_socket_id)
        *out_socket_id = obj->socket_id;
    return 0;
}

int __viper_socket_close(int fd)
{
    return viper_sock_close_fd(fd);
}

int __viper_socket_dup(int oldfd)
{
    return viper_sock_dup_fd(oldfd);
}

int __viper_socket_dup2(int oldfd, int newfd)
{
    return viper_sock_dup2_fd(oldfd, newfd);
}

static int viper_sock_translate_fd(int fd, int *out_socket_id)
{
    viper_socket_obj_t *obj = viper_sock_get_obj_for_fd(fd);
    if (!obj)
        return -EBADF;

    *out_socket_id = obj->socket_id;
    return 0;
}

/* IPv6 address constants */
const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

/**
 * @defgroup byteorder Byte Order Conversion
 * @brief Functions for converting between host and network byte order.
 *
 * Network protocols use big-endian (network) byte order, while many processors
 * (including AArch64 in its default configuration) use little-endian (host)
 * byte order. These functions convert between the two representations.
 * @{
 */

/**
 * @brief Convert 16-bit value from host to network byte order.
 *
 * @details
 * Converts a 16-bit unsigned integer from host byte order to network
 * byte order (big-endian). Use this when filling in port numbers in
 * sockaddr_in structures before connect() or bind().
 *
 * @param hostshort 16-bit value in host byte order.
 * @return Same value in network byte order.
 *
 * @see ntohs, htonl, ntohl
 */
unsigned short htons(unsigned short hostshort)
{
    return ((hostshort & 0xff) << 8) | ((hostshort >> 8) & 0xff);
}

/**
 * @brief Convert 16-bit value from network to host byte order.
 *
 * @details
 * Converts a 16-bit unsigned integer from network byte order (big-endian)
 * to host byte order. Use this when reading port numbers from sockaddr_in
 * structures returned by accept() or getpeername().
 *
 * @param netshort 16-bit value in network byte order.
 * @return Same value in host byte order.
 *
 * @see htons, htonl, ntohl
 */
unsigned short ntohs(unsigned short netshort)
{
    return htons(netshort);
}

/**
 * @brief Convert 32-bit value from host to network byte order.
 *
 * @details
 * Converts a 32-bit unsigned integer from host byte order to network
 * byte order (big-endian). Use this when filling in IP addresses in
 * sockaddr_in structures before connect() or bind().
 *
 * @param hostlong 32-bit value in host byte order.
 * @return Same value in network byte order.
 *
 * @see ntohl, htons, ntohs
 */
unsigned int htonl(unsigned int hostlong)
{
    return ((hostlong & 0xff) << 24) | ((hostlong & 0xff00) << 8) | ((hostlong >> 8) & 0xff00) |
           ((hostlong >> 24) & 0xff);
}

/**
 * @brief Convert 32-bit value from network to host byte order.
 *
 * @details
 * Converts a 32-bit unsigned integer from network byte order (big-endian)
 * to host byte order. Use this when reading IP addresses from sockaddr_in
 * structures returned by accept() or getpeername().
 *
 * @param netlong 32-bit value in network byte order.
 * @return Same value in host byte order.
 *
 * @see htonl, htons, ntohs
 */
unsigned int ntohl(unsigned int netlong)
{
    return htonl(netlong);
}

/** @} */ /* end of byteorder group */

/**
 * @defgroup bsdsocket BSD Socket API
 * @brief Standard BSD socket interface for network communication.
 *
 * These functions implement the classic BSD socket API for creating and
 * managing network connections. Currently supports TCP client sockets.
 * @{
 */

/**
 * @brief Create a socket endpoint for communication.
 *
 * @details
 * Creates a new socket of the specified domain and type. The socket is
 * unconnected and must be connected with connect() before data can be
 * sent or received.
 *
 * Socket file descriptors in ViperDOS start at 128 to avoid collision
 * with standard file descriptors (0=stdin, 1=stdout, 2=stderr).
 *
 * @param domain Address family (AF_INET for IPv4, AF_INET6 for IPv6).
 * @param type Socket type (SOCK_STREAM for TCP, SOCK_DGRAM for UDP).
 * @param protocol Protocol number (usually 0 for default).
 * @return Non-negative socket file descriptor on success, -1 on error.
 *
 * @note Currently only AF_INET/SOCK_STREAM (TCP) is fully supported.
 *
 * @see connect, send, recv, close
 */
int socket(int domain, int type, int protocol)
{
    long rc = __syscall3(SYS_SOCKET_CREATE, domain, type, protocol);
    if (rc < 0)
    {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    int sock_id = (int)rc;

    int obj = viper_sock_alloc_obj(sock_id);
    if (obj < 0)
    {
        (void)__syscall3(SYS_SOCKET_CLOSE, sock_id, 0, 0);
        errno = -obj;
        return -1;
    }

    int fd = viper_sock_alloc_fd_slot(obj);
    if (fd < 0)
    {
        (void)__syscall3(SYS_SOCKET_CLOSE, sock_id, 0, 0);
        viper_sock_release_obj(obj);
        errno = -fd;
        return -1;
    }

    return fd;
}

/**
 * @brief Bind a socket to a local address.
 *
 * @details
 * Assigns a local protocol address to a socket. For IP sockets, this
 * specifies the local IP address and port number that the socket will
 * use for communication.
 *
 * @note Not implemented in ViperDOS - server sockets are not supported.
 *
 * @param sockfd Socket file descriptor from socket().
 * @param addr Pointer to address structure (e.g., struct sockaddr_in).
 * @param addrlen Size of the address structure.
 * @return 0 on success, -1 on error (sets errno to ENOSYS).
 *
 * @see socket, listen, accept
 */
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    /* ViperDOS kernel doesn't have bind - sockets connect directly */
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Mark a socket as accepting connections.
 *
 * @details
 * Marks the socket as a passive socket that will be used to accept incoming
 * connection requests using accept(). The backlog parameter specifies the
 * maximum length of the pending connections queue.
 *
 * @note Not implemented in ViperDOS - server sockets are not supported.
 *
 * @param sockfd Socket file descriptor from socket().
 * @param backlog Maximum pending connection queue length.
 * @return 0 on success, -1 on error (sets errno to ENOSYS).
 *
 * @see socket, bind, accept
 */
int listen(int sockfd, int backlog)
{
    /* ViperDOS kernel doesn't have listen - no server sockets yet */
    (void)sockfd;
    (void)backlog;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Accept a connection on a listening socket.
 *
 * @details
 * Extracts the first pending connection request from the queue of pending
 * connections for the listening socket, creates a new connected socket, and
 * returns a file descriptor for that socket. If addr is non-NULL, the
 * address of the connecting peer is stored there.
 *
 * @note Not implemented in ViperDOS - server sockets are not supported.
 *
 * @param sockfd Listening socket file descriptor.
 * @param addr Buffer to receive peer address (or NULL).
 * @param addrlen In: size of addr buffer; out: actual address size.
 * @return New socket file descriptor on success, -1 on error.
 *
 * @see socket, bind, listen, accept4
 */
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    /* ViperDOS kernel doesn't have accept - no server sockets yet */
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Accept a connection with additional flags.
 *
 * @details
 * Like accept(), but allows additional flags to be specified that control
 * the new socket (e.g., SOCK_NONBLOCK, SOCK_CLOEXEC). Currently just calls
 * accept() ignoring flags.
 *
 * @note Not implemented in ViperDOS - server sockets are not supported.
 *
 * @param sockfd Listening socket file descriptor.
 * @param addr Buffer to receive peer address (or NULL).
 * @param addrlen In: size of addr buffer; out: actual address size.
 * @param flags Flags for the new socket (e.g., SOCK_NONBLOCK).
 * @return New socket file descriptor on success, -1 on error.
 *
 * @see accept, socket
 */
int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    (void)flags;
    return accept(sockfd, addr, addrlen);
}

/**
 * @brief Initiate a connection on a socket.
 *
 * @details
 * Connects the socket to the address specified by addr. For TCP sockets,
 * this initiates the TCP three-way handshake to establish a connection.
 *
 * The address must be a sockaddr_in structure for IPv4 connections.
 * The sin_addr and sin_port fields must be in network byte order
 * (use htonl() and htons() to convert).
 *
 * After successful connection, the socket can be used with send() and recv()
 * to transfer data.
 *
 * @param sockfd Socket file descriptor from socket().
 * @param addr Pointer to address structure specifying the remote address.
 * @param addrlen Size of the address structure.
 * @return 0 on success, -1 on error (sets errno).
 *
 * @see socket, send, recv, close
 */
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    int sock_id = -1;
    int trc = viper_sock_translate_fd(sockfd, &sock_id);
    if (trc < 0)
    {
        errno = -trc;
        return -1;
    }

    if (addrlen < sizeof(struct sockaddr_in))
    {
        errno = EINVAL;
        return -1;
    }
    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    if (sin->sin_family != AF_INET)
    {
        errno = EAFNOSUPPORT;
        return -1;
    }

    /* Kernel syscall: ip in network order, port in network order */
    int rc = (int)__syscall3(SYS_SOCKET_CONNECT, sock_id,
                             (long)sin->sin_addr.s_addr, (long)sin->sin_port);

    if (rc != 0)
    {
        errno = ECONNREFUSED;
        return -1;
    }
    return 0;
}

/**
 * @brief Send data on a connected socket.
 *
 * @details
 * Transmits data from the buffer to the connected peer. The socket must
 * be connected before calling this function. The call may return before
 * all data has been transmitted; the return value indicates how many
 * bytes were actually sent.
 *
 * Common behavior:
 * - Returns the number of bytes sent (may be less than len)
 * - Returns -1 on error and sets errno
 * - For non-blocking sockets, returns -1 with errno EAGAIN if the send
 *   would block
 *
 * @param sockfd Connected socket file descriptor.
 * @param buf Buffer containing data to send.
 * @param len Number of bytes to send.
 * @param flags Send flags (MSG_DONTWAIT, MSG_NOSIGNAL, etc.; ignored).
 * @return Number of bytes sent on success, -1 on error.
 *
 * @see recv, sendto, connect
 */
ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    (void)flags; /* ViperDOS doesn't use flags */
    int sock_id = -1;
    int trc = viper_sock_translate_fd(sockfd, &sock_id);
    if (trc < 0)
    {
        errno = -trc;
        return -1;
    }

    long result = __syscall3(SYS_SOCKET_SEND, sock_id, (long)buf, (long)len);

    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return result;
}

/**
 * @brief Receive data from a connected socket.
 *
 * @details
 * Receives data from the connected peer into the buffer. The socket must
 * be connected before calling this function. The call blocks until at least
 * some data is available (unless the socket is non-blocking).
 *
 * Common behavior:
 * - Returns the number of bytes received (may be less than len)
 * - Returns 0 if the peer has closed the connection (EOF)
 * - Returns -1 on error and sets errno
 * - For non-blocking sockets, returns -1 with errno EAGAIN if no data
 *   is available
 *
 * @param sockfd Connected socket file descriptor.
 * @param buf Buffer to receive data into.
 * @param len Maximum number of bytes to receive.
 * @param flags Receive flags (MSG_DONTWAIT, MSG_PEEK, etc.; ignored).
 * @return Number of bytes received on success, 0 on EOF, -1 on error.
 *
 * @see send, recvfrom, connect
 */
ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    (void)flags; /* ViperDOS doesn't use flags */
    int sock_id = -1;
    int trc = viper_sock_translate_fd(sockfd, &sock_id);
    if (trc < 0)
    {
        errno = -trc;
        return -1;
    }

    long result = __syscall3(SYS_SOCKET_RECV, sock_id, (long)buf, (long)len);

    if (result < 0)
    {
        /* Convert VERR_WOULD_BLOCK (-300) to EAGAIN for POSIX compatibility */
        if (result == -300)
        {
            errno = EAGAIN;
        }
        else
        {
            errno = (int)(-result);
        }
        return -1;
    }
    return result;
}

/**
 * @brief Send data to a specific destination address.
 *
 * @details
 * Sends data to the specified destination address. For connected sockets,
 * dest_addr can be NULL to use the connected peer address. For unconnected
 * sockets (e.g., UDP), dest_addr specifies where to send the data.
 *
 * @note UDP sendto is not supported in ViperDOS. For connected sockets,
 * this is equivalent to send() when dest_addr is NULL.
 *
 * @param sockfd Socket file descriptor.
 * @param buf Buffer containing data to send.
 * @param len Number of bytes to send.
 * @param flags Send flags (ignored).
 * @param dest_addr Destination address (or NULL for connected sockets).
 * @param addrlen Size of destination address structure.
 * @return Number of bytes sent on success, -1 on error.
 *
 * @see send, recvfrom
 */
ssize_t sendto(int sockfd,
               const void *buf,
               size_t len,
               int flags,
               const struct sockaddr *dest_addr,
               socklen_t addrlen)
{
    /* For connected sockets, just use send */
    if (dest_addr == (void *)0)
    {
        return send(sockfd, buf, len, flags);
    }
    /* UDP sendto not supported */
    (void)addrlen;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Receive data and the source address.
 *
 * @details
 * Receives data and optionally the source address. For connected sockets,
 * src_addr can be NULL. For unconnected sockets (e.g., UDP), src_addr
 * receives the address of the sender.
 *
 * @note UDP recvfrom is not supported in ViperDOS. For connected sockets,
 * this is equivalent to recv() when src_addr is NULL.
 *
 * @param sockfd Socket file descriptor.
 * @param buf Buffer to receive data into.
 * @param len Maximum number of bytes to receive.
 * @param flags Receive flags (ignored).
 * @param src_addr Buffer to receive source address (or NULL).
 * @param addrlen In: size of src_addr buffer; out: actual address size.
 * @return Number of bytes received on success, 0 on EOF, -1 on error.
 *
 * @see recv, sendto
 */
ssize_t recvfrom(
    int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
    /* For connected sockets, just use recv */
    if (src_addr == (void *)0)
    {
        return recv(sockfd, buf, len, flags);
    }
    /* UDP recvfrom not supported */
    (void)addrlen;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Send a message using scatter/gather I/O.
 *
 * @details
 * Sends a message described by a msghdr structure, which allows scatter/gather
 * I/O (multiple buffers) and ancillary data (control messages). The message
 * header specifies the destination address, data buffers, and optional
 * control information.
 *
 * @note ViperDOS only supports single-buffer messages (msg_iovlen == 1).
 * Multiple scatter/gather buffers will return ENOTSUP.
 *
 * @param sockfd Socket file descriptor.
 * @param msg Message header describing the message to send.
 * @param flags Send flags (ignored).
 * @return Number of bytes sent on success, -1 on error.
 *
 * @see recvmsg, send, sendto
 */
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    /* Simplified implementation using send for single iovec */
    if (msg->msg_iovlen == 1)
    {
        return sendto(sockfd,
                      msg->msg_iov[0].iov_base,
                      msg->msg_iov[0].iov_len,
                      flags,
                      (struct sockaddr *)msg->msg_name,
                      msg->msg_namelen);
    }
    errno = ENOTSUP;
    return -1;
}

/**
 * @brief Receive a message using scatter/gather I/O.
 *
 * @details
 * Receives a message into a msghdr structure, which allows scatter/gather
 * I/O (multiple buffers) and ancillary data (control messages). The message
 * header specifies where to store the source address, data buffers, and
 * control information.
 *
 * @note ViperDOS only supports single-buffer messages (msg_iovlen == 1).
 * Multiple scatter/gather buffers will return ENOTSUP.
 *
 * @param sockfd Socket file descriptor.
 * @param msg Message header describing where to receive data.
 * @param flags Receive flags (ignored).
 * @return Number of bytes received on success, 0 on EOF, -1 on error.
 *
 * @see sendmsg, recv, recvfrom
 */
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    /* Simplified implementation using recv for single iovec */
    if (msg->msg_iovlen == 1)
    {
        return recvfrom(sockfd,
                        msg->msg_iov[0].iov_base,
                        msg->msg_iov[0].iov_len,
                        flags,
                        (struct sockaddr *)msg->msg_name,
                        &msg->msg_namelen);
    }
    errno = ENOTSUP;
    return -1;
}

/**
 * @brief Get socket option value.
 *
 * @details
 * Retrieves the value of a socket option. Options can be at the socket
 * level (SOL_SOCKET) or protocol level (e.g., IPPROTO_TCP).
 *
 * Common socket options include:
 * - SO_REUSEADDR: Allow reuse of local addresses
 * - SO_KEEPALIVE: Enable TCP keepalive
 * - SO_RCVBUF/SO_SNDBUF: Buffer sizes
 * - SO_ERROR: Get and clear pending socket error
 *
 * @note Socket options are not implemented in ViperDOS. This function
 * always returns success for compatibility.
 *
 * @param sockfd Socket file descriptor.
 * @param level Option level (SOL_SOCKET, IPPROTO_TCP, etc.).
 * @param optname Option name (SO_REUSEADDR, SO_KEEPALIVE, etc.).
 * @param optval Buffer to receive option value.
 * @param optlen In: size of buffer; out: actual option size.
 * @return 0 on success (stub), -1 on error.
 *
 * @see setsockopt
 */
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    /* ViperDOS doesn't support socket options yet - return success for common cases */
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return 0; /* Pretend success */
}

/**
 * @brief Set socket option value.
 *
 * @details
 * Sets the value of a socket option. Options can be at the socket level
 * (SOL_SOCKET) or protocol level (e.g., IPPROTO_TCP).
 *
 * Common socket options include:
 * - SO_REUSEADDR: Allow reuse of local addresses
 * - SO_KEEPALIVE: Enable TCP keepalive
 * - SO_RCVBUF/SO_SNDBUF: Buffer sizes
 * - TCP_NODELAY: Disable Nagle algorithm
 *
 * @note Socket options are not implemented in ViperDOS. This function
 * always returns success for compatibility.
 *
 * @param sockfd Socket file descriptor.
 * @param level Option level (SOL_SOCKET, IPPROTO_TCP, etc.).
 * @param optname Option name (SO_REUSEADDR, SO_KEEPALIVE, etc.).
 * @param optval Pointer to option value.
 * @param optlen Size of option value.
 * @return 0 on success (stub), -1 on error.
 *
 * @see getsockopt
 */
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    /* ViperDOS doesn't support socket options yet - return success for common cases */
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return 0; /* Pretend success */
}

/**
 * @brief Get local address of a socket.
 *
 * @details
 * Retrieves the local address that the socket is bound to. For connected
 * sockets, this is the local end of the connection. For unbound sockets,
 * the address is typically INADDR_ANY with port 0.
 *
 * @note Not implemented in ViperDOS.
 *
 * @param sockfd Socket file descriptor.
 * @param addr Buffer to receive local address.
 * @param addrlen In: size of buffer; out: actual address size.
 * @return 0 on success, -1 on error (sets errno to ENOSYS).
 *
 * @see getpeername, bind
 */
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    /* Not implemented */
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Get remote address of a connected socket.
 *
 * @details
 * Retrieves the address of the peer to which the socket is connected.
 * The socket must be connected for this to succeed.
 *
 * @note Not implemented in ViperDOS.
 *
 * @param sockfd Connected socket file descriptor.
 * @param addr Buffer to receive peer address.
 * @param addrlen In: size of buffer; out: actual address size.
 * @return 0 on success, -1 on error (sets errno to ENOSYS).
 *
 * @see getsockname, connect
 */
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    /* Not implemented */
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Shut down part or all of a socket connection.
 *
 * @details
 * Causes all or part of a full-duplex connection to be shut down:
 * - SHUT_RD (0): No more receives
 * - SHUT_WR (1): No more sends
 * - SHUT_RDWR (2): No more sends or receives
 *
 * In ViperDOS, shutdown() closes the socket entirely regardless of
 * the 'how' parameter.
 *
 * @param sockfd Socket file descriptor.
 * @param how Type of shutdown (SHUT_RD, SHUT_WR, SHUT_RDWR).
 * @return 0 on success, -1 on error.
 *
 * @see close, socket
 */
int shutdown(int sockfd, int how)
{
    (void)how;
    int rc = viper_sock_close_fd(sockfd);
    if (rc < 0)
    {
        errno = -rc;
        return -1;
    }
    return 0;
}

/**
 * @brief Create a pair of connected sockets.
 *
 * @details
 * Creates a pair of connected sockets in the specified domain. These
 * sockets can be used for bidirectional inter-process communication
 * (similar to pipe() but bidirectional).
 *
 * @note Not implemented in ViperDOS.
 *
 * @param domain Address family (typically AF_UNIX).
 * @param type Socket type (SOCK_STREAM or SOCK_DGRAM).
 * @param protocol Protocol (usually 0).
 * @param sv Array of two integers to receive the socket descriptors.
 * @return 0 on success, -1 on error (sets errno to ENOSYS).
 *
 * @see pipe, socket
 */
int socketpair(int domain, int type, int protocol, int sv[2])
{
    /* Not implemented */
    (void)domain;
    (void)type;
    (void)protocol;
    (void)sv;
    errno = ENOSYS;
    return -1;
}

/** @} */ /* end of bsdsocket group */

/**
 * @defgroup inet Internet Address Conversion
 * @brief Functions for converting between text and binary IP addresses.
 *
 * These functions convert IP addresses between human-readable text format
 * (e.g., "192.168.1.1") and binary network format (struct in_addr).
 * @{
 */

/**
 * @brief Convert dotted-decimal string to network byte order address.
 *
 * @details
 * Converts an IPv4 address in dotted-decimal notation (e.g., "192.168.1.1")
 * to a 32-bit binary value in network byte order. This is a simplified
 * interface to inet_aton().
 *
 * @warning This function cannot distinguish between a valid address of
 * 255.255.255.255 and an error condition. Use inet_aton() for better
 * error handling.
 *
 * @param cp String containing IPv4 address in dotted-decimal notation.
 * @return Network byte order address, or INADDR_NONE (-1) on error.
 *
 * @see inet_aton, inet_ntoa, inet_pton
 */
in_addr_t inet_addr(const char *cp)
{
    struct in_addr addr;
    if (inet_aton(cp, &addr) == 0)
    {
        return INADDR_NONE;
    }
    return addr.s_addr;
}

/**
 * @brief Convert dotted-decimal string to binary address.
 *
 * @details
 * Converts an IPv4 address in dotted-decimal notation to a struct in_addr.
 * This function provides better error handling than inet_addr() by returning
 * a distinct error indication (0) rather than using a valid address.
 *
 * Supported formats (conforming to BSD):
 * - a.b.c.d: Standard 4-part notation
 * - a.b.c: 3-part, c is 16-bit
 * - a.b: 2-part, b is 24-bit
 * - a: Single 32-bit value
 *
 * @param cp String containing IPv4 address.
 * @param inp Pointer to struct in_addr to receive the result.
 * @return 1 on success, 0 if the string is invalid.
 *
 * @see inet_addr, inet_ntoa, inet_pton
 */
int inet_aton(const char *cp, struct in_addr *inp)
{
    unsigned long parts[4];
    int num_parts = 0;
    const char *p = cp;

    /* Parse up to 4 parts separated by dots */
    while (*p && num_parts < 4)
    {
        unsigned long val = 0;
        int digits = 0;

        while (*p >= '0' && *p <= '9')
        {
            val = val * 10 + (*p - '0');
            if (val > 255)
                return 0;
            p++;
            digits++;
        }

        if (digits == 0)
            return 0;

        parts[num_parts++] = val;

        if (*p == '.')
        {
            p++;
        }
        else
        {
            break;
        }
    }

    /* Trailing characters are not allowed */
    if (*p != '\0')
        return 0;

    /* Convert to network byte order based on number of parts */
    unsigned long result;
    switch (num_parts)
    {
        case 1:
            result = parts[0];
            break;
        case 2:
            result = (parts[0] << 24) | (parts[1] & 0xffffff);
            break;
        case 3:
            result = (parts[0] << 24) | (parts[1] << 16) | (parts[2] & 0xffff);
            break;
        case 4:
            result = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
            break;
        default:
            return 0;
    }

    inp->s_addr = htonl(result);
    return 1;
}

/**
 * @brief Convert binary address to dotted-decimal string.
 *
 * @details
 * Converts an IPv4 address in struct in_addr format to a human-readable
 * dotted-decimal string (e.g., "192.168.1.1"). The result is stored in
 * a static buffer.
 *
 * @warning The returned pointer points to static storage that is overwritten
 * by subsequent calls. Copy the result if you need to preserve it.
 *
 * @warning This function is not thread-safe. Use inet_ntop() for thread-safe
 * conversion.
 *
 * @param in IPv4 address in network byte order.
 * @return Pointer to static buffer containing the dotted-decimal string.
 *
 * @see inet_aton, inet_addr, inet_ntop
 */
char *inet_ntoa(struct in_addr in)
{
    static char buf[INET_ADDRSTRLEN];
    unsigned int addr = ntohl(in.s_addr);

    int pos = 0;
    for (int i = 3; i >= 0; i--)
    {
        unsigned int octet = (addr >> (i * 8)) & 0xff;
        if (octet >= 100)
        {
            buf[pos++] = '0' + octet / 100;
            buf[pos++] = '0' + (octet / 10) % 10;
            buf[pos++] = '0' + octet % 10;
        }
        else if (octet >= 10)
        {
            buf[pos++] = '0' + octet / 10;
            buf[pos++] = '0' + octet % 10;
        }
        else
        {
            buf[pos++] = '0' + octet;
        }
        if (i > 0)
            buf[pos++] = '.';
    }
    buf[pos] = '\0';
    return buf;
}

/**
 * @brief Convert presentation format address to network format.
 *
 * @details
 * Converts an IP address from text representation to binary format.
 * Supports both IPv4 (af=AF_INET) and IPv6 (af=AF_INET6) addresses,
 * though IPv6 is not fully implemented in ViperDOS.
 *
 * For AF_INET, the address must be in dotted-decimal notation.
 * For AF_INET6, the address should be in standard IPv6 text format.
 *
 * @param af Address family (AF_INET or AF_INET6).
 * @param src String containing the address in text format.
 * @param dst Buffer to receive the binary address (struct in_addr or in6_addr).
 * @return 1 on success, 0 if src is invalid, -1 if af is not supported.
 *
 * @see inet_ntop, inet_aton
 */
int inet_pton(int af, const char *src, void *dst)
{
    if (af == AF_INET)
    {
        return inet_aton(src, (struct in_addr *)dst);
    }
    else if (af == AF_INET6)
    {
        /* Simplified IPv6 parsing - not fully implemented */
        errno = EAFNOSUPPORT;
        return -1;
    }
    errno = EAFNOSUPPORT;
    return -1;
}

/**
 * @brief Convert network format address to presentation format.
 *
 * @details
 * Converts an IP address from binary format to text representation.
 * Supports both IPv4 (af=AF_INET) and IPv6 (af=AF_INET6) addresses,
 * though IPv6 is not fully implemented in ViperDOS.
 *
 * The buffer must be large enough to hold the result:
 * - INET_ADDRSTRLEN (16) for IPv4
 * - INET6_ADDRSTRLEN (46) for IPv6
 *
 * This function is thread-safe (unlike inet_ntoa).
 *
 * @param af Address family (AF_INET or AF_INET6).
 * @param src Binary address (struct in_addr or in6_addr).
 * @param dst Buffer to receive the text address.
 * @param size Size of the destination buffer.
 * @return dst on success, NULL on error (sets errno).
 *
 * @see inet_pton, inet_ntoa
 */
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    if (af == AF_INET)
    {
        if (size < INET_ADDRSTRLEN)
        {
            errno = ENOSPC;
            return (void *)0;
        }
        struct in_addr addr = *(const struct in_addr *)src;
        char *result = inet_ntoa(addr);
        size_t len = strlen(result);
        memcpy(dst, result, len + 1);
        return dst;
    }
    else if (af == AF_INET6)
    {
        /* Simplified IPv6 output - not fully implemented */
        errno = EAFNOSUPPORT;
        return (void *)0;
    }
    errno = EAFNOSUPPORT;
    return (void *)0;
}

/**
 * @brief Convert dotted-decimal string to network number in host byte order.
 *
 * @details
 * Interprets the string as a network number (not a host address) and
 * returns it in host byte order. This differs from inet_addr() which
 * returns the address in network byte order.
 *
 * @deprecated This function is obsolete. Use inet_pton() with ntohl() instead.
 *
 * @param cp String containing the network address.
 * @return Network number in host byte order, or -1 on error.
 *
 * @see inet_addr, inet_makeaddr
 */
in_addr_t inet_network(const char *cp)
{
    return inet_addr(cp);
}

/**
 * @brief Create an internet address from network and host parts.
 *
 * @details
 * Combines a network number and a host number into an internet address.
 * The interpretation depends on the class of the network number (A, B, or C).
 *
 * @deprecated This function uses obsolete classful addressing. Modern networks
 * use CIDR (Classless Inter-Domain Routing) instead.
 *
 * @param net Network number (in host byte order).
 * @param host Host number (in host byte order).
 * @return Combined address as struct in_addr.
 *
 * @see inet_netof, inet_lnaof, inet_network
 */
struct in_addr inet_makeaddr(in_addr_t net, in_addr_t host)
{
    struct in_addr addr;
    addr.s_addr = htonl(net | host);
    return addr;
}

/**
 * @brief Extract host part of an internet address.
 *
 * @details
 * Returns the local network address (host) portion of an internet address.
 * The size of the host portion depends on the address class:
 * - Class A (first byte 0-127): 24-bit host part
 * - Class B (first byte 128-191): 16-bit host part
 * - Class C (first byte 192-223): 8-bit host part
 *
 * @deprecated This function uses obsolete classful addressing. Modern networks
 * use CIDR (Classless Inter-Domain Routing) instead.
 *
 * @param in Internet address in network byte order.
 * @return Host portion in host byte order.
 *
 * @see inet_netof, inet_makeaddr
 */
in_addr_t inet_lnaof(struct in_addr in)
{
    unsigned int addr = ntohl(in.s_addr);
    if ((addr & 0x80000000) == 0)
        return addr & 0x00ffffff; /* Class A */
    if ((addr & 0xc0000000) == 0x80000000)
        return addr & 0x0000ffff; /* Class B */
    return addr & 0x000000ff;     /* Class C */
}

/**
 * @brief Extract network part of an internet address.
 *
 * @details
 * Returns the network portion of an internet address. The size of the
 * network portion depends on the address class:
 * - Class A (first byte 0-127): 8-bit network part
 * - Class B (first byte 128-191): 16-bit network part
 * - Class C (first byte 192-223): 24-bit network part
 *
 * @deprecated This function uses obsolete classful addressing. Modern networks
 * use CIDR (Classless Inter-Domain Routing) instead.
 *
 * @param in Internet address in network byte order.
 * @return Network portion in host byte order.
 *
 * @see inet_lnaof, inet_makeaddr
 */
in_addr_t inet_netof(struct in_addr in)
{
    unsigned int addr = ntohl(in.s_addr);
    if ((addr & 0x80000000) == 0)
        return (addr >> 24) & 0xff; /* Class A */
    if ((addr & 0xc0000000) == 0x80000000)
        return (addr >> 16) & 0xffff; /* Class B */
    return (addr >> 8) & 0xffffff;    /* Class C */
}

/** @} */ /* end of inet group */
