//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_msgbus.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

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
    void *bus = rt_msgbus_new();
    assert(bus != NULL);
    assert(rt_msgbus_total_subscriptions(bus) == 0);
}

static void test_subscribe()
{
    void *bus = rt_msgbus_new();
    rt_string topic = make_str("click");
    // Use a dummy callback pointer (not invoked in tests)
    void *cb = make_str("callback_placeholder");

    int64_t id = rt_msgbus_subscribe(bus, topic, cb);
    assert(id > 0);
    assert(rt_msgbus_total_subscriptions(bus) == 1);
    assert(rt_msgbus_subscriber_count(bus, topic) == 1);
}

static void test_multiple_subscribers()
{
    void *bus = rt_msgbus_new();
    rt_string topic = make_str("event");

    rt_msgbus_subscribe(bus, topic, make_str("cb1"));
    rt_msgbus_subscribe(bus, topic, make_str("cb2"));
    rt_msgbus_subscribe(bus, topic, make_str("cb3"));

    assert(rt_msgbus_subscriber_count(bus, topic) == 3);
    assert(rt_msgbus_total_subscriptions(bus) == 3);
}

static void test_multiple_topics()
{
    void *bus = rt_msgbus_new();
    rt_string t1 = make_str("topic1");
    rt_string t2 = make_str("topic2");

    rt_msgbus_subscribe(bus, t1, make_str("cb1"));
    rt_msgbus_subscribe(bus, t2, make_str("cb2"));

    assert(rt_msgbus_subscriber_count(bus, t1) == 1);
    assert(rt_msgbus_subscriber_count(bus, t2) == 1);
    assert(rt_msgbus_total_subscriptions(bus) == 2);
}

static void test_unsubscribe()
{
    void *bus = rt_msgbus_new();
    rt_string topic = make_str("test");

    int64_t id = rt_msgbus_subscribe(bus, topic, make_str("cb"));
    assert(rt_msgbus_unsubscribe(bus, id) == 1);
    assert(rt_msgbus_subscriber_count(bus, topic) == 0);
    assert(rt_msgbus_total_subscriptions(bus) == 0);

    // Double unsubscribe returns 0
    assert(rt_msgbus_unsubscribe(bus, id) == 0);
}

static void test_publish()
{
    void *bus = rt_msgbus_new();
    rt_string topic = make_str("signal");

    rt_msgbus_subscribe(bus, topic, make_str("cb1"));
    rt_msgbus_subscribe(bus, topic, make_str("cb2"));

    rt_string t = make_str("signal");
    int64_t notified = rt_msgbus_publish(bus, t, make_str("data"));
    assert(notified == 2);

    rt_string missing = make_str("no_such_topic");
    assert(rt_msgbus_publish(bus, missing, NULL) == 0);

    rt_string_unref(t);
    rt_string_unref(missing);
}

static void test_topics()
{
    void *bus = rt_msgbus_new();
    rt_msgbus_subscribe(bus, make_str("alpha"), make_str("cb"));
    rt_msgbus_subscribe(bus, make_str("beta"), make_str("cb"));

    void *topics = rt_msgbus_topics(bus);
    assert(rt_seq_len(topics) == 2);
}

static void test_clear_topic()
{
    void *bus = rt_msgbus_new();
    rt_string t = make_str("temp");
    rt_msgbus_subscribe(bus, t, make_str("cb1"));
    rt_msgbus_subscribe(bus, t, make_str("cb2"));

    rt_string t2 = make_str("temp");
    rt_msgbus_clear_topic(bus, t2);
    assert(rt_msgbus_subscriber_count(bus, t) == 0);
    assert(rt_msgbus_total_subscriptions(bus) == 0);

    rt_string_unref(t2);
}

static void test_clear()
{
    void *bus = rt_msgbus_new();
    rt_msgbus_subscribe(bus, make_str("a"), make_str("cb"));
    rt_msgbus_subscribe(bus, make_str("b"), make_str("cb"));
    rt_msgbus_subscribe(bus, make_str("c"), make_str("cb"));

    rt_msgbus_clear(bus);
    assert(rt_msgbus_total_subscriptions(bus) == 0);
}

static void test_null_safety()
{
    assert(rt_msgbus_total_subscriptions(NULL) == 0);
    assert(rt_msgbus_subscriber_count(NULL, NULL) == 0);
    assert(rt_msgbus_publish(NULL, NULL, NULL) == 0);
    assert(rt_msgbus_subscribe(NULL, NULL, NULL) == -1);
    assert(rt_msgbus_unsubscribe(NULL, 1) == 0);
}

int main()
{
    test_new();
    test_subscribe();
    test_multiple_subscribers();
    test_multiple_topics();
    test_unsubscribe();
    test_publish();
    test_topics();
    test_clear_topic();
    test_clear();
    test_null_safety();
    return 0;
}
