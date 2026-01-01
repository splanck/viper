#include "../include/sys/socket.h"
#include "../include/arpa/inet.h"
#include "../include/errno.h"
#include "../include/netinet/in.h"
#include "../include/string.h"

/* Syscall helpers */
extern long __syscall0(long num);
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall4(long num, long arg0, long arg1, long arg2, long arg3);
extern long __syscall5(long num, long arg0, long arg1, long arg2, long arg3, long arg4);
extern long __syscall6(long num, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5);

/* Syscall numbers - must match kernel's syscall_nums.hpp */
#define SYS_SOCKET_CREATE 0x50
#define SYS_SOCKET_CONNECT 0x51
#define SYS_SOCKET_SEND 0x52
#define SYS_SOCKET_RECV 0x53
#define SYS_SOCKET_CLOSE 0x54
#define SYS_DNS_RESOLVE 0x55

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

typedef enum
{
    VIPER_SOCKET_BACKEND_NONE = 0,
    VIPER_SOCKET_BACKEND_KERNEL = 1,
} viper_socket_backend_t;

typedef struct
{
    int in_use;
    viper_socket_backend_t backend;
    int socket_id;          /* kernel socket id (index in tcp socket table) */
    unsigned int refs;      /* reference count across duplicated FDs */
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

static int viper_sock_alloc_obj(viper_socket_backend_t backend, int socket_id)
{
    for (int i = 0; i < VIPER_SOCKET_MAX_FDS; i++)
    {
        if (!g_sock_objs[i].in_use)
        {
            g_sock_objs[i].in_use = 1;
            g_sock_objs[i].backend = backend;
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
    g_sock_objs[obj].backend = VIPER_SOCKET_BACKEND_NONE;
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

    if (obj->backend == VIPER_SOCKET_BACKEND_KERNEL)
    {
        long rc = __syscall1(SYS_SOCKET_CLOSE, obj->socket_id);
        if (rc < 0)
        {
            return (int)rc;
        }
        return 0;
    }

    return -ENOSYS;
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

    if (obj->backend != VIPER_SOCKET_BACKEND_KERNEL)
        return -ENOSYS;

    *out_socket_id = obj->socket_id;
    return 0;
}

/* IPv6 address constants */
const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

/* Byte order functions (for little-endian AArch64) */
unsigned short htons(unsigned short hostshort)
{
    return ((hostshort & 0xff) << 8) | ((hostshort >> 8) & 0xff);
}

unsigned short ntohs(unsigned short netshort)
{
    return htons(netshort);
}

unsigned int htonl(unsigned int hostlong)
{
    return ((hostlong & 0xff) << 24) | ((hostlong & 0xff00) << 8) | ((hostlong >> 8) & 0xff00) |
           ((hostlong >> 24) & 0xff);
}

unsigned int ntohl(unsigned int netlong)
{
    return htonl(netlong);
}

/* Socket functions */
int socket(int domain, int type, int protocol)
{
    /* ViperOS kernel only supports TCP sockets for now */
    (void)domain;
    (void)protocol;
    if (type != SOCK_STREAM)
    {
        errno = EPROTONOSUPPORT;
        return -1;
    }

    long sock_id = __syscall0(SYS_SOCKET_CREATE);
    if (sock_id < 0)
    {
        errno = (int)(-sock_id);
        return -1;
    }

    int obj = viper_sock_alloc_obj(VIPER_SOCKET_BACKEND_KERNEL, (int)sock_id);
    if (obj < 0)
    {
        (void)__syscall1(SYS_SOCKET_CLOSE, (int)sock_id);
        errno = -obj;
        return -1;
    }

    int fd = viper_sock_alloc_fd_slot(obj);
    if (fd < 0)
    {
        (void)__syscall1(SYS_SOCKET_CLOSE, (int)sock_id);
        viper_sock_release_obj(obj);
        errno = -fd;
        return -1;
    }

    return fd;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    /* ViperOS kernel doesn't have bind - sockets connect directly */
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    errno = ENOSYS;
    return -1;
}

int listen(int sockfd, int backlog)
{
    /* ViperOS kernel doesn't have listen - no server sockets yet */
    (void)sockfd;
    (void)backlog;
    errno = ENOSYS;
    return -1;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    /* ViperOS kernel doesn't have accept - no server sockets yet */
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    errno = ENOSYS;
    return -1;
}

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    (void)flags;
    return accept(sockfd, addr, addrlen);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    int sock_id = -1;
    int trc = viper_sock_translate_fd(sockfd, &sock_id);
    if (trc < 0)
    {
        errno = -trc;
        return -1;
    }

    /* ViperOS kernel expects: sock, ip (u32), port (u16) */
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
    /* sin_addr.s_addr is already in network byte order, kernel expects host order */
    unsigned int ip = ntohl(sin->sin_addr.s_addr);
    unsigned short port = ntohs(sin->sin_port);
    long result = __syscall3(SYS_SOCKET_CONNECT, sock_id, ip, port);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    (void)flags; /* ViperOS doesn't use flags */
    int sock_id = -1;
    int trc = viper_sock_translate_fd(sockfd, &sock_id);
    if (trc < 0)
    {
        errno = -trc;
        return -1;
    }

    long result = __syscall3(SYS_SOCKET_SEND, sock_id, (long)buf, len);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return result;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    (void)flags; /* ViperOS doesn't use flags */
    int sock_id = -1;
    int trc = viper_sock_translate_fd(sockfd, &sock_id);
    if (trc < 0)
    {
        errno = -trc;
        return -1;
    }

    long result = __syscall3(SYS_SOCKET_RECV, sock_id, (long)buf, len);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return result;
}

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

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    /* ViperOS doesn't support socket options yet - return success for common cases */
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return 0; /* Pretend success */
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    /* ViperOS doesn't support socket options yet - return success for common cases */
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return 0; /* Pretend success */
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    /* Not implemented */
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    errno = ENOSYS;
    return -1;
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    /* Not implemented */
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    errno = ENOSYS;
    return -1;
}

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

/* inet_addr - Convert dotted-decimal to network byte order */
in_addr_t inet_addr(const char *cp)
{
    struct in_addr addr;
    if (inet_aton(cp, &addr) == 0)
    {
        return INADDR_NONE;
    }
    return addr.s_addr;
}

/* inet_aton - Convert dotted-decimal to struct in_addr */
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

/* inet_ntoa - Convert struct in_addr to dotted-decimal string */
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

/* inet_pton - Convert address from presentation to network format */
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

/* inet_ntop - Convert address from network to presentation format */
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

/* Other inet functions - simplified implementations */
in_addr_t inet_network(const char *cp)
{
    return inet_addr(cp);
}

struct in_addr inet_makeaddr(in_addr_t net, in_addr_t host)
{
    struct in_addr addr;
    addr.s_addr = htonl(net | host);
    return addr;
}

in_addr_t inet_lnaof(struct in_addr in)
{
    unsigned int addr = ntohl(in.s_addr);
    if ((addr & 0x80000000) == 0)
        return addr & 0x00ffffff; /* Class A */
    if ((addr & 0xc0000000) == 0x80000000)
        return addr & 0x0000ffff; /* Class B */
    return addr & 0x000000ff;     /* Class C */
}

in_addr_t inet_netof(struct in_addr in)
{
    unsigned int addr = ntohl(in.s_addr);
    if ((addr & 0x80000000) == 0)
        return (addr >> 24) & 0xff; /* Class A */
    if ((addr & 0xc0000000) == 0x80000000)
        return (addr >> 16) & 0xffff; /* Class B */
    return (addr >> 8) & 0xffffff;    /* Class C */
}
