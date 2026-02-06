//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_concqueue.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <pthread.h>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static void test_new()
{
    void *q = rt_concqueue_new();
    assert(q != NULL);
    assert(rt_concqueue_len(q) == 0);
    assert(rt_concqueue_is_empty(q) == 1);
}

static void test_enqueue_dequeue()
{
    void *q = rt_concqueue_new();
    rt_string v1 = make_str("first");
    rt_string v2 = make_str("second");

    rt_concqueue_enqueue(q, v1);
    rt_concqueue_enqueue(q, v2);

    assert(rt_concqueue_len(q) == 2);
    assert(rt_concqueue_try_dequeue(q) == v1);
    assert(rt_concqueue_try_dequeue(q) == v2);
    assert(rt_concqueue_len(q) == 0);
}

static void test_try_dequeue_empty()
{
    void *q = rt_concqueue_new();
    assert(rt_concqueue_try_dequeue(q) == NULL);
}

static void test_peek()
{
    void *q = rt_concqueue_new();
    rt_string v = make_str("peeked");
    rt_concqueue_enqueue(q, v);

    assert(rt_concqueue_peek(q) == v);
    assert(rt_concqueue_len(q) == 1); // Still there
}

static void test_clear()
{
    void *q = rt_concqueue_new();
    rt_concqueue_enqueue(q, make_str("a"));
    rt_concqueue_enqueue(q, make_str("b"));
    rt_concqueue_enqueue(q, make_str("c"));

    rt_concqueue_clear(q);
    assert(rt_concqueue_len(q) == 0);
    assert(rt_concqueue_try_dequeue(q) == NULL);
}

static void test_timeout_empty()
{
    void *q = rt_concqueue_new();
    // Should return NULL after ~10ms timeout
    void *result = rt_concqueue_dequeue_timeout(q, 10);
    assert(result == NULL);
}

// Producer thread function
struct producer_args
{
    void *queue;
    int count;
};

static void *producer_fn(void *arg)
{
    struct producer_args *pa = (struct producer_args *)arg;
    for (int i = 0; i < pa->count; i++)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "item_%d", i);
        rt_concqueue_enqueue(pa->queue, make_str(buf));
    }
    return NULL;
}

static void test_concurrent_produce_consume()
{
    void *q = rt_concqueue_new();
    const int N = 100;

    struct producer_args args = {q, N};
    pthread_t producer;
    pthread_create(&producer, NULL, producer_fn, &args);

    // Consumer: dequeue all N items
    int received = 0;
    while (received < N)
    {
        void *item = rt_concqueue_dequeue_timeout(q, 500);
        if (item)
            received++;
    }
    pthread_join(producer, NULL);

    assert(received == N);
    assert(rt_concqueue_len(q) == 0);
}

static void test_null_safety()
{
    assert(rt_concqueue_len(NULL) == 0);
    assert(rt_concqueue_is_empty(NULL) == 1);
    assert(rt_concqueue_try_dequeue(NULL) == NULL);
    assert(rt_concqueue_peek(NULL) == NULL);
    assert(rt_concqueue_dequeue_timeout(NULL, 10) == NULL);
}

int main()
{
    test_new();
    test_enqueue_dequeue();
    test_try_dequeue_empty();
    test_peek();
    test_clear();
    test_timeout_empty();
    test_concurrent_produce_consume();
    test_null_safety();
    return 0;
}
