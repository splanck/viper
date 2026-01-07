#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#include "../netinet/in.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * inet_addr - Convert IPv4 dotted-decimal string to network byte order
     * Returns INADDR_NONE on error
     */
    in_addr_t inet_addr(const char *cp);

    /*
     * inet_aton - Convert IPv4 dotted-decimal string to struct in_addr
     * Returns 1 on success, 0 on failure
     */
    int inet_aton(const char *cp, struct in_addr *inp);

    /*
     * inet_ntoa - Convert struct in_addr to dotted-decimal string
     * Returns pointer to static buffer (not thread-safe)
     */
    char *inet_ntoa(struct in_addr in);

    /*
     * inet_pton - Convert address from presentation to network format
     * af: AF_INET or AF_INET6
     * src: string representation
     * dst: output buffer
     * Returns 1 on success, 0 if src invalid, -1 on error
     */
    int inet_pton(int af, const char *src, void *dst);

    /*
     * inet_ntop - Convert address from network to presentation format
     * af: AF_INET or AF_INET6
     * src: binary address
     * dst: output buffer
     * size: size of dst buffer
     * Returns dst on success, NULL on error
     */
    const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

    /*
     * inet_network - Extract network number from address
     */
    in_addr_t inet_network(const char *cp);

    /*
     * inet_makeaddr - Create internet address from network and host
     */
    struct in_addr inet_makeaddr(in_addr_t net, in_addr_t host);

    /*
     * inet_lnaof - Extract local network address from internet address
     */
    in_addr_t inet_lnaof(struct in_addr in);

    /*
     * inet_netof - Extract network number from internet address
     */
    in_addr_t inet_netof(struct in_addr in);

/* Maximum sizes for address strings */
#define INET_ADDRSTRLEN 16  /* "255.255.255.255" + null */
#define INET6_ADDRSTRLEN 46 /* Full IPv6 with scope */

#ifdef __cplusplus
}
#endif

#endif /* _ARPA_INET_H */
