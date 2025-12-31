#pragma once

/**
 * @file ed25519.hpp
 * @brief Ed25519 digital signature scheme (RFC 8032).
 *
 * @details
 * Provides Ed25519 signature operations for SSH public key authentication.
 * Ed25519 is a high-speed, high-security signature algorithm based on the
 * twisted Edwards curve equivalent to Curve25519.
 *
 * Features:
 * - Fast signature generation and verification
 * - 32-byte public keys, 64-byte private keys, 64-byte signatures
 * - Deterministic signatures (no RNG needed for signing)
 * - Compatible with OpenSSH ssh-ed25519 keys
 */

#include "../../../include/types.hpp"

namespace viper::crypto
{

/** @name Ed25519 parameter sizes */
///@{
constexpr usize ED25519_PUBLIC_KEY_SIZE = 32;
constexpr usize ED25519_SECRET_KEY_SIZE = 64;  // seed (32) + public key (32)
constexpr usize ED25519_SEED_SIZE = 32;
constexpr usize ED25519_SIGNATURE_SIZE = 64;
///@}

/**
 * @brief Generate an Ed25519 key pair from a 32-byte seed.
 *
 * @details
 * Creates a public/secret key pair deterministically from the seed.
 * The secret key is stored as seed || public_key (64 bytes total).
 *
 * @param seed 32-byte random seed.
 * @param public_key Output 32-byte public key.
 * @param secret_key Output 64-byte secret key (seed || public_key).
 */
void ed25519_keypair_from_seed(const u8 seed[ED25519_SEED_SIZE],
                                u8 public_key[ED25519_PUBLIC_KEY_SIZE],
                                u8 secret_key[ED25519_SECRET_KEY_SIZE]);

/**
 * @brief Generate an Ed25519 key pair using random seed.
 *
 * @param public_key Output 32-byte public key.
 * @param secret_key Output 64-byte secret key.
 */
void ed25519_keypair(u8 public_key[ED25519_PUBLIC_KEY_SIZE],
                     u8 secret_key[ED25519_SECRET_KEY_SIZE]);

/**
 * @brief Sign a message using Ed25519.
 *
 * @details
 * Creates a 64-byte signature using the secret key. The signature is
 * deterministic - the same message and key always produce the same signature.
 *
 * @param message Message bytes to sign.
 * @param message_len Length of message.
 * @param secret_key 64-byte secret key.
 * @param signature Output 64-byte signature.
 */
void ed25519_sign(const void *message,
                  usize message_len,
                  const u8 secret_key[ED25519_SECRET_KEY_SIZE],
                  u8 signature[ED25519_SIGNATURE_SIZE]);

/**
 * @brief Verify an Ed25519 signature.
 *
 * @param message Message bytes.
 * @param message_len Length of message.
 * @param signature 64-byte signature.
 * @param public_key 32-byte public key.
 * @return true if signature is valid, false otherwise.
 */
bool ed25519_verify(const void *message,
                    usize message_len,
                    const u8 signature[ED25519_SIGNATURE_SIZE],
                    const u8 public_key[ED25519_PUBLIC_KEY_SIZE]);

/**
 * @brief Derive the public key from a secret key.
 *
 * @param secret_key 64-byte secret key.
 * @param public_key Output 32-byte public key.
 */
void ed25519_public_key_from_secret(const u8 secret_key[ED25519_SECRET_KEY_SIZE],
                                     u8 public_key[ED25519_PUBLIC_KEY_SIZE]);

} // namespace viper::crypto
