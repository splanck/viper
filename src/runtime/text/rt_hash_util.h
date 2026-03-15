//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_hash_util.h
// Purpose: SipHash-2-4 hash utility providing a keyed 64-bit hash of arbitrary
//          byte sequences, used by multiple runtime collection types. Uses a
//          per-process random seed from the OS CSPRNG for HashDoS resistance.
//
// Key invariants:
//   - Uses SipHash-2-4 with a 128-bit key seeded once per process.
//   - Output is deterministic within a single process run but varies between runs.
//   - The SipHash algorithm is inlined; seed state is shared via extern linkage
//     (defined in rt_hash_util.c) for consistent hashing across translation units.
//   - Used by rt_map, rt_bag, rt_countmap, rt_multimap, rt_bimap, rt_lrucache,
//     rt_intmap, rt_concmap, and rt_box.
//
// Ownership/Lifetime:
//   - No heap allocation; pure computation.
//   - No ownership transfer; input pointer is borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_hash_util.c (seed init),
//        src/runtime/collections/rt_map.h, src/runtime/collections/rt_bag.h (users)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=============================================================================
    // SipHash-2-4 Per-Process Seed (defined in rt_hash_util.c)
    //=============================================================================

    extern uint64_t rt_siphash_k0_;
    extern uint64_t rt_siphash_k1_;
    extern int rt_siphash_seeded_;

    /// @brief Ensure the SipHash key is seeded (thread-safe, called once per process).
    void rt_hash_ensure_seeded_(void);

    //=============================================================================
    // SipHash-2-4 Implementation
    //=============================================================================

#define RT_SIPROUND_                                                                               \
    do                                                                                             \
    {                                                                                              \
        v0 += v1;                                                                                  \
        v1 = (v1 << 13) | (v1 >> 51);                                                              \
        v1 ^= v0;                                                                                  \
        v0 = (v0 << 32) | (v0 >> 32);                                                              \
        v2 += v3;                                                                                  \
        v3 = (v3 << 16) | (v3 >> 48);                                                              \
        v3 ^= v2;                                                                                  \
        v0 += v3;                                                                                  \
        v3 = (v3 << 21) | (v3 >> 43);                                                              \
        v3 ^= v0;                                                                                  \
        v2 += v1;                                                                                  \
        v1 = (v1 << 17) | (v1 >> 47);                                                              \
        v1 ^= v2;                                                                                  \
        v2 = (v2 << 32) | (v2 >> 32);                                                              \
    } while (0)

    /// @brief Compute the SipHash-2-4 64-bit hash of a byte sequence.
    /// @details Uses a per-process random 128-bit key for HashDoS resistance.
    ///          The algorithm processes 8-byte blocks with 2 compression rounds
    ///          and 4 finalization rounds.
    /// @param data Pointer to the byte sequence to hash (must not be NULL when len > 0).
    /// @param len  Length of the byte sequence in bytes.
    /// @return 64-bit SipHash-2-4 hash value.
    static inline uint64_t rt_fnv1a(const void *data, size_t len)
    {
#if defined(_MSC_VER) && !defined(__clang__)
        if (!rt_siphash_seeded_)
#else
    if (!__atomic_load_n(&rt_siphash_seeded_, __ATOMIC_ACQUIRE))
#endif
            rt_hash_ensure_seeded_();

        const uint8_t *bytes = (const uint8_t *)data;
        uint64_t k0 = rt_siphash_k0_;
        uint64_t k1 = rt_siphash_k1_;

        uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;
        uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;
        uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;
        uint64_t v3 = k1 ^ 0x7465646279746573ULL;

        /* Process 8-byte blocks. */
        size_t blocks = len / 8;
        for (size_t i = 0; i < blocks; i++)
        {
            uint64_t m = 0;
            for (int j = 0; j < 8; j++)
                m |= ((uint64_t)bytes[i * 8 + j]) << (j * 8);
            v3 ^= m;
            RT_SIPROUND_;
            RT_SIPROUND_;
            v0 ^= m;
        }

        /* Process remaining bytes + length tag. */
        uint64_t last = ((uint64_t)len) << 56;
        const uint8_t *tail = bytes + blocks * 8;
        size_t remain = len & 7;
        if (remain >= 7)
            last |= ((uint64_t)tail[6]) << 48;
        if (remain >= 6)
            last |= ((uint64_t)tail[5]) << 40;
        if (remain >= 5)
            last |= ((uint64_t)tail[4]) << 32;
        if (remain >= 4)
            last |= ((uint64_t)tail[3]) << 24;
        if (remain >= 3)
            last |= ((uint64_t)tail[2]) << 16;
        if (remain >= 2)
            last |= ((uint64_t)tail[1]) << 8;
        if (remain >= 1)
            last |= ((uint64_t)tail[0]);

        v3 ^= last;
        RT_SIPROUND_;
        RT_SIPROUND_;
        v0 ^= last;

        /* Finalization: 4 rounds. */
        v2 ^= 0xff;
        RT_SIPROUND_;
        RT_SIPROUND_;
        RT_SIPROUND_;
        RT_SIPROUND_;

        return v0 ^ v1 ^ v2 ^ v3;
    }

#undef RT_SIPROUND_

#ifdef __cplusplus
}
#endif
