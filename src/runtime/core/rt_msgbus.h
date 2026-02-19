//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_msgbus.h
// Purpose: Pub/sub message bus for decoupled event communication.
// Key invariants: String topics. Subscribers identified by integer IDs.
//                 Publish delivers to all subscribers on that topic.
// Ownership/Lifetime: Bus retains subscriber function pointers.
// Links: docs/viperlib.md
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
