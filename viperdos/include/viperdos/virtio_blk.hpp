//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viperdos/virtio_blk.hpp
// Purpose: Shared VirtIO block device types and constants.
// Key invariants: Matches virtio-blk specification; used by kernel and userspace.
// Ownership/Lifetime: Header-only; type definitions.
// Links: kernel/drivers/virtio/blk.hpp, user/libvirtio/include/blk.hpp
//
//===----------------------------------------------------------------------===//

/**
 * @file virtio_blk.hpp
 * @brief Shared VirtIO block device types and constants for kernel and userspace.
 *
 * @details
 * This header defines the constants, structures, and types used by the VirtIO
 * block device specification. Both kernel and userspace drivers include this
 * header to ensure ABI compatibility.
 *
 * Reference: Virtual I/O Device (VIRTIO) Version 1.1, Section 5.2
 */

#pragma once

#include "types.hpp"

namespace virtio {

/**
 * @brief VirtIO-blk request type values.
 *
 * @details
 * These values are placed in the request header's `type` field to indicate
 * the operation being requested.
 */
namespace blk_type {
constexpr u32 IN = 0;    ///< Read from device
constexpr u32 OUT = 1;   ///< Write to device
constexpr u32 FLUSH = 4; ///< Flush write cache
} // namespace blk_type

/**
 * @brief VirtIO-blk completion status values written by the device.
 *
 * @details
 * After processing a request, the device writes one of these values to the
 * status byte in the request descriptor chain.
 */
namespace blk_status {
constexpr u8 OK = 0;     ///< Request completed successfully
constexpr u8 IOERR = 1;  ///< Device or driver error
constexpr u8 UNSUPP = 2; ///< Request type not supported
} // namespace blk_status

/**
 * @brief VirtIO-blk feature bits.
 *
 * @details
 * Feature bits are negotiated during device initialization. The driver
 * advertises supported features and the device confirms which are enabled.
 */
namespace blk_features {
constexpr u64 SIZE_MAX = 1ULL << 1;      ///< Max size of any single segment
constexpr u64 SEG_MAX = 1ULL << 2;       ///< Max number of segments in a request
constexpr u64 GEOMETRY = 1ULL << 4;      ///< Legacy geometry available
constexpr u64 RO = 1ULL << 5;            ///< Disk is read-only
constexpr u64 BLK_SIZE = 1ULL << 6;      ///< Block size available in config
constexpr u64 FLUSH = 1ULL << 9;         ///< Cache flush command supported
constexpr u64 TOPOLOGY = 1ULL << 10;     ///< Topology info available
constexpr u64 CONFIG_WCE = 1ULL << 11;   ///< Writeback caching config
constexpr u64 MQ = 1ULL << 12;           ///< Multiple queues supported
constexpr u64 DISCARD = 1ULL << 13;      ///< Discard command supported
constexpr u64 WRITE_ZEROES = 1ULL << 14; ///< Write zeroes command supported
} // namespace blk_features

/**
 * @brief VirtIO-blk request header placed at the start of a request chain.
 *
 * @details
 * Every virtio-blk request begins with this 16-byte header describing the
 * operation type and target sector.
 */
struct BlkReqHeader {
    u32 type;     ///< Request type (blk_type::IN, OUT, or FLUSH)
    u32 reserved; ///< Reserved, must be zero
    u64 sector;   ///< Starting sector for the operation
};

/**
 * @brief VirtIO-blk configuration space layout (partial).
 *
 * @details
 * The config space is read from the device's MMIO config region. This
 * structure represents the commonly-used fields; additional topology
 * fields may follow.
 */
struct BlkConfig {
    u64 capacity; ///< Number of 512-byte sectors

    u32 size_max; ///< Max size of single segment (if SIZE_MAX feature)
    u32 seg_max;  ///< Max segments per request (if SEG_MAX feature)

    struct {
        u16 cylinders;
        u8 heads;
        u8 sectors;
    } geometry; ///< Legacy CHS geometry (if GEOMETRY feature)

    u32 blk_size; ///< Logical block size (if BLK_SIZE feature)
};

} // namespace virtio
