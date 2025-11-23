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

/// @brief Seed the random generator with an unsigned 64-bit value.
/// @details Replaces the linear congruential generator state in the current
///          VM's context with the provided seed so that future calls to
///          @ref rt_rnd produce the same deterministic sequence.
void rt_randomize_u64(uint64_t seed)
{
    RtContext *ctx = rt_get_current_context();
    assert(ctx && "rt_randomize_u64 called without active RtContext");
    ctx->rng_state = seed;
}

/// @brief Seed the random generator with a signed 64-bit value.
/// @details Casts the argument to an unsigned representation before updating
///          the current VM's RNG state so that negative seeds map to the
///          expected bit patterns produced by the BASIC runtime.
void rt_randomize_i64(long long seed)
{
    RtContext *ctx = rt_get_current_context();
    assert(ctx && "rt_randomize_i64 called without active RtContext");
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
    assert(ctx && "rt_rnd called without active RtContext");
    ctx->rng_state = ctx->rng_state * 6364136223846793005ULL + 1ULL;
    uint64_t x = (ctx->rng_state >> 11) & ((1ULL << 53) - 1);
    return (double)x * (1.0 / 9007199254740992.0);
}
