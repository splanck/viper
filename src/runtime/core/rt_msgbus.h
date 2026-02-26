//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_msgbus.h
// Purpose: Pub/sub message bus for decoupled event communication, providing topic-based
// publish/subscribe with integer subscription IDs for selective unsubscription.
//
// Key invariants:
//   - Topics are string keys; each topic can have multiple subscribers.
//   - Subscribers are identified by unique integer IDs returned from rt_msgbus_subscribe.
//   - rt_msgbus_publish delivers to all current subscribers synchronously before returning.
//   - Unsubscribing from within a callback is safe (deferred removal).
//
// Ownership/Lifetime:
//   - MessageBus objects are heap-allocated; caller owns and must free when done.
//   - The bus retains subscriber function pointers; callbacks must remain valid while subscribed.
//   - Published data pointers are not retained; callers manage their own lifetime.
//
// Links: src/runtime/core/rt_msgbus.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new message bus.
    /// @return Pointer to message bus object.
    void *rt_msgbus_new(void);

    /// @brief Subscribe to a topic. Returns subscription ID for later unsubscribe.
    /// @param obj MessageBus pointer.
    /// @param topic Topic string.
    /// @param callback Function pointer (receives topic and data).
    /// @return Unique subscription ID.
    int64_t rt_msgbus_subscribe(void *obj, rt_string topic, void *callback);

    /// @brief Unsubscribe by subscription ID.
    /// @param obj MessageBus pointer.
    /// @param sub_id Subscription ID returned by subscribe.
    /// @return 1 if found and removed, 0 otherwise.
    int8_t rt_msgbus_unsubscribe(void *obj, int64_t sub_id);

    /// @brief Publish a message to a topic.
    /// @param obj MessageBus pointer.
    /// @param topic Topic string.
    /// @param data Message data (any object).
    /// @return Number of subscribers notified.
    int64_t rt_msgbus_publish(void *obj, rt_string topic, void *data);

    /// @brief Get number of subscribers for a topic.
    /// @param obj MessageBus pointer.
    /// @param topic Topic string.
    /// @return Subscriber count for topic.
    int64_t rt_msgbus_subscriber_count(void *obj, rt_string topic);

    /// @brief Get total number of subscriptions across all topics.
    /// @param obj MessageBus pointer.
    /// @return Total subscription count.
    int64_t rt_msgbus_total_subscriptions(void *obj);

    /// @brief Get all active topic names.
    /// @param obj MessageBus pointer.
    /// @return Seq of topic strings.
    void *rt_msgbus_topics(void *obj);

    /// @brief Remove all subscriptions for a topic.
    /// @param obj MessageBus pointer.
    /// @param topic Topic string.
    void rt_msgbus_clear_topic(void *obj, rt_string topic);

    /// @brief Remove all subscriptions.
    /// @param obj MessageBus pointer.
    void rt_msgbus_clear(void *obj);

#ifdef __cplusplus
}
#endif
