/**
 * @file dns.cpp
 * @brief DNS resolver implementation.
 *
 * @details
 * Implements a small UDP-based DNS client used by higher-level network code
 * (e.g., HTTP). The resolver supports querying A records and caches results
 * using TTL information from responses.
 */

#include "dns.hpp"
#include "../../arch/aarch64/timer.hpp"
#include "../../console/serial.hpp"
#include "../ip/udp.hpp"
#include "../netif.hpp"
#include "../network.hpp"

namespace net
{
namespace dns
{

// Cache
static CacheEntry cache[DNS_CACHE_SIZE];
static bool initialized = false;

// Query ID counter
static u16 next_query_id = 1;

/**
 * @brief Encode a hostname into DNS wire format (label-length encoding).
 *
 * @details
 * Converts a dotted hostname like `"www.example.com"` into the DNS QNAME
 * encoding:
 * `3 'w' 'w' 'w' 7 'e'... 3 'c''o''m' 0`.
 *
 * The encoding is written to `buffer` and always terminated with a 0-length
 * label. Labels longer than 63 bytes are rejected.
 *
 * @param hostname Input hostname string.
 * @param buffer Output buffer for encoded QNAME.
 * @param max_len Output buffer capacity.
 * @return Number of bytes written, or 0 on error.
 */
static usize encode_hostname(const char *hostname, u8 *buffer, usize max_len)
{
    usize pos = 0;
    usize label_start = 0;

    for (usize i = 0;; i++)
    {
        if (hostname[i] == '.' || hostname[i] == '\0')
        {
            usize label_len = i - label_start;
            if (label_len == 0 || label_len > 63)
            {
                return 0; // Invalid label
            }
            if (pos + 1 + label_len >= max_len)
            {
                return 0; // Buffer overflow
            }

            buffer[pos++] = static_cast<u8>(label_len);
            for (usize j = label_start; j < i; j++)
            {
                buffer[pos++] = hostname[j];
            }

            label_start = i + 1;

            if (hostname[i] == '\0')
            {
                break;
            }
        }
    }

    // Null terminator
    if (pos >= max_len)
    {
        return 0;
    }
    buffer[pos++] = 0;

    return pos;
}

/**
 * @brief Skip over a DNS name in wire format.
 *
 * @details
 * Advances `pos` past a QNAME in `data`, handling both normal labels and
 * compression pointers. This is used when parsing the question/answer sections.
 *
 * @param data DNS message bytes.
 * @param len Total message length.
 * @param pos Starting offset of the name.
 * @return New offset immediately after the name, or `len` on parse error.
 */
static usize skip_name(const u8 *data, usize len, usize pos)
{
    while (pos < len)
    {
        u8 label_len = data[pos];
        if (label_len == 0)
        {
            return pos + 1; // End of name
        }
        if ((label_len & 0xC0) == 0xC0)
        {
            return pos + 2; // Compression pointer
        }
        pos += 1 + label_len;
    }
    return len; // Error
}

/** @copydoc net::dns::dns_init */
void dns_init()
{
    for (usize i = 0; i < DNS_CACHE_SIZE; i++)
    {
        cache[i].valid = false;
    }
    initialized = true;
    serial::puts("[dns] DNS resolver initialized\n");
}

/**
 * @brief Look up a hostname in the DNS cache.
 *
 * @details
 * Searches the cache for a non-expired entry with an exact hostname match.
 * Expired entries are invalidated during the lookup.
 *
 * @param hostname Hostname to search for.
 * @param result Output address on success.
 * @return `true` if a valid cached entry was found, otherwise `false`.
 */
static bool cache_lookup(const char *hostname, Ipv4Addr *result)
{
    u64 now = timer::get_ticks();

    for (usize i = 0; i < DNS_CACHE_SIZE; i++)
    {
        if (!cache[i].valid)
            continue;
        if (cache[i].expires < now)
        {
            cache[i].valid = false;
            continue;
        }

        // Compare hostname
        bool match = true;
        for (usize j = 0; j < 64; j++)
        {
            if (cache[i].hostname[j] != hostname[j])
            {
                match = false;
                break;
            }
            if (hostname[j] == '\0')
                break;
        }

        if (match)
        {
            copy_ip(*result, cache[i].addr);
            return true;
        }
    }
    return false;
}

/**
 * @brief Add a resolved hostname->address mapping to the cache.
 *
 * @details
 * Inserts or replaces a cache entry and records an expiration timestamp based
 * on `ttl` seconds. If the cache is full, the entry with the earliest
 * expiration is replaced.
 *
 * @param hostname Hostname string.
 * @param addr Resolved IPv4 address.
 * @param ttl Time-to-live in seconds.
 */
static void cache_add(const char *hostname, const Ipv4Addr &addr, u32 ttl)
{
    // Find empty slot or oldest entry
    usize slot = 0;
    u64 oldest = ~0ULL;

    for (usize i = 0; i < DNS_CACHE_SIZE; i++)
    {
        if (!cache[i].valid)
        {
            slot = i;
            break;
        }
        if (cache[i].expires < oldest)
        {
            oldest = cache[i].expires;
            slot = i;
        }
    }

    // Copy hostname
    usize j = 0;
    while (hostname[j] && j < 63)
    {
        cache[slot].hostname[j] = hostname[j];
        j++;
    }
    cache[slot].hostname[j] = '\0';

    copy_ip(cache[slot].addr, addr);
    cache[slot].expires = timer::get_ticks() + (ttl * 1000); // Convert seconds to ticks
    cache[slot].valid = true;
}

/** @copydoc net::dns::resolve */
bool resolve(const char *hostname, Ipv4Addr *result, u32 timeout_ms)
{
    if (!initialized || !hostname || !result)
    {
        return false;
    }

    // Check cache first
    if (cache_lookup(hostname, result))
    {
        return true;
    }

    // Build DNS query
    static u8 query[512] __attribute__((aligned(4)));
    DnsHeader *hdr = reinterpret_cast<DnsHeader *>(query);

    u16 query_id = next_query_id++;
    hdr->id = htons(query_id);
    hdr->flags = htons(flags::RD); // Recursion desired
    hdr->qdcount = htons(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;

    // Encode question
    u8 *qname = query + DNS_HEADER_SIZE;
    usize qname_len = encode_hostname(hostname, qname, 256);
    if (qname_len == 0)
    {
        serial::puts("[dns] Invalid hostname\n");
        return false;
    }

    // QTYPE and QCLASS
    u8 *qtype_ptr = qname + qname_len;
    qtype_ptr[0] = 0;
    qtype_ptr[1] = rtype::A; // A record
    qtype_ptr[2] = 0;
    qtype_ptr[3] = rclass::IN; // Internet

    usize query_len = DNS_HEADER_SIZE + qname_len + 4;

    // Create UDP socket
    i32 sock = udp::socket_create();
    if (sock < 0)
    {
        serial::puts("[dns] Failed to create socket\n");
        return false;
    }

    // Bind to ephemeral port
    static u16 next_port = 50000;
    u16 local_port = next_port++;
    if (next_port < 50000)
        next_port = 50000;

    if (!udp::socket_bind(sock, local_port))
    {
        serial::puts("[dns] Failed to bind socket\n");
        udp::socket_close(sock);
        return false;
    }

    // Get DNS server
    Ipv4Addr dns_server = netif().dns();

    // Send query (with ARP retry)
    bool sent = false;
    u64 send_start = timer::get_ticks();
    while (!sent && timer::get_ticks() - send_start < 2000)
    {
        if (udp::send(dns_server, local_port, DNS_PORT, query, query_len))
        {
            sent = true;
        }
        else
        {
            // Wait for ARP to resolve
            for (int i = 0; i < 100; i++)
            {
                network_poll();
                asm volatile("wfi");
            }
        }
    }
    if (!sent)
    {
        serial::puts("[dns] Failed to send query\n");
        udp::socket_close(sock);
        return false;
    }

    // Wait for response
    static u8 response[512];
    u64 start = timer::get_ticks();

    while (timer::get_ticks() - start < timeout_ms)
    {
        Ipv4Addr src_ip;
        u16 src_port;
        i32 len = udp::socket_recv(sock, response, sizeof(response), &src_ip, &src_port);

        if (len > static_cast<i32>(DNS_HEADER_SIZE))
        {
            DnsHeader *resp_hdr = reinterpret_cast<DnsHeader *>(response);

            // Check query ID and response flag
            if (ntohs(resp_hdr->id) == query_id && (ntohs(resp_hdr->flags) & flags::QR))
            {
                u16 ancount = ntohs(resp_hdr->ancount);
                if (ancount > 0)
                {
                    // Skip question section
                    usize pos = DNS_HEADER_SIZE;
                    pos = skip_name(response, len, pos);
                    pos += 4; // QTYPE + QCLASS

                    // Parse answers
                    for (u16 i = 0; i < ancount && pos < static_cast<usize>(len); i++)
                    {
                        // Skip name
                        pos = skip_name(response, len, pos);
                        if (pos + 10 > static_cast<usize>(len))
                            break;

                        u16 rtype_val = (response[pos] << 8) | response[pos + 1];
                        u16 rclass_val = (response[pos + 2] << 8) | response[pos + 3];
                        u32 ttl = (response[pos + 4] << 24) | (response[pos + 5] << 16) |
                                  (response[pos + 6] << 8) | response[pos + 7];
                        u16 rdlength = (response[pos + 8] << 8) | response[pos + 9];
                        pos += 10;

                        if (rtype_val == rtype::A && rclass_val == rclass::IN && rdlength == 4)
                        {
                            // Found A record!
                            result->bytes[0] = response[pos];
                            result->bytes[1] = response[pos + 1];
                            result->bytes[2] = response[pos + 2];
                            result->bytes[3] = response[pos + 3];

                            // Add to cache
                            cache_add(hostname, *result, ttl);

                            udp::socket_close(sock);
                            return true;
                        }

                        pos += rdlength;
                    }
                }
            }
        }

        asm volatile("wfi");
    }

    serial::puts("[dns] Resolution timeout\n");
    udp::socket_close(sock);
    return false;
}

} // namespace dns
} // namespace net
