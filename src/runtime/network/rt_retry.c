//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_retry.h"

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

// --- Internal structure ---

typedef struct {
    int64_t max_retries;
    int64_t base_delay_ms;
    int64_t max_delay_ms;
    int64_t current_attempt;
    int8_t exponential;
} rt_retry_data;

static void retry_finalizer(void *obj) {
    (void)obj;
}

// --- Public API ---

/// @brief Construct a fixed-delay retry policy: every retry waits exactly `base_delay_ms`.
/// `max_retries` clamped to >= 0. Returned policy is GC-managed; consume via `_can_retry` /
/// `_next_delay` in a loop.
void *rt_retry_new(int64_t max_retries, int64_t base_delay_ms) {
    void *obj = rt_obj_new_i64(0, sizeof(rt_retry_data));
    rt_retry_data *data = (rt_retry_data *)obj;
    data->max_retries = max_retries >= 0 ? max_retries : 0;
    data->base_delay_ms = base_delay_ms >= 0 ? base_delay_ms : 0;
    data->max_delay_ms = base_delay_ms; // Fixed delay
    data->current_attempt = 0;
    data->exponential = 0;
    rt_obj_set_finalizer(obj, retry_finalizer);
    return obj;
}

/// @brief Construct an exponential-backoff retry policy: delays double each attempt
/// (`base * 2^attempt`), capped at `max_delay_ms`, with 0–25% additive jitter to avoid
/// thundering-herd retries from coordinated clients. Uses thread-local xorshift PRNG (no global
/// state) so jitter is independent across threads.
void *rt_retry_exponential(int64_t max_retries, int64_t base_delay_ms, int64_t max_delay_ms) {
    void *obj = rt_obj_new_i64(0, sizeof(rt_retry_data));
    rt_retry_data *data = (rt_retry_data *)obj;
    data->max_retries = max_retries >= 0 ? max_retries : 0;
    data->base_delay_ms = base_delay_ms >= 0 ? base_delay_ms : 0;
    data->max_delay_ms = max_delay_ms >= base_delay_ms ? max_delay_ms : base_delay_ms;
    data->current_attempt = 0;
    data->exponential = 1;
    rt_obj_set_finalizer(obj, retry_finalizer);
    return obj;
}

/// @brief Check whether another retry attempt is allowed (current < max).
int8_t rt_retry_can_retry(void *policy) {
    if (!policy)
        return 0;
    rt_retry_data *data = (rt_retry_data *)policy;
    return data->current_attempt < data->max_retries ? 1 : 0;
}

/// @brief Compute the delay (ms) before the next retry attempt and advance the counter.
/// @details In exponential mode the delay is `base_delay_ms * 2^attempt`,
///          capped at `max_delay_ms`. The doubling loop checks
///          `delay > INT64_MAX / 2` *before* multiplying — checking after the
///          fact would already have wrapped past INT64_MAX into negative
///          territory, which would then compare incorrectly against
///          `max_delay_ms`. Once the cap is reached, the loop short-circuits
///          so further attempts don't keep iterating uselessly.
///
///          A 0–25% additive jitter is applied to the computed delay to
///          prevent the **thundering herd** problem — when N clients all
///          retry on the same backoff schedule (e.g., a server burped at
///          T=0 and they all retry at T=base, then T=2*base, then T=4*base
///          ...), they synchronize and re-overload the server at every
///          retry boundary. Adding a per-call random offset spreads them out.
///
///          The jitter PRNG is a thread-local xorshift seeded from the
///          address of its own slot — this gives each thread an independent
///          stream without taking a lock or sharing state, and the seeding
///          is good enough for jitter (no cryptographic strength needed).
///          Using the global `rand()` would be a contention point and
///          would coordinate jitter across threads, defeating the purpose.
///
///          Returns -1 when retries are exhausted (`current_attempt >=
///          max_retries`); the counter is *not* advanced in that case.
int64_t rt_retry_next_delay(void *policy) {
    if (!policy)
        return -1;
    rt_retry_data *data = (rt_retry_data *)policy;
    if (data->current_attempt >= data->max_retries)
        return -1;

    int64_t delay;
    if (data->exponential) {
        // Exponential backoff: base * 2^attempt, capped at max_delay_ms.
        // Overflow guard: check before multiplying to avoid wrapping past INT64_MAX.
        delay = data->base_delay_ms;
        for (int64_t i = 0; i < data->current_attempt; i++) {
            if (delay >= data->max_delay_ms) {
                delay = data->max_delay_ms;
                break;
            }
            // Avoid overflow: if doubling would exceed INT64_MAX, clamp to max_delay_ms
            if (delay > (INT64_MAX / 2)) {
                delay = data->max_delay_ms;
                break;
            }
            delay *= 2;
            if (delay > data->max_delay_ms)
                delay = data->max_delay_ms;
        }

        // Add 0-25% additive jitter to prevent thundering-herd on coordinated retries
        // Uses thread-safe local xorshift PRNG instead of global rand()
        if (delay > 0) {
            static _Thread_local uint64_t jitter_rng = 0;
            if (!jitter_rng)
                jitter_rng = (uint64_t)(uintptr_t)&jitter_rng ^ 0x5DEECE66DULL;
            jitter_rng ^= jitter_rng >> 12;
            jitter_rng ^= jitter_rng << 25;
            jitter_rng ^= jitter_rng >> 27;
            uint64_t r = jitter_rng * 0x2545F4914F6CDD1DULL;
            int64_t jitter_range = delay / 4 + 1;
            delay += (int64_t)(r % (uint64_t)jitter_range);
            // Keep within max_delay_ms (jitter may push slightly over)
            if (delay > data->max_delay_ms)
                delay = data->max_delay_ms;
        }
    } else {
        // Fixed delay
        delay = data->base_delay_ms;
    }

    data->current_attempt++;
    return delay;
}

/// @brief Return the number of retry attempts consumed so far.
int64_t rt_retry_get_attempt(void *policy) {
    if (!policy)
        return 0;
    return ((rt_retry_data *)policy)->current_attempt;
}

/// @brief Return the maximum number of retries configured for this policy.
int64_t rt_retry_get_max_retries(void *policy) {
    if (!policy)
        return 0;
    return ((rt_retry_data *)policy)->max_retries;
}

/// @brief Reset the attempt counter to zero so the policy can be reused.
void rt_retry_reset(void *policy) {
    if (!policy)
        return;
    ((rt_retry_data *)policy)->current_attempt = 0;
}

/// @brief Return the total number of attempts made (same as get_attempt).
int64_t rt_retry_get_total_attempts(void *policy) {
    if (!policy)
        return 0;
    return ((rt_retry_data *)policy)->current_attempt;
}

/// @brief Check whether all retry attempts have been used up.
int8_t rt_retry_is_exhausted(void *policy) {
    if (!policy)
        return 1;
    rt_retry_data *data = (rt_retry_data *)policy;
    return data->current_attempt >= data->max_retries ? 1 : 0;
}
