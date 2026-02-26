//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_parallel.h
// Purpose: High-level parallel execution patterns (ForEach, Map, Invoke, Reduce) using the thread
// pool, preserving output order and supporting custom pool injection.
//
// Key invariants:
//   - Output order matches input order for Map; ForEach has no defined result order.
//   - NULL pool argument falls back to the default global thread pool.
//   - All patterns wait for all tasks to complete before returning.
//   - Reduce uses a binary combine function with an initial accumulator value.
//
// Ownership/Lifetime:
//   - Returned sequences are caller-owned; caller must release.
//   - Thread pools are shared; caller must not destroy a pool in use by parallel operations.
//
// Links: src/runtime/threads/rt_parallel.c (implementation), src/runtime/threads/rt_threadpool.h
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_PARALLEL_H
#define VIPER_RT_PARALLEL_H

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=============================================================================
    // Viper.Threads.Parallel
    //=============================================================================

    /// @brief Execute a function for each item in a sequence, in parallel.
    /// @details Distributes work across available CPU cores.
    /// @param seq Sequence of items to process.
    /// @param func Function to call for each item (signature: void(*)(void*)).
    void rt_parallel_foreach(void *seq, void *func);

    /// @brief Execute a function for each item with a custom thread pool.
    /// @param seq Sequence of items to process.
    /// @param func Function to call for each item.
    /// @param pool Thread pool to use (or NULL for default).
    void rt_parallel_foreach_pool(void *seq, void *func, void *pool);

    /// @brief Transform a sequence in parallel using a map function.
    /// @details Applies func to each item and collects results.
    /// @param seq Sequence of items to transform.
    /// @param func Function to transform each item (signature: void*(*)(void*)).
    /// @return New sequence containing transformed items (same order as input).
    void *rt_parallel_map(void *seq, void *func);

    /// @brief Transform a sequence in parallel with a custom thread pool.
    /// @param seq Sequence of items to transform.
    /// @param func Transform function.
    /// @param pool Thread pool to use (or NULL for default).
    /// @return New sequence containing transformed items.
    void *rt_parallel_map_pool(void *seq, void *func, void *pool);

    /// @brief Execute multiple functions in parallel and wait for all to complete.
    /// @details Functions are executed concurrently on a thread pool.
    /// @param funcs Sequence of functions (void(*)()).
    void rt_parallel_invoke(void *funcs);

    /// @brief Execute multiple functions in parallel with custom pool.
    /// @param funcs Sequence of functions.
    /// @param pool Thread pool to use (or NULL for default).
    void rt_parallel_invoke_pool(void *funcs, void *pool);

    /// @brief Parallel for loop over a range of integers.
    /// @details Calls func(i) for each i in [start, end).
    /// @param start Start of range (inclusive).
    /// @param end End of range (exclusive).
    /// @param func Function to call (signature: void(*)(int64_t)).
    void rt_parallel_for(int64_t start, int64_t end, void *func);

    /// @brief Parallel for loop with custom pool.
    /// @param start Start of range (inclusive).
    /// @param end End of range (exclusive).
    /// @param func Function to call.
    /// @param pool Thread pool to use (or NULL for default).
    void rt_parallel_for_pool(int64_t start, int64_t end, void *func, void *pool);

    /// @brief Reduce a sequence in parallel using a binary combine function.
    /// @details Splits the sequence into chunks, reduces each chunk in parallel,
    /// then combines partial results on the calling thread.
    /// @param seq Sequence of items to reduce.
    /// @param func Binary function combining two items (signature: void*(*)(void*, void*)).
    /// @param identity Identity element for the reduce operation.
    /// @return The final reduced value.
    void *rt_parallel_reduce(void *seq, void *func, void *identity);

    /// @brief Reduce a sequence in parallel with a custom thread pool.
    /// @param seq Sequence of items to reduce.
    /// @param func Binary combine function.
    /// @param identity Identity element.
    /// @param pool Thread pool to use (or NULL for default).
    /// @return The final reduced value.
    void *rt_parallel_reduce_pool(void *seq, void *func, void *identity, void *pool);

    /// @brief Get the default number of parallel workers.
    /// @return Number of CPU cores, or 4 if detection fails.
    int64_t rt_parallel_default_workers(void);

    /// @brief Create a shared default thread pool.
    /// @details Lazily creates a pool with rt_parallel_default_workers threads.
    /// @return The default shared thread pool.
    void *rt_parallel_default_pool(void);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_PARALLEL_H
