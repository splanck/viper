/**
 * @file verify.cpp
 * @brief Certificate chain verification implementation.
 *
 * @details
 * Implements the certificate verification routines declared in `verify.hpp`.
 * The v0.2.0 verifier focuses on RSA-based signature checking and simple chain
 * building against an embedded root CA store.
 *
 * This file also includes a small "verification-only" big integer
 * implementation to support RSA modular exponentiation. It is intentionally
 * simple and not optimized; it exists solely to validate signatures during
 * bring-up.
 */

#include "verify.hpp"
#include "../../../lib/mem.hpp"
#include "../crypto/sha256.hpp"

namespace viper::tls::cert
{

// Big integer for RSA operations (simple implementation for verification only)
// Supports up to 4096-bit numbers
/**
 * @brief Big integer type used for RSA verification.
 *
 * @details
 * Represents an unsigned big integer as an array of 32-bit words in
 * little-endian order (word 0 is the least significant). Only the operations
 * required for RSA verification are implemented.
 */
struct BigInt
{
    static constexpr usize MAX_WORDS = 128; // 128 * 32 bits = 4096 bits
    u32 words[MAX_WORDS];
    usize length; // Number of words used
};

/**
 * @brief Initialize a BigInt to zero.
 *
 * @param n BigInt to initialize.
 */
static void bigint_init(BigInt *n)
{
    lib::memset(n->words, 0, sizeof(n->words));
    n->length = 0;
}

/**
 * @brief Convert a big-endian byte string into a BigInt.
 *
 * @details
 * Skips leading zeros and converts the remaining big-endian bytes into the
 * internal little-endian word representation. Values larger than the supported
 * maximum are truncated.
 *
 * @param n Output BigInt.
 * @param data Big-endian bytes.
 * @param len Number of bytes in `data`.
 */
static void bigint_from_bytes(BigInt *n, const u8 *data, usize len)
{
    bigint_init(n);

    // Skip leading zeros in input
    while (len > 0 && *data == 0)
    {
        data++;
        len--;
    }

    if (len == 0)
    {
        n->length = 1;
        return;
    }

    // Convert big-endian bytes to little-endian words
    n->length = (len + 3) / 4;
    if (n->length > BigInt::MAX_WORDS)
    {
        n->length = BigInt::MAX_WORDS;
        len = n->length * 4;
    }

    for (usize i = 0; i < n->length; i++)
    {
        n->words[i] = 0;
    }

    for (usize i = 0; i < len; i++)
    {
        usize word_idx = (len - 1 - i) / 4;
        usize byte_idx = (len - 1 - i) % 4;
        n->words[word_idx] |= static_cast<u32>(data[i]) << (byte_idx * 8);
    }
}

/**
 * @brief Convert a BigInt to a fixed-length big-endian byte string.
 *
 * @details
 * Writes the value into `out` as a big-endian integer of exactly `out_len`
 * bytes, zero-padding as needed.
 *
 * @param n Input BigInt.
 * @param out Output buffer.
 * @param out_len Number of bytes to write.
 */
static void bigint_to_bytes(const BigInt *n, u8 *out, usize out_len)
{
    lib::memset(out, 0, out_len);

    for (usize i = 0; i < n->length && i < (out_len + 3) / 4; i++)
    {
        u32 word = n->words[i];
        for (int j = 0; j < 4; j++)
        {
            usize byte_pos = out_len - 1 - (i * 4 + j);
            if (byte_pos < out_len)
            {
                out[byte_pos] = (word >> (j * 8)) & 0xFF;
            }
        }
    }
}

/**
 * @brief Compare two BigInts.
 *
 * @param a First value.
 * @param b Second value.
 * @return 1 if a > b, 0 if equal, -1 if a < b.
 */
static int bigint_compare(const BigInt *a, const BigInt *b)
{
    if (a->length != b->length)
    {
        return a->length > b->length ? 1 : -1;
    }
    for (usize i = a->length; i > 0; i--)
    {
        if (a->words[i - 1] != b->words[i - 1])
        {
            return a->words[i - 1] > b->words[i - 1] ? 1 : -1;
        }
    }
    return 0;
}

/**
 * @brief Copy a BigInt.
 *
 * @param dst Destination BigInt.
 * @param src Source BigInt.
 */
static void bigint_copy(BigInt *dst, const BigInt *src)
{
    lib::memcpy(dst->words, src->words, src->length * sizeof(u32));
    dst->length = src->length;
}

// dst = a - b (assumes a >= b)
/**
 * @brief Compute `dst = a - b` for unsigned BigInts (a must be >= b).
 *
 * @details
 * Performs word-wise subtraction with borrow and normalizes the resulting
 * length by trimming leading zero words.
 *
 * @param dst Output result.
 * @param a Minuend (must be >= b).
 * @param b Subtrahend.
 */
static void bigint_sub(BigInt *dst, const BigInt *a, const BigInt *b)
{
    i64 borrow = 0;
    dst->length = a->length;

    for (usize i = 0; i < a->length; i++)
    {
        i64 diff = static_cast<i64>(a->words[i]) -
                   static_cast<i64>(i < b->length ? b->words[i] : 0) - borrow;
        if (diff < 0)
        {
            diff += 0x100000000LL;
            borrow = 1;
        }
        else
        {
            borrow = 0;
        }
        dst->words[i] = static_cast<u32>(diff);
    }

    // Normalize length
    while (dst->length > 1 && dst->words[dst->length - 1] == 0)
    {
        dst->length--;
    }
}

// dst = (a * b) mod m using Montgomery-like reduction
// Simple but slow - used only for verification
/**
 * @brief Compute `dst = (a * b) mod m`.
 *
 * @details
 * Multiplies two BigInts into a wider intermediate and then reduces modulo `m`
 * using repeated subtraction. This is extremely slow but sufficient for
 * verification during bring-up.
 *
 * @param dst Output result.
 * @param a First operand.
 * @param b Second operand.
 * @param m Modulus.
 */
static void bigint_mulmod(BigInt *dst, const BigInt *a, const BigInt *b, const BigInt *m)
{
    // Result buffer (double size for intermediate)
    u64 result[BigInt::MAX_WORDS * 2] = {0};
    usize result_len = a->length + b->length;

    // Multiply a * b
    for (usize i = 0; i < a->length; i++)
    {
        u64 carry = 0;
        for (usize j = 0; j < b->length; j++)
        {
            u64 prod = static_cast<u64>(a->words[i]) * static_cast<u64>(b->words[j]) +
                       result[i + j] + carry;
            result[i + j] = prod & 0xFFFFFFFF;
            carry = prod >> 32;
        }
        result[i + b->length] += carry;
    }

    // Normalize result length
    while (result_len > 1 && result[result_len - 1] == 0)
    {
        result_len--;
    }

    // Now reduce mod m using repeated subtraction
    // This is slow but simple and correct for verification
    BigInt temp;
    temp.length = result_len;
    for (usize i = 0; i < result_len; i++)
    {
        temp.words[i] = static_cast<u32>(result[i]);
    }

    // Simple modular reduction by subtraction
    while (bigint_compare(&temp, m) >= 0)
    {
        // Find how much we can shift m
        BigInt shifted;
        bigint_copy(&shifted, m);

        // Shift m left until it's just larger than temp
        int shift = 0;
        while (bigint_compare(&shifted, &temp) < 0 && shift < 32)
        {
            // Shift left by 1 bit
            u32 carry = 0;
            for (usize i = 0; i < shifted.length; i++)
            {
                u32 new_carry = shifted.words[i] >> 31;
                shifted.words[i] = (shifted.words[i] << 1) | carry;
                carry = new_carry;
            }
            if (carry)
            {
                if (shifted.length < BigInt::MAX_WORDS)
                {
                    shifted.words[shifted.length++] = carry;
                }
            }
            shift++;
        }

        // Shift back if we overshot
        if (bigint_compare(&shifted, &temp) > 0 && shift > 0)
        {
            // Shift right by 1 bit
            u32 carry = 0;
            for (usize i = shifted.length; i > 0; i--)
            {
                u32 new_carry = shifted.words[i - 1] & 1;
                shifted.words[i - 1] = (shifted.words[i - 1] >> 1) | (carry << 31);
                carry = new_carry;
            }
            while (shifted.length > 1 && shifted.words[shifted.length - 1] == 0)
            {
                shifted.length--;
            }
        }

        // Subtract
        if (bigint_compare(&temp, &shifted) >= 0)
        {
            BigInt new_temp;
            bigint_sub(&new_temp, &temp, &shifted);
            bigint_copy(&temp, &new_temp);
        }
        else
        {
            // Just subtract m once
            BigInt new_temp;
            bigint_sub(&new_temp, &temp, m);
            bigint_copy(&temp, &new_temp);
        }
    }

    bigint_copy(dst, &temp);
}

// dst = base^exp mod m (modular exponentiation)
// Uses square-and-multiply (binary exponentiation)
/**
 * @brief Compute modular exponentiation `dst = base^exp mod m`.
 *
 * @details
 * Uses a square-and-multiply algorithm scanning the exponent bits. This is
 * used to perform the RSA public operation during signature verification.
 *
 * @param dst Output result.
 * @param base Base value.
 * @param exp Exponent value.
 * @param m Modulus.
 */
static void bigint_powmod(BigInt *dst, const BigInt *base, const BigInt *exp, const BigInt *m)
{
    BigInt result, b;
    bigint_init(&result);
    result.words[0] = 1;
    result.length = 1;

    bigint_copy(&b, base);

    // Reduce base mod m first
    BigInt temp;
    bigint_mulmod(&temp, &b, &result, m);
    bigint_copy(&b, &temp);

    // Binary exponentiation
    for (usize i = 0; i < exp->length; i++)
    {
        u32 word = exp->words[i];
        for (int bit = 0; bit < 32; bit++)
        {
            if (word & 1)
            {
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

// PKCS#1 v1.5 DigestInfo for SHA-256
// DER encoding: SEQUENCE { SEQUENCE { OID sha256, NULL }, OCTET STRING hash }
/**
 * @brief ASN.1 DigestInfo prefix for SHA-256 (PKCS#1 v1.5).
 *
 * @details
 * PKCS#1 v1.5 encodes the hashed message as:
 * `0x00 0x01 0xFF... 0x00 || DigestInfo(SHA-256) || hash`.
 * This byte array represents the DER-encoded DigestInfo header for SHA-256.
 */
static const u8 sha256_digest_info_prefix[] = {0x30,
                                               0x31,
                                               0x30,
                                               0x0d,
                                               0x06,
                                               0x09,
                                               0x60,
                                               0x86,
                                               0x48,
                                               0x01,
                                               0x65,
                                               0x03,
                                               0x04,
                                               0x02,
                                               0x01,
                                               0x05,
                                               0x00,
                                               0x04,
                                               0x20};

// Verify RSA PKCS#1 v1.5 signature
/**
 * @brief Verify an RSA PKCS#1 v1.5 signature over SHA-256.
 *
 * @details
 * Computes SHA-256 over the provided `tbs_data`, performs the RSA public
 * operation `sig^e mod n`, and then checks that the resulting encoded message
 * matches the PKCS#1 v1.5 `EMSA-PKCS1-v1_5` structure for SHA-256.
 *
 * @param modulus RSA modulus bytes (big-endian).
 * @param mod_len Modulus length in bytes.
 * @param exponent RSA exponent bytes (big-endian).
 * @param exp_len Exponent length in bytes.
 * @param signature Signature bytes (big-endian).
 * @param sig_len Signature length in bytes.
 * @param tbs_data Pointer to the "to-be-signed" certificate bytes.
 * @param tbs_len Length of `tbs_data`.
 * @return `true` if the signature is valid, otherwise `false`.
 */
static bool verify_rsa_pkcs1(const u8 *modulus,
                             usize mod_len,
                             const u8 *exponent,
                             usize exp_len,
                             const u8 *signature,
                             usize sig_len,
                             const u8 *tbs_data,
                             usize tbs_len)
{
    // Skip leading zero in modulus if present
    while (mod_len > 0 && *modulus == 0)
    {
        modulus++;
        mod_len--;
    }

    if (mod_len == 0 || mod_len > 512)
        return false; // Max 4096-bit

    // Compute hash of TBS certificate
    u8 hash[crypto::SHA256_DIGEST_SIZE];
    crypto::sha256(tbs_data, tbs_len, hash);

    // Convert signature to BigInt and perform RSA operation
    BigInt sig, n, e, decrypted;
    bigint_from_bytes(&sig, signature, sig_len);
    bigint_from_bytes(&n, modulus, mod_len);
    bigint_from_bytes(&e, exponent, exp_len);

    // decrypted = sig^e mod n
    bigint_powmod(&decrypted, &sig, &e, &n);

    // Convert result to bytes
    u8 em[512];
    bigint_to_bytes(&decrypted, em, mod_len);

    // Verify PKCS#1 v1.5 padding
    // EM = 0x00 || 0x01 || PS || 0x00 || DigestInfo
    if (em[0] != 0x00 || em[1] != 0x01)
    {
        return false;
    }

    // Find end of padding (0xFF bytes followed by 0x00)
    usize pad_end = 2;
    while (pad_end < mod_len && em[pad_end] == 0xFF)
    {
        pad_end++;
    }

    if (pad_end >= mod_len || em[pad_end] != 0x00)
    {
        return false;
    }
    pad_end++; // Skip the 0x00 separator

    // Check DigestInfo prefix for SHA-256
    constexpr usize prefix_len = sizeof(sha256_digest_info_prefix);
    if (mod_len - pad_end < prefix_len + crypto::SHA256_DIGEST_SIZE)
    {
        return false;
    }

    for (usize i = 0; i < prefix_len; i++)
    {
        if (em[pad_end + i] != sha256_digest_info_prefix[i])
        {
            return false;
        }
    }

    // Compare hash
    for (usize i = 0; i < crypto::SHA256_DIGEST_SIZE; i++)
    {
        if (em[pad_end + prefix_len + i] != hash[i])
        {
            return false;
        }
    }

    return true;
}

/** @copydoc viper::tls::cert::verify_certificate_signature */
bool verify_certificate_signature(const x509::Certificate *cert, const x509::Certificate *issuer)
{
    if (!cert || !issuer)
        return false;
    if (!cert->tbs_certificate || !cert->signature)
        return false;

    // Currently only support SHA256-RSA
    if (cert->signature_algorithm != x509::SignatureAlgorithm::SHA256_RSA)
    {
        return false;
    }

    if (issuer->key_type != x509::KeyType::RSA)
    {
        return false;
    }

    if (!issuer->rsa_modulus || !issuer->rsa_exponent)
    {
        return false;
    }

    return verify_rsa_pkcs1(issuer->rsa_modulus,
                            issuer->rsa_modulus_length,
                            issuer->rsa_exponent,
                            issuer->rsa_exponent_length,
                            cert->signature,
                            (cert->signature_length + 7) / 8,
                            cert->tbs_certificate,
                            cert->tbs_certificate_length);
}

/** @copydoc viper::tls::cert::verify_certificate_signature_with_ca */
bool verify_certificate_signature_with_ca(const x509::Certificate *cert, const RootCaEntry *ca)
{
    if (!cert || !ca)
        return false;
    if (!cert->tbs_certificate || !cert->signature)
        return false;

    // Currently only support SHA256-RSA
    if (cert->signature_algorithm != x509::SignatureAlgorithm::SHA256_RSA)
    {
        return false;
    }

    if (ca->key_type != x509::KeyType::RSA)
    {
        return false;
    }

    // Parse the public key from CA entry
    // The CA public key is stored as SubjectPublicKeyInfo
    asn1::Parser key_parser;
    asn1::parser_init(&key_parser, ca->public_key, ca->public_key_len);

    asn1::Element spki;
    if (!asn1::parse_element(&key_parser, &spki) || !spki.constructed)
    {
        return false;
    }

    asn1::Parser spki_parser = asn1::enter_constructed(&spki);

    // Skip algorithm identifier
    asn1::Element alg_id;
    if (!asn1::parse_element(&spki_parser, &alg_id))
        return false;

    // Get BIT STRING containing the key
    asn1::Element key_bits;
    if (!asn1::parse_element(&spki_parser, &key_bits))
        return false;

    const u8 *key_data;
    usize key_len;
    asn1::parse_bitstring(&key_bits, &key_data, &key_len);

    if (!key_data)
        return false;

    // Parse RSA public key (SEQUENCE { modulus, exponent })
    asn1::Parser rsa_parser;
    asn1::parser_init(&rsa_parser, key_data, key_len / 8);

    asn1::Element rsa_seq;
    if (!asn1::parse_element(&rsa_parser, &rsa_seq) || !rsa_seq.constructed)
    {
        return false;
    }

    asn1::Parser rsa_inner = asn1::enter_constructed(&rsa_seq);

    asn1::Element modulus, exponent;
    if (!asn1::parse_element(&rsa_inner, &modulus))
        return false;
    if (!asn1::parse_element(&rsa_inner, &exponent))
        return false;

    return verify_rsa_pkcs1(modulus.data,
                            modulus.length,
                            exponent.data,
                            exponent.length,
                            cert->signature,
                            (cert->signature_length + 7) / 8,
                            cert->tbs_certificate,
                            cert->tbs_certificate_length);
}

/** @copydoc viper::tls::cert::verify_chain */
VerifyResult verify_chain(const x509::Certificate *chain,
                          usize chain_length,
                          const char *hostname,
                          const VerifyOptions *options)
{
    if (!chain || chain_length == 0)
    {
        return VerifyResult::INVALID_CHAIN;
    }

    if (chain_length > x509::MAX_CERT_CHAIN)
    {
        return VerifyResult::CHAIN_TOO_LONG;
    }

    // Check hostname against leaf certificate
    if (options->verify_hostname && hostname)
    {
        if (!x509::matches_hostname(&chain[0], hostname))
        {
            return VerifyResult::HOSTNAME_MISMATCH;
        }
    }

    // Check time validity
    if (options->verify_time)
    {
        for (usize i = 0; i < chain_length; i++)
        {
            if (!x509::is_time_valid(&chain[i]))
            {
                return VerifyResult::EXPIRED;
            }
        }
    }

    if (!options->verify_chain)
    {
        return VerifyResult::OK;
    }

    // Verify chain signatures
    for (usize i = 0; i < chain_length - 1; i++)
    {
        // Verify chain[i] is signed by chain[i+1]
        if (!x509::is_issued_by(&chain[i], &chain[i + 1]))
        {
            return VerifyResult::INVALID_CHAIN;
        }

        if (!verify_certificate_signature(&chain[i], &chain[i + 1]))
        {
            return VerifyResult::INVALID_SIGNATURE;
        }
    }

    // Verify the last certificate (root or intermediate)
    const x509::Certificate *last = &chain[chain_length - 1];

    // Check if it's a known root CA
    const RootCaEntry *root = ca_store_find_by_subject(last->issuer_cn);
    if (root)
    {
        // Verify signature with root CA
        if (!verify_certificate_signature_with_ca(last, root))
        {
            return VerifyResult::INVALID_SIGNATURE;
        }
        return VerifyResult::OK;
    }

    // Check if it's self-signed
    if (x509::is_issued_by(last, last))
    {
        if (options->allow_self_signed)
        {
            return VerifyResult::OK;
        }
        return VerifyResult::SELF_SIGNED_NOT_TRUSTED;
    }

    return VerifyResult::UNKNOWN_ISSUER;
}

/** @copydoc viper::tls::cert::verify_chain_der */
VerifyResult verify_chain_der(const void *data,
                              usize length,
                              const char *hostname,
                              const VerifyOptions *options)
{
    x509::Certificate chain[x509::MAX_CERT_CHAIN];
    usize chain_length = 0;

    const u8 *ptr = static_cast<const u8 *>(data);
    usize remaining = length;

    // Parse all certificates in the chain
    while (remaining > 0 && chain_length < x509::MAX_CERT_CHAIN)
    {
        // Parse certificate length from DER
        if (remaining < 4)
            break;

        // Check for SEQUENCE tag
        if (ptr[0] != 0x30)
            break;

        usize cert_len;
        usize header_len;

        if (ptr[1] < 0x80)
        {
            cert_len = ptr[1];
            header_len = 2;
        }
        else if (ptr[1] == 0x81)
        {
            if (remaining < 3)
                break;
            cert_len = ptr[2];
            header_len = 3;
        }
        else if (ptr[1] == 0x82)
        {
            if (remaining < 4)
                break;
            cert_len = (static_cast<usize>(ptr[2]) << 8) | ptr[3];
            header_len = 4;
        }
        else
        {
            break;
        }

        usize total_len = header_len + cert_len;
        if (total_len > remaining)
            break;

        if (!x509::parse_certificate(ptr, total_len, &chain[chain_length]))
        {
            return VerifyResult::INVALID_CERTIFICATE;
        }

        chain_length++;
        ptr += total_len;
        remaining -= total_len;
    }

    if (chain_length == 0)
    {
        return VerifyResult::INVALID_CERTIFICATE;
    }

    return verify_chain(chain, chain_length, hostname, options);
}

/** @copydoc viper::tls::cert::verify_result_message */
const char *verify_result_message(VerifyResult result)
{
    switch (result)
    {
        case VerifyResult::OK:
            return "Certificate verification successful";
        case VerifyResult::INVALID_SIGNATURE:
            return "Invalid certificate signature";
        case VerifyResult::EXPIRED:
            return "Certificate has expired";
        case VerifyResult::NOT_YET_VALID:
            return "Certificate not yet valid";
        case VerifyResult::UNKNOWN_ISSUER:
            return "Unknown certificate issuer";
        case VerifyResult::CHAIN_TOO_LONG:
            return "Certificate chain too long";
        case VerifyResult::HOSTNAME_MISMATCH:
            return "Certificate hostname mismatch";
        case VerifyResult::INVALID_CERTIFICATE:
            return "Invalid certificate format";
        case VerifyResult::INVALID_CHAIN:
            return "Invalid certificate chain";
        case VerifyResult::SELF_SIGNED_NOT_TRUSTED:
            return "Self-signed certificate not trusted";
    }
    return "Unknown verification error";
}

} // namespace viper::tls::cert
