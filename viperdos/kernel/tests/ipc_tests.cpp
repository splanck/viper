/**
 * @file ipc_tests.cpp
 * @brief Ping-pong IPC test tasks.
 *
 * @details
 * This file contains test tasks that demonstrate bidirectional channel-based
 * IPC between kernel tasks. The ping task sends PING messages and waits for
 * PONG responses. The pong task receives PING messages and sends PONG responses.
 */

#include "../console/serial.hpp"
#include "../include/syscall.hpp"
#include "../ipc/channel.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"
#include "tests.hpp"

namespace tests {

/// Channel IDs for ping-pong communication
struct PingPongChannels {
    u32 ping_to_pong; // Channel for PING messages
    u32 pong_to_ping; // Channel for PONG messages
};

// Static storage for channel IDs (accessed by task lambdas)
static PingPongChannels g_channels;

// Ping task function - sends "PING", receives "PONG"
static void ping_task_fn(void *arg) {
    auto *ch = static_cast<PingPongChannels *>(arg);
    char buffer[32];

    serial::puts("[ping] Starting ping task\n");

    for (int i = 0; i < 3; i++) {
        // Send PING on channel 1
        const char *ping_msg = "PING";
        serial::puts("[ping] Sending PING #");
        serial::put_dec(i);
        serial::puts("\n");

        i64 send_result = sys::channel_send(ch->ping_to_pong, ping_msg, 5);
        if (send_result < 0) {
            serial::puts("[ping] Send failed!\n");
            break;
        }

        // Wait for PONG on channel 2
        i64 recv_result = sys::channel_recv(ch->pong_to_ping, buffer, sizeof(buffer));
        if (recv_result < 0) {
            serial::puts("[ping] Recv failed!\n");
            break;
        }

        serial::puts("[ping] Received: ");
        serial::puts(buffer);
        serial::puts("\n");

        // Sleep 500ms between iterations to test timer
        serial::puts("[ping] Sleeping 500ms...\n");
        sys::sleep(500);
        serial::puts("[ping] Woke up!\n");
    }

    serial::puts("[ping] Ping task done!\n");
    sys::exit(0);
}

// Pong task function - receives "PING", sends "PONG"
static void pong_task_fn(void *arg) {
    auto *ch = static_cast<PingPongChannels *>(arg);
    char buffer[32];

    serial::puts("[pong] Starting pong task\n");

    for (int i = 0; i < 3; i++) {
        // Wait for PING on channel 1
        i64 recv_result = sys::channel_recv(ch->ping_to_pong, buffer, sizeof(buffer));
        if (recv_result < 0) {
            serial::puts("[pong] Recv failed!\n");
            break;
        }

        serial::puts("[pong] Received: ");
        serial::puts(buffer);
        serial::puts("\n");

        // Send PONG on channel 2
        const char *pong_msg = "PONG";
        serial::puts("[pong] Sending PONG #");
        serial::put_dec(i);
        serial::puts("\n");

        i64 send_result = sys::channel_send(ch->pong_to_ping, pong_msg, 5);
        if (send_result < 0) {
            serial::puts("[pong] Send failed!\n");
            break;
        }
    }

    serial::puts("[pong] Pong task done!\n");
    sys::exit(0);
}

void create_ipc_test_tasks() {
    // Create two channels for bidirectional ping-pong IPC
    // Channel 1: ping -> pong (PING messages)
    // Channel 2: pong -> ping (PONG messages)
    i64 ch1_result = channel::create();
    i64 ch2_result = channel::create();
    if (ch1_result < 0 || ch2_result < 0) {
        serial::puts("[tests] Failed to create channels!\n");
        return;
    }

    // Store channel IDs
    g_channels.ping_to_pong = static_cast<u32>(ch1_result);
    g_channels.pong_to_ping = static_cast<u32>(ch2_result);

    serial::puts("[tests] Created channels: ping->pong=");
    serial::put_dec(g_channels.ping_to_pong);
    serial::puts(", pong->ping=");
    serial::put_dec(g_channels.pong_to_ping);
    serial::puts("\n");

    // Create test tasks
    serial::puts("[tests] Creating ping-pong test tasks...\n");
    task::Task *t1 = task::create("ping", ping_task_fn, &g_channels);
    task::Task *t2 = task::create("pong", pong_task_fn, &g_channels);

    if (t1) {
        task::print_info(t1);
        scheduler::enqueue(t1);
    }
    if (t2) {
        task::print_info(t2);
        scheduler::enqueue(t2);
    }
}

} // namespace tests
