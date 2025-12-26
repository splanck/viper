#pragma once

/**
 * @file x25519.hpp
 * @brief X25519 Diffie-Hellman key exchange (RFC 7748).
 *
 * @details
 * TLS 1.3 uses elliptic-curve Diffie-Hellman for key agreement. This module
 * implements X25519 (Curve25519 scalar multiplication) as specified in RFC 7748.
 *
 * The API provides:
 * - Clamping of private scalars.
 * - Derivation of the public key from a private key.
 * - Computation of a shared secret given a private key and peer public key.
 *
 * The implementation is intended for use by the TLS handshake code and is not
 * a general-purpose ECC library.
 */

#include "../../../include/types.hpp"

namespace viper::crypto
{

/** @name X25519 parameter sizes */
///@{
constexpr usize X25519_KEY_SIZE = 32;
constexpr usize X25519_SCALAR_SIZE = 32;
///@}

/**
 * @brief Derive the X25519 public key from a private key.
 *
 * @details
 * Computes scalar multiplication of the Curve25519 basepoint by the private
 * scalar. The private key bytes are clamped internally per RFC 7748.
 *
 * @param private_key 32-byte private key (scalar).
 * @param public_key Output 32-byte public key.
 */
void x25519_public_key(const u8 private_key[X25519_KEY_SIZE], u8 public_key[X25519_KEY_SIZE]);

/**
 * @brief Compute the X25519 shared secret.
 *
 * @details
 * Computes scalar multiplication of the peer public key by the local private
 * scalar. The resulting 32-byte shared secret is used as input to the TLS key
 * schedule.
 *
 * Some implementations validate the peer public key to reduce risk of
 * degenerate points; this function returns `false` if the peer public key is
 * rejected by the implementation.
 *
 * @param private_key 32-byte private key (scalar).
 * @param peer_public_key 32-byte peer public key.
 * @param shared_secret Output 32-byte shared secret.
 * @return `true` on success, otherwise `false`.
 */
bool x25519_shared_secret(const u8 private_key[X25519_KEY_SIZE],
                          const u8 peer_public_key[X25519_KEY_SIZE],
                          u8 shared_secret[X25519_KEY_SIZE]);

/**
 * @brief Clamp a Curve25519 private scalar in place.
 *
 * @details
 * X25519 requires clamping the scalar:
 * - Clear the lowest 3 bits.
 * - Clear the highest bit.
 * - Set the second-highest bit.
 *
 * This ensures the scalar is within the correct subgroup and avoids small
 * subgroup issues.
 *
 * @param key 32-byte scalar to clamp.
 */
void x25519_clamp(u8 key[X25519_KEY_SIZE]);

} // namespace viper::crypto
