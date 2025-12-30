#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Socket types */
#define SOCK_STREAM 1    /* TCP */
#define SOCK_DGRAM 2     /* UDP */
#define SOCK_RAW 3       /* Raw socket */
#define SOCK_SEQPACKET 5 /* Sequenced packets */

/* Socket type flags */
#define SOCK_NONBLOCK 0x800  /* Set O_NONBLOCK */
#define SOCK_CLOEXEC 0x80000 /* Set FD_CLOEXEC */

/* Address families */
#define AF_UNSPEC 0 /* Unspecified */
#define AF_UNIX 1   /* Unix domain sockets */
#define AF_LOCAL AF_UNIX
#define AF_INET 2    /* IPv4 */
#define AF_INET6 10  /* IPv6 */
#define AF_PACKET 17 /* Raw packets */

/* Protocol families (same as address families) */
#define PF_UNSPEC AF_UNSPEC
#define PF_UNIX AF_UNIX
#define PF_LOCAL AF_LOCAL
#define PF_INET AF_INET
#define PF_INET6 AF_INET6

/* Shutdown how values */
#define SHUT_RD 0   /* No more reads */
#define SHUT_WR 1   /* No more writes */
#define SHUT_RDWR 2 /* No more reads or writes */

/* Socket option levels */
#define SOL_SOCKET 1 /* Socket level options */

/* Socket options */
#define SO_DEBUG 1
#define SO_REUSEADDR 2
#define SO_TYPE 3
#define SO_ERROR 4
#define SO_DONTROUTE 5
#define SO_BROADCAST 6
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE 9
#define SO_OOBINLINE 10
#define SO_NO_CHECK 11
#define SO_PRIORITY 12
#define SO_LINGER 13
#define SO_BSDCOMPAT 14
#define SO_REUSEPORT 15
#define SO_RCVLOWAT 18
#define SO_SNDLOWAT 19
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define SO_ACCEPTCONN 30
#define SO_PEERNAME 28
#define SO_TIMESTAMP 29

/* Message flags */
#define MSG_OOB 0x01        /* Out-of-band data */
#define MSG_PEEK 0x02       /* Peek at data without removing */
#define MSG_DONTROUTE 0x04  /* Don't route */
#define MSG_CTRUNC 0x08     /* Control data truncated */
#define MSG_TRUNC 0x20      /* Data truncated */
#define MSG_DONTWAIT 0x40   /* Non-blocking */
#define MSG_WAITALL 0x100   /* Wait for full request */
#define MSG_NOSIGNAL 0x4000 /* Don't generate SIGPIPE */

    /* sockaddr size */
    typedef unsigned int socklen_t;

    /* Generic socket address structure */
    struct sockaddr
    {
        unsigned short sa_family; /* Address family */
        char sa_data[14];         /* Address data */
    };

    /* Storage for any socket address */
    struct sockaddr_storage
    {
        unsigned short ss_family; /* Address family */
        char __ss_padding[126];   /* Padding to 128 bytes */
    };

    /* Linger structure */
    struct linger
    {
        int l_onoff;  /* Linger on/off */
        int l_linger; /* Linger time in seconds */
    };

    /* Message header for sendmsg/recvmsg */
    struct iovec
    {
        void *iov_base; /* Base address */
        size_t iov_len; /* Length */
    };

    struct msghdr
    {
        void *msg_name;        /* Optional address */
        socklen_t msg_namelen; /* Size of address */
        struct iovec *msg_iov; /* Scatter/gather array */
        size_t msg_iovlen;     /* # elements in msg_iov */
        void *msg_control;     /* Ancillary data */
        size_t msg_controllen; /* Ancillary data length */
        int msg_flags;         /* Flags on received message */
    };

    /* Control message header */
    struct cmsghdr
    {
        size_t cmsg_len; /* Data length including header */
        int cmsg_level;  /* Originating protocol */
        int cmsg_type;   /* Protocol-specific type */
        /* followed by unsigned char cmsg_data[] */
    };

/* Control message macros */
#define CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_DATA(cmsg) ((unsigned char *)((struct cmsghdr *)(cmsg) + 1))
#define CMSG_NXTHDR(mhdr, cmsg)                                                                    \
    ((cmsg)->cmsg_len < sizeof(struct cmsghdr)                                                     \
         ? (struct cmsghdr *)0                                                                     \
         : ((unsigned char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len) >=                              \
                    (unsigned char *)(mhdr)->msg_control + (mhdr)->msg_controllen                  \
                ? (struct cmsghdr *)0                                                              \
                : (struct cmsghdr *)((unsigned char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len))))
#define CMSG_FIRSTHDR(mhdr)                                                                        \
    ((mhdr)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr *)(mhdr)->msg_control      \
                                                      : (struct cmsghdr *)0)
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))

    /* Socket functions */
    int socket(int domain, int type, int protocol);
    int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    int listen(int sockfd, int backlog);
    int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
    int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

    /* Data transfer */
    ssize_t send(int sockfd, const void *buf, size_t len, int flags);
    ssize_t recv(int sockfd, void *buf, size_t len, int flags);
    ssize_t sendto(int sockfd,
                   const void *buf,
                   size_t len,
                   int flags,
                   const struct sockaddr *dest_addr,
                   socklen_t addrlen);
    ssize_t recvfrom(int sockfd,
                     void *buf,
                     size_t len,
                     int flags,
                     struct sockaddr *src_addr,
                     socklen_t *addrlen);
    ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
    ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);

    /* Socket options */
    int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
    int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

    /* Socket address */
    int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

    /* Shutdown */
    int shutdown(int sockfd, int how);

    /* Socket pair */
    int socketpair(int domain, int type, int protocol, int sv[2]);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SOCKET_H */
