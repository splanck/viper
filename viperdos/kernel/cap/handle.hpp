#pragma once

#include "../include/types.hpp"

/**
 * @file handle.hpp
 * @brief Capability handle encoding helpers.
 *
 * @details
 * Capabilities in ViperDOS are referenced via opaque 32-bit handles. The handle
 * encodes:
 * - A 24-bit table index (slot number).
 * - An 8-bit generation counter.
 *
 * The generation counter is incremented whenever a slot is freed and reused.
 * This helps detect stale handles (the ABA/use-after-free problem): a handle
 * pointing to a recycled slot will have a mismatched generation and will be
 * rejected by the capability table lookup.
 */
namespace cap {

// Handle = 24-bit index + 8-bit generation
// Generation detects use-after-free (ABA problem)

/// @brief Opaque capability handle type.
using Handle = u32;

/// @brief Sentinel value representing an invalid handle.
constexpr Handle HANDLE_INVALID = 0xFFFFFFFF;

/// @brief Bitmask extracting the low 24-bit index portion of a handle.
constexpr u32 INDEX_MASK = 0x00FFFFFF;
/// @brief Bit shift for the 8-bit generation portion of a handle.
constexpr u32 GEN_SHIFT = 24;
/// @brief Bitmask for the 8-bit generation portion after shifting.
constexpr u32 GEN_MASK = 0xFF;

/**
 * @brief Extract the table index portion of a handle.
 *
 * @param h Handle value.
 * @return 24-bit index.
 */
inline u32 handle_index(Handle h) {
    return h & INDEX_MASK;
}

/**
 * @brief Extract the generation portion of a handle.
 *
 * @param h Handle value.
 * @return 8-bit generation.
 */
inline u8 handle_gen(Handle h) {
    return (h >> GEN_SHIFT) & GEN_MASK;
}

/**
 * @brief Construct a handle from an index and generation.
 *
 * @param index Table slot index.
 * @param gen Generation counter.
 * @return Encoded handle.
 */
inline Handle make_handle(u32 index, u8 gen) {
    return (index & INDEX_MASK) | (static_cast<u32>(gen) << GEN_SHIFT);
}

} // namespace cap
