/**
 * @file rsa.cpp
 * @brief RSA signing implementation for SSH.
 *
 * @details
 * Implements RSA PKCS#1 v1.5 signing for SSH public key authentication.
 * Uses a simple big integer implementation suitable for kernel use.
 */

#include "rsa.hpp"
#include "sha256.hpp"
#include "../../../lib/mem.hpp"

namespace viper::crypto
{

//=============================================================================
// Big Integer Operations (similar to cert/verify.cpp but with signing support)
//=============================================================================

struct BigInt
{
    static constexpr usize MAX_WORDS = 128;  // 4096 bits
    u32 words[MAX_WORDS];
    usize length;
};

static void bigint_init(BigInt *n)
{
    lib::memset(n->words, 0, sizeof(n->words));
    n->length = 0;
}

static void bigint_from_bytes(BigInt *n, const u8 *data, usize len)
{
    bigint_init(n);

    while (len > 0 && *data == 0) {
        data++;
        len--;
    }

    if (len == 0) {
        n->length = 1;
        return;
    }

    n->length = (len + 3) / 4;
    if (n->length > BigInt::MAX_WORDS) {
        n->length = BigInt::MAX_WORDS;
        len = n->length * 4;
    }

    for (usize i = 0; i < len; i++) {
        usize word_idx = (len - 1 - i) / 4;
        usize byte_idx = (len - 1 - i) % 4;
        n->words[word_idx] |= static_cast<u32>(data[i]) << (byte_idx * 8);
    }
}

static void bigint_to_bytes(const BigInt *n, u8 *out, usize out_len)
{
    lib::memset(out, 0, out_len);

    for (usize i = 0; i < n->length && i < (out_len + 3) / 4; i++) {
        u32 word = n->words[i];
        for (int j = 0; j < 4; j++) {
            usize byte_pos = out_len - 1 - (i * 4 + j);
            if (byte_pos < out_len) {
                out[byte_pos] = (word >> (j * 8)) & 0xFF;
            }
        }
    }
}

static int bigint_compare(const BigInt *a, const BigInt *b)
{
    if (a->length != b->length) {
        return a->length > b->length ? 1 : -1;
    }
    for (usize i = a->length; i > 0; i--) {
        if (a->words[i - 1] != b->words[i - 1]) {
            return a->words[i - 1] > b->words[i - 1] ? 1 : -1;
        }
    }
    return 0;
}

static void bigint_copy(BigInt *dst, const BigInt *src)
{
    lib::memcpy(dst->words, src->words, src->length * sizeof(u32));
    dst->length = src->length;
}

static void bigint_sub(BigInt *dst, const BigInt *a, const BigInt *b)
{
    i64 borrow = 0;
    dst->length = a->length;

    for (usize i = 0; i < a->length; i++) {
        i64 diff = static_cast<i64>(a->words[i]) -
                   static_cast<i64>(i < b->length ? b->words[i] : 0) - borrow;
        if (diff < 0) {
            diff += 0x100000000LL;
            borrow = 1;
        } else {
            borrow = 0;
        }
        dst->words[i] = static_cast<u32>(diff);
    }

    while (dst->length > 1 && dst->words[dst->length - 1] == 0) {
        dst->length--;
    }
}

static void bigint_mulmod(BigInt *dst, const BigInt *a, const BigInt *b, const BigInt *m)
{
    u64 result[BigInt::MAX_WORDS * 2] = {0};
    usize result_len = a->length + b->length;

    for (usize i = 0; i < a->length; i++) {
        u64 carry = 0;
        for (usize j = 0; j < b->length; j++) {
            u64 prod = static_cast<u64>(a->words[i]) * static_cast<u64>(b->words[j]) +
                       result[i + j] + carry;
            result[i + j] = prod & 0xFFFFFFFF;
            carry = prod >> 32;
        }
        result[i + b->length] += carry;
    }

    while (result_len > 1 && result[result_len - 1] == 0) {
        result_len--;
    }

    BigInt temp;
    temp.length = result_len;
    for (usize i = 0; i < result_len; i++) {
        temp.words[i] = static_cast<u32>(result[i]);
    }

    while (bigint_compare(&temp, m) >= 0) {
        BigInt shifted;
        bigint_copy(&shifted, m);

        int shift = 0;
        while (bigint_compare(&shifted, &temp) < 0 && shift < 32) {
            u32 carry = 0;
            for (usize i = 0; i < shifted.length; i++) {
                u32 new_carry = shifted.words[i] >> 31;
                shifted.words[i] = (shifted.words[i] << 1) | carry;
                carry = new_carry;
            }
            if (carry && shifted.length < BigInt::MAX_WORDS) {
                shifted.words[shifted.length++] = carry;
            }
            shift++;
        }

        if (bigint_compare(&shifted, &temp) > 0 && shift > 0) {
            u32 carry = 0;
            for (usize i = shifted.length; i > 0; i--) {
                u32 new_carry = shifted.words[i - 1] & 1;
                shifted.words[i - 1] = (shifted.words[i - 1] >> 1) | (carry << 31);
                carry = new_carry;
            }
            while (shifted.length > 1 && shifted.words[shifted.length - 1] == 0) {
                shifted.length--;
            }
        }

        if (bigint_compare(&temp, &shifted) >= 0) {
            BigInt new_temp;
            bigint_sub(&new_temp, &temp, &shifted);
            bigint_copy(&temp, &new_temp);
        } else {
            BigInt new_temp;
            bigint_sub(&new_temp, &temp, m);
            bigint_copy(&temp, &new_temp);
        }
    }

    bigint_copy(dst, &temp);
}

static void bigint_powmod(BigInt *dst, const BigInt *base, const BigInt *exp, const BigInt *m)
{
    BigInt result, b;
    bigint_init(&result);
    result.words[0] = 1;
    result.length = 1;

    bigint_copy(&b, base);

    BigInt temp;
    bigint_mulmod(&temp, &b, &result, m);
    bigint_copy(&b, &temp);

    for (usize i = 0; i < exp->length; i++) {
        u32 word = exp->words[i];
        for (int bit = 0; bit < 32; bit++) {
            if (word & 1) {
                bigint_mulmod(&temp, &result, &b, m);
                bigint_copy(&result, &temp);
            }
            bigint_mulmod(&temp, &b, &b, m);
            bigint_copy(&b, &temp);
            word >>= 1;
        }
    }

    bigint_copy(dst, &result);
}

//=============================================================================
// PKCS#1 v1.5 Padding
//=============================================================================

// DigestInfo for SHA-256
static const u8 sha256_digest_info[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0x04, 0x20
};

/**
 * @brief Create PKCS#1 v1.5 signature padding.
 *
 * Format: 0x00 || 0x01 || PS || 0x00 || DigestInfo || Hash
 * where PS is 0xFF bytes
 */
static bool create_pkcs1_padding(u8 *em, usize em_len, const u8 hash[32])
{
    constexpr usize digest_info_len = sizeof(sha256_digest_info);
    constexpr usize hash_len = 32;
    usize t_len = digest_info_len + hash_len;

    if (em_len < t_len + 11) {
        return false;  // Key too short
    }

    usize ps_len = em_len - t_len - 3;

    // Build: 0x00 || 0x01 || PS || 0x00 || T
    em[0] = 0x00;
    em[1] = 0x01;
    lib::memset(em + 2, 0xFF, ps_len);
    em[2 + ps_len] = 0x00;
    lib::memcpy(em + 3 + ps_len, sha256_digest_info, digest_info_len);
    lib::memcpy(em + 3 + ps_len + digest_info_len, hash, hash_len);

    return true;
}

//=============================================================================
// Public API
//=============================================================================

bool rsa_sign_sha256(const RsaPrivateKey *key,
                     const void *data,
                     usize data_len,
                     u8 *signature,
                     usize *sig_len)
{
    if (!key || !data || !signature || !sig_len) {
        return false;
    }

    // Hash the data
    u8 hash[SHA256_DIGEST_SIZE];
    sha256(data, data_len, hash);

    return rsa_sign_hash_sha256(key, hash, signature, sig_len);
}

bool rsa_sign_hash_sha256(const RsaPrivateKey *key,
                          const u8 hash[32],
                          u8 *signature,
                          usize *sig_len)
{
    if (!key || !hash || !signature || !sig_len) {
        return false;
    }

    if (key->modulus_len < RSA_MIN_KEY_BYTES || key->modulus_len > RSA_MAX_KEY_BYTES) {
        return false;
    }

    // Create padded message
    u8 em[RSA_MAX_KEY_BYTES];
    if (!create_pkcs1_padding(em, key->modulus_len, hash)) {
        return false;
    }

    // Convert to BigInt
    BigInt m, n, d, sig;
    bigint_from_bytes(&m, em, key->modulus_len);
    bigint_from_bytes(&n, key->modulus, key->modulus_len);
    bigint_from_bytes(&d, key->private_exponent, key->private_exponent_len);

    // sig = m^d mod n
    bigint_powmod(&sig, &m, &d, &n);

    // Convert back to bytes
    bigint_to_bytes(&sig, signature, key->modulus_len);
    *sig_len = key->modulus_len;

    return true;
}

bool rsa_verify_sha256(const RsaPublicKey *key,
                       const void *data,
                       usize data_len,
                       const u8 *signature,
                       usize sig_len)
{
    if (!key || !data || !signature) {
        return false;
    }

    if (key->modulus_len < RSA_MIN_KEY_BYTES || key->modulus_len > RSA_MAX_KEY_BYTES) {
        return false;
    }

    // Hash the data
    u8 hash[SHA256_DIGEST_SIZE];
    sha256(data, data_len, hash);

    // Convert signature to BigInt
    BigInt s, n, e, decrypted;
    bigint_from_bytes(&s, signature, sig_len);
    bigint_from_bytes(&n, key->modulus, key->modulus_len);
    bigint_from_bytes(&e, key->exponent, key->exponent_len);

    // decrypted = s^e mod n
    bigint_powmod(&decrypted, &s, &e, &n);

    // Convert to bytes
    u8 em[RSA_MAX_KEY_BYTES];
    bigint_to_bytes(&decrypted, em, key->modulus_len);

    // Verify padding
    if (em[0] != 0x00 || em[1] != 0x01) {
        return false;
    }

    usize pad_end = 2;
    while (pad_end < key->modulus_len && em[pad_end] == 0xFF) {
        pad_end++;
    }

    if (pad_end >= key->modulus_len || em[pad_end] != 0x00) {
        return false;
    }
    pad_end++;

    constexpr usize prefix_len = sizeof(sha256_digest_info);
    if (key->modulus_len - pad_end < prefix_len + SHA256_DIGEST_SIZE) {
        return false;
    }

    // Check DigestInfo
    for (usize i = 0; i < prefix_len; i++) {
        if (em[pad_end + i] != sha256_digest_info[i]) {
            return false;
        }
    }

    // Check hash
    for (usize i = 0; i < SHA256_DIGEST_SIZE; i++) {
        if (em[pad_end + prefix_len + i] != hash[i]) {
            return false;
        }
    }

    return true;
}

// Helper to read SSH mpint (big-endian with length prefix)
static bool read_mpint(const u8 *&ptr, usize &remaining, u8 *out, usize max_len, usize *out_len)
{
    if (remaining < 4) return false;

    u32 len = (static_cast<u32>(ptr[0]) << 24) |
              (static_cast<u32>(ptr[1]) << 16) |
              (static_cast<u32>(ptr[2]) << 8) |
              static_cast<u32>(ptr[3]);
    ptr += 4;
    remaining -= 4;

    if (len > remaining || len > max_len) return false;

    // Skip leading zero if present (sign bit handling)
    const u8 *data = ptr;
    usize data_len = len;
    if (data_len > 0 && data[0] == 0) {
        data++;
        data_len--;
    }

    lib::memcpy(out, data, data_len);
    *out_len = data_len;

    ptr += len;
    remaining -= len;
    return true;
}

// Helper to read SSH string
static bool read_string(const u8 *&ptr, usize &remaining, const char *expected)
{
    if (remaining < 4) return false;

    u32 len = (static_cast<u32>(ptr[0]) << 24) |
              (static_cast<u32>(ptr[1]) << 16) |
              (static_cast<u32>(ptr[2]) << 8) |
              static_cast<u32>(ptr[3]);
    ptr += 4;
    remaining -= 4;

    if (len > remaining) return false;

    usize expected_len = 0;
    while (expected[expected_len]) expected_len++;

    if (len != expected_len) {
        ptr += len;
        remaining -= len;
        return false;
    }

    for (usize i = 0; i < len; i++) {
        if (ptr[i] != static_cast<u8>(expected[i])) {
            ptr += len;
            remaining -= len;
            return false;
        }
    }

    ptr += len;
    remaining -= len;
    return true;
}

bool rsa_parse_ssh_public_key(const u8 *blob, usize blob_len, RsaPublicKey *key)
{
    if (!blob || !key || blob_len < 4) return false;

    const u8 *ptr = blob;
    usize remaining = blob_len;

    // Read and verify key type
    if (!read_string(ptr, remaining, "ssh-rsa")) {
        return false;
    }

    // Read e (public exponent)
    if (!read_mpint(ptr, remaining, key->exponent, sizeof(key->exponent), &key->exponent_len)) {
        return false;
    }

    // Read n (modulus)
    if (!read_mpint(ptr, remaining, key->modulus, sizeof(key->modulus), &key->modulus_len)) {
        return false;
    }

    return true;
}

bool rsa_parse_openssh_private_key(const u8 *data, usize data_len, RsaPrivateKey *key)
{
    // OpenSSH private key format is complex
    // For now, this is a placeholder - actual implementation would need
    // to handle the openssh-key-v1 format including:
    // - AUTH_MAGIC header
    // - cipher name
    // - kdf name
    // - kdf options
    // - number of keys
    // - public key blob
    // - encrypted private key blob

    // This simplified version assumes an unencrypted key in raw format
    // Real implementation would need proper parsing

    (void)data;
    (void)data_len;
    (void)key;

    // TODO: Implement full OpenSSH private key parsing
    return false;
}

void rsa_public_from_private(const RsaPrivateKey *priv, RsaPublicKey *pub)
{
    if (!priv || !pub) return;

    lib::memcpy(pub->modulus, priv->modulus, priv->modulus_len);
    pub->modulus_len = priv->modulus_len;
    lib::memcpy(pub->exponent, priv->public_exponent, priv->public_exponent_len);
    pub->exponent_len = priv->public_exponent_len;
}

} // namespace viper::crypto
