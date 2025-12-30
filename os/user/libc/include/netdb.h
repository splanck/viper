#ifndef _NETDB_H
#define _NETDB_H

#include "sys/socket.h"
#include "netinet/in.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Host entry structure */
struct hostent {
    char  *h_name;       /* Official name of host */
    char **h_aliases;    /* Alias list */
    int    h_addrtype;   /* Host address type */
    int    h_length;     /* Length of address */
    char **h_addr_list;  /* List of addresses */
};
#define h_addr h_addr_list[0]  /* First address */

/* Service entry structure */
struct servent {
    char  *s_name;       /* Official service name */
    char **s_aliases;    /* Alias list */
    int    s_port;       /* Port number (network byte order) */
    char  *s_proto;      /* Protocol to use */
};

/* Protocol entry structure */
struct protoent {
    char  *p_name;       /* Official protocol name */
    char **p_aliases;    /* Alias list */
    int    p_proto;      /* Protocol number */
};

/* Address info structure for getaddrinfo */
struct addrinfo {
    int              ai_flags;     /* Input flags */
    int              ai_family;    /* Address family */
    int              ai_socktype;  /* Socket type */
    int              ai_protocol;  /* Protocol */
    socklen_t        ai_addrlen;   /* Length of socket address */
    struct sockaddr *ai_addr;      /* Socket address */
    char            *ai_canonname; /* Canonical name */
    struct addrinfo *ai_next;      /* Next structure in list */
};

/* ai_flags values */
#define AI_PASSIVE     0x0001  /* Socket for binding */
#define AI_CANONNAME   0x0002  /* Request canonical name */
#define AI_NUMERICHOST 0x0004  /* Don't use DNS */
#define AI_V4MAPPED    0x0008  /* Map IPv4 to IPv6 */
#define AI_ALL         0x0010  /* Return both IPv4 and IPv6 */
#define AI_ADDRCONFIG  0x0020  /* Only if interface has IPv4/IPv6 */
#define AI_NUMERICSERV 0x0400  /* Service is numeric port */

/* Name info flags */
#define NI_NUMERICHOST 0x0001  /* Return numeric host */
#define NI_NUMERICSERV 0x0002  /* Return numeric service */
#define NI_NOFQDN      0x0004  /* Don't return FQDN */
#define NI_NAMEREQD    0x0008  /* Error if host unknown */
#define NI_DGRAM       0x0010  /* Service is datagram */

/* Maximum lengths */
#define NI_MAXHOST 1025
#define NI_MAXSERV 32

/* Error codes for getaddrinfo/getnameinfo */
#define EAI_AGAIN      -3   /* Try again later */
#define EAI_BADFLAGS   -1   /* Invalid flags */
#define EAI_FAIL       -4   /* Non-recoverable error */
#define EAI_FAMILY     -6   /* Address family not supported */
#define EAI_MEMORY     -10  /* Memory allocation failure */
#define EAI_NONAME     -2   /* Name not known */
#define EAI_SERVICE    -8   /* Service not known */
#define EAI_SOCKTYPE   -7   /* Socket type not supported */
#define EAI_SYSTEM     -11  /* System error in errno */
#define EAI_OVERFLOW   -12  /* Buffer overflow */

/* Host lookup functions */
struct hostent *gethostbyname(const char *name);
struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type);
struct hostent *gethostent(void);
void sethostent(int stayopen);
void endhostent(void);
int gethostbyname_r(const char *name, struct hostent *ret,
                    char *buf, size_t buflen,
                    struct hostent **result, int *h_errnop);

/* Service lookup functions */
struct servent *getservbyname(const char *name, const char *proto);
struct servent *getservbyport(int port, const char *proto);
struct servent *getservent(void);
void setservent(int stayopen);
void endservent(void);

/* Protocol lookup functions */
struct protoent *getprotobyname(const char *name);
struct protoent *getprotobynumber(int proto);
struct protoent *getprotoent(void);
void setprotoent(int stayopen);
void endprotoent(void);

/* Modern address resolution */
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints,
                struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);

/* Reverse lookup */
int getnameinfo(const struct sockaddr *addr, socklen_t addrlen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);

/* Error string */
const char *gai_strerror(int errcode);

/* Herror support */
extern int h_errno;
#define HOST_NOT_FOUND 1
#define TRY_AGAIN      2
#define NO_RECOVERY    3
#define NO_DATA        4
#define NO_ADDRESS     NO_DATA

void herror(const char *s);
const char *hstrerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* _NETDB_H */
