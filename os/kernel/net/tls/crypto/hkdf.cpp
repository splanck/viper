/**
 * @file hkdf.cpp
 * @brief HKDF-SHA256 implementation and TLS 1.3 label helpers.
 *
 * @details
 * Implements the HKDF routines declared in `hkdf.hpp` using HMAC-SHA256. The
 * TLS 1.3 helper functions follow RFC 8446 conventions for building the
 * HkdfLabel structure.
 */

#include "hkdf.hpp"
#include "../../../lib/str.hpp"

namespace viper::crypto
{

// Use lib::strlen for string length operations

/** @copydoc viper::crypto::hkdf_extract */
void hkdf_extract(
    const void *salt, usize salt_len, const void *ikm, usize ikm_len, u8 prk[SHA256_DIGEST_SIZE])
{
    // If salt is null, use zero-filled salt
    u8 zero_salt[SHA256_DIGEST_SIZE] = {0};

    if (!salt || salt_len == 0)
    {
        salt = zero_salt;
        salt_len = SHA256_DIGEST_SIZE;
    }

    // PRK = HMAC-Hash(salt, IKM)
    hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

/** @copydoc viper::crypto::hkdf_expand */
void hkdf_expand(
    const u8 prk[SHA256_DIGEST_SIZE], const void *info, usize info_len, u8 *okm, usize okm_len)
{
    // N = ceil(L/HashLen)
    usize n = (okm_len + SHA256_DIGEST_SIZE - 1) / SHA256_DIGEST_SIZE;

    if (n > 255)
    {
        // Error: output too long
        return;
    }

    u8 t[SHA256_DIGEST_SIZE];
    usize t_len = 0;
    usize offset = 0;

    for (usize i = 1; i <= n; i++)
    {
        // T(i) = HMAC-Hash(PRK, T(i-1) | info | i)
        HmacSha256Context ctx;
        hmac_sha256_init(&ctx, prk, SHA256_DIGEST_SIZE);

        if (t_len > 0)
        {
            hmac_sha256_update(&ctx, t, t_len);
        }

        if (info && info_len > 0)
        {
            hmac_sha256_update(&ctx, info, info_len);
        }

        u8 counter = static_cast<u8>(i);
        hmac_sha256_update(&ctx, &counter, 1);

        hmac_sha256_final(&ctx, t);
        t_len = SHA256_DIGEST_SIZE;

        // Copy to output
        usize copy = okm_len - offset;
        if (copy > SHA256_DIGEST_SIZE)
        {
            copy = SHA256_DIGEST_SIZE;
        }

        for (usize j = 0; j < copy; j++)
        {
            okm[offset + j] = t[j];
        }
        offset += copy;
    }
}

/** @copydoc viper::crypto::hkdf */
void hkdf(const void *salt,
          usize salt_len,
          const void *ikm,
          usize ikm_len,
          const void *info,
          usize info_len,
          u8 *okm,
          usize okm_len)
{
    u8 prk[SHA256_DIGEST_SIZE];
    hkdf_extract(salt, salt_len, ikm, ikm_len, prk);
    hkdf_expand(prk, info, info_len, okm, okm_len);
}

// TLS 1.3 HKDF-Expand-Label
// struct HkdfLabel {
//   uint16 length;
//   opaque label<7..255>;    // "tls13 " + Label
//   opaque context<0..255>;
// };
/** @copydoc viper::crypto::hkdf_expand_label */
void hkdf_expand_label(const u8 secret[SHA256_DIGEST_SIZE],
                       const char *label,
                       const void *context,
                       usize context_len,
                       u8 *out,
                       usize out_len)
{
    // Build HkdfLabel structure
    u8 hkdf_label[512];
    usize pos = 0;

    // Length (2 bytes, big-endian)
    hkdf_label[pos++] = static_cast<u8>(out_len >> 8);
    hkdf_label[pos++] = static_cast<u8>(out_len);

    // Label: length + "tls13 " + label
    const char *prefix = "tls13 ";
    usize prefix_len = 6;
    usize label_len = lib::strlen(label);
    usize total_label_len = prefix_len + label_len;

    hkdf_label[pos++] = static_cast<u8>(total_label_len);

    // Copy "tls13 "
    for (usize i = 0; i < prefix_len; i++)
    {
        hkdf_label[pos++] = prefix[i];
    }

    // Copy label
    for (usize i = 0; i < label_len; i++)
    {
        hkdf_label[pos++] = label[i];
    }

    // Context: length + data
    hkdf_label[pos++] = static_cast<u8>(context_len);
    if (context && context_len > 0)
    {
        const u8 *ctx_bytes = static_cast<const u8 *>(context);
        for (usize i = 0; i < context_len; i++)
        {
            hkdf_label[pos++] = ctx_bytes[i];
        }
    }

    // Expand
    hkdf_expand(secret, hkdf_label, pos, out, out_len);
}

/** @copydoc viper::crypto::derive_secret */
void derive_secret(const u8 secret[SHA256_DIGEST_SIZE],
                   const char *label,
                   const void *messages,
                   usize messages_len,
                   u8 out[SHA256_DIGEST_SIZE])
{
    // Hash the messages
    u8 transcript_hash[SHA256_DIGEST_SIZE];
    sha256(messages, messages_len, transcript_hash);

    // Derive-Secret(Secret, Label, Messages) =
    //   HKDF-Expand-Label(Secret, Label, Hash(Messages), Hash.length)
    hkdf_expand_label(secret, label, transcript_hash, SHA256_DIGEST_SIZE, out, SHA256_DIGEST_SIZE);
}

} // namespace viper::crypto
