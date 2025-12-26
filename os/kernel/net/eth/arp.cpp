/**
 * @file arp.cpp
 * @brief ARP implementation (cache + request/reply handling).
 *
 * @details
 * Maintains a small ARP cache and implements the request/reply logic needed to
 * resolve IPv4 next-hop MAC addresses on Ethernet.
 */

#include "arp.hpp"
#include "../../arch/aarch64/timer.hpp"
#include "../../console/serial.hpp"
#include "../netif.hpp"
#include "ethernet.hpp"

namespace net
{
namespace arp
{

/**
 * @brief One ARP cache entry.
 *
 * @details
 * Stores the resolved IPv4->MAC mapping along with a timestamp used for cache
 * expiration.
 */
struct CacheEntry
{
    Ipv4Addr ip;
    MacAddr mac;
    u64 timestamp; // When entry was added (in ticks)
    bool valid;
};

// ARP cache
constexpr usize ARP_CACHE_SIZE = 16;
constexpr u64 ARP_CACHE_TIMEOUT = 300000; // 5 minutes in ms

static CacheEntry cache[ARP_CACHE_SIZE];

/** @copydoc net::arp::arp_init */
void arp_init()
{
    serial::puts("[arp] ARP layer initialized\n");
    for (usize i = 0; i < ARP_CACHE_SIZE; i++)
    {
        cache[i].valid = false;
    }
}

/** @copydoc net::arp::cache_add */
void cache_add(const Ipv4Addr &ip, const MacAddr &mac)
{
    // Check if already in cache
    for (usize i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (cache[i].valid && cache[i].ip == ip)
        {
            copy_mac(cache[i].mac, mac);
            cache[i].timestamp = timer::get_ticks();
            return;
        }
    }

    // Find empty slot or oldest entry
    usize oldest = 0;
    u64 oldest_time = ~0ULL;
    for (usize i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (!cache[i].valid)
        {
            oldest = i;
            break;
        }
        if (cache[i].timestamp < oldest_time)
        {
            oldest_time = cache[i].timestamp;
            oldest = i;
        }
    }

    // Add entry
    copy_ip(cache[oldest].ip, ip);
    copy_mac(cache[oldest].mac, mac);
    cache[oldest].timestamp = timer::get_ticks();
    cache[oldest].valid = true;
}

/** @copydoc net::arp::resolve */
bool resolve(const Ipv4Addr &ip, MacAddr *mac_out)
{
    // Check cache
    u64 now = timer::get_ticks();
    for (usize i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (cache[i].valid && cache[i].ip == ip)
        {
            // Check if entry is still valid
            if (now - cache[i].timestamp < ARP_CACHE_TIMEOUT)
            {
                copy_mac(*mac_out, cache[i].mac);
                return true;
            }
            // Entry expired
            cache[i].valid = false;
        }
    }

    // Not in cache, send ARP request
    send_request(ip);
    return false;
}

/** @copydoc net::arp::send_request */
void send_request(const Ipv4Addr &target_ip)
{
    // Use aligned buffer for ArpHeader to avoid alignment issues
    static u8 arp_buf[sizeof(ArpHeader)] __attribute__((aligned(4)));
    ArpHeader *arp = reinterpret_cast<ArpHeader *>(arp_buf);

    arp->htype = htons(htype::ETHERNET);
    arp->ptype = htons(0x0800); // IPv4
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = htons(oper::REQUEST);

    // Use byte-by-byte copy for MAC/IP addresses (avoid alignment issues)
    MacAddr our_mac = netif().mac();
    Ipv4Addr our_ip = netif().ip();
    MacAddr zero_mac = MacAddr::zero();
    copy_mac(arp->sha, our_mac);
    copy_ip(arp->spa, our_ip);
    copy_mac(arp->tha, zero_mac);
    copy_ip(arp->tpa, target_ip);

    // Send as broadcast
    eth::tx_frame(MacAddr::broadcast(), eth::ethertype::ARP, arp, sizeof(ArpHeader));

    serial::puts("[arp] Sent ARP request for ");
    serial::put_dec(target_ip.bytes[0]);
    serial::putc('.');
    serial::put_dec(target_ip.bytes[1]);
    serial::putc('.');
    serial::put_dec(target_ip.bytes[2]);
    serial::putc('.');
    serial::put_dec(target_ip.bytes[3]);
    serial::puts("\n");
}

/** @copydoc net::arp::rx_packet */
void rx_packet(const void *data, usize len)
{
    if (len < sizeof(ArpHeader))
    {
        return;
    }

    const ArpHeader *arp = static_cast<const ArpHeader *>(data);

    // Validate ARP packet
    if (ntohs(arp->htype) != htype::ETHERNET || ntohs(arp->ptype) != 0x0800 || arp->hlen != 6 ||
        arp->plen != 4)
    {
        return;
    }

    u16 op = ntohs(arp->oper);

    // Copy sender MAC/IP to local aligned variables before using
    MacAddr sender_mac;
    Ipv4Addr sender_ip;
    copy_mac(sender_mac, arp->sha);
    copy_ip(sender_ip, arp->spa);

    // Always add sender to cache (even if not for us)
    cache_add(sender_ip, sender_mac);

    // Check if this is for us
    Ipv4Addr target_ip;
    copy_ip(target_ip, arp->tpa);
    if (target_ip != netif().ip())
    {
        return;
    }

    if (op == oper::REQUEST)
    {
        // Send ARP reply using aligned buffer
        static u8 reply_buf[sizeof(ArpHeader)] __attribute__((aligned(4)));
        ArpHeader *reply = reinterpret_cast<ArpHeader *>(reply_buf);

        reply->htype = htons(htype::ETHERNET);
        reply->ptype = htons(0x0800);
        reply->hlen = 6;
        reply->plen = 4;
        reply->oper = htons(oper::REPLY);

        MacAddr our_mac = netif().mac();
        Ipv4Addr our_ip = netif().ip();
        copy_mac(reply->sha, our_mac);
        copy_ip(reply->spa, our_ip);
        copy_mac(reply->tha, sender_mac);
        copy_ip(reply->tpa, sender_ip);

        eth::tx_frame(sender_mac, eth::ethertype::ARP, reply, sizeof(ArpHeader));

        serial::puts("[arp] Sent ARP reply to ");
        serial::put_dec(sender_ip.bytes[0]);
        serial::putc('.');
        serial::put_dec(sender_ip.bytes[1]);
        serial::putc('.');
        serial::put_dec(sender_ip.bytes[2]);
        serial::putc('.');
        serial::put_dec(sender_ip.bytes[3]);
        serial::puts("\n");
    }
    else if (op == oper::REPLY)
    {
        serial::puts("[arp] Received ARP reply from ");
        serial::put_dec(sender_ip.bytes[0]);
        serial::putc('.');
        serial::put_dec(sender_ip.bytes[1]);
        serial::putc('.');
        serial::put_dec(sender_ip.bytes[2]);
        serial::putc('.');
        serial::put_dec(sender_ip.bytes[3]);
        serial::puts("\n");
    }
}

/** @copydoc net::arp::print_cache */
void print_cache()
{
    serial::puts("[arp] ARP Cache:\n");
    u64 now = timer::get_ticks();
    const char hex[] = "0123456789abcdef";

    for (usize i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (!cache[i].valid)
            continue;
        if (now - cache[i].timestamp >= ARP_CACHE_TIMEOUT)
            continue;

        serial::puts("  ");
        serial::put_dec(cache[i].ip.bytes[0]);
        serial::putc('.');
        serial::put_dec(cache[i].ip.bytes[1]);
        serial::putc('.');
        serial::put_dec(cache[i].ip.bytes[2]);
        serial::putc('.');
        serial::put_dec(cache[i].ip.bytes[3]);
        serial::puts(" -> ");

        for (int j = 0; j < 6; j++)
        {
            if (j > 0)
                serial::putc(':');
            serial::putc(hex[(cache[i].mac.bytes[j] >> 4) & 0xf]);
            serial::putc(hex[cache[i].mac.bytes[j] & 0xf]);
        }

        serial::puts(" (");
        serial::put_dec((now - cache[i].timestamp) / 1000);
        serial::puts("s ago)\n");
    }
}

} // namespace arp
} // namespace net
