#pragma once

/**
 * @file icmp.hpp
 * @brief ICMP (Internet Control Message Protocol) support for IPv4.
 *
 * @details
 * Provides a small ICMP implementation sufficient for basic diagnostics:
 * - Responds to ICMP Echo Requests (ping) with Echo Replies.
 * - Can transmit Echo Requests and track replies to compute RTT.
 *
 * The implementation is intentionally minimal and does not support all ICMP
 * message types or error reporting semantics.
 */

#include "../net.hpp"

namespace net
{
namespace icmp
{

/**
 * @brief ICMP Echo header used by Echo Request/Reply.
 *
 * @details
 * This structure matches the ICMP header fields used for Echo messages. The
 * `identifier` and `sequence` fields are used to match replies to requests.
 */
struct IcmpHeader
{
    u8 type;
    u8 code;
    u16 checksum;
    u16 identifier;
    u16 sequence;
} __attribute__((packed));

/**
 * @brief ICMP message type constants.
 *
 * @details
 * Only a small subset is used by the current implementation.
 */
namespace type
{
constexpr u8 ECHO_REPLY = 0;
constexpr u8 DEST_UNREACH = 3;
constexpr u8 ECHO_REQUEST = 8;
constexpr u8 TIME_EXCEEDED = 11;
} // namespace type

/**
 * @brief Initialize the ICMP layer.
 *
 * @details
 * Clears internal state used to track outstanding echo requests.
 */
void icmp_init();

/**
 * @brief Process an incoming ICMP message.
 *
 * @details
 * Handles ICMP Echo Requests by generating and sending an Echo Reply, and
 * handles Echo Replies by matching them against the pending request table to
 * compute a round-trip time.
 *
 * @param src Source IPv4 address.
 * @param data Pointer to ICMP message bytes.
 * @param len Length of ICMP message in bytes.
 */
void rx_packet(const Ipv4Addr &src, const void *data, usize len);

/**
 * @brief Send an ICMP Echo Request (ping).
 *
 * @details
 * Allocates a slot from the pending ping table, builds an Echo Request with a
 * small payload pattern, and transmits it to `dst` via the IPv4 layer.
 *
 * If the underlying IPv4 transmit fails due to pending ARP resolution, this
 * function may return a negative error and callers may retry.
 *
 * @param dst Destination IPv4 address.
 * @return The sequence number of the ping on success, or a negative value on error.
 */
i32 send_echo_request(const Ipv4Addr &dst);

/**
 * @brief Check whether an Echo Reply has been received for a given sequence.
 *
 * @details
 * Looks up the pending ping table entry matching `sequence` and returns:
 * - RTT in milliseconds if a reply has been recorded.
 * - -1 if the request is still pending.
 * - -2 if no pending ping with that sequence exists.
 *
 * @param sequence Sequence number returned by @ref send_echo_request.
 * @return RTT in ms on success, -1 if still pending, -2 if not found.
 */
i32 check_echo_reply(u16 sequence);

/**
 * @brief Perform a blocking ping with timeout.
 *
 * @details
 * Attempts to transmit an Echo Request (retrying to handle ARP resolution),
 * then waits until a reply is received or the timeout elapses. The wait uses
 * `wfi` to yield while polling the network.
 *
 * @param dst Destination IPv4 address.
 * @param timeout_ms Timeout in milliseconds.
 * @return RTT in ms on success, or a negative error code on failure/timeout.
 */
i32 ping(const Ipv4Addr &dst, u32 timeout_ms);

} // namespace icmp
} // namespace net
