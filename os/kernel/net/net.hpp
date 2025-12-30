#pragma once

#include "../include/types.hpp"

namespace net
{

/**
 * @file net.hpp
 * @brief Core network types and low-level helpers.
 *
 * @details
 * This header provides small, freestanding building blocks used throughout the
 * ViperOS network stack:
 * - Basic address types (@ref MacAddr and @ref Ipv4Addr) with convenience
 *   helpers and comparisons.
 * - Byte-copy helpers (@ref copy_mac, @ref copy_ip) to avoid alignment issues
 *   when reading packed on-the-wire structures.
 * - Endianness conversion helpers (@ref htons, @ref ntohs, @ref htonl, @ref ntohl).
 * - The Internet checksum routine used by IPv4/ICMP/UDP/TCP.
 *
 * The network stack targets a freestanding kernel environment and therefore
 * does not rely on libc for these primitives.
 */

/**
 * @brief Ethernet MAC address (48-bit).
 *
 * @details
 * A MAC address is represented as 6 bytes in the order used on the wire. The
 * type is packed so it can appear inside protocol headers.
 */
struct MacAddr
{
    u8 bytes[6];

    /**
     * @brief Compare two MAC addresses for equality.
     *
     * @details
     * Performs a byte-wise comparison across all six octets.
     *
     * @param other Address to compare against.
     * @return `true` if all bytes match, otherwise `false`.
     */
    bool operator==(const MacAddr &other) const
    {
        for (int i = 0; i < 6; i++)
        {
            if (bytes[i] != other.bytes[i])
                return false;
        }
        return true;
    }

    /**
     * @brief Check whether this address is the broadcast MAC (FF:FF:FF:FF:FF:FF).
     *
     * @return `true` if all bytes are `0xFF`, otherwise `false`.
     */
    bool is_broadcast() const
    {
        return bytes[0] == 0xff && bytes[1] == 0xff && bytes[2] == 0xff && bytes[3] == 0xff &&
               bytes[4] == 0xff && bytes[5] == 0xff;
    }

    /**
     * @brief Check whether this address is a multicast address.
     *
     * @details
     * Ethernet multicast addresses are identified by the least-significant bit
     * of the first octet being set.
     *
     * @return `true` if the address is multicast, otherwise `false`.
     */
    bool is_multicast() const
    {
        return (bytes[0] & 0x01) != 0;
    }

    /**
     * @brief Get the broadcast MAC address constant.
     *
     * @return Broadcast MAC address.
     */
    static MacAddr broadcast()
    {
        return {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    }

    /**
     * @brief Get the all-zero MAC address constant.
     *
     * @details
     * Often used as a placeholder or "unknown" value.
     *
     * @return Zero MAC address.
     */
    static MacAddr zero()
    {
        return {{0, 0, 0, 0, 0, 0}};
    }
} __attribute__((packed));

/**
 * @brief IPv4 address (32-bit).
 *
 * @details
 * The address is stored as four bytes in network order (a.b.c.d). The struct is
 * packed so it can be embedded inside protocol headers without padding.
 */
struct Ipv4Addr
{
    u8 bytes[4];

    /**
     * @brief Compare two IPv4 addresses for equality.
     *
     * @param other Address to compare against.
     * @return `true` if all octets match, otherwise `false`.
     */
    bool operator==(const Ipv4Addr &other) const
    {
        return bytes[0] == other.bytes[0] && bytes[1] == other.bytes[1] &&
               bytes[2] == other.bytes[2] && bytes[3] == other.bytes[3];
    }

    /**
     * @brief Inequality operator.
     *
     * @param other Address to compare against.
     * @return `true` if the addresses differ, otherwise `false`.
     */
    bool operator!=(const Ipv4Addr &other) const
    {
        return !(*this == other);
    }

    /**
     * @brief Convert the address to a 32-bit integer.
     *
     * @details
     * Packs the octets into a 32-bit value in big-endian order:
     * `a.b.c.d` becomes `0xAABBCCDD`.
     *
     * @return Packed address value.
     */
    u32 to_u32() const
    {
        return (static_cast<u32>(bytes[0]) << 24) | (static_cast<u32>(bytes[1]) << 16) |
               (static_cast<u32>(bytes[2]) << 8) | static_cast<u32>(bytes[3]);
    }

    /**
     * @brief Construct an IPv4 address from a packed 32-bit value.
     *
     * @details
     * Interprets `addr` as a big-endian packed value (`0xAABBCCDD`) and expands
     * it to `a.b.c.d` octets.
     *
     * @param addr Packed address value.
     * @return IPv4 address with corresponding bytes.
     */
    static Ipv4Addr from_u32(u32 addr)
    {
        return {{static_cast<u8>((addr >> 24) & 0xff),
                 static_cast<u8>((addr >> 16) & 0xff),
                 static_cast<u8>((addr >> 8) & 0xff),
                 static_cast<u8>(addr & 0xff)}};
    }

    /**
     * @brief Check whether the address is the IPv4 broadcast address (255.255.255.255).
     *
     * @return `true` if broadcast, otherwise `false`.
     */
    bool is_broadcast() const
    {
        return bytes[0] == 255 && bytes[1] == 255 && bytes[2] == 255 && bytes[3] == 255;
    }

    /**
     * @brief Check whether the address is the all-zero address (0.0.0.0).
     *
     * @return `true` if all octets are zero, otherwise `false`.
     */
    bool is_zero() const
    {
        return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
    }

    /**
     * @brief Check whether two addresses are on the same subnet.
     *
     * @details
     * Applies `netmask` to both addresses and compares the resulting network
     * prefixes.
     *
     * @param other Other address to compare.
     * @param netmask Subnet mask (e.g., 255.255.255.0).
     * @return `true` if both addresses are in the same subnet, otherwise `false`.
     */
    bool same_subnet(const Ipv4Addr &other, const Ipv4Addr &netmask) const
    {
        return (to_u32() & netmask.to_u32()) == (other.to_u32() & netmask.to_u32());
    }

    /**
     * @brief Get the all-zero IPv4 address constant (0.0.0.0).
     *
     * @return Zero address.
     */
    static Ipv4Addr zero()
    {
        return {{0, 0, 0, 0}};
    }

    /**
     * @brief Get the IPv4 broadcast address constant (255.255.255.255).
     *
     * @return Broadcast address.
     */
    static Ipv4Addr broadcast()
    {
        return {{255, 255, 255, 255}};
    }
} __attribute__((packed));

/**
 * @brief Copy a MAC address byte-by-byte.
 *
 * @details
 * Some protocol headers are packed and may not be aligned for direct loads on
 * all architectures. The network stack uses these helpers when copying
 * addresses out of received packets to avoid alignment faults.
 *
 * @param dst Destination address.
 * @param src Source address.
 */
inline void copy_mac(MacAddr &dst, const MacAddr &src)
{
    for (int i = 0; i < 6; i++)
        dst.bytes[i] = src.bytes[i];
}

/**
 * @brief Copy an IPv4 address byte-by-byte.
 *
 * @param dst Destination address.
 * @param src Source address.
 */
inline void copy_ip(Ipv4Addr &dst, const Ipv4Addr &src)
{
    for (int i = 0; i < 4; i++)
        dst.bytes[i] = src.bytes[i];
}

/**
 * @brief Host-to-network short conversion.
 *
 * @details
 * Converts a 16-bit value from host byte order to network byte order
 * (big-endian). On little-endian machines this swaps bytes; on big-endian it
 * is a no-op.
 *
 * @param x Host-order value.
 * @return Network-order value.
 */
inline u16 htons(u16 x)
{
    return ((x & 0xff) << 8) | ((x >> 8) & 0xff);
}

/**
 * @brief Network-to-host short conversion.
 *
 * @param x Network-order value.
 * @return Host-order value.
 */
inline u16 ntohs(u16 x)
{
    return htons(x);
}

/**
 * @brief Host-to-network long conversion.
 *
 * @param x Host-order value.
 * @return Network-order value.
 */
inline u32 htonl(u32 x)
{
    return ((x & 0xff) << 24) | ((x & 0xff00) << 8) | ((x >> 8) & 0xff00) | ((x >> 24) & 0xff);
}

/**
 * @brief Network-to-host long conversion.
 *
 * @param x Network-order value.
 * @return Host-order value.
 */
inline u32 ntohl(u32 x)
{
    return htonl(x);
}

/**
 * @brief Compute the Internet checksum (one's complement sum).
 *
 * @details
 * Computes the 16-bit one's complement checksum used by IPv4 and transport
 * protocols. The input is interpreted as a sequence of 16-bit words in network
 * byte order. If the length is odd, the final byte is padded in the low-order
 * byte position.
 *
 * @param data Pointer to data to checksum.
 * @param len Length in bytes.
 * @return The one's complement checksum value.
 */
inline u16 checksum(const void *data, usize len)
{
    const u16 *ptr = static_cast<const u16 *>(data);
    u32 sum = 0;

    while (len > 1)
    {
        sum += *ptr++;
        len -= 2;
    }

    // Add left-over byte if any
    if (len > 0)
    {
        sum += *reinterpret_cast<const u8 *>(ptr);
    }

    // Fold 32-bit sum to 16 bits
    while (sum >> 16)
    {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return ~static_cast<u16>(sum);
}

/**
 * @brief IPv6 address (128-bit).
 *
 * @details
 * The address is stored as 16 bytes in network order. The struct is packed
 * so it can be embedded inside protocol headers without padding.
 */
struct Ipv6Addr
{
    u8 bytes[16];

    /**
     * @brief Compare two IPv6 addresses for equality.
     */
    bool operator==(const Ipv6Addr &other) const
    {
        for (int i = 0; i < 16; i++)
        {
            if (bytes[i] != other.bytes[i])
                return false;
        }
        return true;
    }

    bool operator!=(const Ipv6Addr &other) const
    {
        return !(*this == other);
    }

    /**
     * @brief Check if this is the unspecified address (::).
     */
    bool is_unspecified() const
    {
        for (int i = 0; i < 16; i++)
        {
            if (bytes[i] != 0)
                return false;
        }
        return true;
    }

    /**
     * @brief Check if this is the loopback address (::1).
     */
    bool is_loopback() const
    {
        for (int i = 0; i < 15; i++)
        {
            if (bytes[i] != 0)
                return false;
        }
        return bytes[15] == 1;
    }

    /**
     * @brief Check if this is a link-local address (fe80::/10).
     */
    bool is_link_local() const
    {
        return bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80;
    }

    /**
     * @brief Check if this is a multicast address (ff00::/8).
     */
    bool is_multicast() const
    {
        return bytes[0] == 0xff;
    }

    /**
     * @brief Get the unspecified address (::).
     */
    static Ipv6Addr unspecified()
    {
        return {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    }

    /**
     * @brief Get the loopback address (::1).
     */
    static Ipv6Addr loopback()
    {
        return {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
    }

    /**
     * @brief Construct a link-local address from interface identifier.
     */
    static Ipv6Addr link_local_from_mac(const MacAddr &mac)
    {
        Ipv6Addr addr = {{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
        // EUI-64: insert ff:fe in middle and flip U/L bit
        addr.bytes[8] = mac.bytes[0] ^ 0x02; // Flip U/L bit
        addr.bytes[9] = mac.bytes[1];
        addr.bytes[10] = mac.bytes[2];
        addr.bytes[11] = 0xff;
        addr.bytes[12] = 0xfe;
        addr.bytes[13] = mac.bytes[3];
        addr.bytes[14] = mac.bytes[4];
        addr.bytes[15] = mac.bytes[5];
        return addr;
    }

    /**
     * @brief Get solicited-node multicast address for this address.
     */
    Ipv6Addr solicited_node_multicast() const
    {
        return {{0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, bytes[13], bytes[14], bytes[15]}};
    }
} __attribute__((packed));

/**
 * @brief Copy an IPv6 address byte-by-byte.
 */
inline void copy_ipv6(Ipv6Addr &dst, const Ipv6Addr &src)
{
    for (int i = 0; i < 16; i++)
        dst.bytes[i] = src.bytes[i];
}

} // namespace net
