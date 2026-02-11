//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the deterministic pseudo-random number generator exposed through
// the C runtime ABI.  The generator mirrors the BASIC `RND` semantics by using
// a 64-bit linear congruential algorithm with a fixed multiplier and
// increment.  Runtime clients can reseed the generator using signed or unsigned
// inputs and retrieve uniform doubles in the half-open interval [0, 1).  The
// state now lives in the per-VM RtContext so multiple VMs can maintain
// independent RNG sequences.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Deterministic pseudo-random number generator for the runtime.
/// @details Provides seeding helpers for both signed and unsigned integers and
///          exposes the BASIC-compatible `rt_rnd` entry point that returns
///          doubles in the range [0, 1).  The implementation uses a linear
///          congruential generator so that the VM and native builds produce
///          identical sequences for a given seed. RNG state is now stored in
///          the per-VM RtContext for isolation.

#include "rt_random.h"
#include "rt_context.h"
#include "rt_internal.h"
#include <assert.h>
#include <math.h>

/// @brief Seed the random generator with an unsigned 64-bit value.
/// @details Replaces the linear congruential generator state in the current
///          VM's context with the provided seed so that future calls to
///          @ref rt_rnd produce the same deterministic sequence.
void rt_randomize_u64(uint64_t seed)
{
    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();
    ctx->rng_state = seed;
}

/// @brief Seed the random generator with a signed 64-bit value.
/// @details Casts the argument to an unsigned representation before updating
///          the current VM's RNG state so that negative seeds map to the
///          expected bit patterns produced by the BASIC runtime.
void rt_randomize_i64(long long seed)
{
    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();
    ctx->rng_state = (uint64_t)seed;
}

/// @brief Produce a pseudo-random double in the half-open interval [0, 1).
/// @details Advances the linear congruential generator in the current VM's
///          context using the Numerical Recipes multiplier and increment,
///          extracts the top 53 bits, and scales them into IEEE 754 double
///          range.  The algorithm mirrors the VM implementation so identical
///          seeds yield identical sequences across VM instances.
double rt_rnd(void)
{
    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();
    ctx->rng_state = ctx->rng_state * 6364136223846793005ULL + 1ULL;
    uint64_t x = (ctx->rng_state >> 11) & ((1ULL << 53) - 1);
    return (double)x * (1.0 / 9007199254740992.0);
}

/// @brief Generate a random integer in the half-open interval [0, max).
/// @details Advances the linear congruential generator and returns the result
///          modulo max to produce an integer in the range [0, max).  When max
///          is non-positive, returns 0.
long long rt_rand_int(long long max)
{
    if (max <= 0)
        return 0;
    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();
    ctx->rng_state = ctx->rng_state * 6364136223846793005ULL + 1ULL;
    // Use unsigned modulo to avoid bias issues with negative numbers
    uint64_t umax = (uint64_t)max;
    return (long long)(ctx->rng_state % umax);
}

//=============================================================================
// Random Distributions
//=============================================================================

/// @brief Generate a random integer in the range [min, max] (inclusive).
/// @details Swaps bounds if inverted, then uses the core LCG.
long long rt_rand_range(long long min, long long max)
{
    if (min > max)
    {
        long long tmp = min;
        min = max;
        max = tmp;
    }
    long long range = max - min + 1;
    return min + rt_rand_int(range);
}

/// @brief Generate a random number from a Gaussian (normal) distribution.
/// @details Uses the Box-Muller transform: given two uniform U1, U2 in (0,1),
///          Z = sqrt(-2*ln(U1)) * cos(2*pi*U2) is standard normal N(0,1).
///          Then scale/shift to N(mean, stddev^2).
double rt_rand_gaussian(double mean, double stddev)
{
    if (stddev <= 0.0)
        return mean;

    // Generate two uniform random numbers in (0, 1)
    // Avoid exact 0 to prevent log(0)
    double u1 = rt_rnd();
    double u2 = rt_rnd();

    // Ensure u1 is not zero (extremely rare but possible)
    while (u1 == 0.0)
        u1 = rt_rnd();

    // Box-Muller transform
    static const double TWO_PI = 6.283185307179586476925286766559;
    double z = sqrt(-2.0 * log(u1)) * cos(TWO_PI * u2);

    return mean + stddev * z;
}

/// @brief Generate a random number from an exponential distribution.
/// @details Uses inverse transform sampling: X = -ln(1-U)/lambda where U is
///          uniform [0,1). This produces Exp(lambda) with mean 1/lambda.
double rt_rand_exponential(double lambda)
{
    if (lambda <= 0.0)
        return 0.0;

    double u = rt_rnd();
    // Avoid log(0) when u is exactly 1.0 (extremely rare)
    while (u >= 1.0)
        u = rt_rnd();

    return -log(1.0 - u) / lambda;
}

/// @brief Simulate a dice roll (1 to sides inclusive).
/// @details Returns a uniform random integer in [1, sides].
long long rt_rand_dice(long long sides)
{
    if (sides <= 0)
        return 1;
    return 1 + rt_rand_int(sides);
}

/// @brief Generate a random boolean with given probability.
/// @details Returns 1 with probability p, 0 with probability 1-p.
long long rt_rand_chance(double probability)
{
    if (probability <= 0.0)
        return 0;
    if (probability >= 1.0)
        return 1;
    return rt_rnd() < probability ? 1 : 0;
}

/// @brief Create a Random object (seeds the global RNG and returns a wrapper).
/// @details Seeds the global RNG with the given seed and returns a GC-managed
///          object. This enables `NEW Viper.Math.Random(seed)` in frontends.
///          The Random class uses global state, so this is equivalent to calling
///          Random.Seed(seed) and returning a handle.
void *rt_random_new(long long seed)
{
    // Seed the global RNG
    rt_randomize_i64(seed);

    // Return a minimal GC-managed object as the Random instance handle
    void *obj = rt_obj_new_i64(0, (int64_t)sizeof(void *));
    return obj;
}

/// @brief Shuffle elements in a Seq randomly (Fisher-Yates algorithm).
/// @details Uses the current RNG state for deterministic shuffling.
void rt_rand_shuffle(void *seq)
{
    if (!seq)
        return;

    // Forward declare to avoid header dependency
    extern int64_t rt_seq_len(void *);
    extern void *rt_seq_get(void *, int64_t);
    extern void rt_seq_set(void *, int64_t, void *);

    int64_t n = rt_seq_len(seq);
    if (n <= 1)
        return;

    // Fisher-Yates shuffle
    for (int64_t i = n - 1; i > 0; i--)
    {
        int64_t j = rt_rand_int(i + 1);
        if (i != j)
        {
            void *tmp = rt_seq_get(seq, i);
            rt_seq_set(seq, i, rt_seq_get(seq, j));
            rt_seq_set(seq, j, tmp);
        }
    }
}
