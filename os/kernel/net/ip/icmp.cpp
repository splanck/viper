/**
 * @file icmp.cpp
 * @brief ICMP implementation (echo request/reply and ping RTT tracking).
 *
 * @details
 * Implements basic ICMP echo functionality:
 * - Replies to inbound Echo Requests.
 * - Sends Echo Requests and tracks pending pings to compute RTT.
 */

#include "icmp.hpp"
#include "../../arch/aarch64/timer.hpp"
#include "../../console/serial.hpp"
#include "../../sched/scheduler.hpp"
#include "../../sched/task.hpp"
#include "../netif.hpp"
#include "../network.hpp"
#include "ipv4.hpp"

namespace net
{
namespace icmp
{

/**
 * @brief Pending ping state used to match Echo Replies.
 *
 * @details
 * Each entry records the identifier/sequence, send timestamp, and an RTT value
 * set once a reply is received.
 */
struct PendingPing
{
    u16 identifier;
    u16 sequence;
    u64 send_time; // ticks
    i32 rtt;       // -1 = pending, >= 0 = RTT in ms
    bool active;
};

constexpr usize MAX_PENDING_PINGS = 8;
static PendingPing pending_pings[MAX_PENDING_PINGS];
static u16 next_identifier = 1;
static u16 next_sequence = 1;

/** @copydoc net::icmp::icmp_init */
void icmp_init()
{
    serial::puts("[icmp] ICMP layer initialized\n");
    for (usize i = 0; i < MAX_PENDING_PINGS; i++)
    {
        pending_pings[i].active = false;
    }
}

/** @copydoc net::icmp::rx_packet */
void rx_packet(const Ipv4Addr &src, const void *data, usize len)
{
    if (len < sizeof(IcmpHeader))
    {
        return;
    }

    const IcmpHeader *hdr = static_cast<const IcmpHeader *>(data);

    if (hdr->type == type::ECHO_REQUEST)
    {
        // Respond to ping
        serial::puts("[icmp] Echo request from ");
        serial::put_dec(src.bytes[0]);
        serial::putc('.');
        serial::put_dec(src.bytes[1]);
        serial::putc('.');
        serial::put_dec(src.bytes[2]);
        serial::putc('.');
        serial::put_dec(src.bytes[3]);
        serial::puts("\n");

        // Build echo reply
        static u8 reply_buf[64];
        usize reply_len = len > sizeof(reply_buf) ? sizeof(reply_buf) : len;

        // Copy the ICMP packet
        const u8 *src_data = static_cast<const u8 *>(data);
        for (usize i = 0; i < reply_len; i++)
        {
            reply_buf[i] = src_data[i];
        }

        // Change type to reply
        IcmpHeader *reply_hdr = reinterpret_cast<IcmpHeader *>(reply_buf);
        reply_hdr->type = type::ECHO_REPLY;
        reply_hdr->checksum = 0;
        reply_hdr->checksum = checksum(reply_buf, reply_len);

        // Send reply
        ip::tx_packet(src, ip::protocol::ICMP, reply_buf, reply_len);
    }
    else if (hdr->type == type::ECHO_REPLY)
    {
        // Match against pending pings
        u16 id = ntohs(hdr->identifier);
        u16 seq = ntohs(hdr->sequence);

        for (usize i = 0; i < MAX_PENDING_PINGS; i++)
        {
            if (pending_pings[i].active && pending_pings[i].identifier == id &&
                pending_pings[i].sequence == seq)
            {
                u64 now = timer::get_ticks();
                pending_pings[i].rtt = static_cast<i32>(now - pending_pings[i].send_time);
                break;
            }
        }

        serial::puts("[icmp] Echo reply from ");
        serial::put_dec(src.bytes[0]);
        serial::putc('.');
        serial::put_dec(src.bytes[1]);
        serial::putc('.');
        serial::put_dec(src.bytes[2]);
        serial::putc('.');
        serial::put_dec(src.bytes[3]);
        serial::puts(" seq=");
        serial::put_dec(seq);
        serial::puts("\n");
    }
}

/** @copydoc net::icmp::send_echo_request */
i32 send_echo_request(const Ipv4Addr &dst)
{
    // Find free slot
    usize slot = MAX_PENDING_PINGS;
    for (usize i = 0; i < MAX_PENDING_PINGS; i++)
    {
        if (!pending_pings[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot >= MAX_PENDING_PINGS)
    {
        return -1; // No free slots
    }

    // Build echo request
    struct
    {
        IcmpHeader hdr;
        u8 data[32]; // Payload
    } __attribute__((packed)) request;

    request.hdr.type = type::ECHO_REQUEST;
    request.hdr.code = 0;
    request.hdr.checksum = 0;
    request.hdr.identifier = htons(next_identifier);
    request.hdr.sequence = htons(next_sequence);

    // Fill payload with pattern
    for (usize i = 0; i < sizeof(request.data); i++)
    {
        request.data[i] = static_cast<u8>(i);
    }

    // Calculate checksum
    request.hdr.checksum = checksum(&request, sizeof(request));

    // Track pending ping
    pending_pings[slot].identifier = next_identifier;
    pending_pings[slot].sequence = next_sequence;
    pending_pings[slot].send_time = timer::get_ticks();
    pending_pings[slot].rtt = -1;
    pending_pings[slot].active = true;

    u16 seq = next_sequence;
    next_sequence++;

    // Send packet
    if (!ip::tx_packet(dst, ip::protocol::ICMP, &request, sizeof(request)))
    {
        pending_pings[slot].active = false;
        return -2; // Send failed
    }

    serial::puts("[icmp] Sent echo request to ");
    serial::put_dec(dst.bytes[0]);
    serial::putc('.');
    serial::put_dec(dst.bytes[1]);
    serial::putc('.');
    serial::put_dec(dst.bytes[2]);
    serial::putc('.');
    serial::put_dec(dst.bytes[3]);
    serial::puts(" seq=");
    serial::put_dec(seq);
    serial::puts("\n");

    return seq;
}

/** @copydoc net::icmp::check_echo_reply */
i32 check_echo_reply(u16 sequence)
{
    for (usize i = 0; i < MAX_PENDING_PINGS; i++)
    {
        if (pending_pings[i].active && pending_pings[i].sequence == sequence)
        {
            if (pending_pings[i].rtt >= 0)
            {
                // Got reply
                i32 rtt = pending_pings[i].rtt;
                pending_pings[i].active = false;
                return rtt;
            }
            return -1; // Still pending
        }
    }
    return -2; // Not found
}

/** @copydoc net::icmp::ping */
i32 ping(const Ipv4Addr &dst, u32 timeout_ms)
{
    u64 start = timer::get_ticks();

    // Retry send until success or timeout (handles ARP resolution)
    i32 seq = -2;
    while (seq < 0 && (timer::get_ticks() - start < timeout_ms))
    {
        seq = send_echo_request(dst);
        if (seq < 0)
        {
            // Wait for ARP resolution, then retry
            // Poll network to process ARP replies and yield to scheduler
            for (int i = 0; i < 10; i++)
            {
                network_poll();
                task::yield();
            }
        }
    }

    if (seq < 0)
    {
        return seq; // Send never succeeded
    }

    // Wait for reply
    while (timer::get_ticks() - start < timeout_ms)
    {
        // Poll network to receive ICMP reply
        network_poll();

        i32 result = check_echo_reply(static_cast<u16>(seq));
        if (result >= 0)
        {
            return result; // RTT
        }
        if (result == -2)
        {
            return -2; // Not found (shouldn't happen)
        }
        // Yield to scheduler while waiting
        task::yield();
    }

    // Timeout - clean up
    for (usize i = 0; i < MAX_PENDING_PINGS; i++)
    {
        if (pending_pings[i].active && pending_pings[i].sequence == static_cast<u16>(seq))
        {
            pending_pings[i].active = false;
            break;
        }
    }

    return -3; // Timeout
}

} // namespace icmp
} // namespace net
