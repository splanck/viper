//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_rsa.c
// Purpose: Native RSA parsing and signature helpers for TLS/X.509.
//
//===----------------------------------------------------------------------===//

#include "rt_rsa.h"

#include "rt_crypto.h"

#include <stdlib.h>
#include <string.h>

#if !defined(__SIZEOF_INT128__)
#error "Viper RSA requires unsigned __int128 on macOS and Linux"
#endif

#define RT_RSA_MAX_MOD_BYTES 512
#define RT_RSA_MAX_WORDS (RT_RSA_MAX_MOD_BYTES / sizeof(uint64_t))

/// @brief Decode one DER tag-length-value header from the start of `buf`.
/// @details DER (Distinguished Encoding Rules) is the wire format
///          ASN.1 structures use for X.509 certificates and PKCS#1
///          RSA key blobs. Each TLV begins with a 1-byte tag, then
///          a length encoded as either:
///          - **Short form** (`0x00`–`0x7F`): the length byte itself.
///          - **Long form** (`0x80 | n`): `n` length bytes follow,
///            big-endian. We cap `n` at 4 (1 GB max length, more than
///            adequate for any TLS payload).
///          Returns 1 on success with `tag`, `val_len`, and `hdr_len`
///          filled. Returns 0 for malformed or truncated input.
///          Used by every higher-level DER walker in this file.
static int rsa_der_read_tlv(
    const uint8_t *buf, size_t buf_len, uint8_t *tag, size_t *val_len, size_t *hdr_len) {
    if (!buf || buf_len < 2 || !tag || !val_len || !hdr_len)
        return 0;

    *tag = buf[0];
    if (buf[1] < 0x80) {
        *val_len = buf[1];
        *hdr_len = 2;
    } else {
        size_t num_len_bytes = (size_t)(buf[1] & 0x7F);
        size_t value = 0;
        if (num_len_bytes == 0 || num_len_bytes > 4 || 2 + num_len_bytes > buf_len)
            return 0;
        for (size_t i = 0; i < num_len_bytes; i++)
            value = (value << 8) | buf[2 + i];
        *val_len = value;
        *hdr_len = 2 + num_len_bytes;
    }

    return (*hdr_len + *val_len <= buf_len) ? 1 : 0;
}

/// @brief Heap-duplicate a big-endian integer value, stripping any leading zero padding.
/// @details DER-encoded INTEGER values include a leading `0x00` byte
///          when the most-significant bit of the first content byte
///          is 1 — this prevents the value from being misread as
///          negative under DER's two's-complement convention. RSA
///          modulus and exponent values are unsigned, so we strip
///          that leading zero before storing them. Caller owns the
///          returned buffer (must `free`).
static uint8_t *rsa_dup_be_trimmed(const uint8_t *data, size_t len, size_t *out_len) {
    size_t start = 0;
    uint8_t *copy = NULL;

    if (!data || len == 0 || !out_len)
        return NULL;
    while (start + 1 < len && data[start] == 0x00)
        start++;
    len -= start;
    data += start;

    copy = (uint8_t *)malloc(len);
    if (!copy)
        return NULL;
    memcpy(copy, data, len);
    *out_len = len;
    return copy;
}

/// @brief Parse one DER INTEGER (`tag = 0x02`) from `der[*pos]` into a fresh buffer.
/// @details Wrapper that combines `rsa_der_read_tlv` (header decode +
///          tag check) with `rsa_dup_be_trimmed` (strip the
///          DER-mandated leading-zero padding). Advances `*pos` past
///          the consumed TLV on success. Returns 0 if the next field
///          isn't an INTEGER or if the buffer is truncated.
static int rsa_parse_der_integer(
    const uint8_t *der, size_t der_len, size_t *pos, uint8_t **out, size_t *out_len) {
    uint8_t tag;
    size_t value_len, hdr_len;

    if (!der || !pos || !out || !out_len || *pos >= der_len)
        return 0;
    if (!rsa_der_read_tlv(der + *pos, der_len - *pos, &tag, &value_len, &hdr_len) || tag != 0x02)
        return 0;
    *out = rsa_dup_be_trimmed(der + *pos + hdr_len, value_len, out_len);
    if (!*out)
        return 0;
    *pos += hdr_len + value_len;
    return 1;
}

/// @brief Return the digest length in bytes for an RSA hash identifier (32/48/64 for SHA-256/384/512).
static size_t rsa_hash_len(rt_rsa_hash_t hash_id) {
    switch (hash_id) {
        case RT_RSA_HASH_SHA256:
            return 32;
        case RT_RSA_HASH_SHA384:
            return 48;
        case RT_RSA_HASH_SHA512:
            return 64;
        default:
            return 0;
    }
}

/// @brief Dispatch a SHA-2 family hash by `hash_id` over the input buffer.
/// @details Routes to `rt_sha256` / `rt_sha384` / `rt_sha512` based on
///          the algorithm tag. Returns 0 for unknown identifiers
///          (caller treats as signature verification failure).
static int rsa_hash_buffer(rt_rsa_hash_t hash_id, const void *data, size_t len, uint8_t *out) {
    switch (hash_id) {
        case RT_RSA_HASH_SHA256:
            rt_sha256(data, len, out);
            return 1;
        case RT_RSA_HASH_SHA384:
            rt_sha384(data, len, out);
            return 1;
        case RT_RSA_HASH_SHA512:
            rt_sha512(data, len, out);
            return 1;
        default:
            return 0;
    }
}

/// @brief Return the bit-length of the RSA modulus (e.g. 2048, 3072, 4096).
/// @details Multiplies `modulus_len * 8`, then walks down by counting
///          leading zero bits in the most-significant byte. Used by
///          PSS encode/verify to compute `emLen` (the PSS-encoded
///          message length, which is `ceil((modBits - 1) / 8)`).
static size_t rsa_modulus_bits(const rt_rsa_key_t *key) {
    size_t bits = 0;
    uint8_t first = 0;

    if (!key || !key->modulus || key->modulus_len == 0)
        return 0;
    first = key->modulus[0];
    bits = key->modulus_len * 8;
    while ((first & 0x80u) == 0) {
        first <<= 1;
        bits--;
    }
    return bits;
}

/// @brief MGF1 mask generation function (RFC 8017 §B.2.1) — produces an arbitrary-length mask.
/// @details Stretches a fixed-size hash output to any desired mask
///          length by hashing `seed || INT32_BE(counter)` for
///          `counter = 0, 1, 2, ...` and concatenating the digests
///          until the requested length is filled. Used by RSA-PSS
///          to produce the salt mask and by OAEP to produce the
///          payload mask. The hash function is parameterized so
///          PSS/OAEP can pick a different MGF hash than the message
///          hash if needed (in practice they almost always match).
static int rsa_mgf1(
    rt_rsa_hash_t hash_id, const uint8_t *seed, size_t seed_len, uint8_t *out, size_t out_len) {
    uint8_t hash[64];
    uint8_t block[68];
    uint32_t counter = 0;
    size_t h_len = rsa_hash_len(hash_id);
    size_t pos = 0;

    if (!seed || !out || h_len == 0 || seed_len > sizeof(block) - 4)
        return 0;

    memcpy(block, seed, seed_len);
    while (pos < out_len) {
        block[seed_len + 0] = (uint8_t)(counter >> 24);
        block[seed_len + 1] = (uint8_t)(counter >> 16);
        block[seed_len + 2] = (uint8_t)(counter >> 8);
        block[seed_len + 3] = (uint8_t)counter;
        if (!rsa_hash_buffer(hash_id, block, seed_len + 4, hash))
            return 0;
        {
            size_t copy = out_len - pos;
            if (copy > h_len)
                copy = h_len;
            memcpy(out + pos, hash, copy);
            pos += copy;
        }
        counter++;
    }
    return 1;
}

/// @brief Round byte length up to the number of 64-bit words needed to hold it.
static size_t rsa_word_count_from_len(size_t len) {
    return (len + sizeof(uint64_t) - 1) / sizeof(uint64_t);
}

/// @brief Zero-fill a multi-word integer (`count` × 64-bit limbs).
static void rsa_words_zero(uint64_t *words, size_t count) {
    memset(words, 0, count * sizeof(*words));
}

/// @brief Convert a big-endian byte buffer into the little-endian-limb internal representation.
/// @details All RSA values cross the wire as big-endian byte arrays
///          (network order), but our arithmetic uses little-endian
///          64-bit limbs (`out[0]` = least significant 8 bytes).
///          The walk reverses byte order while packing 8 bytes per
///          limb. Higher limbs are zero-filled if the input is
///          shorter than the internal representation needs.
static void rsa_words_from_be(const uint8_t *data, size_t len, uint64_t *out, size_t out_words) {
    rsa_words_zero(out, out_words);
    for (size_t i = 0; i < len; i++)
        out[i / 8] |= (uint64_t)data[len - 1 - i] << ((i % 8) * 8);
}

/// @brief Inverse of `rsa_words_from_be`: emit limbs as a big-endian byte buffer.
/// @details Pads / truncates to the requested output length (the
///          PSS/PKCS#1 padding routines need a specific output size).
///          Stops early once the limb supply is exhausted, leaving
///          the high bytes of `out` zero-padded.
static void rsa_words_to_be(const uint64_t *words, size_t word_count, uint8_t *out, size_t out_len) {
    memset(out, 0, out_len);
    for (size_t i = 0; i < out_len; i++) {
        size_t word_index = i / 8;
        if (word_index >= word_count)
            break;
        out[out_len - 1 - i] = (uint8_t)(words[word_index] >> ((i % 8) * 8));
    }
}

/// @brief Compare two multi-word integers — returns -1 / 0 / 1 like `memcmp`.
/// @details Walks limbs from most-significant down (the `i--` loop is
///          deliberately ordered that way), short-circuiting at the
///          first inequality. Used by the modular-reduction step in
///          `rsa_mod_double_inplace` to decide whether subtraction
///          is needed.
static int rsa_words_cmp(const uint64_t *a, const uint64_t *b, size_t count) {
    for (size_t i = count; i-- > 0;) {
        if (a[i] < b[i])
            return -1;
        if (a[i] > b[i])
            return 1;
    }
    return 0;
}

/// @brief Big-integer subtraction: `a -= b` with borrow propagation across limbs.
/// @details Uses `__int128` arithmetic to capture the borrow bit in a
///          way the optimizer can fold cleanly into `sbb` instructions
///          on x86_64 / `subs/sbcs` on AArch64. Caller is responsible
///          for ensuring `a >= b` (we don't return underflow status —
///          this is used only in the modular-reduction step where
///          underflow can't happen by construction).
static void rsa_words_sub_inplace(uint64_t *a, const uint64_t *b, size_t count) {
    unsigned __int128 borrow = 0;
    for (size_t i = 0; i < count; i++) {
        unsigned __int128 lhs = a[i];
        unsigned __int128 rhs = (unsigned __int128)b[i] + borrow;
        a[i] = (uint64_t)(lhs - rhs);
        borrow = lhs < rhs;
    }
}

/// @brief Constant-time conditional swap of two big integers.
/// @details `mask` must be all-ones (`UINT64_MAX`) to swap or all-zeros
///          to leave alone — caller derives that from a boolean
///          condition without branching. The XOR-three-times pattern
///          (`a ^= diff; b ^= diff` after `diff = mask & (a ^ b)`)
///          swaps when `diff` is the difference and no-ops when
///          `diff` is zero. Used to make modular-exponentiation
///          timing-attack resistant: the same instruction sequence
///          runs whether or not the swap happens.
static void rsa_words_cswap(uint64_t *a, uint64_t *b, size_t count, uint64_t mask) {
    for (size_t i = 0; i < count; i++) {
        uint64_t diff = mask & (a[i] ^ b[i]);
        a[i] ^= diff;
        b[i] ^= diff;
    }
}

/// @brief Add `value` into `words[index]`, propagating carry up through higher limbs.
/// @details Used inside the Montgomery multiplication inner loop to
///          fold partial products into the running accumulator. Stops
///          early once the carry chain reaches a limb where it
///          doesn't propagate further, which keeps the typical case
///          fast even for large modulus sizes.
static void rsa_words_accumulate(uint64_t *words, size_t word_count, size_t index, uint64_t value) {
    while (value != 0 && index < word_count) {
        unsigned __int128 acc = (unsigned __int128)words[index] + value;
        words[index] = (uint64_t)acc;
        value = (uint64_t)(acc >> 64);
        index++;
    }
}

/// @brief Double a big integer modulo `modulus` (i.e. `value = (value * 2) % modulus`).
/// @details Implements `value <<= 1` across limbs, capturing the
///          out-of-range high bit, then conditionally subtracts
///          `modulus` if the result either overflowed (carry set)
///          or simply became `>= modulus`. Used by
///          `rsa_compute_r_squared` as a primitive building block —
///          repeatedly doubling 1 modulo N produces R² mod N, which
///          is the constant the Montgomery multiplication needs to
///          convert values into Montgomery form.
static void rsa_mod_double_inplace(uint64_t *value, const uint64_t *modulus, size_t word_count) {
    uint64_t carry = 0;
    for (size_t i = 0; i < word_count; i++) {
        uint64_t next = value[i] >> 63;
        value[i] = (value[i] << 1) | carry;
        carry = next;
    }
    if (carry || rsa_words_cmp(value, modulus, word_count) >= 0)
        rsa_words_sub_inplace(value, modulus, word_count);
}

/// @brief Compute `R² mod N` where R = 2^(64*word_count) — the Montgomery domain conversion constant.
/// @details Montgomery arithmetic represents values in the form
///          `aR mod N` so multiplications can avoid the cost of
///          division-based modular reduction. Going from a normal
///          value `a` to Montgomery form requires multiplying by
///          R² and then doing one Montgomery reduction, which
///          gives `aR mod N`. This helper precomputes that R².
///          Implementation: start with 1 and double `2 * word_count
///          * 64` times, each time reducing modulo N. That gives
///          `2^(2*word_count*64) mod N = R² mod N`. Slow (linear
///          in modulus bits) but only done once per modular-exp call.
static void rsa_compute_r_squared(uint64_t *out, const uint64_t *modulus, size_t word_count) {
    rsa_words_zero(out, word_count);
    out[0] = 1;
    for (size_t i = 0; i < word_count * 128; i++)
        rsa_mod_double_inplace(out, modulus, word_count);
}

/// @brief Compute `-N⁻¹ mod 2⁶⁴` (the modular inverse of the modulus's lowest limb, negated).
/// @details Montgomery reduction needs `-N⁻¹ mod 2⁶⁴` precomputed
///          (just the bottom 64 bits of the inverse, since the
///          reduction step only uses that value). Uses 6 rounds of
///          Newton-style iteration: starting from `x = 1`, the
///          recurrence `x ← x * (2 - N₀*x)` doubles the number of
///          correct bits each round. 6 rounds is enough to converge
///          to a full 64-bit inverse for any odd `N₀` (and N is
///          odd by RSA construction — it's the product of two
///          odd primes). Returns the *negated* inverse (`0 - x`)
///          because that's the form Montgomery reduction wants.
static uint64_t rsa_montgomery_n0_inv(uint64_t n0) {
    uint64_t x = 1;
    for (int i = 0; i < 6; i++)
        x *= 2 - n0 * x;
    return (uint64_t)(0 - x);
}

/// @brief Montgomery modular multiplication: `out = a * b * R⁻¹ mod modulus`.
/// @details The core primitive of Montgomery exponentiation. Implements
///          the CIOS (Coarsely Integrated Operand Scanning) algorithm:
///          for each limb `a[i]`, multiply-and-add `a[i] * b` into a
///          running accumulator, then perform one Montgomery reduction
///          step that adds a multiple of the modulus chosen to zero
///          out the lowest accumulator limb. After `word_count`
///          iterations, the accumulator holds `(a * b * R⁻¹) mod N`
///          plus possibly one extra `N` (handled by a final
///          conditional subtract).
///          The `n0_inv` parameter (precomputed by
///          `rsa_montgomery_n0_inv`) is what makes the per-limb
///          reduction work without division.
///          Time complexity is `O(word_count²)`. For 2048-bit RSA
///          (32 limbs), each Mont-mul is ~1024 limb-multiplies,
///          which the wide-multiply intrinsic compiles to single
///          `mulx` / `umulh` instructions.
static void rsa_mont_mul(uint64_t *out,
                         const uint64_t *a,
                         const uint64_t *b,
                         const uint64_t *modulus,
                         size_t word_count,
                         uint64_t n0_inv) {
    uint64_t t[RT_RSA_MAX_WORDS + 3];

    rsa_words_zero(t, RT_RSA_MAX_WORDS + 3);
    for (size_t i = 0; i < word_count; i++) {
        unsigned __int128 carry = 0;
        uint64_t m;

        for (size_t j = 0; j < word_count; j++) {
            unsigned __int128 acc = (unsigned __int128)a[j] * b[i] + t[j] + carry;
            t[j] = (uint64_t)acc;
            carry = acc >> 64;
        }
        rsa_words_accumulate(t, word_count + 3, word_count, (uint64_t)carry);

        m = t[0] * n0_inv;
        carry = 0;
        for (size_t j = 0; j < word_count; j++) {
            unsigned __int128 acc = (unsigned __int128)m * modulus[j] + t[j] + carry;
            t[j] = (uint64_t)acc;
            carry = acc >> 64;
        }
        rsa_words_accumulate(t, word_count + 3, word_count, (uint64_t)carry);

        for (size_t j = 0; j < word_count + 2; j++)
            t[j] = t[j + 1];
        t[word_count + 2] = 0;
    }

    memcpy(out, t, word_count * sizeof(*out));
    if (rsa_words_cmp(out, modulus, word_count) >= 0)
        rsa_words_sub_inplace(out, modulus, word_count);
}

/// @brief Count the bit-length of a big-endian integer (highest set bit + 1, or 0 if all-zero).
/// @details Walks from the most-significant byte forward to find the
///          first non-zero byte, then counts the bits within it. Used
///          by `rsa_modexp_bytes` to know how many iterations of the
///          Montgomery ladder are needed (one per exponent bit).
static size_t rsa_bit_length_be(const uint8_t *data, size_t len) {
    if (!data || len == 0)
        return 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        if (byte != 0) {
            size_t bits = (len - i - 1) * 8;
            while (byte != 0) {
                bits++;
                byte >>= 1;
            }
            return bits;
        }
    }
    return 0;
}

/// @brief Test whether a specific bit of a big-endian integer is set (bit 0 is least significant).
static int rsa_bit_test_be(const uint8_t *data, size_t len, size_t bit_index) {
    size_t byte_index = len - 1 - (bit_index / 8);
    return (data[byte_index] >> (bit_index % 8)) & 1;
}

/// @brief Compute `base^exp mod modulus` using the constant-time Montgomery ladder.
/// @details The complete RSA primitive — every signature verify and
///          every public-key operation in this file ultimately calls
///          here. Algorithm:
///          1. Convert `modulus`, `base` to limb arrays.
///          2. Precompute `R² mod N` and `-N⁻¹ mod 2⁶⁴` for the
///             Montgomery layer.
///          3. Convert `1` and `base` into Montgomery form
///             (multiply by R²).
///          4. Walk exponent bits from most-significant down using
///             the **Montgomery ladder**: at each step, the pair
///             `(r0, r1)` is conditionally swapped based on the
///             current exponent bit, then both are squared/multiplied
///             together. The conditional swap (via `rsa_words_cswap`)
///             keeps the operation count identical regardless of
///             exponent bit values, which is the timing-attack
///             defense.
///          5. Convert the result back out of Montgomery form
///             (multiply by 1).
///          6. Emit as big-endian bytes.
///          Validates that `base < modulus` (per RSA primitive
///          definition) and that `modulus` is odd (Montgomery
///          arithmetic requires odd modulus). Returns 0 on validation
///          failure, 1 on success.
static int rsa_modexp_bytes(const uint8_t *base,
                            size_t base_len,
                            const uint8_t *exp,
                            size_t exp_len,
                            const uint8_t *mod,
                            size_t mod_len,
                            uint8_t *out,
                            size_t out_len) {
    uint64_t modulus_words[RT_RSA_MAX_WORDS];
    uint64_t base_words[RT_RSA_MAX_WORDS];
    uint64_t r0[RT_RSA_MAX_WORDS];
    uint64_t r1[RT_RSA_MAX_WORDS];
    uint64_t tmp0[RT_RSA_MAX_WORDS];
    uint64_t tmp1[RT_RSA_MAX_WORDS];
    uint64_t rr[RT_RSA_MAX_WORDS];
    uint64_t one_words[RT_RSA_MAX_WORDS];
    uint64_t n0_inv;
    size_t word_count;
    size_t bit_count;

    if (!base || !exp || !mod || !out || mod_len == 0 || out_len < mod_len || mod_len > RT_RSA_MAX_MOD_BYTES)
        return 0;
    if ((mod[mod_len - 1] & 1u) == 0)
        return 0;

    word_count = rsa_word_count_from_len(mod_len);
    if (word_count == 0 || word_count > RT_RSA_MAX_WORDS)
        return 0;

    rsa_words_from_be(mod, mod_len, modulus_words, word_count);
    rsa_words_from_be(base, base_len, base_words, word_count);
    if (rsa_words_cmp(base_words, modulus_words, word_count) >= 0)
        return 0;

    rsa_words_zero(one_words, word_count);
    one_words[0] = 1;
    rsa_compute_r_squared(rr, modulus_words, word_count);
    n0_inv = rsa_montgomery_n0_inv(modulus_words[0]);

    rsa_mont_mul(r0, one_words, rr, modulus_words, word_count, n0_inv);
    rsa_mont_mul(r1, base_words, rr, modulus_words, word_count, n0_inv);

    bit_count = rsa_bit_length_be(exp, exp_len);
    for (size_t bit_index = bit_count; bit_index-- > 0;) {
        uint64_t swap_mask = 0u - (uint64_t)rsa_bit_test_be(exp, exp_len, bit_index);
        rsa_words_cswap(r0, r1, word_count, swap_mask);
        rsa_mont_mul(tmp1, r0, r1, modulus_words, word_count, n0_inv);
        rsa_mont_mul(tmp0, r0, r0, modulus_words, word_count, n0_inv);
        memcpy(r0, tmp0, word_count * sizeof(*r0));
        memcpy(r1, tmp1, word_count * sizeof(*r1));
        rsa_words_cswap(r0, r1, word_count, swap_mask);
    }

    rsa_mont_mul(base_words, r0, one_words, modulus_words, word_count, n0_inv);
    rsa_words_to_be(base_words, word_count, out, mod_len);
    return 1;
}

/// @brief Encode a message digest into RSASSA-PSS padded form (RFC 8017 §9.1.1).
/// @details PSS = Probabilistic Signature Scheme. Adds a random salt
///          per signature so the same message signed twice produces
///          different signatures, plugging a class of weakness in
///          textbook RSA. Layout of the encoded message `EM`:
///          ```
///          EM = maskedDB || H || 0xBC
///          ```
///          where:
///          - `H = Hash(0x0000000000000000 || mHash || salt)` —
///             the "M' prime" construction binds salt to message.
///          - `DB = PS || 0x01 || salt` — padding string + sentinel
///            + salt, then XORed with `MGF1(H, |DB|)` to produce
///            `maskedDB`.
///          - The high bits of `EM[0]` are masked off to guarantee
///            `EM < N` (the RSA modulus) so the modular exponentiation
///            won't reject it.
///          Salt length = hash length here (the conventional choice).
///          Used only by signing operations, which Viper doesn't do
///          today — kept for symmetry with `rsa_pss_verify_encoded`.
static int rsa_pss_encode(const rt_rsa_key_t *key,
                          rt_rsa_hash_t hash_id,
                          const uint8_t *digest,
                          size_t digest_len,
                          uint8_t *em,
                          size_t em_len) {
    uint8_t salt[64];
    uint8_t m_prime[136];
    uint8_t h[64];
    uint8_t db[512];
    uint8_t db_mask[512];
    size_t h_len = rsa_hash_len(hash_id);
    size_t mod_bits = rsa_modulus_bits(key);
    size_t em_bits = mod_bits > 0 ? mod_bits - 1 : 0;
    size_t masked_db_len = 0;
    size_t ps_len = 0;
    uint8_t top_mask = 0xFF;

    if (!key || !digest || !em || h_len == 0 || digest_len != h_len)
        return 0;
    if (em_len < h_len * 2 + 2 || h_len > sizeof(salt))
        return 0;

    masked_db_len = em_len - h_len - 1;
    if (masked_db_len > sizeof(db))
        return 0;
    ps_len = masked_db_len - h_len - 1;
    if (8 * em_len > em_bits)
        top_mask = (uint8_t)(0xFFu >> (8 * em_len - em_bits));

    rt_crypto_random_bytes(salt, h_len);
    memset(m_prime, 0, 8);
    memcpy(m_prime + 8, digest, h_len);
    memcpy(m_prime + 8 + h_len, salt, h_len);
    if (!rsa_hash_buffer(hash_id, m_prime, 8 + h_len + h_len, h))
        return 0;

    memset(db, 0, ps_len);
    db[ps_len] = 0x01;
    memcpy(db + ps_len + 1, salt, h_len);
    if (!rsa_mgf1(hash_id, h, h_len, db_mask, masked_db_len))
        return 0;
    for (size_t i = 0; i < masked_db_len; i++)
        em[i] = db[i] ^ db_mask[i];
    em[0] &= top_mask;
    memcpy(em + masked_db_len, h, h_len);
    em[em_len - 1] = 0xBC;
    return 1;
}

/// @brief Verify a PSS-encoded message against an expected digest (RFC 8017 §9.1.2).
/// @details Inverse of `rsa_pss_encode`. Steps:
///          1. Validate the trailing `0xBC` sentinel and length floor.
///          2. Recover `H` from the right side of `EM`.
///          3. Recompute `dbMask = MGF1(H, |DB|)` and XOR with the
///             left side of `EM` to recover `DB = PS || 0x01 || salt`.
///          4. Mask the high bits of `DB[0]` to match what encoding
///             would have done.
///          5. Validate `PS` is all-zero and that the `0x01` separator
///             is present at the expected position.
///          6. Reconstruct `M' = 0x0000000000000000 || mHash || salt`,
///             hash it, and compare against `H` from step 2.
///          Returns 1 only if every check passes — any tampering
///          with the signature or the message digest causes one of
///          the checks to fail. This is the function the TLS
///          certificate validator and the TLS-server CertificateVerify
///          path call into for RSA-PSS-SHA256 signatures.
static int rsa_pss_verify_encoded(const rt_rsa_key_t *key,
                                  rt_rsa_hash_t hash_id,
                                  const uint8_t *digest,
                                  size_t digest_len,
                                  const uint8_t *em,
                                  size_t em_len) {
    uint8_t h[64];
    uint8_t db[512];
    uint8_t db_mask[512];
    uint8_t m_prime[136];
    size_t h_len = rsa_hash_len(hash_id);
    size_t mod_bits = rsa_modulus_bits(key);
    size_t em_bits = mod_bits > 0 ? mod_bits - 1 : 0;
    size_t masked_db_len = 0;
    size_t ps_len = 0;
    uint8_t top_mask = 0xFF;

    if (!key || !digest || !em || h_len == 0 || digest_len != h_len)
        return 0;
    if (em_len < h_len * 2 + 2 || em[em_len - 1] != 0xBC)
        return 0;

    masked_db_len = em_len - h_len - 1;
    if (masked_db_len > sizeof(db))
        return 0;
    if (8 * em_len > em_bits)
        top_mask = (uint8_t)(0xFFu >> (8 * em_len - em_bits));
    if ((em[0] & (uint8_t)~top_mask) != 0)
        return 0;

    memcpy(h, em + masked_db_len, h_len);
    if (!rsa_mgf1(hash_id, h, h_len, db_mask, masked_db_len))
        return 0;
    for (size_t i = 0; i < masked_db_len; i++)
        db[i] = em[i] ^ db_mask[i];
    db[0] &= top_mask;

    if (masked_db_len < h_len + 1)
        return 0;
    ps_len = masked_db_len - h_len - 1;
    for (size_t i = 0; i < ps_len; i++) {
        if (db[i] != 0x00)
            return 0;
    }
    if (db[ps_len] != 0x01)
        return 0;

    memset(m_prime, 0, 8);
    memcpy(m_prime + 8, digest, h_len);
    memcpy(m_prime + 8 + h_len, db + ps_len + 1, h_len);
    if (!rsa_hash_buffer(hash_id, m_prime, 8 + h_len + h_len, db_mask))
        return 0;
    return memcmp(db_mask, h, h_len) == 0 ? 1 : 0;
}

/// @brief Return the canonical DigestInfo DER prefix for PKCS#1 v1.5 RSA signatures.
/// @details PKCS#1 v1.5 (the older-style RSA signature scheme, still
///          widely used for cert-chain RSA signatures even when the
///          leaf cert uses PSS) wraps the message digest in a
///          fixed DER `DigestInfo` ASN.1 structure that names which
///          hash function produced it. These three byte arrays are
///          the precomputed DER for `SEQUENCE { algorithmIdentifier(SHA-N), OCTET STRING(digest) }`
///          with the digest portion left to be appended by the caller.
///          Returns NULL for unknown hash IDs.
static const uint8_t *rsa_pkcs1_digest_info_prefix(rt_rsa_hash_t hash_id, size_t *len_out) {
    static const uint8_t SHA256_PREFIX[] = {
        0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
        0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
    static const uint8_t SHA384_PREFIX[] = {
        0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
        0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30};
    static const uint8_t SHA512_PREFIX[] = {
        0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
        0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40};

    if (!len_out)
        return NULL;
    switch (hash_id) {
        case RT_RSA_HASH_SHA256:
            *len_out = sizeof(SHA256_PREFIX);
            return SHA256_PREFIX;
        case RT_RSA_HASH_SHA384:
            *len_out = sizeof(SHA384_PREFIX);
            return SHA384_PREFIX;
        case RT_RSA_HASH_SHA512:
            *len_out = sizeof(SHA512_PREFIX);
            return SHA512_PREFIX;
        default:
            *len_out = 0;
            return NULL;
    }
}

/// @brief Zero-initialize an RSA key struct (sets all pointers and lengths to 0).
/// @details Idempotent. Safe to call on uninitialized memory before
///          first use. Pair with `rt_rsa_key_free` for proper
///          lifetime management.
void rt_rsa_key_init(rt_rsa_key_t *key) {
    if (!key)
        return;
    memset(key, 0, sizeof(*key));
}

/// @brief Free heap-allocated key components and zero out the struct.
/// @details Frees the modulus, public exponent, and private exponent
///          buffers (each was malloc'd by the parsers via
///          `rsa_dup_be_trimmed`). Final memset ensures any
///          subsequent accidental read sees zeros, not stale
///          pointers — a small defense against use-after-free
///          patterns. Safe on null input.
void rt_rsa_key_free(rt_rsa_key_t *key) {
    if (!key)
        return;
    free(key->modulus);
    free(key->public_exponent);
    free(key->private_exponent);
    memset(key, 0, sizeof(*key));
}

/// @brief Parse an RSA public key from PKCS#1 RSAPublicKey DER bytes.
/// @details PKCS#1 RSAPublicKey is the simplest RSA public-key
///          encoding: a SEQUENCE of two INTEGERs — modulus first,
///          public exponent second. This is the format used inside
///          the SubjectPublicKeyInfo BIT STRING of an X.509
///          certificate (X.509 wraps it in another layer that the
///          caller has to peel off first). Returns 0 on malformed
///          input; on success, fills `out` and the caller owns the
///          modulus/exponent buffers via `rt_rsa_key_free`.
int rt_rsa_parse_public_key_pkcs1(const uint8_t *der, size_t der_len, rt_rsa_key_t *out) {
    uint8_t tag;
    size_t value_len, hdr_len, pos = 0;
    rt_rsa_key_t key;

    if (!der || der_len == 0 || !out)
        return 0;
    rt_rsa_key_init(&key);
    if (!rsa_der_read_tlv(der, der_len, &tag, &value_len, &hdr_len) || tag != 0x30)
        return 0;
    pos = hdr_len;
    if (!rsa_parse_der_integer(der, hdr_len + value_len, &pos, &key.modulus, &key.modulus_len) ||
        !rsa_parse_der_integer(
            der, hdr_len + value_len, &pos, &key.public_exponent, &key.public_exponent_len)) {
        rt_rsa_key_free(&key);
        return 0;
    }
    rt_rsa_key_free(out);
    *out = key;
    return 1;
}

/// @brief Parse an RSA private key from PKCS#1 RSAPrivateKey DER bytes.
/// @details PKCS#1 RSAPrivateKey is a longer SEQUENCE: version,
///          modulus, public exponent, private exponent, primes p
///          and q, exponents dP and dQ, coefficient qInv, and an
///          optional otherPrimeInfos. We parse just the version,
///          modulus, public exponent, and private exponent —
///          everything else is for CRT-accelerated decryption,
///          which Viper doesn't currently use (we only need the
///          private exponent for direct mod-exp).
///          The version field is parsed and discarded (we don't
///          enforce a specific value because both single-prime and
///          multi-prime versions use the same first four fields).
int rt_rsa_parse_private_key_pkcs1(const uint8_t *der, size_t der_len, rt_rsa_key_t *out) {
    uint8_t tag;
    size_t value_len, hdr_len, pos = 0;
    uint8_t *version = NULL;
    size_t version_len = 0;
    rt_rsa_key_t key;

    if (!der || der_len == 0 || !out)
        return 0;
    rt_rsa_key_init(&key);
    if (!rsa_der_read_tlv(der, der_len, &tag, &value_len, &hdr_len) || tag != 0x30)
        return 0;
    pos = hdr_len;
    if (!rsa_parse_der_integer(der, hdr_len + value_len, &pos, &version, &version_len) ||
        !rsa_parse_der_integer(der, hdr_len + value_len, &pos, &key.modulus, &key.modulus_len) ||
        !rsa_parse_der_integer(
            der, hdr_len + value_len, &pos, &key.public_exponent, &key.public_exponent_len) ||
        !rsa_parse_der_integer(
            der, hdr_len + value_len, &pos, &key.private_exponent, &key.private_exponent_len)) {
        free(version);
        rt_rsa_key_free(&key);
        return 0;
    }
    free(version);
    rt_rsa_key_free(out);
    *out = key;
    return 1;
}

/// @brief Parse an RSA private key from PKCS#8 PrivateKeyInfo DER bytes.
/// @details PKCS#8 is the modern wrapper that most contemporary tools
///          (OpenSSL, Go, etc.) emit by default for `.key` files.
///          Layout:
///          ```
///          PrivateKeyInfo ::= SEQUENCE {
///              version                Integer (0)
///              algorithm              AlgorithmIdentifier
///              privateKey             OCTET STRING (containing PKCS#1 RSAPrivateKey)
///          }
///          ```
///          We:
///          1. Read the outer SEQUENCE.
///          2. Skip the version INTEGER.
///          3. Read the AlgorithmIdentifier and verify the OID is
///             rsaEncryption (`1.2.840.113549.1.1.1` =
///             `OID_RSA_ENCRYPTION` in DER).
///          4. Read the OCTET STRING and recursively parse its
///             contents as PKCS#1 RSAPrivateKey via
///             `rt_rsa_parse_private_key_pkcs1`.
///          Returns 0 on any algorithm mismatch (we only support
///          rsaEncryption — encrypted PKCS#8 keys would need
///          additional handling).
int rt_rsa_parse_private_key_pkcs8(const uint8_t *der, size_t der_len, rt_rsa_key_t *out) {
    static const uint8_t OID_RSA_ENCRYPTION[] = {
        0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
    uint8_t tag;
    size_t value_len, hdr_len, pos = 0;
    size_t alg_len = 0, alg_hdr = 0;
    uint8_t *version = NULL;
    size_t version_len = 0;

    if (!der || der_len == 0 || !out)
        return 0;
    if (!rsa_der_read_tlv(der, der_len, &tag, &value_len, &hdr_len) || tag != 0x30)
        return 0;
    pos = hdr_len;
    if (!rsa_parse_der_integer(der, hdr_len + value_len, &pos, &version, &version_len)) {
        free(version);
        return 0;
    }
    free(version);
    if (!rsa_der_read_tlv(der + pos, hdr_len + value_len - pos, &tag, &alg_len, &alg_hdr) ||
        tag != 0x30) {
        return 0;
    }
    {
        const uint8_t *alg = der + pos + alg_hdr;
        size_t alg_rem = alg_len;
        pos += alg_hdr + alg_len;
        if (!rsa_der_read_tlv(alg, alg_rem, &tag, &value_len, &hdr_len) || tag != 0x06)
            return 0;
        if (value_len != sizeof(OID_RSA_ENCRYPTION) ||
            memcmp(alg + hdr_len, OID_RSA_ENCRYPTION, sizeof(OID_RSA_ENCRYPTION)) != 0) {
            return 0;
        }
    }
    if (!rsa_der_read_tlv(der + pos, hdr_len + value_len - pos, &tag, &value_len, &hdr_len) ||
        tag != 0x04) {
        return 0;
    }
    return rt_rsa_parse_private_key_pkcs1(der + pos + hdr_len, value_len, out);
}

/// @brief Compare two RSA public keys for byte-for-byte equality of modulus and public exponent.
/// @details Used by the TLS server to check whether a leaf cert's
///          public key matches a previously-loaded private key
///          (mismatched leaf-cert / private-key pairs are a common
///          configuration mistake; better to detect it at load time
///          than during the first failed handshake). Returns 0 if
///          either key is missing required fields.
int rt_rsa_public_equals(const rt_rsa_key_t *lhs, const rt_rsa_key_t *rhs) {
    if (!lhs || !rhs || !lhs->modulus || !rhs->modulus || !lhs->public_exponent ||
        !rhs->public_exponent) {
        return 0;
    }
    return lhs->modulus_len == rhs->modulus_len &&
                   lhs->public_exponent_len == rhs->public_exponent_len &&
                   memcmp(lhs->modulus, rhs->modulus, lhs->modulus_len) == 0 &&
                   memcmp(lhs->public_exponent, rhs->public_exponent, lhs->public_exponent_len) ==
                       0
               ? 1
               : 0;
}

/// @brief Produce an RSA-PSS signature over `digest` using the private key in `key`.
/// @details Two-step process:
///          1. PSS-encode `digest` into a `modulus_len`-byte block
///             (`rsa_pss_encode` adds the salt + MGF1 mask + sentinel).
///          2. Modular-exponentiate the encoded block by the private
///             exponent to produce the signature
///             (`signature = encoded^d mod N`).
///          On success writes the signature to `sig_out` and the
///          actual length to `*sig_len_out` (always equals
///          `modulus_len`). Returns 0 on any encoding or arithmetic
///          failure. Used by the TLS server's CertificateVerify
///          path when the server's key is RSA.
int rt_rsa_pss_sign(const rt_rsa_key_t *key,
                    rt_rsa_hash_t hash_id,
                    const uint8_t *digest,
                    size_t digest_len,
                    uint8_t *sig_out,
                    size_t *sig_len_out) {
    uint8_t em[RT_RSA_MAX_MOD_BYTES];
    size_t em_len = 0;

    if (!key || !key->modulus || !key->private_exponent || !sig_out || !sig_len_out)
        return 0;
    em_len = key->modulus_len;
    if (em_len == 0 || em_len > sizeof(em) || *sig_len_out < em_len)
        return 0;
    if (!rsa_pss_encode(key, hash_id, digest, digest_len, em, em_len))
        return 0;
    if (!rsa_modexp_bytes(em,
                          em_len,
                          key->private_exponent,
                          key->private_exponent_len,
                          key->modulus,
                          key->modulus_len,
                          sig_out,
                          *sig_len_out)) {
        return 0;
    }
    *sig_len_out = em_len;
    return 1;
}

/// @brief Verify an RSA-PSS signature against an expected digest using the public key in `key`.
/// @details Two-step process (inverse of sign):
///          1. Modular-exponentiate the signature bytes by the public
///             exponent to recover the encoded message
///             (`encoded = signature^e mod N`).
///          2. PSS-verify the recovered encoding against `digest`
///             (`rsa_pss_verify_encoded` checks the salt-binding hash).
///          Returns 1 only if the recovered encoding is well-formed
///          AND its digest matches what the caller provided.
///          This is the function the TLS validator and TLS-server
///          handshake call into for RSA-PSS-SHA256 cert / handshake
///          signature verification, which is the most common
///          modern-TLS signature path.
int rt_rsa_pss_verify(const rt_rsa_key_t *key,
                      rt_rsa_hash_t hash_id,
                      const uint8_t *digest,
                      size_t digest_len,
                      const uint8_t *sig,
                      size_t sig_len) {
    uint8_t em[RT_RSA_MAX_MOD_BYTES];

    if (!key || !key->modulus || !key->public_exponent || !sig)
        return 0;
    if (key->modulus_len == 0 || key->modulus_len > sizeof(em) || sig_len != key->modulus_len)
        return 0;
    if (!rsa_modexp_bytes(sig,
                          sig_len,
                          key->public_exponent,
                          key->public_exponent_len,
                          key->modulus,
                          key->modulus_len,
                          em,
                          sizeof(em))) {
        return 0;
    }
    return rsa_pss_verify_encoded(key, hash_id, digest, digest_len, em, key->modulus_len);
}

/// @brief Verify an RSA PKCS#1 v1.5 signature against an expected digest using the public key.
/// @details The older RSA signature scheme, still widely used for
///          X.509 certificate-chain signatures even when the leaf
///          uses PSS. Verification:
///          1. Recover the encoded message via `signature^e mod N`.
///          2. Validate the deterministic v1.5 padding layout:
///             `0x00 0x01 0xFF...0xFF 0x00 DigestInfo digest`.
///          3. Match `DigestInfo` against the expected hash-algorithm
///             prefix (from `rsa_pkcs1_digest_info_prefix`).
///          4. Compare the trailing digest bytes against the
///             provided digest.
///          The 8+ `0xFF` minimum padding length prevents the
///          Bleichenbacher-style attacks that targeted weak v1.5
///          implementations (the original 1998 attack required
///          variable-length padding to leak information; modern
///          deterministic encoding closes that). Returns 1 only on
///          full structural and digest match. Used by the X.509
///          chain validator for cert-chain RSA signatures.
int rt_rsa_pkcs1_v15_verify(const rt_rsa_key_t *key,
                            rt_rsa_hash_t hash_id,
                            const uint8_t *digest,
                            size_t digest_len,
                            const uint8_t *sig,
                            size_t sig_len) {
    uint8_t em[RT_RSA_MAX_MOD_BYTES];
    const uint8_t *prefix = NULL;
    size_t prefix_len = 0;
    size_t i = 0;

    if (!key || !key->modulus || !key->public_exponent || !digest || !sig)
        return 0;
    if (key->modulus_len == 0 || key->modulus_len > sizeof(em) || sig_len != key->modulus_len)
        return 0;

    prefix = rsa_pkcs1_digest_info_prefix(hash_id, &prefix_len);
    if (!prefix || digest_len != rsa_hash_len(hash_id))
        return 0;

    if (!rsa_modexp_bytes(sig,
                          sig_len,
                          key->public_exponent,
                          key->public_exponent_len,
                          key->modulus,
                          key->modulus_len,
                          em,
                          sizeof(em))) {
        return 0;
    }
    if (em[0] != 0x00 || em[1] != 0x01)
        return 0;
    for (i = 2; i < key->modulus_len && em[i] == 0xFF; i++) {
    }
    if (i < 10 || i >= key->modulus_len || em[i] != 0x00)
        return 0;
    i++;
    if (key->modulus_len - i != prefix_len + digest_len)
        return 0;
    if (memcmp(em + i, prefix, prefix_len) != 0)
        return 0;
    return memcmp(em + i + prefix_len, digest, digest_len) == 0 ? 1 : 0;
}
