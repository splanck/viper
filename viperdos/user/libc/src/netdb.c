//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/netdb.c
// Purpose: Network database functions for ViperDOS libc.
// Key invariants: DNS via kernel syscall; static service/protocol tables.
// Ownership/Lifetime: Library; static storage for results.
// Links: user/libc/include/netdb.h
//
//===----------------------------------------------------------------------===//

/**
 * @file netdb.c
 * @brief Network database functions for ViperDOS libc.
 *
 * @details
 * This file implements network name resolution and service lookup:
 *
 * - Host lookup: gethostbyname, gethostbyaddr, getaddrinfo, getnameinfo
 * - Service lookup: getservbyname, getservbyport
 * - Protocol lookup: getprotobyname, getprotobynumber
 * - Error handling: herror, hstrerror, gai_strerror
 *
 * DNS resolution is performed via kernel syscall.
 * Service and protocol lookups use static built-in tables for
 * common services (http, https, ssh, etc.) and protocols (tcp, udp).
 */

#include "../include/netdb.h"
#include "../include/arpa/inet.h"
#include "../include/errno.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"

/* Kernel DNS syscall */
extern long __syscall2(long num, long arg0, long arg1);
#define SYS_DNS_RESOLVE 0x55

/* Thread-local h_errno */
int h_errno = 0;

/* Static storage for returned structures */
static struct hostent static_hostent;
static char static_hostname[256];
static char *static_alias_list[1] = {(void *)0};
static char *static_addr_list[2];
static struct in_addr static_addr;

/* Error messages for getaddrinfo */
static const char *gai_errmsgs[] = {
    "Success",                      /* 0 */
    "Invalid flags",                /* EAI_BADFLAGS */
    "Name not known",               /* EAI_NONAME */
    "Try again later",              /* EAI_AGAIN */
    "Non-recoverable error",        /* EAI_FAIL */
    "Unknown error 5",              /* reserved */
    "Address family not supported", /* EAI_FAMILY */
    "Socket type not supported",    /* EAI_SOCKTYPE */
    "Service not known",            /* EAI_SERVICE */
    "Unknown error 9",              /* reserved */
    "Memory allocation failure",    /* EAI_MEMORY */
    "System error",                 /* EAI_SYSTEM */
    "Buffer overflow",              /* EAI_OVERFLOW */
};

/**
 * @defgroup hostlookup Host Name Lookup
 * @brief Functions for resolving hostnames to IP addresses.
 * @{
 */

/**
 * @brief Resolve a hostname to an IPv4 address.
 *
 * @details
 * Looks up the given hostname and returns a structure containing its
 * IPv4 address(es). The lookup is performed in this order:
 *
 * 1. If the name is a valid dotted-decimal IPv4 address, convert directly
 * 2. Otherwise, perform DNS resolution via kernel syscall
 *
 * The returned hostent structure is stored in static memory and will be
 * overwritten by subsequent calls to this function or gethostbyaddr().
 *
 * @warning This function is not thread-safe. Use gethostbyname_r() for
 * reentrant hostname lookup, or preferably use getaddrinfo().
 *
 * @param name Hostname to resolve (e.g., "www.example.com") or dotted-decimal
 *             IPv4 address (e.g., "192.168.1.1").
 * @return Pointer to static hostent structure on success, or NULL on error
 *         (check h_errno for details).
 *
 * @see gethostbyaddr, gethostbyname_r, getaddrinfo
 */
struct hostent *gethostbyname(const char *name)
{
    if (!name)
    {
        h_errno = HOST_NOT_FOUND;
        return (void *)0;
    }

    /* Try to parse as numeric address first */
    struct in_addr addr;
    if (inet_aton(name, &addr))
    {
        static_addr = addr;
        static_addr_list[0] = (char *)&static_addr;
        static_addr_list[1] = (void *)0;

        size_t len = strlen(name);
        if (len >= sizeof(static_hostname))
            len = sizeof(static_hostname) - 1;
        memcpy(static_hostname, name, len);
        static_hostname[len] = '\0';

        static_hostent.h_name = static_hostname;
        static_hostent.h_aliases = static_alias_list;
        static_hostent.h_addrtype = AF_INET;
        static_hostent.h_length = 4;
        static_hostent.h_addr_list = static_addr_list;

        return &static_hostent;
    }

    unsigned int ip = 0;
    int rc = (int)__syscall2(SYS_DNS_RESOLVE, (long)name, (long)&ip);

    if (rc != 0)
    {
        h_errno = HOST_NOT_FOUND;
        return (void *)0;
    }

    static_addr.s_addr = ip;
    static_addr_list[0] = (char *)&static_addr;
    static_addr_list[1] = (void *)0;

    size_t len = strlen(name);
    if (len >= sizeof(static_hostname))
        len = sizeof(static_hostname) - 1;
    memcpy(static_hostname, name, len);
    static_hostname[len] = '\0';

    static_hostent.h_name = static_hostname;
    static_hostent.h_aliases = static_alias_list;
    static_hostent.h_addrtype = AF_INET;
    static_hostent.h_length = 4;
    static_hostent.h_addr_list = static_addr_list;

    return &static_hostent;
}

/**
 * @brief Resolve an IP address to a hostname (reverse DNS).
 *
 * @details
 * Performs a reverse DNS lookup, converting an IP address to its associated
 * hostname. This is the inverse of gethostbyname().
 *
 * @note Not implemented in ViperDOS. Always returns NULL with h_errno set
 * to NO_DATA.
 *
 * @param addr Pointer to the address to look up (struct in_addr for IPv4).
 * @param len Size of the address structure.
 * @param type Address family (AF_INET or AF_INET6).
 * @return NULL (not implemented).
 *
 * @see gethostbyname, getnameinfo
 */
struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type)
{
    /* Reverse DNS not implemented */
    (void)addr;
    (void)len;
    (void)type;
    h_errno = NO_DATA;
    return (void *)0;
}

/**
 * @brief Get next entry from the hosts database.
 *
 * @details
 * In a traditional Unix system, this function reads the next entry from
 * /etc/hosts. ViperDOS does not maintain a hosts database file.
 *
 * @note Not implemented. Always returns NULL.
 *
 * @return NULL (not implemented).
 *
 * @see sethostent, endhostent
 */
struct hostent *gethostent(void)
{
    return (void *)0;
}

/**
 * @brief Open or rewind the hosts database.
 *
 * @details
 * In a traditional Unix system, this function opens /etc/hosts or rewinds
 * it to the beginning if already open. If stayopen is non-zero, the file
 * remains open between gethostent() calls.
 *
 * @note Not implemented in ViperDOS.
 *
 * @param stayopen If non-zero, keep the database open between calls.
 *
 * @see gethostent, endhostent
 */
void sethostent(int stayopen)
{
    (void)stayopen;
}

/**
 * @brief Close the hosts database.
 *
 * @details
 * In a traditional Unix system, this function closes /etc/hosts.
 *
 * @note Not implemented in ViperDOS.
 *
 * @see gethostent, sethostent
 */
void endhostent(void) {}

/**
 * @brief Thread-safe hostname resolution.
 *
 * @details
 * Reentrant version of gethostbyname(). Instead of using static storage,
 * the caller provides buffers for the result. This function is thread-safe.
 *
 * This implementation wraps the non-thread-safe gethostbyname() and copies
 * the result to the caller-provided buffers. True thread safety would
 * require a thread-safe DNS implementation.
 *
 * @param name Hostname to resolve.
 * @param ret Caller-provided hostent structure to fill in.
 * @param buf Buffer for storing strings and address data.
 * @param buflen Size of the buffer.
 * @param result Pointer set to ret on success, or NULL on failure.
 * @param h_errnop Pointer to receive the h_errno value on error.
 * @return 0 on success, ERANGE if buffer too small, -1 on lookup failure.
 *
 * @see gethostbyname, getaddrinfo
 */
int gethostbyname_r(const char *name,
                    struct hostent *ret,
                    char *buf,
                    size_t buflen,
                    struct hostent **result,
                    int *h_errnop)
{
    /* Simplified implementation - use gethostbyname and copy */
    struct hostent *he = gethostbyname(name);
    if (!he)
    {
        if (h_errnop)
            *h_errnop = h_errno;
        *result = (void *)0;
        return -1;
    }

    /* Check buffer size */
    size_t name_len = strlen(he->h_name) + 1;
    size_t needed = name_len + sizeof(struct in_addr) + 2 * sizeof(char *);
    if (buflen < needed)
    {
        *result = (void *)0;
        return ERANGE;
    }

    /* Copy data to provided buffer */
    char *ptr = buf;

    /* Copy name */
    ret->h_name = ptr;
    memcpy(ptr, he->h_name, name_len);
    ptr += name_len;

    /* Copy address */
    memcpy(ptr, he->h_addr_list[0], he->h_length);
    char *addr_ptr = ptr;
    ptr += he->h_length;

    /* Set up address list in buffer */
    char **addr_list = (char **)ptr;
    addr_list[0] = addr_ptr;
    addr_list[1] = (void *)0;
    ret->h_addr_list = addr_list;

    ret->h_aliases = static_alias_list;
    ret->h_addrtype = he->h_addrtype;
    ret->h_length = he->h_length;

    *result = ret;
    return 0;
}

/* Service lookup - simplified static table */
static struct
{
    const char *name;
    int port;
    const char *proto;
} known_services[] = {{"http", 80, "tcp"},
                      {"https", 443, "tcp"},
                      {"ftp", 21, "tcp"},
                      {"ssh", 22, "tcp"},
                      {"telnet", 23, "tcp"},
                      {"smtp", 25, "tcp"},
                      {"dns", 53, "udp"},
                      {"domain", 53, "udp"},
                      {"ntp", 123, "udp"},
                      {(void *)0, 0, (void *)0}};

static struct servent static_servent;
static char static_servname[64];
static char static_proto[16];
static char *static_serv_aliases[1] = {(void *)0};

/** @} */ /* end of hostlookup group */

/**
 * @defgroup servicelookup Service Name Lookup
 * @brief Functions for mapping service names to port numbers.
 * @{
 */

/**
 * @brief Look up a network service by name.
 *
 * @details
 * Searches for a service entry matching the given name and protocol.
 * ViperDOS uses a built-in table of common services (http, https, ssh,
 * ftp, smtp, dns, telnet, ntp) rather than reading from /etc/services.
 *
 * @param name Service name to look up (e.g., "http", "ssh").
 * @param proto Protocol name filter (e.g., "tcp", "udp"), or NULL for any.
 * @return Pointer to static servent structure on success, or NULL if not found.
 *
 * @warning The returned structure uses static storage and is overwritten
 * by subsequent calls to this function or getservbyport().
 *
 * @see getservbyport, getaddrinfo
 */
struct servent *getservbyname(const char *name, const char *proto)
{
    for (int i = 0; known_services[i].name; i++)
    {
        if (strcmp(known_services[i].name, name) == 0)
        {
            if (proto && strcmp(known_services[i].proto, proto) != 0)
                continue;

            strncpy(static_servname, name, sizeof(static_servname) - 1);
            strncpy(static_proto, known_services[i].proto, sizeof(static_proto) - 1);

            static_servent.s_name = static_servname;
            static_servent.s_aliases = static_serv_aliases;
            static_servent.s_port = htons(known_services[i].port);
            static_servent.s_proto = static_proto;

            return &static_servent;
        }
    }
    return (void *)0;
}

/**
 * @brief Look up a network service by port number.
 *
 * @details
 * Searches for a service entry matching the given port number and protocol.
 * ViperDOS uses a built-in table of common services rather than reading
 * from /etc/services.
 *
 * @param port Port number in network byte order.
 * @param proto Protocol name filter (e.g., "tcp", "udp"), or NULL for any.
 * @return Pointer to static servent structure on success, or NULL if not found.
 *
 * @warning The returned structure uses static storage and is overwritten
 * by subsequent calls to this function or getservbyname().
 *
 * @see getservbyname, getnameinfo
 */
struct servent *getservbyport(int port, const char *proto)
{
    int host_port = ntohs(port);
    for (int i = 0; known_services[i].name; i++)
    {
        if (known_services[i].port == host_port)
        {
            if (proto && strcmp(known_services[i].proto, proto) != 0)
                continue;

            strncpy(static_servname, known_services[i].name, sizeof(static_servname) - 1);
            strncpy(static_proto, known_services[i].proto, sizeof(static_proto) - 1);

            static_servent.s_name = static_servname;
            static_servent.s_aliases = static_serv_aliases;
            static_servent.s_port = port;
            static_servent.s_proto = static_proto;

            return &static_servent;
        }
    }
    return (void *)0;
}

/**
 * @brief Get next entry from the services database.
 *
 * @details
 * In a traditional Unix system, this function reads the next entry from
 * /etc/services. ViperDOS does not support iterating the services database.
 *
 * @note Not implemented. Always returns NULL.
 *
 * @return NULL (not implemented).
 *
 * @see setservent, endservent
 */
struct servent *getservent(void)
{
    return (void *)0;
}

/**
 * @brief Open or rewind the services database.
 *
 * @details
 * In a traditional Unix system, this function opens /etc/services or
 * rewinds it to the beginning if already open.
 *
 * @note Not implemented in ViperDOS.
 *
 * @param stayopen If non-zero, keep the database open between calls.
 *
 * @see getservent, endservent
 */
void setservent(int stayopen)
{
    (void)stayopen;
}

/**
 * @brief Close the services database.
 *
 * @note Not implemented in ViperDOS.
 *
 * @see getservent, setservent
 */
void endservent(void) {}

/** @} */ /* end of servicelookup group */

/**
 * @defgroup protolookup Protocol Lookup
 * @brief Functions for mapping protocol names to numbers.
 * @{
 */
static struct
{
    const char *name;
    int number;
} known_protos[] = {{"ip", 0}, {"icmp", 1}, {"tcp", 6}, {"udp", 17}, {(void *)0, 0}};

static struct protoent static_protoent;
static char static_protoname[32];
static char *static_proto_aliases[1] = {(void *)0};

/**
 * @brief Look up a protocol by name.
 *
 * @details
 * Searches for a protocol entry matching the given name. ViperDOS uses
 * a built-in table of common protocols (ip, icmp, tcp, udp) rather than
 * reading from /etc/protocols.
 *
 * @param name Protocol name to look up (e.g., "tcp", "udp", "icmp").
 * @return Pointer to static protoent structure on success, or NULL if not found.
 *
 * @warning The returned structure uses static storage and is overwritten
 * by subsequent calls to this function or getprotobynumber().
 *
 * @see getprotobynumber
 */
struct protoent *getprotobyname(const char *name)
{
    for (int i = 0; known_protos[i].name; i++)
    {
        if (strcmp(known_protos[i].name, name) == 0)
        {
            strncpy(static_protoname, name, sizeof(static_protoname) - 1);
            static_protoent.p_name = static_protoname;
            static_protoent.p_aliases = static_proto_aliases;
            static_protoent.p_proto = known_protos[i].number;
            return &static_protoent;
        }
    }
    return (void *)0;
}

/**
 * @brief Look up a protocol by number.
 *
 * @details
 * Searches for a protocol entry matching the given protocol number.
 * ViperDOS uses a built-in table of common protocols:
 * - 0: IP (Internet Protocol)
 * - 1: ICMP (Internet Control Message Protocol)
 * - 6: TCP (Transmission Control Protocol)
 * - 17: UDP (User Datagram Protocol)
 *
 * @param proto Protocol number to look up.
 * @return Pointer to static protoent structure on success, or NULL if not found.
 *
 * @warning The returned structure uses static storage and is overwritten
 * by subsequent calls to this function or getprotobyname().
 *
 * @see getprotobyname
 */
struct protoent *getprotobynumber(int proto)
{
    for (int i = 0; known_protos[i].name; i++)
    {
        if (known_protos[i].number == proto)
        {
            strncpy(static_protoname, known_protos[i].name, sizeof(static_protoname) - 1);
            static_protoent.p_name = static_protoname;
            static_protoent.p_aliases = static_proto_aliases;
            static_protoent.p_proto = proto;
            return &static_protoent;
        }
    }
    return (void *)0;
}

/**
 * @brief Get next entry from the protocols database.
 *
 * @details
 * In a traditional Unix system, this function reads the next entry from
 * /etc/protocols. ViperDOS does not support iterating the protocols database.
 *
 * @note Not implemented. Always returns NULL.
 *
 * @return NULL (not implemented).
 *
 * @see setprotoent, endprotoent
 */
struct protoent *getprotoent(void)
{
    return (void *)0;
}

/**
 * @brief Open or rewind the protocols database.
 *
 * @details
 * In a traditional Unix system, this function opens /etc/protocols or
 * rewinds it to the beginning if already open.
 *
 * @note Not implemented in ViperDOS.
 *
 * @param stayopen If non-zero, keep the database open between calls.
 *
 * @see getprotoent, endprotoent
 */
void setprotoent(int stayopen)
{
    (void)stayopen;
}

/**
 * @brief Close the protocols database.
 *
 * @note Not implemented in ViperDOS.
 *
 * @see getprotoent, setprotoent
 */
void endprotoent(void) {}

/** @} */ /* end of protolookup group */

/**
 * @defgroup addrinfo Modern Address Resolution
 * @brief Protocol-independent hostname and service resolution.
 *
 * The getaddrinfo() and getnameinfo() functions provide a modern,
 * protocol-independent interface for name resolution, replacing the
 * older gethostbyname() and getservbyname() functions.
 * @{
 */

/**
 * @brief Resolve hostname and service to socket addresses.
 *
 * @details
 * The getaddrinfo() function performs hostname and service name resolution,
 * returning socket address structures suitable for creating a socket and
 * connecting to or binding to.
 *
 * The function can:
 * - Resolve hostnames to IP addresses via DNS
 * - Map service names (e.g., "http") to port numbers
 * - Handle both IPv4 and IPv6 addresses (though ViperDOS only supports IPv4)
 * - Accept numeric addresses and port numbers directly
 *
 * The hints parameter allows the caller to specify preferences for:
 * - Address family (ai_family): AF_INET, AF_INET6, or AF_UNSPEC
 * - Socket type (ai_socktype): SOCK_STREAM, SOCK_DGRAM, etc.
 * - Protocol (ai_protocol): IPPROTO_TCP, IPPROTO_UDP, etc.
 * - Flags (ai_flags): AI_PASSIVE, AI_CANONNAME, AI_NUMERICHOST, etc.
 *
 * @param node Hostname or numeric address string (or NULL for local address).
 * @param service Service name or port number string (or NULL for any port).
 * @param hints Criteria for selecting addresses (or NULL for defaults).
 * @param res Pointer to receive the linked list of results.
 * @return 0 on success, or non-zero EAI_* error code on failure.
 *
 * @note Results must be freed with freeaddrinfo().
 *
 * @see freeaddrinfo, getnameinfo, gai_strerror
 */
int getaddrinfo(const char *node,
                const char *service,
                const struct addrinfo *hints,
                struct addrinfo **res)
{
    int family = AF_UNSPEC;
    int socktype = 0;
    int protocol = 0;
    int flags = 0;

    if (hints)
    {
        family = hints->ai_family;
        socktype = hints->ai_socktype;
        protocol = hints->ai_protocol;
        flags = hints->ai_flags;
    }

    /* Get port from service */
    in_port_t port = 0;
    if (service)
    {
        /* Try numeric first */
        char *endp;
        long p = strtol(service, &endp, 10);
        if (*endp == '\0' && p >= 0 && p <= 65535)
        {
            port = htons((unsigned short)p);
        }
        else if (!(flags & AI_NUMERICSERV))
        {
            struct servent *se = getservbyname(service, (void *)0);
            if (se)
            {
                port = se->s_port;
            }
            else
            {
                return EAI_SERVICE;
            }
        }
        else
        {
            return EAI_SERVICE;
        }
    }

    /* Get address from node */
    struct in_addr addr;
    addr.s_addr = INADDR_ANY;
    char *canonname = (void *)0;

    if (node)
    {
        /* Try numeric first */
        if (inet_aton(node, &addr))
        {
            /* Numeric address */
        }
        else if (!(flags & AI_NUMERICHOST))
        {
            /* DNS lookup */
            struct hostent *he = gethostbyname(node);
            if (!he)
            {
                return EAI_NONAME;
            }
            memcpy(&addr, he->h_addr, sizeof(addr));
            if (flags & AI_CANONNAME)
            {
                canonname = he->h_name;
            }
        }
        else
        {
            return EAI_NONAME;
        }
    }
    else if (flags & AI_PASSIVE)
    {
        addr.s_addr = INADDR_ANY;
    }
    else
    {
        addr.s_addr = htonl(INADDR_LOOPBACK);
    }

    /* IPv4 only for now */
    if (family != AF_UNSPEC && family != AF_INET)
    {
        return EAI_FAMILY;
    }

    /* Allocate result */
    struct addrinfo *ai = (struct addrinfo *)malloc(sizeof(struct addrinfo));
    if (!ai)
    {
        return EAI_MEMORY;
    }

    struct sockaddr_in *sin = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    if (!sin)
    {
        free(ai);
        return EAI_MEMORY;
    }

    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_port = port;
    sin->sin_addr = addr;

    ai->ai_flags = flags;
    ai->ai_family = AF_INET;
    ai->ai_socktype = socktype ? socktype : SOCK_STREAM;
    ai->ai_protocol = protocol;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    ai->ai_addr = (struct sockaddr *)sin;
    ai->ai_canonname = (void *)0;
    ai->ai_next = (void *)0;

    if (canonname)
    {
        size_t len = strlen(canonname) + 1;
        ai->ai_canonname = (char *)malloc(len);
        if (ai->ai_canonname)
        {
            memcpy(ai->ai_canonname, canonname, len);
        }
    }

    *res = ai;
    return 0;
}

/**
 * @brief Free address information returned by getaddrinfo().
 *
 * @details
 * Frees the memory allocated by a successful call to getaddrinfo().
 * This function walks the linked list of addrinfo structures and
 * frees each one, including any associated memory for socket addresses
 * and canonical names.
 *
 * @param res Pointer to the addrinfo list to free (may be NULL).
 *
 * @see getaddrinfo
 */
void freeaddrinfo(struct addrinfo *res)
{
    while (res)
    {
        struct addrinfo *next = res->ai_next;
        if (res->ai_addr)
            free(res->ai_addr);
        if (res->ai_canonname)
            free(res->ai_canonname);
        free(res);
        res = next;
    }
}

/**
 * @brief Convert socket address to host and service names.
 *
 * @details
 * The getnameinfo() function is the inverse of getaddrinfo(). It converts
 * a socket address to a host name and service name string.
 *
 * The function can perform:
 * - Reverse DNS lookup (address to hostname)
 * - Port number to service name mapping
 * - Or return numeric strings if requested or if lookup fails
 *
 * Flags control the behavior:
 * - NI_NUMERICHOST: Return numeric address string instead of hostname
 * - NI_NUMERICSERV: Return numeric port instead of service name
 * - NI_DGRAM: Service is a datagram service (affects service lookup)
 * - NI_NAMEREQD: Return error if hostname cannot be resolved
 *
 * @note Reverse DNS is not implemented in ViperDOS. Host lookups will
 * fall back to numeric format.
 *
 * @param addr Socket address to convert.
 * @param addrlen Size of the socket address.
 * @param host Buffer to receive the hostname (or NULL if not needed).
 * @param hostlen Size of the host buffer.
 * @param serv Buffer to receive the service name (or NULL if not needed).
 * @param servlen Size of the service buffer.
 * @param flags Control flags (NI_NUMERICHOST, NI_NUMERICSERV, etc.).
 * @return 0 on success, or non-zero EAI_* error code on failure.
 *
 * @see getaddrinfo, gai_strerror
 */
int getnameinfo(const struct sockaddr *addr,
                socklen_t addrlen,
                char *host,
                socklen_t hostlen,
                char *serv,
                socklen_t servlen,
                int flags)
{
    (void)addrlen;

    if (addr->sa_family != AF_INET)
    {
        return EAI_FAMILY;
    }

    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;

    /* Host */
    if (host && hostlen > 0)
    {
        if (flags & NI_NUMERICHOST)
        {
            const char *result = inet_ntop(AF_INET, &sin->sin_addr, host, hostlen);
            if (!result)
                return EAI_OVERFLOW;
        }
        else
        {
            /* Reverse DNS not implemented, fall back to numeric */
            const char *result = inet_ntop(AF_INET, &sin->sin_addr, host, hostlen);
            if (!result)
                return EAI_OVERFLOW;
        }
    }

    /* Service */
    if (serv && servlen > 0)
    {
        if (flags & NI_NUMERICSERV)
        {
            snprintf(serv, servlen, "%d", ntohs(sin->sin_port));
        }
        else
        {
            struct servent *se = getservbyport(sin->sin_port, (flags & NI_DGRAM) ? "udp" : "tcp");
            if (se)
            {
                strncpy(serv, se->s_name, servlen - 1);
                serv[servlen - 1] = '\0';
            }
            else
            {
                snprintf(serv, servlen, "%d", ntohs(sin->sin_port));
            }
        }
    }

    return 0;
}

/** @} */ /* end of addrinfo group */

/**
 * @defgroup neterror Network Error Functions
 * @brief Functions for reporting network-related errors.
 * @{
 */

/**
 * @brief Get error message for getaddrinfo() errors.
 *
 * @details
 * Returns a human-readable string describing the error indicated by
 * the error code returned from getaddrinfo() or getnameinfo().
 *
 * Common error codes include:
 * - EAI_NONAME: Node or service not known
 * - EAI_AGAIN: Temporary failure in name resolution
 * - EAI_FAIL: Non-recoverable failure in name resolution
 * - EAI_MEMORY: Memory allocation failure
 * - EAI_SERVICE: Service not supported for socket type
 * - EAI_FAMILY: Address family not supported
 *
 * @param errcode Error code from getaddrinfo() or getnameinfo().
 * @return Pointer to a static string describing the error.
 *
 * @see getaddrinfo, getnameinfo
 */
const char *gai_strerror(int errcode)
{
    if (errcode >= 0)
        return gai_errmsgs[0];
    int idx = -errcode;
    if (idx < (int)(sizeof(gai_errmsgs) / sizeof(gai_errmsgs[0])))
        return gai_errmsgs[idx];
    return "Unknown error";
}

/**
 * @brief Print a host lookup error message to stderr.
 *
 * @details
 * Prints a message to stderr describing the most recent host lookup error
 * (stored in the global h_errno variable). If s is non-NULL and non-empty,
 * it is printed followed by ": " before the error description.
 *
 * This function is analogous to perror() for errno, but for host lookup
 * errors from gethostbyname() and gethostbyaddr().
 *
 * @param s Optional prefix string (or NULL for no prefix).
 *
 * @see hstrerror, perror, gethostbyname
 */
void herror(const char *s)
{
    if (s && *s)
    {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs(hstrerror(h_errno), stderr);
    fputc('\n', stderr);
}

/**
 * @brief Get error message for host lookup errors.
 *
 * @details
 * Returns a human-readable string describing the host lookup error code.
 * This is typically used with the h_errno global variable after a failed
 * call to gethostbyname() or gethostbyaddr().
 *
 * Error codes include:
 * - HOST_NOT_FOUND: The specified host is unknown
 * - TRY_AGAIN: Temporary error; try again later
 * - NO_RECOVERY: Non-recoverable error
 * - NO_DATA: Host exists but has no address data
 *
 * @param err Host error code (typically from h_errno).
 * @return Pointer to a static string describing the error.
 *
 * @see herror, gethostbyname, gethostbyaddr
 */
const char *hstrerror(int err)
{
    switch (err)
    {
        case 0:
            return "No error";
        case HOST_NOT_FOUND:
            return "Host not found";
        case TRY_AGAIN:
            return "Try again";
        case NO_RECOVERY:
            return "Non-recoverable error";
        case NO_DATA:
            return "No data";
        default:
            return "Unknown error";
    }
}

/** @} */ /* end of neterror group */
