//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_hash_util.h
// Purpose: Shared FNV-1a hash utility for the Viper runtime.
// Key invariants: Deterministic output for any given byte sequence. FNV-1a
//                 parameters are fixed (64-bit offset basis and prime).
// Ownership: Header-only utility; no heap allocation.
// Lifetime: Static inline function; no state.
// Links: rt_map.h, rt_bag.h, rt_countmap.h, rt_multimap.h, rt_bimap.h,
//        rt_lrucache.h, rt_box.h
//
// Provides a single implementation of the FNV-1a hash function used by
// multiple runtime collection types (map, bag, countmap, multimap, bimap,
// lrucache, box). Include this header instead of duplicating the hash
// function in each translation unit.
//
//===----------------------------------------------------------------------===//

/// @file rt_hash_util.h
/// @brief Shared FNV-1a hash utility for the Viper runtime.
/// @details Offers a single implementation of the FNV-1a hash function used by
///          multiple runtime collection types (map, bag, countmap, multimap,
///          bimap, lrucache, box). Include this header instead of duplicating
///          the hash function in each translation unit.

#ifndef RT_HASH_UTIL_H
#define RT_HASH_UTIL_H

#include <stddef.h>
#include <stdint.h>

/// @brief FNV-1a 64-bit offset basis constant.
#define RT_FNV_OFFSET_BASIS 0xcbf29ce484222325ULL

/// @brief FNV-1a 64-bit prime constant.
#define RT_FNV_PRIME 0x100000001b3ULL

/// @brief Compute the FNV-1a 64-bit hash of a byte sequence.
/// @details Iterates over each byte in the input, XORing it with the running
///          hash and multiplying by the FNV prime. The algorithm produces a
///          well-distributed 64-bit hash suitable for hash table use.
/// @param data Pointer to the byte sequence to hash (must not be NULL when len > 0).
/// @param len  Length of the byte sequence in bytes.
/// @return 64-bit FNV-1a hash value.
static inline uint64_t rt_fnv1a(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = RT_FNV_OFFSET_BASIS;
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= bytes[i];
        hash *= RT_FNV_PRIME;
    }
    return hash;
}

#endif /* RT_HASH_UTIL_H */
