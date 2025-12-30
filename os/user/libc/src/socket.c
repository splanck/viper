#include "../include/sys/socket.h"
#include "../include/netinet/in.h"
#include "../include/arpa/inet.h"
#include "../include/errno.h"
#include "../include/string.h"

/* Syscall helpers */
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall4(long num, long arg0, long arg1, long arg2, long arg3);
extern long __syscall5(long num, long arg0, long arg1, long arg2, long arg3, long arg4);
extern long __syscall6(long num, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5);

/* Syscall numbers */
#define SYS_SOCKET      0xC0
#define SYS_BIND        0xC1
#define SYS_LISTEN      0xC2
#define SYS_ACCEPT      0xC3
#define SYS_CONNECT     0xC4
#define SYS_SEND        0xC5
#define SYS_RECV        0xC6
#define SYS_SENDTO      0xC7
#define SYS_RECVFROM    0xC8
#define SYS_SHUTDOWN    0xC9
#define SYS_GETSOCKOPT  0xCA
#define SYS_SETSOCKOPT  0xCB
#define SYS_GETSOCKNAME 0xCC
#define SYS_GETPEERNAME 0xCD

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
    return ((hostlong & 0xff) << 24) |
           ((hostlong & 0xff00) << 8) |
           ((hostlong >> 8) & 0xff00) |
           ((hostlong >> 24) & 0xff);
}

unsigned int ntohl(unsigned int netlong)
{
    return htonl(netlong);
}

/* Socket functions */
int socket(int domain, int type, int protocol)
{
    long result = __syscall3(SYS_SOCKET, domain, type, protocol);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return (int)result;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    long result = __syscall3(SYS_BIND, sockfd, (long)addr, addrlen);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

int listen(int sockfd, int backlog)
{
    long result = __syscall2(SYS_LISTEN, sockfd, backlog);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    long result = __syscall3(SYS_ACCEPT, sockfd, (long)addr, (long)addrlen);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return (int)result;
}

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    /* Not fully implemented - ignore flags for now */
    (void)flags;
    return accept(sockfd, addr, addrlen);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    long result = __syscall3(SYS_CONNECT, sockfd, (long)addr, addrlen);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    long result = __syscall4(SYS_SEND, sockfd, (long)buf, len, flags);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return result;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    long result = __syscall4(SYS_RECV, sockfd, (long)buf, len, flags);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return result;
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen)
{
    long result = __syscall6(SYS_SENDTO, sockfd, (long)buf, len, flags,
                             (long)dest_addr, addrlen);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return result;
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen)
{
    long result = __syscall6(SYS_RECVFROM, sockfd, (long)buf, len, flags,
                             (long)src_addr, (long)addrlen);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return result;
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    /* Simplified implementation using send for single iovec */
    if (msg->msg_iovlen == 1)
    {
        return sendto(sockfd, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len,
                      flags, (struct sockaddr *)msg->msg_name, msg->msg_namelen);
    }
    errno = ENOTSUP;
    return -1;
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    /* Simplified implementation using recv for single iovec */
    if (msg->msg_iovlen == 1)
    {
        return recvfrom(sockfd, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len,
                        flags, (struct sockaddr *)msg->msg_name, &msg->msg_namelen);
    }
    errno = ENOTSUP;
    return -1;
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    long result = __syscall5(SYS_GETSOCKOPT, sockfd, level, optname,
                             (long)optval, (long)optlen);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    long result = __syscall5(SYS_SETSOCKOPT, sockfd, level, optname,
                             (long)optval, optlen);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    long result = __syscall3(SYS_GETSOCKNAME, sockfd, (long)addr, (long)addrlen);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    long result = __syscall3(SYS_GETPEERNAME, sockfd, (long)addr, (long)addrlen);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

int shutdown(int sockfd, int how)
{
    long result = __syscall2(SYS_SHUTDOWN, sockfd, how);
    if (result < 0)
    {
        errno = (int)(-result);
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
        return (addr >> 24) & 0xff;       /* Class A */
    if ((addr & 0xc0000000) == 0x80000000)
        return (addr >> 16) & 0xffff;     /* Class B */
    return (addr >> 8) & 0xffffff;        /* Class C */
}
