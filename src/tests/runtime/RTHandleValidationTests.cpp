//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTHandleValidationTests.cpp
// Purpose: Verify opaque runtime handles reject forged, too-small objects before
//          implementation-specific payload casts.
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <setjmp.h>

extern "C" {
#include "rt_cancellation.h"
#include "rt_channel.h"
#include "rt_concmap.h"
#include "rt_concqueue.h"
#include "rt_debounce.h"
#include "rt_future.h"
#include "rt_internal.h"
#include "rt_msgbus.h"
#include "rt_object.h"
#include "rt_random.h"
#include "rt_scheduler.h"
#include "rt_threadpool.h"
#include "rt_threads.h"

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

void vm_trap(const char *msg) {
    rt_abort(msg);
}
}

using HandleCall = void (*)(void *);

static void release_fake(void *fake) {
    if (fake && rt_obj_release_check0(fake))
        rt_obj_free(fake);
}

static void *make_fake(int64_t class_id, int64_t byte_size, uint32_t magic = 0) {
    void *fake = rt_obj_new_i64(class_id, byte_size);
    assert(fake != nullptr);
    if (magic != 0 && byte_size >= static_cast<int64_t>(sizeof(magic)))
        std::memcpy(fake, &magic, sizeof(magic));
    return fake;
}

static void expect_invalid_handle(HandleCall call,
                                  int64_t class_id,
                                  int64_t byte_size,
                                  const char *message_substring,
                                  uint32_t magic = 0) {
    void *fake = make_fake(class_id, byte_size, magic);

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        call(fake);
        rt_trap_clear_recovery();
        release_fake(fake);
        assert(false && "expected invalid handle trap");
    }

    const char *err = rt_trap_get_error();
    assert(err != nullptr);
    assert(std::strstr(err, message_substring) != nullptr);
    rt_trap_clear_recovery();
    release_fake(fake);
}

static void call_thread_join(void *obj) {
    rt_thread_join(obj);
}

static void call_safe_thread_has_error(void *obj) {
    (void)rt_thread_has_error(obj);
}

static void call_safe_i64_get(void *obj) {
    (void)rt_safe_i64_get(obj);
}

static void call_gate_get_permits(void *obj) {
    (void)rt_gate_get_permits(obj);
}

static void call_promise_is_done(void *obj) {
    (void)rt_promise_is_done(obj);
}

static void call_future_is_done(void *obj) {
    (void)rt_future_is_done(obj);
}

static void call_threadpool_get_size(void *obj) {
    (void)rt_threadpool_get_size(obj);
}

static void call_channel_get_len(void *obj) {
    (void)rt_channel_get_len(obj);
}

static void call_concqueue_len(void *obj) {
    (void)rt_concqueue_len(obj);
}

static void call_concmap_len(void *obj) {
    (void)rt_concmap_len(obj);
}

static void call_cancellation_check(void *obj) {
    (void)rt_cancellation_is_cancelled(obj);
}

static void call_debounce_get_delay(void *obj) {
    (void)rt_debounce_get_delay(obj);
}

static void call_throttle_get_interval(void *obj) {
    (void)rt_throttle_get_interval(obj);
}

static void call_scheduler_pending(void *obj) {
    (void)rt_scheduler_pending(obj);
}

static void call_msgbus_total_subscriptions(void *obj) {
    (void)rt_msgbus_total_subscriptions(obj);
}

static void call_random_next(void *obj) {
    (void)rt_rnd_method(obj);
}

int main() {
    constexpr uint32_t kThreadMagic = 0x56545244u;
    constexpr uint32_t kSafeThreadMagic = 0x56545346u;

    expect_invalid_handle(call_thread_join,
                          RT_THREAD_CLASS_ID,
                          sizeof(uint32_t),
                          "Thread: invalid thread handle",
                          kThreadMagic);
    expect_invalid_handle(call_safe_thread_has_error,
                          RT_SAFE_THREAD_CLASS_ID,
                          sizeof(uint32_t),
                          "Thread.HasError: invalid thread handle",
                          kSafeThreadMagic);
    expect_invalid_handle(call_safe_i64_get, RT_SAFE_I64_CLASS_ID, 1, "SafeI64: invalid object");
    expect_invalid_handle(call_gate_get_permits, RT_GATE_CLASS_ID, 1, "Gate: invalid object");
    expect_invalid_handle(call_promise_is_done, RT_PROMISE_CLASS_ID, 1, "Promise: invalid object");
    expect_invalid_handle(call_future_is_done, RT_FUTURE_CLASS_ID, 1, "Future: invalid object");
    expect_invalid_handle(call_threadpool_get_size, RT_THREADPOOL_CLASS_ID, 1, "Pool: invalid object");
    expect_invalid_handle(call_channel_get_len, RT_CHANNEL_CLASS_ID, 1, "Channel: invalid object");
    expect_invalid_handle(
        call_concqueue_len, RT_CONCQUEUE_CLASS_ID, 1, "ConcurrentQueue: invalid object");
    expect_invalid_handle(
        call_concmap_len, RT_CONCMAP_CLASS_ID, 1, "ConcurrentMap: invalid object");
    expect_invalid_handle(
        call_cancellation_check, RT_CANCELLATION_CLASS_ID, 1, "CancelToken: invalid object");
    expect_invalid_handle(call_debounce_get_delay,
                          RT_DEBOUNCER_CLASS_ID,
                          1,
                          "Debouncer: invalid object");
    expect_invalid_handle(call_throttle_get_interval,
                          RT_THROTTLER_CLASS_ID,
                          1,
                          "Throttler: invalid object");
    expect_invalid_handle(call_scheduler_pending, RT_SCHEDULER_CLASS_ID, 1, "Scheduler: invalid object");
    expect_invalid_handle(call_msgbus_total_subscriptions,
                          RT_MSGBUS_CLASS_ID,
                          1,
                          "invalid MessageBus object");
    expect_invalid_handle(call_random_next, RT_RANDOM_CLASS_ID, 1, "Random: invalid Random object");

    std::printf("Handle validation tests: all passed\n");
    return 0;
}
