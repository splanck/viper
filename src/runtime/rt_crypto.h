//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file rt_crypto.h
/// @brief Cryptographic primitives for TLS support.
///
/// Provides SHA-256, HMAC-SHA256, HKDF, ChaCha20-Poly1305 AEAD, and X25519.
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_CRYPTO_H
#define VIPER_RT_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // SHA-256
    //=========================================================================

    /// @brief Compute SHA-256 hash.
    void rt_sha256(const void *data, size_t len, uint8_t digest[32]);

    //=========================================================================
    // HMAC-SHA256
    //=========================================================================

    /// @brief Compute HMAC-SHA256.
    void rt_hmac_sha256(
        const uint8_t *key, size_t key_len, const void *data, size_t data_len, uint8_t mac[32]);

    //=========================================================================
    // HKDF-SHA256 (RFC 5869)
    //=========================================================================

    /// @brief HKDF-Extract.
    void rt_hkdf_extract(
        const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t prk[32]);

    /// @brief HKDF-Expand.
    void rt_hkdf_expand(
        const uint8_t prk[32], const uint8_t *info, size_t info_len, uint8_t *okm, size_t okm_len);

    /// @brief HKDF-Expand-Label for TLS 1.3.
    void rt_hkdf_expand_label(const uint8_t secret[32],
                              const char *label,
                              const uint8_t *context,
                              size_t context_len,
                              uint8_t *out,
                              size_t out_len);

    //=========================================================================
    // ChaCha20-Poly1305 AEAD
    //=========================================================================

    /// @brief Encrypt with ChaCha20-Poly1305.
    /// @return Ciphertext length (plaintext_len + 16 for tag).
    size_t rt_chacha20_poly1305_encrypt(const uint8_t key[32],
                                        const uint8_t nonce[12],
                                        const void *aad,
                                        size_t aad_len,
                                        const void *plaintext,
                                        size_t plaintext_len,
                                        uint8_t *ciphertext);

    /// @brief Decrypt with ChaCha20-Poly1305.
    /// @return Plaintext length on success, -1 on authentication failure.
    long rt_chacha20_poly1305_decrypt(const uint8_t key[32],
                                      const uint8_t nonce[12],
                                      const void *aad,
                                      size_t aad_len,
                                      const void *ciphertext,
                                      size_t ciphertext_len,
                                      uint8_t *plaintext);

    //=========================================================================
    // X25519 Key Exchange
    //=========================================================================

    /// @brief Generate X25519 key pair.
    void rt_x25519_keygen(uint8_t secret[32], uint8_t public_key[32]);

    /// @brief Compute X25519 shared secret.
    void rt_x25519(const uint8_t secret[32], const uint8_t peer_public[32], uint8_t shared[32]);

    //=========================================================================
    // Random
    //=========================================================================

    /// @brief Generate cryptographically secure random bytes.
    void rt_crypto_random_bytes(uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_CRYPTO_H
