#pragma once

/**
 * @file dns.hpp
 * @brief DNS resolver (UDP-based) for IPv4 A records.
 *
 * @details
 * Provides a small DNS client suitable for resolving hostnames during early
 * networking bring-up. The resolver:
 * - Constructs a DNS query for an A record.
 * - Sends it to the configured DNS server using UDP.
 * - Parses the response and extracts the first IPv4 A record.
 * - Caches successful results with a TTL-based expiration timestamp.
 *
 * The implementation is intentionally minimal and does not support:
 * - TCP fallback, EDNS0, or large responses.
 * - AAAA records or complex CNAME chaining.
 * - Full RFC-compliant parsing beyond what is required for typical replies.
 */

#include "../net.hpp"

namespace net
{
namespace dns
{

/**
 * @brief DNS message header (12 bytes).
 *
 * @details
 * Matches the DNS wire format header. Fields are encoded in network byte order.
 */
struct DnsHeader
{
    u16 id;      // Query ID
    u16 flags;   // Flags
    u16 qdcount; // Number of questions
    u16 ancount; // Number of answers
    u16 nscount; // Number of authority records
    u16 arcount; // Number of additional records
} __attribute__((packed));

/** @brief Size of the DNS header in bytes. */
constexpr usize DNS_HEADER_SIZE = 12;

/**
 * @brief DNS header flag bits.
 *
 * @details
 * Only a subset is used by the current resolver.
 */
namespace flags
{
constexpr u16 QR = 0x8000; // Query/Response
constexpr u16 AA = 0x0400; // Authoritative Answer
constexpr u16 TC = 0x0200; // Truncated
constexpr u16 RD = 0x0100; // Recursion Desired
constexpr u16 RA = 0x0080; // Recursion Available
} // namespace flags

/** @brief DNS resource record type codes. */
namespace rtype
{
constexpr u16 A = 1;     // IPv4 address
constexpr u16 AAAA = 28; // IPv6 address
constexpr u16 CNAME = 5; // Canonical name
} // namespace rtype

/** @brief DNS class codes. */
namespace rclass
{
constexpr u16 IN = 1; // Internet
}

/** @brief Standard DNS server port number. */
constexpr u16 DNS_PORT = 53;

/**
 * @brief Initialize the DNS resolver and clear the cache.
 *
 * @details
 * Clears the internal cache table. Should be called during network stack
 * initialization before invoking @ref resolve.
 */
void dns_init();

/**
 * @brief Resolve a hostname to an IPv4 address (A record).
 *
 * @details
 * Checks the cache first. On a cache miss, constructs and sends a DNS query via
 * UDP and waits up to `timeout_ms` for a response. The resolver parses the
 * answer section and returns the first A record it finds, caching it according
 * to the returned TTL.
 *
 * This call is blocking in the sense that it waits for a response while polling
 * the network stack. Callers should choose an appropriate timeout.
 *
 * @param hostname NUL-terminated hostname (e.g., "example.com").
 * @param result Output IPv4 address.
 * @param timeout_ms Timeout in milliseconds.
 * @return `true` on success, otherwise `false`.
 */
bool resolve(const char *hostname, Ipv4Addr *result, u32 timeout_ms = 5000);

/**
 * @brief DNS cache entry for A record results.
 *
 * @details
 * Stores the hostname, resolved address, and an expiration tick timestamp.
 */
struct CacheEntry
{
    char hostname[64];
    Ipv4Addr addr;
    u64 expires;
    bool valid;
};

/** @brief Maximum number of cached DNS entries. */
constexpr usize DNS_CACHE_SIZE = 16;

} // namespace dns
} // namespace net
