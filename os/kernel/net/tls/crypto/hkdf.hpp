#pragma once

/**
 * @file hkdf.hpp
 * @brief HKDF-SHA256 and TLS 1.3 key-derivation helpers.
 *
 * @details
 * HKDF (RFC 5869) is used by TLS 1.3 (RFC 8446) to derive traffic secrets,
 * keys, and IVs from shared secrets and transcript hashes.
 *
 * This module implements:
 * - HKDF-Extract and HKDF-Expand using HMAC-SHA256.
 * - A one-shot HKDF helper.
 * - TLS 1.3-specific HKDF-Expand-Label and Derive-Secret helpers.
 */

#include "../../../include/types.hpp"
#include "sha256.hpp"

namespace viper::crypto
{

/** @brief Maximum HKDF-SHA256 output length (RFC 5869: 255 * HashLen). */
constexpr usize HKDF_SHA256_MAX_OUTPUT = 255 * SHA256_DIGEST_SIZE;

/**
 * @brief HKDF-Extract (RFC 5869).
 *
 * @details
 * Computes a pseudorandom key (PRK) from input keying material (IKM):
 * `PRK = HMAC(salt, IKM)`.
 *
 * If `salt` is null/empty, a salt of HashLen zero bytes is used.
 *
 * @param salt Optional salt bytes (may be null).
 * @param salt_len Salt length in bytes.
 * @param ikm Input keying material.
 * @param ikm_len IKM length in bytes.
 * @param prk Output PRK buffer (32 bytes for SHA-256).
 */
void hkdf_extract(
    const void *salt, usize salt_len, const void *ikm, usize ikm_len, u8 prk[SHA256_DIGEST_SIZE]);

/**
 * @brief HKDF-Expand (RFC 5869).
 *
 * @details
 * Expands a PRK into output keying material (OKM) of length `okm_len` using the
 * provided `info` string. Output length must not exceed
 * @ref HKDF_SHA256_MAX_OUTPUT.
 *
 * @param prk PRK from @ref hkdf_extract (32 bytes).
 * @param info Optional context string (may be null).
 * @param info_len Length of `info` in bytes.
 * @param okm Output buffer to fill.
 * @param okm_len Desired output length in bytes.
 */
void hkdf_expand(
    const u8 prk[SHA256_DIGEST_SIZE], const void *info, usize info_len, u8 *okm, usize okm_len);

/**
 * @brief HKDF one-shot helper (Extract + Expand).
 *
 * @param salt Optional salt bytes.
 * @param salt_len Salt length.
 * @param ikm Input keying material.
 * @param ikm_len IKM length.
 * @param info Optional info bytes.
 * @param info_len Info length.
 * @param okm Output buffer.
 * @param okm_len Output length.
 */
void hkdf(const void *salt,
          usize salt_len,
          const void *ikm,
          usize ikm_len,
          const void *info,
          usize info_len,
          u8 *okm,
          usize okm_len);

/**
 * @brief TLS 1.3 HKDF-Expand-Label helper (RFC 8446).
 *
 * @details
 * Builds the TLS 1.3 HKDF label structure and performs HKDF-Expand with that
 * label as the `info` field. The label is encoded as `"tls13 " + label`.
 *
 * @param secret Input secret (usually a traffic secret).
 * @param label ASCII label string without the `"tls13 "` prefix.
 * @param context Optional context bytes (often a transcript hash).
 * @param context_len Context length in bytes.
 * @param out Output buffer.
 * @param out_len Output length in bytes.
 */
void hkdf_expand_label(const u8 secret[SHA256_DIGEST_SIZE],
                       const char *label,
                       const void *context,
                       usize context_len,
                       u8 *out,
                       usize out_len);

/**
 * @brief TLS 1.3 Derive-Secret helper (RFC 8446).
 *
 * @details
 * Computes the transcript hash of `messages` and then calls
 * @ref hkdf_expand_label with that hash as context.
 *
 * @param secret Input secret.
 * @param label TLS label string.
 * @param messages Handshake messages to hash.
 * @param messages_len Length of `messages`.
 * @param out Output secret (32 bytes).
 */
void derive_secret(const u8 secret[SHA256_DIGEST_SIZE],
                   const char *label,
                   const void *messages,
                   usize messages_len,
                   u8 out[SHA256_DIGEST_SIZE]);

} // namespace viper::crypto
