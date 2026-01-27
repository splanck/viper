//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../include/types.hpp"

/**
 * @file rights.hpp
 * @brief Capability rights bit flags and helpers.
 *
 * @details
 * Capabilities in ViperDOS are accompanied by a rights bitmask that restricts
 * what operations can be performed with the handle. This is the basis for
 * least-privilege: a subsystem can hand out a derived handle with a reduced set
 * of rights and the kernel will enforce those restrictions at use time.
 *
 * The rights are defined as bit flags so they can be combined and tested
 * efficiently.
 */
namespace cap {

// Capability rights flags
/**
 * @brief Bitmask flags describing allowed operations on a capability.
 *
 * @details
 * The meaning of each right is object-kind dependent (file vs channel vs task),
 * but the flags provide a common vocabulary (read/write, create/delete, derive,
 * transfer, etc.).
 *
 * The enum values can be combined with bitwise operators. Helper presets for
 * common combinations are provided.
 */
enum Rights : u32 {
    CAP_NONE = 0,
    CAP_READ = 1 << 0,
    CAP_WRITE = 1 << 1,
    CAP_EXECUTE = 1 << 2,
    CAP_LIST = 1 << 3,
    CAP_CREATE = 1 << 4,
    CAP_DELETE = 1 << 5,
    CAP_DERIVE = 1 << 6,
    CAP_TRANSFER = 1 << 7,
    CAP_SPAWN = 1 << 8,
    CAP_TRAVERSE = 1 << 9, // Directory traversal right

    // Device access rights (for user-space display servers)
    CAP_DEVICE_ACCESS = 1 << 10, // Can map device MMIO memory
    CAP_IRQ_ACCESS = 1 << 11,    // Can register/wait for IRQs
    CAP_DMA_ACCESS = 1 << 12,    // Can allocate DMA buffers

    // Common combinations
    CAP_RW = CAP_READ | CAP_WRITE,
    CAP_RWX = CAP_READ | CAP_WRITE | CAP_EXECUTE,
    CAP_ALL = 0xFFFFFFFF,
};

/**
 * @brief Combine two rights masks.
 *
 * @param a Left mask.
 * @param b Right mask.
 * @return Combined mask.
 */
inline Rights operator|(Rights a, Rights b) {
    return static_cast<Rights>(static_cast<u32>(a) | static_cast<u32>(b));
}

/**
 * @brief Intersect two rights masks.
 *
 * @param a Left mask.
 * @param b Right mask.
 * @return Intersection mask.
 */
inline Rights operator&(Rights a, Rights b) {
    return static_cast<Rights>(static_cast<u32>(a) & static_cast<u32>(b));
}

/**
 * @brief Bitwise negate a rights mask.
 *
 * @param a Mask to negate.
 * @return Negated mask.
 */
inline Rights operator~(Rights a) {
    return static_cast<Rights>(~static_cast<u32>(a));
}

/**
 * @brief Check whether a rights bitmask contains all required rights.
 *
 * @details
 * Interprets `current` as a raw `u32` bitmask and verifies that every bit set
 * in `required` is also set in `current`.
 *
 * @param current Current rights bitmask.
 * @param required Rights required to perform an operation.
 * @return `true` if all required rights are present.
 */
inline bool has_rights(u32 current, Rights required) {
    return (current & static_cast<u32>(required)) == static_cast<u32>(required);
}

} // namespace cap
