//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTThreadsTypeNameTests.cpp
// Purpose: Regression coverage for Object.TypeName/ToString on built-in
//          Viper.Threads runtime objects.
//
//===----------------------------------------------------------------------===//

#include "rt_cancellation.h"
#include "rt_channel.h"
#include "rt_concmap.h"
#include "rt_concqueue.h"
#include "rt_debounce.h"
#include "rt_future.h"
#include "rt_object.h"
#include "rt_scheduler.h"
#include "rt_string.h"
#include "rt_threadpool.h"
#include "rt_threads.h"

#include <cassert>
#include <cstring>

extern "C" void quick_type_name_entry(void *arg) {
    (void)arg;
}

static void expect_name(void *obj, const char *expected) {
    assert(obj != nullptr);

    rt_string type = rt_obj_type_name(obj);
    assert(std::strcmp(rt_string_cstr(type), expected) == 0);
    rt_string_unref(type);

    rt_string text = rt_obj_to_string(obj);
    assert(std::strcmp(rt_string_cstr(text), expected) == 0);
    rt_string_unref(text);
}

int main() {
    void *thread = rt_thread_start((void *)&quick_type_name_entry, nullptr);
    expect_name(thread, "Viper.Threads.Thread");
    rt_thread_join(thread);

    void *safe_thread = rt_thread_start_safe((void *)&quick_type_name_entry, nullptr);
    expect_name(safe_thread, "Viper.Threads.Thread");
    rt_thread_safe_join(safe_thread);

    expect_name(rt_safe_i64_new(0), "Viper.Threads.SafeI64");
    expect_name(rt_gate_new(0), "Viper.Threads.Gate");
    expect_name(rt_barrier_new(1), "Viper.Threads.Barrier");
    expect_name(rt_rwlock_new(), "Viper.Threads.RwLock");
    expect_name(rt_channel_new(1), "Viper.Threads.Channel");
    expect_name(rt_cancellation_new(), "Viper.Threads.CancelToken");

    void *promise = rt_promise_new();
    expect_name(promise, "Viper.Threads.Promise");
    expect_name(rt_promise_get_future(promise), "Viper.Threads.Future");

    void *pool = rt_threadpool_new(1);
    expect_name(pool, "Viper.Threads.Pool");
    rt_threadpool_shutdown(pool);

    expect_name(rt_concqueue_new(), "Viper.Threads.ConcurrentQueue");
    expect_name(rt_concmap_new(), "Viper.Threads.ConcurrentMap");
    expect_name(rt_scheduler_new(), "Viper.Threads.Scheduler");
    expect_name(rt_debounce_new(1), "Viper.Threads.Debouncer");
    expect_name(rt_throttle_new(1), "Viper.Threads.Throttler");
    return 0;
}
