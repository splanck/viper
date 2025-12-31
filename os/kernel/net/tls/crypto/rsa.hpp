#pragma once

/**
 * @file rsa.hpp
 * @brief RSA cryptographic operations for SSH.
 *
 * @details
 * Provides RSA signing and verification for SSH public key authentication.
 * Supports PKCS#1 v1.5 signatures with SHA-256 as used by ssh-rsa.
 *
 * This module extends the verification-only BigInt implementation in
 * cert/verify.cpp to support private key operations.
 */

#include "../../../include/types.hpp"

namespace viper::crypto
{

/** @name RSA key size limits */
///@{
constexpr usize RSA_MAX_KEY_BYTES = 512;  // 4096 bits max
constexpr usize RSA_MIN_KEY_BYTES = 128;  // 1024 bits min
///@}

/**
 * @brief RSA public key.
 */
struct RsaPublicKey
{
    u8 modulus[RSA_MAX_KEY_BYTES];      ///< RSA modulus n (big-endian)
    usize modulus_len;                   ///< Length of modulus in bytes
    u8 exponent[8];                      ///< Public exponent e (typically 65537)
    usize exponent_len;                  ///< Length of exponent in bytes
};

/**
 * @brief RSA private key (for signing).
 *
 * @details
 * For SSH, the private key typically includes n, e, d, p, q, dp, dq, qinv.
 * We use the simplified form with just n and d for basic signing.
 */
struct RsaPrivateKey
{
    u8 modulus[RSA_MAX_KEY_BYTES];       ///< RSA modulus n (big-endian)
    usize modulus_len;                    ///< Length of modulus in bytes
    u8 public_exponent[8];               ///< Public exponent e
    usize public_exponent_len;
    u8 private_exponent[RSA_MAX_KEY_BYTES];  ///< Private exponent d
    usize private_exponent_len;
};

/**
 * @brief Sign data using RSA PKCS#1 v1.5 with SHA-256.
 *
 * @details
 * Creates an RSA signature using the private key. The signature is
 * computed as: signature = EMSA-PKCS1-v1_5(hash)^d mod n
 *
 * @param key RSA private key.
 * @param data Data to sign.
 * @param data_len Length of data.
 * @param signature Output buffer for signature (must be modulus_len bytes).
 * @param sig_len Output: actual signature length.
 * @return true on success, false on error.
 */
bool rsa_sign_sha256(const RsaPrivateKey *key,
                     const void *data,
                     usize data_len,
                     u8 *signature,
                     usize *sig_len);

/**
 * @brief Sign a pre-computed hash using RSA PKCS#1 v1.5.
 *
 * @param key RSA private key.
 * @param hash 32-byte SHA-256 hash.
 * @param signature Output buffer for signature.
 * @param sig_len Output: actual signature length.
 * @return true on success, false on error.
 */
bool rsa_sign_hash_sha256(const RsaPrivateKey *key,
                          const u8 hash[32],
                          u8 *signature,
                          usize *sig_len);

/**
 * @brief Verify an RSA PKCS#1 v1.5 signature with SHA-256.
 *
 * @param key RSA public key.
 * @param data Data that was signed.
 * @param data_len Length of data.
 * @param signature Signature to verify.
 * @param sig_len Length of signature.
 * @return true if signature is valid, false otherwise.
 */
bool rsa_verify_sha256(const RsaPublicKey *key,
                       const void *data,
                       usize data_len,
                       const u8 *signature,
                       usize sig_len);

/**
 * @brief Parse an SSH RSA public key blob.
 *
 * @details
 * SSH public key format: string "ssh-rsa" || mpint e || mpint n
 *
 * @param blob SSH key blob.
 * @param blob_len Length of blob.
 * @param key Output public key structure.
 * @return true on success, false on parse error.
 */
bool rsa_parse_ssh_public_key(const u8 *blob, usize blob_len, RsaPublicKey *key);

/**
 * @brief Parse an OpenSSH private key.
 *
 * @details
 * Parses the newer OpenSSH private key format (openssh-key-v1).
 * This is a simplified parser that doesn't handle encrypted keys.
 *
 * @param data Private key data.
 * @param data_len Length of data.
 * @param key Output private key structure.
 * @return true on success, false on parse error.
 */
bool rsa_parse_openssh_private_key(const u8 *data, usize data_len, RsaPrivateKey *key);

/**
 * @brief Get the public key from a private key.
 *
 * @param priv Private key.
 * @param pub Output public key.
 */
void rsa_public_from_private(const RsaPrivateKey *priv, RsaPublicKey *pub);

} // namespace viper::crypto
