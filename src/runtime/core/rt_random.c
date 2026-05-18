//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_random.c
// Purpose: Implements the deterministic pseudo-random number generator (PRNG)
//          for the Viper runtime. Mirrors BASIC RND semantics using a 64-bit
//          linear congruential generator (LCG) with fixed multiplier and
//          increment. RNG state lives in the per-VM RtContext so multiple VMs
//          maintain independent, isolated sequences.
//
// Key invariants:
//   - The LCG uses multiplier 6364136223846793005 and increment 1 (Numerical
//     Recipes constants); the same seed always produces the same sequence.
//   - rt_rnd returns values in the half-open interval [0.0, 1.0) by extracting
//     the top 53 bits of state and scaling to IEEE 754 double range.
//   - State is stored per-RtContext; rt_get_current_context() is consulted
//     first, falling back to rt_legacy_context() if no context is active.
//   - VM and native builds share this implementation, ensuring identical
//     sequences for a given seed across execution modes.
//
// Ownership/Lifetime:
//   - Static RNG state is a uint64_t field inside RtContext.
//   - Random class instances are GC-managed objects with their own uint64_t state.
//
// Links: src/runtime/core/rt_random.h (public API),
//        src/runtime/core/rt_context.h (RtContext definition, rng_state field)
//
//===----------------------------------------------------------------------===//

#include "rt_random.h"
#include "rt_context.h"
#include "rt_internal.h"
#include "rt_object.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>

// Forward declare to avoid header dependency (must be at file scope for MSVC)
extern int64_t rt_seq_len(void *);
extern void *rt_seq_get(void *, int64_t);
extern void rt_seq_set(void *, int64_t, void *);

typedef struct rt_random_impl {
    void **vptr;
    uint64_t state;
} rt_random_impl;

static uint64_t rt_random_bounded_u64_from_state(uint64_t *state, uint64_t bound);

/// @brief Resolve the runtime context whose RNG state we read/write.
/// @details Prefers the per-thread `rt_get_current_context()` for proper isolation when
///          multiple VMs are active; falls back to the legacy global context for native
///          callers that haven't pushed a context yet.
static RtContext *rt_random_context(void) {
    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();
    return ctx;
}

/// @brief Validate a `void *` handle as a `Random` instance.
/// @details Checks the class ID and traps on mismatch so a wrong-type handle
///          surfaces as a real error instead of corrupted RNG state.
/// @param self Caller-supplied handle expected to be a `Random` object.
/// @return Validated typed pointer; traps on mismatch.
static rt_random_impl *as_random(void *self) {
    if (!rt_obj_is_instance(self, RT_RANDOM_CLASS_ID, sizeof(rt_random_impl)))
        rt_trap("Random: invalid Random object");
    return (rt_random_impl *)self;
}

/// @brief Advance an LCG state and return the new value.
/// @details Single step of a 64-bit linear-congruential generator with the
///          MMIX multiplier (`6364136223846793005`) and increment `1`. The
///          increment is co-prime with `2^64` so the period is the full
///          state space.
/// @param state Pointer to the 64-bit state; updated in place.
/// @return The new state value (also the function's raw u64 output).
static uint64_t rt_random_step_u64(uint64_t *state) {
    *state = *state * 6364136223846793005ULL + 1ULL;
    return *state;
}

/// @brief Step the active context's RNG and return one raw 64-bit sample.
/// @details Thin wrapper over @ref rt_random_step_u64 against the per-context
///          RNG state, so independent contexts/threads see independent sequences.
/// @return Raw 64-bit LCG output (no bound applied).
static uint64_t rt_random_next_u64(void) {
    return rt_random_step_u64(&rt_random_context()->rng_state);
}

/// @brief Sample a uniform random integer in `[0, bound)` with rejection sampling.
/// @details Naïve `next_u64() % bound` introduces modulo bias when `bound` does not
///          evenly divide `2^64`. The threshold trick rejects the small biased band so
///          the surviving samples are exactly uniform. Worst-case rejection rate is
///          tiny for any realistic `bound`. A `bound` of 0 is treated as "no upper
///          bound" and returns the raw next-state.
static uint64_t rt_random_bounded_u64(uint64_t bound) {
    return rt_random_bounded_u64_from_state(&rt_random_context()->rng_state, bound);
}

/// @brief Sample `[0, bound)` from an explicit RNG state with rejection sampling.
/// @details Implementation behind @ref rt_random_bounded_u64 that takes the
///          state by pointer so it can also serve the per-instance `Random`
///          object. Computes the rejection threshold `(2^64 mod bound)` and
///          rejects samples below it; the surviving samples are exactly
///          uniform after `x % bound`. `bound == 0` falls back to returning
///          the raw step output (treated as unbounded).
/// @param state Pointer to the LCG state; updated each iteration.
/// @param bound Upper bound (exclusive); 0 means "no bound".
/// @return Uniform random integer in `[0, bound)`, or a raw u64 when `bound == 0`.
static uint64_t rt_random_bounded_u64_from_state(uint64_t *state, uint64_t bound) {
    if (bound == 0)
        return rt_random_step_u64(state);

    const uint64_t threshold = (uint64_t)(0ULL - bound) % bound;
    for (;;) {
        uint64_t x = rt_random_step_u64(state);
        if (x >= threshold)
            return x % bound;
    }
}

/// @brief Sample a uniform `[0.0, 1.0)` double from an explicit RNG state.
/// @details Takes the top 53 bits of one LCG step (matching the precision of
///          a `double`'s significand) and divides by `2^53` to produce a
///          uniformly-distributed result in the half-open interval `[0, 1)`.
///          Skipping the low 11 bits avoids the LCG's weaker low-order bits.
/// @param state Pointer to the LCG state; advanced by one step.
/// @return Uniform double in `[0.0, 1.0)`.
static double rt_random_unit_from_state(uint64_t *state) {
    uint64_t x = (rt_random_step_u64(state) >> 11) & ((1ULL << 53) - 1);
    return (double)x * (1.0 / 9007199254740992.0);
}

/// @brief Seed the random generator with an unsigned 64-bit value.
/// @details Replaces the linear congruential generator state in the current
///          VM's context with the provided seed so that future calls to
///          @ref rt_rnd produce the same deterministic sequence.
void rt_randomize_u64(uint64_t seed) {
    rt_random_context()->rng_state = seed;
}

/// @brief Seed the random generator with a signed 64-bit value.
/// @details Casts the argument to an unsigned representation before updating
///          the current VM's RNG state so that negative seeds map to the
///          expected bit patterns produced by the BASIC runtime.
void rt_randomize_i64(long long seed) {
    rt_random_context()->rng_state = (uint64_t)seed;
}

/// @brief Produce a pseudo-random double in the half-open interval [0, 1).
/// @details Advances the linear congruential generator in the current VM's
///          context using the Numerical Recipes multiplier and increment,
///          extracts the top 53 bits, and scales them into IEEE 754 double
///          range.  The algorithm mirrors the VM implementation so identical
///          seeds yield identical sequences across VM instances.
double rt_rnd(void) {
    return rt_random_unit_from_state(&rt_random_context()->rng_state);
}

/// @brief Generate a random integer in the half-open interval [0, max).
/// @details Advances the linear congruential generator and returns the result
///          modulo max to produce an integer in the range [0, max).  When max
///          is non-positive, returns 0.
long long rt_rand_int(long long max) {
    if (max <= 0)
        return 0;
    return (long long)rt_random_bounded_u64((uint64_t)max);
}

//=============================================================================
// Random Distributions
//=============================================================================

/// @brief Generate a random integer in the range [min, max] (inclusive).
/// @details Swaps bounds if inverted, then uses the core LCG.
long long rt_rand_range(long long min, long long max) {
    if (min > max) {
        long long tmp = min;
        min = max;
        max = tmp;
    }
    // Use unsigned arithmetic to avoid signed overflow when max - min == LLONG_MAX
    unsigned long long urange = (unsigned long long)max - (unsigned long long)min + 1ULL;
    if (urange == 0) {
        // Full signed range: every 64-bit LCG state maps to one signed value.
        return (long long)rt_random_next_u64();
    }
    return (long long)((uint64_t)min + rt_random_bounded_u64((uint64_t)urange));
}

/// @brief Generate a random number from a Gaussian (normal) distribution.
/// @details Uses the Box-Muller transform: given two uniform U1, U2 in (0,1),
///          Z = sqrt(-2*ln(U1)) * cos(2*pi*U2) is standard normal N(0,1).
///          Then scale/shift to N(mean, stddev^2).
double rt_rand_gaussian(double mean, double stddev) {
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
double rt_rand_exponential(double lambda) {
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
long long rt_rand_dice(long long sides) {
    if (sides <= 0)
        return 1;
    return 1 + rt_rand_int(sides);
}

/// @brief Generate a random boolean with given probability.
/// @details Returns 1 with probability p, 0 with probability 1-p.
long long rt_rand_chance(double probability) {
    if (probability <= 0.0)
        return 0;
    if (probability >= 1.0)
        return 1;
    return rt_rnd() < probability ? 1 : 0;
}

/// @brief Create a Random object with independent RNG state.
/// @details This enables `new Viper.Math.Random(seed)` without mutating the
///          VM/global RNG used by static Viper.Math.Random functions.
void *rt_random_new(long long seed) {
    rt_random_impl *rng =
        (rt_random_impl *)rt_obj_new_i64(RT_RANDOM_CLASS_ID, (int64_t)sizeof(rt_random_impl));
    if (!rng) {
        rt_trap("Random.New: memory allocation failed");
        return NULL;
    }
    rng->state = (uint64_t)seed;
    return rng;
}

/// @brief Instance method for Random.Next/NextDouble.
double rt_rnd_method(void *self) {
    rt_random_impl *rng = as_random(self);
    return rt_random_unit_from_state(&rng->state);
}

/// @brief Instance method for Random.NextInt(max).
long long rt_rand_int_method(void *self, long long max) {
    if (max <= 0)
        return 0;
    rt_random_impl *rng = as_random(self);
    return (long long)rt_random_bounded_u64_from_state(&rng->state, (uint64_t)max);
}

/// @brief Instance method for Random.Range(min, max) / NextInt(min, max).
long long rt_rand_range_method(void *self, long long min, long long max) {
    rt_random_impl *rng = as_random(self);
    if (min > max) {
        long long tmp = min;
        min = max;
        max = tmp;
    }
    unsigned long long urange = (unsigned long long)max - (unsigned long long)min + 1ULL;
    if (urange == 0)
        return (long long)rt_random_step_u64(&rng->state);
    return (long long)((uint64_t)min +
                       rt_random_bounded_u64_from_state(&rng->state, (uint64_t)urange));
}

/// @brief Instance method for Random.Seed(seed).
void rt_randomize_i64_method(void *self, long long seed) {
    rt_random_impl *rng = as_random(self);
    rng->state = (uint64_t)seed;
}

/// @brief Shuffle elements in a Seq randomly (Fisher-Yates algorithm).
/// @details Uses the current RNG state for deterministic shuffling.
void rt_rand_shuffle(void *seq) {
    if (!seq)
        return;

    int64_t n = rt_seq_len(seq);
    if (n <= 1)
        return;

    // Fisher-Yates shuffle
    for (int64_t i = n - 1; i > 0; i--) {
        int64_t j = rt_rand_int(i + 1);
        if (i != j) {
            // Retain tmp before swap to prevent use-after-free when Seq owns elements.
            // rt_seq_set releases the old value at i (which is tmp); retaining prevents
            // premature deallocation before tmp is stored at j.
            void *tmp = rt_seq_get(seq, i);
            rt_obj_retain_maybe(tmp);
            rt_seq_set(seq, i, rt_seq_get(seq, j));
            rt_seq_set(seq, j, tmp);
            if (rt_obj_release_check0(tmp))
                rt_obj_free(tmp);
        }
    }
}
