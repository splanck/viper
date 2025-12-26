/**
 * @file random.cpp
 * @brief ChaCha20-based CSPRNG implementation.
 *
 * @details
 * Implements the CSPRNG API declared in `random.hpp`. The generator is intended
 * for kernel bring-up and mixes caller-provided entropy into a ChaCha20-like
 * state to produce random bytes for TLS and other cryptographic operations.
 */

#include "random.hpp"
#include "../../../lib/mem.hpp"
#include "sha256.hpp"

namespace viper::tls::crypto
{

// Import sha256 from viper::crypto namespace
using viper::crypto::sha256;
using viper::crypto::SHA256_DIGEST_SIZE;

/**
 * @brief ChaCha20 quarter-round primitive used by the RNG block function.
 *
 * @details
 * The RNG uses a ChaCha20-like core to generate 64-byte output blocks. This
 * helper implements the RFC 8439 quarter-round operation in place on four
 * 32-bit words.
 *
 * @param a Word a (updated in place).
 * @param b Word b (updated in place).
 * @param c Word c (updated in place).
 * @param d Word d (updated in place).
 */
static inline void quarter_round(u32 &a, u32 &b, u32 &c, u32 &d)
{
    a += b;
    d ^= a;
    d = (d << 16) | (d >> 16);
    c += d;
    b ^= c;
    b = (b << 12) | (b >> 20);
    a += b;
    d ^= a;
    d = (d << 8) | (d >> 24);
    c += d;
    b ^= c;
    b = (b << 7) | (b >> 25);
}

/**
 * @brief Generate one 64-byte ChaCha20 block from the provided state.
 *
 * @details
 * This function copies the 16-word input state, performs 20 rounds (10
 * double-rounds), adds the original state, and serializes the result to bytes
 * in little-endian order.
 *
 * Unlike the full ChaCha20 cipher, this function does not increment counters
 * internally; the caller is responsible for updating the counter words in the
 * state.
 *
 * @param state 16-word ChaCha state.
 * @param out Output buffer receiving 64 bytes of keystream.
 */
static void chacha20_block(const u32 state[16], u8 out[64])
{
    u32 x[16];
    for (int i = 0; i < 16; i++)
    {
        x[i] = state[i];
    }

    // 20 rounds (10 double-rounds)
    for (int i = 0; i < 10; i++)
    {
        // Column rounds
        quarter_round(x[0], x[4], x[8], x[12]);
        quarter_round(x[1], x[5], x[9], x[13]);
        quarter_round(x[2], x[6], x[10], x[14]);
        quarter_round(x[3], x[7], x[11], x[15]);
        // Diagonal rounds
        quarter_round(x[0], x[5], x[10], x[15]);
        quarter_round(x[1], x[6], x[11], x[12]);
        quarter_round(x[2], x[7], x[8], x[13]);
        quarter_round(x[3], x[4], x[9], x[14]);
    }

    // Add original state and serialize to little-endian bytes
    for (int i = 0; i < 16; i++)
    {
        u32 v = x[i] + state[i];
        out[i * 4 + 0] = v & 0xff;
        out[i * 4 + 1] = (v >> 8) & 0xff;
        out[i * 4 + 2] = (v >> 16) & 0xff;
        out[i * 4 + 3] = (v >> 24) & 0xff;
    }
}

// CSPRNG state
struct CsprngState
{
    // ChaCha20 state: "expand 32-byte k" + key (8 words) + counter (2 words) + nonce (2 words)
    u32 state[16];

    // Entropy pool for accumulating entropy before mixing
    u8 entropy_pool[64];
    usize entropy_pool_pos;

    // Entropy estimate in bits
    usize entropy_bits;

    // Generation counter (for forward secrecy)
    u64 generation;

    // Output buffer
    u8 buffer[64];
    usize buffer_pos;

    // Has been seeded flag
    bool seeded;
};

static CsprngState g_rng;

// ChaCha20 constants: "expand 32-byte k"
static const u32 CHACHA_CONSTANTS[4] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};

/** @copydoc viper::tls::crypto::random_init */
void random_init()
{
    lib::memset(&g_rng, 0, sizeof(g_rng));

    // Initialize ChaCha20 constants
    g_rng.state[0] = CHACHA_CONSTANTS[0];
    g_rng.state[1] = CHACHA_CONSTANTS[1];
    g_rng.state[2] = CHACHA_CONSTANTS[2];
    g_rng.state[3] = CHACHA_CONSTANTS[3];

    // Key words (state[4..11]) start as zero, will be seeded
    // Counter (state[12..13]) starts at 0
    // Nonce (state[14..15]) starts at 0

    g_rng.buffer_pos = 64; // Buffer empty, will refill on first use
    g_rng.seeded = false;
    g_rng.entropy_bits = 0;
    g_rng.generation = 0;
    g_rng.entropy_pool_pos = 0;
}

/** @copydoc viper::tls::crypto::random_add_entropy */
void random_add_entropy(const void *data, usize len)
{
    const u8 *bytes = static_cast<const u8 *>(data);

    // Add to entropy pool
    for (usize i = 0; i < len; i++)
    {
        g_rng.entropy_pool[g_rng.entropy_pool_pos] ^= bytes[i];
        g_rng.entropy_pool_pos = (g_rng.entropy_pool_pos + 1) % 64;
    }

    // Conservative entropy estimate: 1 bit per byte, max 256 bits
    g_rng.entropy_bits += len;
    if (g_rng.entropy_bits > 256)
    {
        g_rng.entropy_bits = 256;
    }

    // If we have enough entropy, mix it into the state
    if (g_rng.entropy_bits >= 128)
    {
        random_reseed();
    }
}

/** @copydoc viper::tls::crypto::random_reseed */
void random_reseed()
{
    // Hash the entropy pool to get new key material
    u8 hash[SHA256_DIGEST_SIZE];

    // Mix current state + entropy pool + generation counter
    u8 mix_buffer[128];
    lib::memcpy(mix_buffer, g_rng.state, 64);
    lib::memcpy(mix_buffer + 64, g_rng.entropy_pool, 64);

    sha256(mix_buffer, 128, hash);

    // Update key (state[4..11])
    for (int i = 0; i < 8; i++)
    {
        g_rng.state[4 + i] =
            static_cast<u32>(hash[i * 4 + 0]) | (static_cast<u32>(hash[i * 4 + 1]) << 8) |
            (static_cast<u32>(hash[i * 4 + 2]) << 16) | (static_cast<u32>(hash[i * 4 + 3]) << 24);
    }

    // Reset counter
    g_rng.state[12] = 0;
    g_rng.state[13] = 0;

    // Update nonce with generation counter
    g_rng.generation++;
    g_rng.state[14] = static_cast<u32>(g_rng.generation);
    g_rng.state[15] = static_cast<u32>(g_rng.generation >> 32);

    // Clear entropy pool
    lib::memset(g_rng.entropy_pool, 0, 64);
    g_rng.entropy_pool_pos = 0;

    // Invalidate output buffer
    g_rng.buffer_pos = 64;

    // Mark as seeded if we had enough entropy
    if (g_rng.entropy_bits >= 128)
    {
        g_rng.seeded = true;
    }
    g_rng.entropy_bits = 0;
}

/**
 * @brief Refill the output buffer with fresh keystream.
 *
 * @details
 * Generates a new ChaCha block and advances the internal counter. After the
 * first reseed, the generator XORs part of the output back into the key words
 * to provide a form of forward secrecy for past outputs.
 */
static void refill_buffer()
{
    // Generate new block
    chacha20_block(g_rng.state, g_rng.buffer);
    g_rng.buffer_pos = 0;

    // Increment counter
    g_rng.state[12]++;
    if (g_rng.state[12] == 0)
    {
        g_rng.state[13]++;
    }

    // Forward secrecy: use first 32 bytes as new key, output remaining 32
    // This ensures past outputs can't be recovered if state is compromised
    if (g_rng.generation > 0)
    { // Only after first reseed
        for (int i = 0; i < 8; i++)
        {
            g_rng.state[4 + i] ^= static_cast<u32>(g_rng.buffer[i * 4 + 0]) |
                                  (static_cast<u32>(g_rng.buffer[i * 4 + 1]) << 8) |
                                  (static_cast<u32>(g_rng.buffer[i * 4 + 2]) << 16) |
                                  (static_cast<u32>(g_rng.buffer[i * 4 + 3]) << 24);
        }
        g_rng.buffer_pos = 32; // Skip first 32 bytes (used for key update)
    }
}

/** @copydoc viper::tls::crypto::random_bytes */
void random_bytes(void *out, usize len)
{
    u8 *dst = static_cast<u8 *>(out);

    while (len > 0)
    {
        // Refill buffer if needed
        if (g_rng.buffer_pos >= 64)
        {
            refill_buffer();
        }

        // Copy available bytes
        usize available = 64 - g_rng.buffer_pos;
        usize to_copy = (len < available) ? len : available;

        lib::memcpy(dst, g_rng.buffer + g_rng.buffer_pos, to_copy);

        g_rng.buffer_pos += to_copy;
        dst += to_copy;
        len -= to_copy;
    }
}

/** @copydoc viper::tls::crypto::random_u32 */
u32 random_u32()
{
    u32 result;
    random_bytes(&result, sizeof(result));
    return result;
}

/** @copydoc viper::tls::crypto::random_u64 */
u64 random_u64()
{
    u64 result;
    random_bytes(&result, sizeof(result));
    return result;
}

/** @copydoc viper::tls::crypto::random_is_seeded */
bool random_is_seeded()
{
    return g_rng.seeded;
}

/** @copydoc viper::tls::crypto::random_entropy_bits */
usize random_entropy_bits()
{
    return g_rng.entropy_bits;
}

} // namespace viper::tls::crypto
