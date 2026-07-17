//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_ratelimit.c
/// @brief Token bucket rate limiter for network/API operations.
///
/// This file implements a token bucket algorithm for rate limiting. Tokens
/// are consumed when operations are attempted and refill continuously over
/// time at a configured rate.
///
/// **Token Bucket Algorithm:**
/// ```
///   Capacity: 10 tokens
///   Refill rate: 2 tokens/sec
///
///   Time 0s:  [##########] 10/10  -> acquire() succeeds, now 9/10
///   Time 0s:  [######### ] 9/10   -> acquire() succeeds, now 8/10
///   ...
///   Time 0s:  [          ] 0/10   -> acquire() fails
///   Time 1s:  [##        ] 2/10   -> 2 tokens refilled
///   Time 5s:  [##########] 10/10  -> capped at max
/// ```
///
/// **Usage Pattern:**
/// ```
/// Dim limiter = RateLimiter.New(100, 10.0)  ' 100 tokens, 10/sec refill
///
/// If limiter.TryAcquire() Then
///     SendApiRequest()
/// Else
///     Print "Rate limited, try later"
/// End If
/// ```
///
/// **Thread Safety:** Not thread-safe. External synchronization required.
///
//===----------------------------------------------------------------------===//

#include "rt_ratelimit.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

#include "rt_trap.h"

//=============================================================================
// Time Helper
//=============================================================================

/// @brief Get current time in seconds from a monotonic clock (as double).
static double current_time_sec(void) {
#if defined(_WIN32)
    // Benign race: QPC frequency is constant; duplicate init is harmless.
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0) {
        // RC-10: check return value — fall back to GetTickCount64 if QPC is unavailable
        if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
            return (double)GetTickCount64() / 1000.0;
    }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    return 0.0;
#endif
}

//=============================================================================
// Internal Structures
//=============================================================================

/// @brief Internal rate limiter data.
/// @details Capacity and whole-token balance are exact `int64_t` values so the
///          constructor argument round-trips through `get_Max` for the entire
///          Integer range (doubles lose integer precision above 2^53). Only
///          the sub-token refill remainder is fractional.
typedef struct {
    int64_t tokens_whole;    ///< Current available whole tokens (exact).
    int64_t max_tokens;      ///< Maximum token capacity (exact).
    double tokens_frac;      ///< Fractional refill carry in [0, 1).
    double refill_per_sec;   ///< Tokens refilled per second.
    double last_refill_time; ///< Last time tokens were refilled.
} rt_ratelimit_data;

/// @brief Refill tokens based on elapsed time since last refill.
static void refill_tokens(rt_ratelimit_data *data) {
    double now = current_time_sec();
    double elapsed = now - data->last_refill_time;
    if (elapsed <= 0.0)
        return;
    data->last_refill_time = now;

    double credit = elapsed * data->refill_per_sec + data->tokens_frac;
    if (!(credit > 0.0)) // also rejects NaN
        return;

    // A credit this large cannot be cast to int64_t; the bucket is full
    // regardless of the exact capacity.
    if (credit >= 9.2e18) {
        data->tokens_whole = data->max_tokens;
        data->tokens_frac = 0.0;
        return;
    }

    int64_t whole = (int64_t)credit;
    // Both operands are non-negative with tokens_whole <= max_tokens, so the
    // subtraction cannot overflow.
    if (whole >= data->max_tokens - data->tokens_whole) {
        data->tokens_whole = data->max_tokens;
        data->tokens_frac = 0.0;
    } else {
        data->tokens_whole += whole;
        data->tokens_frac = credit - (double)whole;
    }
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Creates a new token bucket rate limiter.
///
/// The limiter starts at full capacity (all tokens available).
///
/// @param max_tokens Maximum token capacity. Values <= 0 default to 1.
/// @param refill_per_sec Tokens refilled per second. Values <= 0 default to 1.0.
/// @return A new rate limiter object. Traps on allocation failure.
void *rt_ratelimit_new(int64_t max_tokens, double refill_per_sec) {
    rt_ratelimit_data *data =
        (rt_ratelimit_data *)rt_obj_new_i64(0, (int64_t)sizeof(rt_ratelimit_data));
    if (!data) {
        rt_trap("RateLimiter: memory allocation failed");
        return NULL;
    }
    data->max_tokens = max_tokens > 0 ? max_tokens : 1;
    data->tokens_whole = data->max_tokens;
    data->tokens_frac = 0.0;
    data->refill_per_sec = refill_per_sec > 0.0 ? refill_per_sec : 1.0;
    data->last_refill_time = current_time_sec();
    return data;
}

/// @brief Tries to consume 1 token.
///
/// Refills tokens based on elapsed time, then attempts to consume one token.
///
/// @param limiter Rate limiter pointer.
/// @return 1 if a token was consumed, 0 if no tokens available.
int8_t rt_ratelimit_try_acquire(void *limiter) {
    return rt_ratelimit_try_acquire_n(limiter, 1);
}

/// @brief Tries to consume N tokens.
///
/// Refills tokens based on elapsed time, then attempts to consume N tokens.
/// Either all N tokens are consumed or none are (atomic semantics).
///
/// @param limiter Rate limiter pointer.
/// @param n Number of tokens to consume. Values <= 0 return 0.
/// @return 1 if tokens were consumed, 0 if insufficient tokens.
int8_t rt_ratelimit_try_acquire_n(void *limiter, int64_t n) {
    if (!limiter || n <= 0)
        return 0;
    rt_ratelimit_data *data = (rt_ratelimit_data *)limiter;
    refill_tokens(data);
    if (data->tokens_whole >= n) {
        data->tokens_whole -= n;
        return 1;
    }
    return 0;
}

/// @brief Gets the number of currently available tokens.
///
/// Refills tokens based on elapsed time before returning.
///
/// @param limiter Rate limiter pointer.
/// @return Number of available whole tokens (exact).
int64_t rt_ratelimit_available(void *limiter) {
    if (!limiter)
        return 0;
    rt_ratelimit_data *data = (rt_ratelimit_data *)limiter;
    refill_tokens(data);
    return data->tokens_whole;
}

/// @brief Resets the limiter to full capacity.
///
/// Sets tokens to max and updates the refill timestamp.
///
/// @param limiter Rate limiter pointer.
void rt_ratelimit_reset(void *limiter) {
    if (!limiter)
        return;
    rt_ratelimit_data *data = (rt_ratelimit_data *)limiter;
    data->tokens_whole = data->max_tokens;
    data->tokens_frac = 0.0;
    data->last_refill_time = current_time_sec();
}

/// @brief Gets the maximum token capacity.
///
/// @param limiter Rate limiter pointer.
/// @return Maximum number of tokens.
int64_t rt_ratelimit_get_max(void *limiter) {
    if (!limiter)
        return 0;
    return ((rt_ratelimit_data *)limiter)->max_tokens;
}

/// @brief Gets the refill rate in tokens per second.
///
/// @param limiter Rate limiter pointer.
/// @return Refill rate (tokens/second).
double rt_ratelimit_get_rate(void *limiter) {
    if (!limiter)
        return 0.0;
    return ((rt_ratelimit_data *)limiter)->refill_per_sec;
}
