//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_network_dns.c
// Purpose: DNS resolution and IP address utilities for the Viper runtime.
//   Provides forward/reverse DNS lookup, IP address validation, and local
//   network address discovery.
//
// Key invariants:
//   - DNS resolution uses getaddrinfo/getnameinfo (cross-platform).
//   - IPv4/IPv6 validation uses simple parsing (no regex dependency).
//   - Local address discovery uses getifaddrs (Unix) or GetAdaptersAddresses (Win).
//   - All returned strings are GC-managed rt_string objects.
//
// Ownership/Lifetime:
//   - Returned strings and sequences are GC-managed.
//   - getaddrinfo results are freed via freeaddrinfo within each function.
//
// Links: src/runtime/network/rt_network_internal.h (platform abstractions),
//        src/runtime/network/rt_network.c (TCP + platform init),
//        src/runtime/network/rt_network.h (public API)
//
//===----------------------------------------------------------------------===//

// Platform feature macros must appear before ANY includes
#if !defined(_WIN32)
#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1
#endif

#include "rt_network_internal.h"

#include "rt_map.h"

//=============================================================================
// DNS Resolution - Static Utility Functions
//=============================================================================

/// @brief Check if a string is a valid IPv4 address (without DNS lookup).
/// @details Parses dotted decimal format: four octets 0-255 separated by dots.
static bool parse_ipv4(const char *addr) {
    if (!addr || !*addr)
        return false;

    int parts = 0;
    int value = 0;
    bool has_digit = false;

    for (const char *p = addr; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            value = value * 10 + (*p - '0');
            if (value > 255)
                return false;
            has_digit = true;
        } else if (*p == '.') {
            if (!has_digit || parts >= 3)
                return false;
            parts++;
            value = 0;
            has_digit = false;
        } else {
            return false;
        }
    }

    return has_digit && parts == 3;
}

/// @brief Check if a string is a valid IPv6 address (without DNS lookup).
/// @details Parses colon hex format with :: for zero compression.
static bool parse_ipv6(const char *addr) {
    if (!addr || !*addr)
        return false;

    // Use inet_pton for validation - it's portable and handles all cases
    struct in6_addr result;
    return inet_pton(AF_INET6, addr, &result) == 1;
}

rt_string rt_dns_resolve(rt_string hostname) {
    rt_net_init_wsa();

    const char *host_ptr = rt_string_cstr(hostname);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: NULL hostname");

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET; // IPv4 only for Resolve()
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    int ret = getaddrinfo(host_ptr, NULL, &hints, &result);
    if (ret != 0 || !result) {
        if (result)
            freeaddrinfo(result);
        rt_trap_net("Network: hostname not found", Err_DnsError);
    }

    char ip_str[INET_ADDRSTRLEN];
    struct sockaddr_in *addr = (struct sockaddr_in *)result->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));

    freeaddrinfo(result);
    return rt_string_from_bytes(ip_str, strlen(ip_str));
}

void *rt_dns_resolve_all(rt_string hostname) {
    rt_net_init_wsa();

    const char *host_ptr = rt_string_cstr(hostname);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: NULL hostname");

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC; // Both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    int ret = getaddrinfo(host_ptr, NULL, &hints, &result);
    if (ret != 0 || !result) {
        if (result)
            freeaddrinfo(result);
        rt_trap_net("Network: hostname not found", Err_DnsError);
    }

    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);

    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        char ip_str[INET6_ADDRSTRLEN];

        if (rp->ai_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)rp->ai_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
        } else if (rp->ai_family == AF_INET6) {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *)rp->ai_addr;
            inet_ntop(AF_INET6, &addr->sin6_addr, ip_str, sizeof(ip_str));
        } else {
            continue;
        }

        rt_string addr_str = rt_string_from_bytes(ip_str, strlen(ip_str));
        rt_seq_push(seq, (void *)addr_str);
        rt_str_release_maybe(addr_str);
    }

    freeaddrinfo(result);
    return seq;
}

rt_string rt_dns_resolve4(rt_string hostname) {
    rt_net_init_wsa();

    const char *host_ptr = rt_string_cstr(hostname);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: NULL hostname");

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET; // IPv4 only
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    int ret = getaddrinfo(host_ptr, NULL, &hints, &result);
    if (ret != 0 || !result) {
        if (result)
            freeaddrinfo(result);
        rt_trap_net("Network: no IPv4 address found", Err_DnsError);
    }

    char ip_str[INET_ADDRSTRLEN];
    struct sockaddr_in *addr = (struct sockaddr_in *)result->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));

    freeaddrinfo(result);
    return rt_string_from_bytes(ip_str, strlen(ip_str));
}

rt_string rt_dns_resolve6(rt_string hostname) {
    rt_net_init_wsa();

    const char *host_ptr = rt_string_cstr(hostname);
    if (!host_ptr || *host_ptr == '\0')
        rt_trap("Network: NULL hostname");

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET6; // IPv6 only
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    int ret = getaddrinfo(host_ptr, NULL, &hints, &result);
    if (ret != 0 || !result) {
        if (result)
            freeaddrinfo(result);
        rt_trap_net("Network: no IPv6 address found", Err_DnsError);
    }

    char ip_str[INET6_ADDRSTRLEN];
    struct sockaddr_in6 *addr = (struct sockaddr_in6 *)result->ai_addr;
    inet_ntop(AF_INET6, &addr->sin6_addr, ip_str, sizeof(ip_str));

    freeaddrinfo(result);
    return rt_string_from_bytes(ip_str, strlen(ip_str));
}

rt_string rt_dns_reverse(rt_string ip_address) {
    rt_net_init_wsa();

    const char *addr_ptr = rt_string_cstr(ip_address);
    if (!addr_ptr || *addr_ptr == '\0')
        rt_trap("Network: NULL address");

    // Try IPv4 first
    struct sockaddr_in sa4;
    struct sockaddr_in6 sa6;
    struct sockaddr *sa = NULL;
    socklen_t sa_len = 0;

    if (inet_pton(AF_INET, addr_ptr, &sa4.sin_addr) == 1) {
        sa4.sin_family = AF_INET;
        sa4.sin_port = 0;
        sa = (struct sockaddr *)&sa4;
        sa_len = sizeof(sa4);
    } else if (inet_pton(AF_INET6, addr_ptr, &sa6.sin6_addr) == 1) {
        sa6.sin6_family = AF_INET6;
        sa6.sin6_port = 0;
        sa6.sin6_flowinfo = 0;
        sa6.sin6_scope_id = 0;
        sa = (struct sockaddr *)&sa6;
        sa_len = sizeof(sa6);
    } else {
        rt_trap_net("Network: invalid IP address", Err_InvalidUrl);
    }

    char host[NI_MAXHOST];
    int ret = getnameinfo(sa, sa_len, host, sizeof(host), NULL, 0, NI_NAMEREQD);
    if (ret != 0) {
        rt_trap_net("Network: reverse lookup failed", Err_DnsError);
    }

    return rt_string_from_bytes(host, strlen(host));
}

int8_t rt_dns_is_ipv4(rt_string address) {
    const char *addr_ptr = rt_string_cstr(address);
    if (!addr_ptr || *addr_ptr == '\0')
        return 0;

    return parse_ipv4(addr_ptr) ? 1 : 0;
}

int8_t rt_dns_is_ipv6(rt_string address) {
    const char *addr_ptr = rt_string_cstr(address);
    if (!addr_ptr || *addr_ptr == '\0')
        return 0;

    return parse_ipv6(addr_ptr) ? 1 : 0;
}

int8_t rt_dns_is_ip(rt_string address) {
    return rt_dns_is_ipv4(address) || rt_dns_is_ipv6(address);
}

rt_string rt_dns_local_host(void) {
    rt_net_init_wsa();

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        rt_trap_net("Network: failed to get hostname", Err_DnsError);
    }

    return rt_string_from_bytes(hostname, strlen(hostname));
}

void *rt_dns_local_addrs(void) {
    rt_net_init_wsa();

    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);

#ifdef _WIN32
    // Windows: use GetAdaptersAddresses for complete interface enumeration
    ULONG bufLen = 15000;
    PIP_ADAPTER_ADDRESSES addrs = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
    if (!addrs)
        return seq;

    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, addrs, &bufLen);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        free(addrs);
        addrs = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
        if (!addrs)
            return seq;
        ret = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, addrs, &bufLen);
    }
    if (ret != NO_ERROR) {
        free(addrs);
        return seq;
    }

    for (PIP_ADAPTER_ADDRESSES adapter = addrs; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp)
            continue;

        for (PIP_ADAPTER_UNICAST_ADDRESS ua = adapter->FirstUnicastAddress; ua; ua = ua->Next) {
            char ip_str[INET6_ADDRSTRLEN];
            int family = ua->Address.lpSockaddr->sa_family;

            if (family == AF_INET) {
                struct sockaddr_in *addr = (struct sockaddr_in *)ua->Address.lpSockaddr;
                inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
            } else if (family == AF_INET6) {
                struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ua->Address.lpSockaddr;
                inet_ntop(AF_INET6, &addr->sin6_addr, ip_str, sizeof(ip_str));
            } else {
                continue;
            }

            rt_string addr_str = rt_string_from_bytes(ip_str, strlen(ip_str));
            rt_seq_push(seq, (void *)addr_str);
            rt_str_release_maybe(addr_str);
        }
    }

    free(addrs);
#elif defined(__viperdos__)
    // ViperDOS does not provide getifaddrs(); return empty list.
    (void)seq;
#else
    // Unix: use getifaddrs for local addresses
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1)
        return seq;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        char ip_str[INET6_ADDRSTRLEN];
        int family = ifa->ifa_addr->sa_family;

        if (family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
        } else if (family == AF_INET6) {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ifa->ifa_addr;
            inet_ntop(AF_INET6, &addr->sin6_addr, ip_str, sizeof(ip_str));
        } else {
            continue;
        }

        rt_string addr_str = rt_string_from_bytes(ip_str, strlen(ip_str));
        rt_seq_push(seq, (void *)addr_str);
        rt_str_release_maybe(addr_str);
    }

    freeifaddrs(ifaddr);
#endif

    return seq;
}
