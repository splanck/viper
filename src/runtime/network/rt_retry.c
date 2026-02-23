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

typedef struct
{
    int64_t max_retries;
    int64_t base_delay_ms;
    int64_t max_delay_ms;
    int64_t current_attempt;
    int8_t exponential;
} rt_retry_data;

static void retry_finalizer(void *obj)
{
    (void)obj;
}

// --- Public API ---

void *rt_retry_new(int64_t max_retries, int64_t base_delay_ms)
{
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

void *rt_retry_exponential(int64_t max_retries, int64_t base_delay_ms, int64_t max_delay_ms)
{
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

int8_t rt_retry_can_retry(void *policy)
{
    if (!policy)
        return 0;
    rt_retry_data *data = (rt_retry_data *)policy;
    return data->current_attempt < data->max_retries ? 1 : 0;
}

int64_t rt_retry_next_delay(void *policy)
{
    if (!policy)
        return -1;
    rt_retry_data *data = (rt_retry_data *)policy;
    if (data->current_attempt >= data->max_retries)
        return -1;

    int64_t delay;
    if (data->exponential)
    {
        // Exponential backoff: base * 2^attempt, capped at max_delay_ms.
        // Overflow guard: check before multiplying to avoid wrapping past INT64_MAX.
        delay = data->base_delay_ms;
        for (int64_t i = 0; i < data->current_attempt; i++)
        {
            if (delay >= data->max_delay_ms)
            {
                delay = data->max_delay_ms;
                break;
            }
            // Avoid overflow: if doubling would exceed INT64_MAX, clamp to max_delay_ms
            if (delay > (INT64_MAX / 2))
            {
                delay = data->max_delay_ms;
                break;
            }
            delay *= 2;
            if (delay > data->max_delay_ms)
                delay = data->max_delay_ms;
        }

        // Add ±25% jitter to prevent thundering-herd on coordinated retries
        if (delay > 0)
        {
            int64_t jitter_range = delay / 4 + 1;
            delay += (int64_t)(rand() % (int)jitter_range);
            // Keep within max_delay_ms (jitter may push slightly over)
            if (delay > data->max_delay_ms)
                delay = data->max_delay_ms;
        }
    }
    else
    {
        // Fixed delay
        delay = data->base_delay_ms;
    }

    data->current_attempt++;
    return delay;
}

int64_t rt_retry_get_attempt(void *policy)
{
    if (!policy)
        return 0;
    return ((rt_retry_data *)policy)->current_attempt;
}

int64_t rt_retry_get_max_retries(void *policy)
{
    if (!policy)
        return 0;
    return ((rt_retry_data *)policy)->max_retries;
}

void rt_retry_reset(void *policy)
{
    if (!policy)
        return;
    ((rt_retry_data *)policy)->current_attempt = 0;
}

int64_t rt_retry_get_total_attempts(void *policy)
{
    if (!policy)
        return 0;
    return ((rt_retry_data *)policy)->current_attempt;
}

int8_t rt_retry_is_exhausted(void *policy)
{
    if (!policy)
        return 1;
    rt_retry_data *data = (rt_retry_data *)policy;
    return data->current_attempt >= data->max_retries ? 1 : 0;
}
