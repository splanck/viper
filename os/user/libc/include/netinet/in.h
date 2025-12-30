#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#include "../sys/socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internet port type */
typedef unsigned short in_port_t;

/* Internet address type */
typedef unsigned int in_addr_t;

/* IPv4 address structure */
struct in_addr {
    in_addr_t s_addr;
};

/* IPv4 socket address */
struct sockaddr_in {
    unsigned short sin_family;  /* AF_INET */
    in_port_t      sin_port;    /* Port number (network byte order) */
    struct in_addr sin_addr;    /* IP address */
    unsigned char  sin_zero[8]; /* Padding */
};

/* IPv6 address structure */
struct in6_addr {
    union {
        unsigned char  s6_addr[16];
        unsigned short s6_addr16[8];
        unsigned int   s6_addr32[4];
    };
};

/* IPv6 socket address */
struct sockaddr_in6 {
    unsigned short  sin6_family;   /* AF_INET6 */
    in_port_t       sin6_port;     /* Port number */
    unsigned int    sin6_flowinfo; /* IPv6 flow info */
    struct in6_addr sin6_addr;     /* IPv6 address */
    unsigned int    sin6_scope_id; /* Scope ID */
};

/* Special IPv4 addresses */
#define INADDR_ANY       ((in_addr_t) 0x00000000)
#define INADDR_BROADCAST ((in_addr_t) 0xffffffff)
#define INADDR_NONE      ((in_addr_t) 0xffffffff)
#define INADDR_LOOPBACK  ((in_addr_t) 0x7f000001) /* 127.0.0.1 */

/* IPv6 in6addr constants */
extern const struct in6_addr in6addr_any;
extern const struct in6_addr in6addr_loopback;

#define IN6ADDR_ANY_INIT {{{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}}
#define IN6ADDR_LOOPBACK_INIT {{{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}}}

/* IPv6 address test macros */
#define IN6_IS_ADDR_UNSPECIFIED(a) \
    (((a)->s6_addr32[0] == 0) && ((a)->s6_addr32[1] == 0) && \
     ((a)->s6_addr32[2] == 0) && ((a)->s6_addr32[3] == 0))

#define IN6_IS_ADDR_LOOPBACK(a) \
    (((a)->s6_addr32[0] == 0) && ((a)->s6_addr32[1] == 0) && \
     ((a)->s6_addr32[2] == 0) && ((a)->s6_addr32[3] == htonl(1)))

#define IN6_IS_ADDR_MULTICAST(a) \
    ((a)->s6_addr[0] == 0xff)

#define IN6_IS_ADDR_LINKLOCAL(a) \
    (((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0x80))

#define IN6_IS_ADDR_V4MAPPED(a) \
    (((a)->s6_addr32[0] == 0) && ((a)->s6_addr32[1] == 0) && \
     ((a)->s6_addr32[2] == htonl(0x0000ffff)))

/* IP protocol numbers */
#define IPPROTO_IP       0    /* Dummy protocol */
#define IPPROTO_ICMP     1    /* ICMP */
#define IPPROTO_IGMP     2    /* IGMP */
#define IPPROTO_TCP      6    /* TCP */
#define IPPROTO_UDP      17   /* UDP */
#define IPPROTO_IPV6     41   /* IPv6 header */
#define IPPROTO_ICMPV6   58   /* ICMPv6 */
#define IPPROTO_RAW      255  /* Raw IP packets */

/* IP options for setsockopt */
#define IP_TOS             1
#define IP_TTL             2
#define IP_HDRINCL         3
#define IP_OPTIONS         4
#define IP_RECVOPTS        6
#define IP_MULTICAST_IF    32
#define IP_MULTICAST_TTL   33
#define IP_MULTICAST_LOOP  34
#define IP_ADD_MEMBERSHIP  35
#define IP_DROP_MEMBERSHIP 36

/* IPv6 options */
#define IPV6_UNICAST_HOPS   16
#define IPV6_MULTICAST_IF   17
#define IPV6_MULTICAST_HOPS 18
#define IPV6_MULTICAST_LOOP 19
#define IPV6_JOIN_GROUP     20
#define IPV6_LEAVE_GROUP    21
#define IPV6_V6ONLY         26

/* TCP options */
#define TCP_NODELAY    1
#define TCP_MAXSEG     2
#define TCP_KEEPIDLE   4
#define TCP_KEEPINTVL  5
#define TCP_KEEPCNT    6

/* Multicast group request */
struct ip_mreq {
    struct in_addr imr_multiaddr;  /* Multicast group address */
    struct in_addr imr_interface;  /* Interface address */
};

struct ipv6_mreq {
    struct in6_addr ipv6mr_multiaddr;  /* IPv6 multicast address */
    unsigned int    ipv6mr_interface;  /* Interface index */
};

/* Byte order conversion functions */
unsigned short htons(unsigned short hostshort);
unsigned short ntohs(unsigned short netshort);
unsigned int   htonl(unsigned int hostlong);
unsigned int   ntohl(unsigned int netlong);

#ifdef __cplusplus
}
#endif

#endif /* _NETINET_IN_H */
