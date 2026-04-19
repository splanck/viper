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

static size_t rsa_word_count_from_len(size_t len) {
    return (len + sizeof(uint64_t) - 1) / sizeof(uint64_t);
}

static void rsa_words_zero(uint64_t *words, size_t count) {
    memset(words, 0, count * sizeof(*words));
}

static void rsa_words_from_be(const uint8_t *data, size_t len, uint64_t *out, size_t out_words) {
    rsa_words_zero(out, out_words);
    for (size_t i = 0; i < len; i++)
        out[i / 8] |= (uint64_t)data[len - 1 - i] << ((i % 8) * 8);
}

static void rsa_words_to_be(const uint64_t *words, size_t word_count, uint8_t *out, size_t out_len) {
    memset(out, 0, out_len);
    for (size_t i = 0; i < out_len; i++) {
        size_t word_index = i / 8;
        if (word_index >= word_count)
            break;
        out[out_len - 1 - i] = (uint8_t)(words[word_index] >> ((i % 8) * 8));
    }
}

static int rsa_words_cmp(const uint64_t *a, const uint64_t *b, size_t count) {
    for (size_t i = count; i-- > 0;) {
        if (a[i] < b[i])
            return -1;
        if (a[i] > b[i])
            return 1;
    }
    return 0;
}

static void rsa_words_sub_inplace(uint64_t *a, const uint64_t *b, size_t count) {
    unsigned __int128 borrow = 0;
    for (size_t i = 0; i < count; i++) {
        unsigned __int128 lhs = a[i];
        unsigned __int128 rhs = (unsigned __int128)b[i] + borrow;
        a[i] = (uint64_t)(lhs - rhs);
        borrow = lhs < rhs;
    }
}

static void rsa_words_cswap(uint64_t *a, uint64_t *b, size_t count, uint64_t mask) {
    for (size_t i = 0; i < count; i++) {
        uint64_t diff = mask & (a[i] ^ b[i]);
        a[i] ^= diff;
        b[i] ^= diff;
    }
}

static void rsa_words_accumulate(uint64_t *words, size_t word_count, size_t index, uint64_t value) {
    while (value != 0 && index < word_count) {
        unsigned __int128 acc = (unsigned __int128)words[index] + value;
        words[index] = (uint64_t)acc;
        value = (uint64_t)(acc >> 64);
        index++;
    }
}

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

static void rsa_compute_r_squared(uint64_t *out, const uint64_t *modulus, size_t word_count) {
    rsa_words_zero(out, word_count);
    out[0] = 1;
    for (size_t i = 0; i < word_count * 128; i++)
        rsa_mod_double_inplace(out, modulus, word_count);
}

static uint64_t rsa_montgomery_n0_inv(uint64_t n0) {
    uint64_t x = 1;
    for (int i = 0; i < 6; i++)
        x *= 2 - n0 * x;
    return (uint64_t)(0 - x);
}

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

static int rsa_bit_test_be(const uint8_t *data, size_t len, size_t bit_index) {
    size_t byte_index = len - 1 - (bit_index / 8);
    return (data[byte_index] >> (bit_index % 8)) & 1;
}

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

void rt_rsa_key_init(rt_rsa_key_t *key) {
    if (!key)
        return;
    memset(key, 0, sizeof(*key));
}

void rt_rsa_key_free(rt_rsa_key_t *key) {
    if (!key)
        return;
    free(key->modulus);
    free(key->public_exponent);
    free(key->private_exponent);
    memset(key, 0, sizeof(*key));
}

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
