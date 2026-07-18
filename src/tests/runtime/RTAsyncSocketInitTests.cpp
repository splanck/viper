//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTAsyncSocketInitTests.cpp
// Purpose: Exercise AsyncSocket's retryable first-use Pool state machine and
//          producer-owned Future settlement under deterministic allocation
//          failure and concurrent initialization.
// Key invariants:
//   - A failed Pool allocation restores the uninitialized state and settles the
//     operation's Future instead of caching a NULL singleton.
//   - Concurrent first-use callers observe one successfully published Pool and
//     every accepted request eventually settles.
//   - Pool-size configuration remains mutable after failed initialization and
//     becomes immutable immediately after successful publication.
// Ownership/Lifetime:
//   - Every Future, optional result, and error String acquired by the test is
//     released before process exit; the shared Pool intentionally lives for the
//     runtime process lifetime.
// Links: src/runtime/network/rt_async_socket.c,
//        src/runtime/threads/rt_future.c,
//        docs/adr/0124-trap-safe-native-promise-completion-and-async-socket-initialization.md
//
//===----------------------------------------------------------------------===//

#include "rt_async_socket.h"
#include "rt_future.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <setjmp.h>
#include <thread>

namespace {

int g_async_alloc_fail_countdown = 0;

/// @brief Release one caller-owned runtime-managed test reference.
/// @details Dispatches through the common deferred-release/free pair so the
///          helper is valid for Future results, Promise/Future objects, Boxes,
///          Bytes, TCP handles, and other ordinary managed objects. NULL is a
///          no-op, allowing unconditional cleanup after either Future outcome.
/// @param obj Caller-owned managed reference, or NULL.
void release_managed(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Fail one selected managed allocation during the first AsyncSocket call.
/// @details Promise and Future construction consume the first two allocations;
///          the third is the shared Pool object. Returning NULL for that call
///          deterministically exercises initialization rollback. Later calls
///          delegate normally even before the hook is removed, allowing the
///          failed Future to allocate an optional diagnostic String.
/// @param bytes Requested allocation size in bytes.
/// @param next Runtime's default allocation function.
/// @return NULL for the selected allocation, otherwise @p next's result.
void *fail_selected_async_allocation(int64_t bytes, void *(*next)(int64_t)) {
    if (g_async_alloc_fail_countdown > 0 && --g_async_alloc_fail_countdown == 0)
        return nullptr;
    return next(bytes);
}

/// @brief Assert that one callback raises a recoverable runtime trap.
/// @details Installs a local legacy recovery frame, runs @p callback, and clears
///          the frame on both normal and non-local paths. The test aborts if the
///          callback unexpectedly returns without trapping.
/// @tparam Callback Nullary callable type.
/// @param callback Operation expected to trap.
template <typename Callback> void require_trap(Callback callback) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        callback();
        rt_trap_clear_recovery();
        assert(false && "expected runtime trap");
    }
    rt_trap_clear_recovery();
}

/// @brief Warm the Promise/Future registries before allocation-count injection.
/// @details Removes one-time registry growth from the deterministic allocation
///          sequence while deliberately avoiding any AsyncSocket call, so the
///          shared Pool remains uninitialized for the actual regression.
void warm_promise_registry() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    assert(promise != nullptr && future != nullptr);
    release_managed(future);
    release_managed(promise);
}

/// @brief Release one completed Future and whichever typed result it produced.
/// @details Network refusal is the expected outcome and yields a retained error
///          String. If a local service unexpectedly accepts the probe port, the
///          successful TCP result is retrieved and released instead. This keeps
///          the test independent of host service configuration.
/// @param future Caller-owned Future known to have settled.
void release_settled_probe_future(void *future) {
    assert(future != nullptr);
    assert(rt_future_wait_for(future, 5000) == 1);
    if (rt_future_is_error(future)) {
        rt_string error = rt_future_get_error(future);
        assert(error != nullptr);
        rt_string_unref(error);
    } else {
        void *result = rt_future_get(future);
        assert(result != nullptr);
        release_managed(result);
    }
    release_managed(future);
}

/// @brief Run the failed-initialization rollback and concurrent-retry regression.
/// @details First injects OOM into Pool-object creation and verifies an error
///          Future with the expected initialization diagnostic. Configuration is
///          then changed successfully, eight callers race the retry path, and all
///          resulting Futures settle. A post-publication configuration attempt
///          must trap, proving the state reached its stable READY phase.
void test_pool_initialization_failure_is_retryable() {
    warm_promise_registry();
    rt_async_socket_set_pool_size(1);
    rt_string host = rt_const_cstr("127.0.0.1");

    g_async_alloc_fail_countdown = 3;
    rt_set_alloc_hook(fail_selected_async_allocation);
    void *failed = rt_async_connect_for(host, 1, 250);
    rt_set_alloc_hook(nullptr);

    assert(g_async_alloc_fail_countdown == 0);
    assert(failed != nullptr);
    assert(rt_future_wait_for(failed, 0) == 1);
    assert(rt_future_is_error(failed) == 1);
    rt_string initialization_error = rt_future_get_error(failed);
    assert(initialization_error != nullptr);
    assert(std::strstr(rt_string_cstr(initialization_error), "pool initialization failed") !=
           nullptr);
    rt_string_unref(initialization_error);
    release_managed(failed);

    // A failed attempt must restore the configurable UNINITIALIZED state.
    rt_async_socket_set_pool_size(2);

    constexpr size_t caller_count = 8;
    std::array<void *, caller_count> futures{};
    std::array<std::thread, caller_count> callers;
    std::atomic<bool> start{false};
    for (size_t i = 0; i < caller_count; ++i) {
        callers[i] = std::thread([&, i]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            futures[i] = rt_async_connect_for(host, 1, 250);
        });
    }
    start.store(true, std::memory_order_release);
    for (std::thread &caller : callers)
        caller.join();

    require_trap([]() { rt_async_socket_set_pool_size(3); });
    for (void *future : futures)
        release_settled_probe_future(future);
}

} // namespace

/// @brief Execute the AsyncSocket initialization regression suite.
/// @return Zero after every ownership, rollback, and concurrency assertion passes.
int main() {
    test_pool_initialization_failure_is_retryable();
    std::puts("All AsyncSocket initialization tests passed.");
    return 0;
}
