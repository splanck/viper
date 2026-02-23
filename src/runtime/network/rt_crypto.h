//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_crypto.h
// Purpose: Cryptographic primitives for TLS support: SHA-256, HMAC-SHA256, HKDF, ChaCha20-Poly1305 AEAD, and X25519 key exchange, implemented in pure C with no external dependencies.
//
// Key invariants:
//   - All key material and digests are handled as raw byte arrays in caller-provided buffers.
//   - Functions do not allocate heap memory for outputs.
//   - ChaCha20-Poly1305 provides authenticated encryption with 16-byte tags.
//   - X25519 computes a 32-byte shared secret from a private key and public key.
//
// Ownership/Lifetime:
//   - Pure functions operating on caller-owned buffers; no ownership transfer.
//   - Callers must provide output buffers of sufficient size (documented per function).
//
// Links: src/runtime/network/rt_crypto.c (implementation)
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
    /// @param data Pointer to the input data to hash.
    /// @param len Length of @p data in bytes.
    /// @param digest Output buffer for the 32-byte (256-bit) hash digest.
    void rt_sha256(const void *data, size_t len, uint8_t digest[32]);

    //=========================================================================
    // HMAC-SHA256
    //=========================================================================

    /// @brief Compute HMAC-SHA256.
    /// @param key Pointer to the HMAC key.
    /// @param key_len Length of @p key in bytes.
    /// @param data Pointer to the input data to authenticate.
    /// @param data_len Length of @p data in bytes.
    /// @param mac Output buffer for the 32-byte message authentication code.
    void rt_hmac_sha256(
        const uint8_t *key, size_t key_len, const void *data, size_t data_len, uint8_t mac[32]);

    //=========================================================================
    // HKDF-SHA256 (RFC 5869)
    //=========================================================================

    /// @brief HKDF-Extract.
    /// @param salt Optional salt value (can be NULL for zero-length salt).
    /// @param salt_len Length of @p salt in bytes.
    /// @param ikm Input keying material.
    /// @param ikm_len Length of @p ikm in bytes.
    /// @param prk Output buffer for the 32-byte pseudorandom key.
    void rt_hkdf_extract(
        const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t prk[32]);

    /// @brief HKDF-Expand.
    /// @param prk Pseudorandom key from HKDF-Extract (32 bytes).
    /// @param info Application-specific context and info (can be NULL).
    /// @param info_len Length of @p info in bytes.
    /// @param okm Output buffer for the derived keying material.
    /// @param okm_len Desired length of output keying material in bytes
    ///               (at most 255 * 32 = 8160 bytes per RFC 5869).
    void rt_hkdf_expand(
        const uint8_t prk[32], const uint8_t *info, size_t info_len, uint8_t *okm, size_t okm_len);

    /// @brief HKDF-Expand-Label for TLS 1.3.
    /// @param secret The 32-byte secret from which to derive keying material.
    /// @param label The TLS 1.3 label string (without "tls13 " prefix).
    /// @param context Hash context or transcript hash (can be NULL).
    /// @param context_len Length of @p context in bytes.
    /// @param out Output buffer for the derived keying material.
    /// @param out_len Desired length of output in bytes.
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
    /// @param key The 256-bit encryption key (32 bytes).
    /// @param nonce The 96-bit nonce (12 bytes, must be unique per key).
    /// @param aad Pointer to additional authenticated data (can be NULL).
    /// @param aad_len Length of @p aad in bytes.
    /// @param plaintext Pointer to the plaintext to encrypt.
    /// @param plaintext_len Length of @p plaintext in bytes.
    /// @param ciphertext Output buffer for ciphertext and 16-byte
    ///                   authentication tag (must hold plaintext_len + 16 bytes).
    /// @return Ciphertext length (plaintext_len + 16 for tag).
    size_t rt_chacha20_poly1305_encrypt(const uint8_t key[32],
                                        const uint8_t nonce[12],
                                        const void *aad,
                                        size_t aad_len,
                                        const void *plaintext,
                                        size_t plaintext_len,
                                        uint8_t *ciphertext);

    /// @brief Decrypt with ChaCha20-Poly1305.
    /// @param key The 256-bit decryption key (32 bytes).
    /// @param nonce The 96-bit nonce (12 bytes, same as used during encryption).
    /// @param aad Pointer to additional authenticated data (can be NULL).
    /// @param aad_len Length of @p aad in bytes.
    /// @param ciphertext Pointer to the ciphertext with appended 16-byte tag.
    /// @param ciphertext_len Length of @p ciphertext in bytes (including tag).
    /// @param plaintext Output buffer for decrypted data (must hold
    ///                  ciphertext_len - 16 bytes).
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
    /// @param secret Output buffer for the 32-byte private key (randomly generated).
    /// @param public_key Output buffer for the 32-byte public key derived from
    ///                   @p secret.
    void rt_x25519_keygen(uint8_t secret[32], uint8_t public_key[32]);

    /// @brief Compute X25519 shared secret.
    /// @param secret The local 32-byte private key.
    /// @param peer_public The peer's 32-byte public key.
    /// @param shared Output buffer for the 32-byte shared secret (result of the
    ///               Diffie-Hellman computation on Curve25519).
    void rt_x25519(const uint8_t secret[32], const uint8_t peer_public[32], uint8_t shared[32]);

    //=========================================================================
    // Random
    //=========================================================================

    /// @brief Generate cryptographically secure random bytes.
    /// @param buf Output buffer to fill with random data.
    /// @param len Number of random bytes to generate.
    void rt_crypto_random_bytes(uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_CRYPTO_H
