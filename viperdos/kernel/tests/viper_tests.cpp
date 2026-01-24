/**
 * @file viper_tests.cpp
 * @brief Tests for Viper subsystem, capability tables, and kernel objects.
 *
 * @details
 * This file contains tests that verify:
 * - Capability table operations
 * - Kernel object creation (channels, blobs)
 * - IPC channel operations
 * - Poll and timer functionality
 */

#include "../cap/handle.hpp"
#include "../cap/table.hpp"
#include "../console/serial.hpp"
#include "../ipc/channel.hpp"
#include "../ipc/poll.hpp"
#include "../kobj/blob.hpp"
#include "../kobj/channel.hpp"
#include "../viper/viper.hpp"
#include "tests.hpp"

namespace tests {

// Test result tracking
static int viper_tests_passed = 0;
static int viper_tests_failed = 0;

static void viper_test_pass(const char *name) {
    serial::puts("[TEST] ");
    serial::puts(name);
    serial::puts(" PASSED\n");
    viper_tests_passed++;
}

static void viper_test_fail(const char *name, const char *reason) {
    serial::puts("[TEST] ");
    serial::puts(name);
    serial::puts(" FAILED: ");
    serial::puts(reason);
    serial::puts("\n");
    viper_tests_failed++;
}

// ============================================================================
// Channel Tests
// ============================================================================

static void test_channel_create() {
    const char *name = "channel_create";

    i64 ch_id = channel::create();
    if (ch_id >= 0) {
        viper_test_pass(name);
    } else {
        viper_test_fail(name, "channel::create returned error");
    }
}

static void test_channel_send_recv() {
    const char *name = "channel_send_recv";

    i64 ch_id = channel::create();
    if (ch_id < 0) {
        viper_test_fail(name, "failed to create channel");
        return;
    }

    // Send a message
    const char *msg = "TEST";
    i64 send_result = channel::send(static_cast<u32>(ch_id), msg, 5);
    if (send_result < 0) {
        viper_test_fail(name, "send failed");
        return;
    }

    // Receive the message
    char buf[32] = {0};
    i64 recv_result = channel::recv(static_cast<u32>(ch_id), buf, sizeof(buf));
    if (recv_result < 0) {
        viper_test_fail(name, "recv failed");
        return;
    }

    // Verify content
    if (buf[0] == 'T' && buf[1] == 'E' && buf[2] == 'S' && buf[3] == 'T') {
        viper_test_pass(name);
    } else {
        viper_test_fail(name, "message content mismatch");
    }
}

static void test_channel_multiple_messages() {
    const char *name = "channel_multiple_messages";

    i64 ch_id = channel::create();
    if (ch_id < 0) {
        viper_test_fail(name, "failed to create channel");
        return;
    }

    // Send multiple messages
    for (int i = 0; i < 5; i++) {
        char msg[8];
        msg[0] = 'M';
        msg[1] = static_cast<char>('0' + i);
        msg[2] = '\0';
        i64 r = channel::send(static_cast<u32>(ch_id), msg, 3);
        if (r < 0) {
            viper_test_fail(name, "send failed");
            return;
        }
    }

    // Receive and verify order
    bool ok = true;
    for (int i = 0; i < 5; i++) {
        char buf[8];
        i64 r = channel::recv(static_cast<u32>(ch_id), buf, sizeof(buf));
        if (r < 0 || buf[1] != static_cast<char>('0' + i)) {
            ok = false;
            break;
        }
    }

    if (ok) {
        viper_test_pass(name);
    } else {
        viper_test_fail(name, "message order or content error");
    }
}

// ============================================================================
// Poll Tests
// ============================================================================

static void test_poll_channel_readable() {
    const char *name = "poll_channel_readable";

    i64 ch_id = channel::create();
    if (ch_id < 0) {
        viper_test_fail(name, "failed to create channel");
        return;
    }

    // Initially, channel should be writable but not readable
    poll::PollEvent ev;
    ev.handle = static_cast<u32>(ch_id);
    ev.events = poll::EventType::CHANNEL_READ | poll::EventType::CHANNEL_WRITE;
    ev.triggered = poll::EventType::NONE;

    poll::poll(&ev, 1, 0); // Non-blocking

    // Should have CHANNEL_WRITE triggered (channel has space)
    if ((ev.triggered & poll::EventType::CHANNEL_WRITE) == poll::EventType::NONE) {
        viper_test_fail(name, "empty channel not writable");
        return;
    }

    // Send a message
    channel::send(static_cast<u32>(ch_id), "X", 2);

    // Now poll again - should be readable
    ev.triggered = poll::EventType::NONE;
    poll::poll(&ev, 1, 0);

    if ((ev.triggered & poll::EventType::CHANNEL_READ) != poll::EventType::NONE) {
        viper_test_pass(name);
    } else {
        viper_test_fail(name, "channel with data not readable");
    }
}

static void test_poll_timeout() {
    const char *name = "poll_timeout";

    i64 ch_id = channel::create();
    if (ch_id < 0) {
        viper_test_fail(name, "failed to create channel");
        return;
    }

    // Poll on empty channel with short timeout
    poll::PollEvent ev;
    ev.handle = static_cast<u32>(ch_id);
    ev.events = poll::EventType::CHANNEL_READ; // Only readable (will timeout)
    ev.triggered = poll::EventType::NONE;

    u64 before = poll::time_now_ms();
    poll::poll(&ev, 1, 50); // 50ms timeout
    u64 after = poll::time_now_ms();

    // Should have waited roughly 50ms
    u64 elapsed = after - before;
    if (elapsed >= 40 && elapsed <= 200) { // Allow some tolerance
        viper_test_pass(name);
    } else {
        viper_test_fail(name, "timeout duration incorrect");
    }
}

static void test_timer_create_expired() {
    const char *name = "timer_create_expired";

    // Create a timer for 10ms
    i64 timer_id = poll::timer_create(10);
    if (timer_id < 0) {
        viper_test_fail(name, "timer_create failed");
        return;
    }

    // Initially should not be expired
    if (poll::timer_expired(static_cast<u32>(timer_id))) {
        viper_test_fail(name, "timer expired immediately");
        return;
    }

    // Sleep for 50ms
    poll::sleep_ms(50);

    // Now should be expired
    if (poll::timer_expired(static_cast<u32>(timer_id))) {
        viper_test_pass(name);
    } else {
        viper_test_fail(name, "timer not expired after delay");
    }
}

static void test_time_monotonic() {
    const char *name = "time_monotonic";

    u64 t1 = poll::time_now_ms();
    poll::sleep_ms(10);
    u64 t2 = poll::time_now_ms();

    if (t2 > t1) {
        viper_test_pass(name);
    } else {
        viper_test_fail(name, "time not monotonic");
    }
}

// ============================================================================
// Capability Table Tests
// ============================================================================

static void test_cap_table_basic() {
    const char *name = "cap_table_basic";

    // Create a capability table directly for testing
    cap::Table table;
    if (!table.init(64)) {
        viper_test_fail(name, "failed to init cap table");
        return;
    }

    // Insert a test capability
    int dummy_object = 42;
    cap::Handle h = table.insert(&dummy_object, cap::Kind::Blob, cap::CAP_RW);
    if (h == cap::HANDLE_INVALID) {
        viper_test_fail(name, "insert failed");
        return;
    }

    // Look it up
    cap::Entry *e = table.get(h);
    if (!e || e->object != &dummy_object) {
        viper_test_fail(name, "get failed");
        return;
    }

    // Remove it
    table.remove(h);
    cap::Entry *e2 = table.get(h);
    if (e2 != nullptr) {
        viper_test_fail(name, "remove failed");
        return;
    }

    table.destroy();
    viper_test_pass(name);
}

// ============================================================================
// Main Test Runner
// ============================================================================

void run_viper_tests() {
    serial::puts("\n");
    serial::puts("========================================\n");
    serial::puts("  ViperDOS Viper Subsystem Tests\n");
    serial::puts("========================================\n\n");

    viper_tests_passed = 0;
    viper_tests_failed = 0;

    // Channel tests
    serial::puts("[SUITE] Channel Tests\n");
    test_channel_create();
    test_channel_send_recv();
    test_channel_multiple_messages();

    // Poll tests
    serial::puts("\n[SUITE] Poll/Timer Tests\n");
    test_poll_channel_readable();
    test_poll_timeout();
    test_timer_create_expired();
    test_time_monotonic();

    // Capability tests
    serial::puts("\n[SUITE] Capability Table Tests\n");
    test_cap_table_basic();

    // Summary
    serial::puts("\n========================================\n");
    serial::puts("  Viper Tests Complete\n");
    serial::puts("  Passed: ");
    serial::put_dec(viper_tests_passed);
    serial::puts("\n  Failed: ");
    serial::put_dec(viper_tests_failed);
    serial::puts("\n========================================\n");

    if (viper_tests_failed == 0) {
        serial::puts("[RESULT] ALL VIPER TESTS PASSED\n");
    } else {
        serial::puts("[RESULT] SOME VIPER TESTS FAILED\n");
    }
}

} // namespace tests
