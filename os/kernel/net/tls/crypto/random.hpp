#pragma once

/**
 * @file random.hpp
 * @brief ChaCha20-based CSPRNG for TLS and crypto operations.
 *
 * @details
 * TLS requires cryptographically secure random numbers for ephemeral keys,
 * nonces, and other security-critical values. This module implements a simple
 * ChaCha20-based CSPRNG suitable for kernel bring-up:
 * - Entropy is accumulated into a small pool via @ref random_add_entropy.
 * - When sufficient entropy is present, the generator is reseeded.
 * - Output is generated using a ChaCha20-like block function and buffered for
 *   efficient consumption.
 *
 * The interface is designed for freestanding use in the kernel and does not
 * rely on user-space libc.
 */

#include "../../../include/types.hpp"

namespace viper::tls::crypto
{

/** @brief CSPRNG internal state size in bytes. */
constexpr usize CSPRNG_STATE_SIZE = 32; // 256-bit state
/** @brief Output buffer size in bytes. */
constexpr usize CSPRNG_POOL_SIZE = 64; // Output buffer size

/**
 * @brief Initialize the CSPRNG.
 *
 * @details
 * Clears state, resets counters, and prepares the generator. This should be
 * called early in boot. Callers should then feed entropy via
 * @ref random_add_entropy as sources become available.
 */
void random_init();

/**
 * @brief Add entropy to the generator.
 *
 * @details
 * Mixes the supplied bytes into the entropy pool and updates a conservative
 * entropy estimate. Once enough entropy has been accumulated, the generator
 * automatically reseeds.
 *
 * Callers should feed any available non-deterministic sources such as timer
 * jitter, interrupt timing, or device-provided randomness.
 *
 * @param data Entropy bytes to mix in.
 * @param len Number of bytes.
 */
void random_add_entropy(const void *data, usize len);

/**
 * @brief Generate cryptographically secure random bytes.
 *
 * @details
 * Produces `len` bytes into `out`, refilling internal buffers as needed. The
 * quality of output depends on whether the generator has been properly seeded;
 * callers can check @ref random_is_seeded and @ref random_entropy_bits.
 *
 * @param out Output buffer.
 * @param len Number of bytes to generate.
 */
void random_bytes(void *out, usize len);

/**
 * @brief Generate a random 32-bit value.
 *
 * @return Random 32-bit value.
 */
u32 random_u32();

/**
 * @brief Generate a random 64-bit value.
 *
 * @return Random 64-bit value.
 */
u64 random_u64();

/**
 * @brief Reseed the generator using accumulated entropy.
 *
 * @details
 * Mixes the entropy pool into the ChaCha state and resets counters. This is
 * called automatically once enough entropy has been accumulated, but callers
 * may also call it explicitly after adding entropy.
 */
void random_reseed();

/**
 * @brief Check whether the generator has been seeded with sufficient entropy.
 *
 * @return `true` if seeded, otherwise `false`.
 */
bool random_is_seeded();

/**
 * @brief Get the current conservative entropy estimate.
 *
 * @details
 * Returns an estimate of how many bits of entropy have been accumulated since
 * the last reseed.
 *
 * @return Estimated entropy in bits.
 */
usize random_entropy_bits();

} // namespace viper::tls::crypto
