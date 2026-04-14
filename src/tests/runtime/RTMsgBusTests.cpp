//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_msgbus.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, std::strlen(s));
}

static void destroy_obj(void *obj) {
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static int g_trace_len = 0;
static int g_trace[8];
static int g_last_payload = 0;
static void *g_current_bus = NULL;
static int64_t g_victim_sub_id = 0;
static int g_callback_finalized = 0;

static void reset_trace() {
    g_trace_len = 0;
    std::memset(g_trace, 0, sizeof(g_trace));
    g_last_payload = 0;
}

static void cb_first(void *data) {
    g_trace[g_trace_len++] = 1;
    g_last_payload = *(int *)data;
}

static void cb_second(void *data) {
    g_trace[g_trace_len++] = 2;
    g_last_payload = *(int *)data;
}

static void cb_third(void *data) {
    g_trace[g_trace_len++] = 3;
    g_last_payload = *(int *)data;
}

static void cb_unsubscribe_other(void *data) {
    g_trace[g_trace_len++] = 10;
    g_last_payload = *(int *)data;
    if (g_current_bus && g_victim_sub_id > 0) {
        assert(rt_msgbus_unsubscribe(g_current_bus, g_victim_sub_id) == 1);
        g_victim_sub_id = 0;
    }
}

static void callback_finalizer(void *obj) {
    (void)obj;
    g_callback_finalized++;
}

static void test_new() {
    void *bus = rt_msgbus_new();
    assert(bus != NULL);
    assert(rt_msgbus_total_subscriptions(bus) == 0);
    destroy_obj(bus);
}

static void test_subscribe() {
    void *bus = rt_msgbus_new();
    rt_string topic = make_str("click");

    int64_t id = rt_msgbus_subscribe(bus, topic, (void *)cb_first);
    assert(id > 0);
    assert(rt_msgbus_total_subscriptions(bus) == 1);
    assert(rt_msgbus_subscriber_count(bus, topic) == 1);

    rt_string_unref(topic);
    destroy_obj(bus);
}

static void test_multiple_subscribers() {
    void *bus = rt_msgbus_new();
    rt_string topic = make_str("event");

    rt_msgbus_subscribe(bus, topic, (void *)cb_first);
    rt_msgbus_subscribe(bus, topic, (void *)cb_second);
    rt_msgbus_subscribe(bus, topic, (void *)cb_third);

    assert(rt_msgbus_subscriber_count(bus, topic) == 3);
    assert(rt_msgbus_total_subscriptions(bus) == 3);

    rt_string_unref(topic);
    destroy_obj(bus);
}

static void test_multiple_topics() {
    void *bus = rt_msgbus_new();
    rt_string t1 = make_str("topic1");
    rt_string t2 = make_str("topic2");

    rt_msgbus_subscribe(bus, t1, (void *)cb_first);
    rt_msgbus_subscribe(bus, t2, (void *)cb_second);

    assert(rt_msgbus_subscriber_count(bus, t1) == 1);
    assert(rt_msgbus_subscriber_count(bus, t2) == 1);
    assert(rt_msgbus_total_subscriptions(bus) == 2);

    rt_string_unref(t1);
    rt_string_unref(t2);
    destroy_obj(bus);
}

static void test_unsubscribe() {
    void *bus = rt_msgbus_new();
    rt_string topic = make_str("test");

    int64_t id = rt_msgbus_subscribe(bus, topic, (void *)cb_first);
    assert(rt_msgbus_unsubscribe(bus, id) == 1);
    assert(rt_msgbus_subscriber_count(bus, topic) == 0);
    assert(rt_msgbus_total_subscriptions(bus) == 0);

    assert(rt_msgbus_unsubscribe(bus, id) == 0);

    rt_string_unref(topic);
    destroy_obj(bus);
}

static void test_publish_invokes_callbacks_in_order() {
    void *bus = rt_msgbus_new();
    rt_string topic = make_str("signal");
    int payload = 42;

    rt_msgbus_subscribe(bus, topic, (void *)cb_first);
    rt_msgbus_subscribe(bus, topic, (void *)cb_second);
    rt_msgbus_subscribe(bus, topic, (void *)cb_third);

    reset_trace();
    int64_t notified = rt_msgbus_publish(bus, topic, &payload);
    assert(notified == 3);
    assert(g_trace_len == 3);
    assert(g_trace[0] == 1);
    assert(g_trace[1] == 2);
    assert(g_trace[2] == 3);
    assert(g_last_payload == 42);

    rt_string missing = make_str("no_such_topic");
    assert(rt_msgbus_publish(bus, missing, &payload) == 0);

    rt_string_unref(topic);
    rt_string_unref(missing);
    destroy_obj(bus);
}

static void test_unsubscribe_during_publish_uses_snapshot() {
    void *bus = rt_msgbus_new();
    rt_string topic = make_str("snapshot");
    int payload = 7;

    g_current_bus = bus;
    int64_t unsub_id = rt_msgbus_subscribe(bus, topic, (void *)cb_unsubscribe_other);
    g_victim_sub_id = rt_msgbus_subscribe(bus, topic, (void *)cb_second);

    reset_trace();
    int64_t notified = rt_msgbus_publish(bus, topic, &payload);
    assert(notified == 2);
    assert(g_trace_len == 2);
    assert(g_trace[0] == 10);
    assert(g_trace[1] == 2);
    assert(rt_msgbus_subscriber_count(bus, topic) == 1);

    reset_trace();
    notified = rt_msgbus_publish(bus, topic, &payload);
    assert(notified == 1);
    assert(g_trace_len == 1);
    assert(g_trace[0] == 10);

    assert(rt_msgbus_unsubscribe(bus, unsub_id) == 1);
    g_current_bus = NULL;
    g_victim_sub_id = 0;
    rt_string_unref(topic);
    destroy_obj(bus);
}

static void test_topics() {
    void *bus = rt_msgbus_new();
    rt_string alpha = make_str("alpha");
    rt_string beta = make_str("beta");
    rt_msgbus_subscribe(bus, alpha, (void *)cb_first);
    rt_msgbus_subscribe(bus, beta, (void *)cb_second);

    void *topics = rt_msgbus_topics(bus);
    assert(rt_seq_len(topics) == 2);

    rt_string_unref(alpha);
    rt_string_unref(beta);
    destroy_obj(topics);
    destroy_obj(bus);
}

static void test_clear_topic() {
    void *bus = rt_msgbus_new();
    rt_string t = make_str("temp");
    rt_msgbus_subscribe(bus, t, (void *)cb_first);
    rt_msgbus_subscribe(bus, t, (void *)cb_second);

    rt_msgbus_clear_topic(bus, t);
    assert(rt_msgbus_subscriber_count(bus, t) == 0);
    assert(rt_msgbus_total_subscriptions(bus) == 0);

    rt_string_unref(t);
    destroy_obj(bus);
}

static void test_clear() {
    void *bus = rt_msgbus_new();
    rt_string a = make_str("a");
    rt_string b = make_str("b");
    rt_string c = make_str("c");
    rt_msgbus_subscribe(bus, a, (void *)cb_first);
    rt_msgbus_subscribe(bus, b, (void *)cb_second);
    rt_msgbus_subscribe(bus, c, (void *)cb_third);

    rt_msgbus_clear(bus);
    assert(rt_msgbus_total_subscriptions(bus) == 0);

    rt_string_unref(a);
    rt_string_unref(b);
    rt_string_unref(c);
    destroy_obj(bus);
}

static void test_callback_object_cleanup_on_unsubscribe() {
    void *bus = rt_msgbus_new();
    rt_string topic = make_str("cleanup");
    void *callback_obj = rt_obj_new_i64(0, 8);
    rt_obj_set_finalizer(callback_obj, callback_finalizer);
    g_callback_finalized = 0;

    int64_t id = rt_msgbus_subscribe(bus, topic, callback_obj);
    destroy_obj(callback_obj); // bus now owns the remaining reference
    assert(g_callback_finalized == 0);

    assert(rt_msgbus_unsubscribe(bus, id) == 1);
    assert(g_callback_finalized == 1);

    rt_string_unref(topic);
    destroy_obj(bus);
}

static void test_null_safety() {
    void *bus = rt_msgbus_new();
    rt_string x = make_str("x");
    assert(rt_msgbus_total_subscriptions(NULL) == 0);
    assert(rt_msgbus_subscriber_count(NULL, NULL) == 0);
    assert(rt_msgbus_publish(NULL, NULL, NULL) == 0);
    assert(rt_msgbus_subscribe(NULL, NULL, NULL) == -1);
    assert(rt_msgbus_subscribe(bus, NULL, (void *)cb_first) == -1);
    assert(rt_msgbus_subscribe(bus, x, NULL) == -1);
    assert(rt_msgbus_unsubscribe(NULL, 1) == 0);
    rt_string_unref(x);
    destroy_obj(bus);
}

int main() {
    test_new();
    test_subscribe();
    test_multiple_subscribers();
    test_multiple_topics();
    test_unsubscribe();
    test_publish_invokes_callbacks_in_order();
    test_unsubscribe_during_publish_uses_snapshot();
    test_topics();
    test_clear_topic();
    test_clear();
    test_callback_object_cleanup_on_unsubscribe();
    test_null_safety();
    return 0;
}
