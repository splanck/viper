//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/3d/rt_g3d_commit_queue.h
// Purpose: Internal main-thread commit queue for Graphics3D/Game3D worker jobs.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rt_g3d_commit_fn)(void *user_data);
typedef void (*rt_g3d_commit_cancel_fn)(void *user_data);

#define RT_G3D_COMMIT_COST_UNIT 1ull
#define RT_G3D_COMMIT_COST_UNLIMITED UINT64_MAX

/// @brief Create an internal FIFO queue for worker-produced main-thread commits.
void *rt_g3d_commit_queue_new(void);

/// @brief Free the queue, discarding any pending commits without running them.
void rt_g3d_commit_queue_free(void *queue);

/// @brief Enqueue a commit callback. The callback runs only when drained.
/// @return Non-zero when queued; zero for invalid input, shutdown, or allocation failure.
int8_t rt_g3d_commit_queue_enqueue(void *queue, rt_g3d_commit_fn fn, void *user_data);

/// @brief Enqueue a commit callback with an estimated main-thread cost.
/// @details The cost is an opaque budget unit; use RT_G3D_COMMIT_COST_UNIT for default work and
/// decoded-byte counts for asset uploads that should honor upload budgets.
/// @return Non-zero when queued; zero for invalid input, shutdown, or allocation failure.
int8_t rt_g3d_commit_queue_enqueue_cost(void *queue,
                                        rt_g3d_commit_fn fn,
                                        void *user_data,
                                        uint64_t cost);

/// @brief Enqueue a commit callback with an ownership cleanup callback for discarded work.
/// @details `cancel_fn` runs only if the queue is closed/freed before `fn` drains. This lets worker
///          jobs hand retained runtime handles or malloc-owned staging buffers to the queue without
///          leaking them during shutdown. A successfully drained item never calls `cancel_fn`.
/// @return Non-zero when queued. On zero, ownership remains with the caller.
int8_t rt_g3d_commit_queue_enqueue_cost_cancel(void *queue,
                                               rt_g3d_commit_fn fn,
                                               void *user_data,
                                               uint64_t cost,
                                               rt_g3d_commit_cancel_fn cancel_fn);

/// @brief Force the next @p count enqueue-wrapper allocations to fail in tests.
/// @details This process-global fault-injection hook is internal to the runtime test surface.
///          Passing zero disables it. It does not affect queue or payload allocations.
void rt_g3d_commit_queue_test_fail_next_allocations(int32_t count);

/// @brief Drain up to @p max_items callbacks on the main thread.
/// @details `max_items <= 0` drains until the queue is empty.
int64_t rt_g3d_commit_queue_drain(void *queue, int64_t max_items);

/// @brief Drain callbacks up to both an item count and cost budget.
/// @details `max_items <= 0` means no item limit. `max_cost == 0` drains only zero-cost work,
/// which lets callers pause positive-cost uploads without starving bookkeeping callbacks.
/// `max_cost == UINT64_MAX` means no cost limit. If the first pending item is larger than a
/// positive budget, it drains alone so oversized assets cannot deadlock the queue. This is a
/// single-consumer, main-thread drain; worker threads may enqueue concurrently but must not drain.
int64_t rt_g3d_commit_queue_drain_budget(void *queue, int64_t max_items, uint64_t max_cost);

/// @brief Approximate number of pending commits; concurrent enqueue/drain can change it instantly.
int64_t rt_g3d_commit_queue_pending(void *queue);

/// @brief Total number of callbacks accepted by the queue.
int64_t rt_g3d_commit_queue_submitted(void *queue);

/// @brief Total number of callbacks run by `rt_g3d_commit_queue_drain`.
int64_t rt_g3d_commit_queue_drained(void *queue);

#ifdef __cplusplus
}
#endif
