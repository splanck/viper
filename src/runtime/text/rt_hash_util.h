//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_hash_util.h
// Purpose: Shared FNV-1a hash utility providing a deterministic 64-bit hash of arbitrary byte
// sequences, used by multiple runtime collection types.
//
// Key invariants:
//   - Uses FNV-1a with fixed 64-bit offset basis and prime constants.
//   - Output is deterministic for any given byte sequence.
//   - This is a header-only utility; the function is declared static inline.
//   - Used by rt_map, rt_bag, rt_countmap, rt_multimap, rt_bimap, rt_lrucache, and rt_box.
//
// Ownership/Lifetime:
//   - No heap allocation; pure computation.
//   - No ownership transfer; input pointer is borrowed for the duration of the call.
//
// Links: src/runtime/collections/rt_map.h, src/runtime/collections/rt_bag.h (users)
//
//===----------------------------------------------------------------------===//
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
