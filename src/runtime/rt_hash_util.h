/// @file rt_hash_util.h
/// @brief Shared FNV-1a hash utility for the Viper runtime.
///
/// Provides a single implementation of the FNV-1a hash function used by
/// multiple runtime collection types (map, bag, countmap, multimap, bimap,
/// lrucache, box). Include this header instead of duplicating the hash
/// function in each translation unit.

#ifndef RT_HASH_UTIL_H
#define RT_HASH_UTIL_H

#include <stddef.h>
#include <stdint.h>

/// FNV-1a 64-bit offset basis.
#define RT_FNV_OFFSET_BASIS 0xcbf29ce484222325ULL

/// FNV-1a 64-bit prime.
#define RT_FNV_PRIME 0x100000001b3ULL

/// @brief Compute FNV-1a 64-bit hash of a byte sequence.
///
/// @param data Pointer to the byte sequence to hash.
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
