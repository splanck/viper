//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viperdos/virtio_net.hpp
// Purpose: Shared VirtIO network device types and constants.
// Key invariants: Matches virtio-net specification; used by kernel and userspace.
// Ownership/Lifetime: Header-only; type definitions.
// Links: kernel/drivers/virtio/net.hpp, user/libvirtio/include/net.hpp
//
//===----------------------------------------------------------------------===//

/**
 * @file virtio_net.hpp
 * @brief Shared VirtIO network device types and constants for kernel and userspace.
 *
 * @details
 * This header defines the constants, structures, and types used by the VirtIO
 * network device specification. Both kernel and userspace drivers include this
 * header to ensure ABI compatibility.
 *
 * Reference: Virtual I/O Device (VIRTIO) Version 1.1, Section 5.1
 */

#pragma once

#include "types.hpp"

namespace virtio {

/**
 * @brief VirtIO-net feature bits.
 *
 * @details
 * Feature bits are negotiated during device initialization. The driver
 * advertises supported features and the device confirms which are enabled.
 */
namespace net_features {
constexpr u64 CSUM = 1ULL << 0;       ///< Checksum offload to device
constexpr u64 GUEST_CSUM = 1ULL << 1; ///< Guest handles checksums
constexpr u64 MAC = 1ULL << 5;        ///< Device has given MAC address
constexpr u64 GSO = 1ULL << 6;        ///< Deprecated: generic segmentation offload
constexpr u64 MRG_RXBUF = 1ULL << 15; ///< Mergeable receive buffers
constexpr u64 STATUS = 1ULL << 16;    ///< Configuration status field available
constexpr u64 CTRL_VQ = 1ULL << 17;   ///< Control virtqueue available
constexpr u64 MQ = 1ULL << 22;        ///< Multiple queue pairs available
} // namespace net_features

/**
 * @brief VirtIO-net header prepended to every packet.
 *
 * @details
 * This header is prepended to every network packet in both TX and RX
 * virtqueues. It provides metadata for checksum offload and GSO.
 */
struct NetHeader {
    u8 flags;       ///< Header flags (net_hdr_flags)
    u8 gso_type;    ///< GSO type (net_gso)
    u16 hdr_len;    ///< Ethernet + IP + TCP/UDP header length
    u16 gso_size;   ///< GSO segment size (MSS)
    u16 csum_start; ///< Offset to start checksumming from
    u16 csum_offset; ///< Offset from csum_start to store checksum
} __attribute__((packed));

/**
 * @brief VirtIO-net header flags.
 */
namespace net_hdr_flags {
constexpr u8 NEEDS_CSUM = 1;  ///< Packet needs checksum
constexpr u8 DATA_VALID = 2;  ///< Checksum is valid (RX only)
} // namespace net_hdr_flags

/**
 * @brief VirtIO-net GSO type values.
 */
namespace net_gso {
constexpr u8 NONE = 0;   ///< No GSO
constexpr u8 TCPV4 = 1;  ///< TCP over IPv4
constexpr u8 UDP = 3;    ///< UDP
constexpr u8 TCPV6 = 4;  ///< TCP over IPv6
} // namespace net_gso

/**
 * @brief VirtIO-net configuration space layout.
 *
 * @details
 * The config space is read from the device's MMIO config region.
 */
struct NetConfig {
    u8 mac[6];                ///< Device MAC address (if MAC feature)
    u16 status;               ///< Link status (if STATUS feature)
    u16 max_virtqueue_pairs;  ///< Max queue pairs (if MQ feature)
    u16 mtu;                  ///< Maximum transmission unit
} __attribute__((packed));

/**
 * @brief VirtIO-net link status bits.
 */
namespace net_status {
constexpr u16 LINK_UP = 1;   ///< Link is up
constexpr u16 ANNOUNCE = 2;  ///< Announce gratuitous ARP
} // namespace net_status

} // namespace virtio
