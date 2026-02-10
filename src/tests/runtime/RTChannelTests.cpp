//===----------------------------------------------------------------------===//
// RTChannelTests.cpp - Tests for rt_channel (thread-safe bounded channel)
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
#include "rt_internal.h"
#include "rt_channel.h"
#include "rt_object.h"

void vm_trap(const char *msg) {
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}
}

static void *make_obj() {
    return rt_obj_new_i64(0, 8);
}

//=============================================================================
// Creation and properties
//=============================================================================

static void test_new_buffered() {
    void *ch = rt_channel_new(10);
    assert(ch != NULL);
    assert(rt_channel_get_cap(ch) == 10);
    assert(rt_channel_get_len(ch) == 0);
    assert(rt_channel_get_is_empty(ch) == 1);
    assert(rt_channel_get_is_full(ch) == 0);
    assert(rt_channel_get_is_closed(ch) == 0);
    rt_channel_close(ch);
}

static void test_new_synchronous() {
    void *ch = rt_channel_new(0);
    assert(ch != NULL);
    assert(rt_channel_get_cap(ch) == 0);
    assert(rt_channel_get_len(ch) == 0);
    assert(rt_channel_get_is_empty(ch) == 1);
    // Synchronous channels report full
    assert(rt_channel_get_is_full(ch) == 1);
    rt_channel_close(ch);
}

static void test_new_negative_capacity() {
    void *ch = rt_channel_new(-5);
    assert(ch != NULL);
    assert(rt_channel_get_cap(ch) == 0); // Clamped to 0
    rt_channel_close(ch);
}

//=============================================================================
// Buffered send/recv (single-threaded via try_ variants)
//=============================================================================

static void test_try_send_recv() {
    void *ch = rt_channel_new(5);
    void *a = make_obj();
    void *b = make_obj();
    void *c = make_obj();

    assert(rt_channel_try_send(ch, a) == 1);
    assert(rt_channel_try_send(ch, b) == 1);
    assert(rt_channel_try_send(ch, c) == 1);
    assert(rt_channel_get_len(ch) == 3);
    assert(rt_channel_get_is_empty(ch) == 0);

    void *out = NULL;
    assert(rt_channel_try_recv(ch, &out) == 1);
    assert(out == a);

    assert(rt_channel_try_recv(ch, &out) == 1);
    assert(out == b);

    assert(rt_channel_try_recv(ch, &out) == 1);
    assert(out == c);

    assert(rt_channel_get_len(ch) == 0);
    assert(rt_channel_get_is_empty(ch) == 1);
    rt_channel_close(ch);
}

static void test_try_recv_empty() {
    void *ch = rt_channel_new(5);
    void *out = NULL;
    assert(rt_channel_try_recv(ch, &out) == 0);
    assert(out == NULL);
    rt_channel_close(ch);
}

static void test_try_send_full() {
    void *ch = rt_channel_new(2);
    void *a = make_obj();
    void *b = make_obj();
    void *c = make_obj();

    assert(rt_channel_try_send(ch, a) == 1);
    assert(rt_channel_try_send(ch, b) == 1);
    assert(rt_channel_get_is_full(ch) == 1);
    assert(rt_channel_try_send(ch, c) == 0); // Full

    rt_channel_close(ch);
}

static void test_fifo_order() {
    void *ch = rt_channel_new(10);
    void *items[5];
    int i;
    for (i = 0; i < 5; i++) {
        items[i] = make_obj();
        rt_channel_try_send(ch, items[i]);
    }

    for (i = 0; i < 5; i++) {
        void *out = NULL;
        assert(rt_channel_try_recv(ch, &out) == 1);
        assert(out == items[i]);
    }
    rt_channel_close(ch);
}

//=============================================================================
// Close semantics
//=============================================================================

static void test_close_prevents_send() {
    void *ch = rt_channel_new(5);
    rt_channel_close(ch);

    assert(rt_channel_get_is_closed(ch) == 1);
    assert(rt_channel_try_send(ch, make_obj()) == 0);
}

static void test_close_allows_drain() {
    void *ch = rt_channel_new(5);
    void *a = make_obj();
    rt_channel_try_send(ch, a);
    rt_channel_close(ch);

    // Can still recv remaining items
    void *out = NULL;
    assert(rt_channel_try_recv(ch, &out) == 1);
    assert(out == a);

    // Empty now
    assert(rt_channel_try_recv(ch, &out) == 0);
}

static void test_double_close() {
    void *ch = rt_channel_new(5);
    rt_channel_close(ch);
    rt_channel_close(ch); // Should not crash
    assert(rt_channel_get_is_closed(ch) == 1);
}

//=============================================================================
// Timed operations
//=============================================================================

static void test_recv_for_timeout() {
    void *ch = rt_channel_new(5);
    void *out = NULL;
    // Should timeout quickly on empty channel
    assert(rt_channel_recv_for(ch, &out, 10) == 0);
    assert(out == NULL);
    rt_channel_close(ch);
}

static void test_recv_for_immediate() {
    void *ch = rt_channel_new(5);
    void *a = make_obj();
    rt_channel_try_send(ch, a);

    void *out = NULL;
    assert(rt_channel_recv_for(ch, &out, 100) == 1);
    assert(out == a);
    rt_channel_close(ch);
}

static void test_send_for_timeout() {
    void *ch = rt_channel_new(1);
    void *a = make_obj();
    void *b = make_obj();

    assert(rt_channel_send_for(ch, a, 100) == 1); // Space available
    assert(rt_channel_send_for(ch, b, 10) == 0);  // Full, should timeout
    rt_channel_close(ch);
}

static void test_send_for_zero_ms() {
    void *ch = rt_channel_new(1);
    void *a = make_obj();

    // ms <= 0 degrades to try_send
    assert(rt_channel_send_for(ch, a, 0) == 1);
    assert(rt_channel_send_for(ch, make_obj(), 0) == 0); // Full
    rt_channel_close(ch);
}

//=============================================================================
// Null safety
//=============================================================================

static void test_null_safety() {
    assert(rt_channel_get_len(NULL) == 0);
    assert(rt_channel_get_cap(NULL) == 0);
    assert(rt_channel_get_is_closed(NULL) == 1);
    assert(rt_channel_get_is_empty(NULL) == 1);
    assert(rt_channel_get_is_full(NULL) == 0);
    assert(rt_channel_try_send(NULL, make_obj()) == 0);

    void *out = NULL;
    assert(rt_channel_try_recv(NULL, &out) == 0);
    assert(rt_channel_recv_for(NULL, &out, 10) == 0);
    assert(rt_channel_send_for(NULL, make_obj(), 10) == 0);

    rt_channel_close(NULL); // Should not crash
}

//=============================================================================
// Multi-threaded tests
//=============================================================================

static void test_producer_consumer() {
    void *ch = rt_channel_new(10);
    const int N = 50;
    void *items[50];
    int i;
    for (i = 0; i < N; i++)
        items[i] = make_obj();

    // Producer thread
    std::thread producer([ch, &items, N]() {
        for (int j = 0; j < N; j++)
            rt_channel_send(ch, items[j]);
    });

    // Consumer on main thread
    std::vector<void *> received;
    for (i = 0; i < N; i++) {
        void *out = NULL;
        int8_t ok = rt_channel_recv_for(ch, &out, 5000);
        assert(ok == 1);
        received.push_back(out);
    }

    producer.join();
    assert((int)received.size() == N);
    // Verify FIFO order
    for (i = 0; i < N; i++)
        assert(received[i] == items[i]);

    rt_channel_close(ch);
}

static void test_close_wakes_receiver() {
    void *ch = rt_channel_new(5);

    std::thread closer([ch]() {
        rt_thread_sleep(50);
        rt_channel_close(ch);
    });

    // Blocking recv should return NULL when channel is closed
    void *result = rt_channel_recv(ch);
    assert(result == NULL);

    closer.join();
}

static void test_synchronous_channel() {
    void *ch = rt_channel_new(0); // Synchronous
    void *item = make_obj();

    // Need sender and receiver on separate threads for synchronous channel
    void *received = NULL;

    std::thread receiver([ch, &received]() {
        received = rt_channel_recv(ch);
    });

    // Give receiver time to start waiting
    rt_thread_sleep(20);
    rt_channel_send(ch, item);

    receiver.join();
    assert(received == item);
    rt_channel_close(ch);
}

int main() {
    test_new_buffered();
    test_new_synchronous();
    test_new_negative_capacity();
    test_try_send_recv();
    test_try_recv_empty();
    test_try_send_full();
    test_fifo_order();
    test_close_prevents_send();
    test_close_allows_drain();
    test_double_close();
    test_recv_for_timeout();
    test_recv_for_immediate();
    test_send_for_timeout();
    test_send_for_zero_ms();
    test_null_safety();
    test_producer_consumer();
    test_close_wakes_receiver();
    test_synchronous_channel();

    printf("Channel tests: all passed\n");
    return 0;
}
