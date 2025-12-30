#include "../include/netdb.h"
#include "../include/arpa/inet.h"
#include "../include/errno.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"

/* Syscall helpers */
extern long __syscall2(long num, long arg0, long arg1);

/* Syscall number for DNS resolve */
#define SYS_DNS_RESOLVE 0xD0

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

/*
 * gethostbyname - Resolve hostname to address
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

    /* Use kernel DNS resolver */
    unsigned int ip = 0;
    long result = __syscall2(SYS_DNS_RESOLVE, (long)name, (long)&ip);
    if (result < 0)
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

/*
 * gethostbyaddr - Reverse lookup
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

struct hostent *gethostent(void)
{
    return (void *)0;
}

void sethostent(int stayopen)
{
    (void)stayopen;
}

void endhostent(void) {}

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

struct servent *getservent(void)
{
    return (void *)0;
}

void setservent(int stayopen)
{
    (void)stayopen;
}

void endservent(void) {}

/* Protocol lookup */
static struct
{
    const char *name;
    int number;
} known_protos[] = {{"ip", 0}, {"icmp", 1}, {"tcp", 6}, {"udp", 17}, {(void *)0, 0}};

static struct protoent static_protoent;
static char static_protoname[32];
static char *static_proto_aliases[1] = {(void *)0};

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

struct protoent *getprotoent(void)
{
    return (void *)0;
}

void setprotoent(int stayopen)
{
    (void)stayopen;
}

void endprotoent(void) {}

/*
 * getaddrinfo - Modern address resolution
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

/*
 * freeaddrinfo - Free result from getaddrinfo
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

/*
 * getnameinfo - Reverse lookup
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

/*
 * gai_strerror - Error string for getaddrinfo errors
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

/*
 * herror - Print host lookup error
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

/*
 * hstrerror - Host error string
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
