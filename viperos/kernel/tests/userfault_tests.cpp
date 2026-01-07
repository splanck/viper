/**
 * @file userfault_tests.cpp
 * @brief User fault recovery tests.
 *
 * @details
 * Tests that the kernel properly handles user-mode faults:
 * 1. Null pointer dereference (translation fault)
 * 2. Illegal instruction (undefined instruction)
 *
 * For each test:
 * - Spawn a user program that intentionally faults
 * - Wait for it to terminate
 * - Verify the kernel is still alive
 * - Check that the process exited with code -1 (fault exit code)
 */

#include "../console/serial.hpp"
#include "../ipc/poll.hpp"
#include "../loader/loader.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"
#include "../viper/viper.hpp"
#include "tests.hpp"

namespace tests
{

// Test state tracking
static int userfault_tests_passed = 0;
static int userfault_tests_failed = 0;

static void test_pass(const char *name)
{
    serial::puts("  [PASS] ");
    serial::puts(name);
    serial::puts("\n");
    userfault_tests_passed++;
}

static void test_fail(const char *name, const char *reason)
{
    serial::puts("  [FAIL] ");
    serial::puts(name);
    serial::puts(" - ");
    serial::puts(reason);
    serial::puts("\n");
    userfault_tests_failed++;
}

/**
 * @brief Run a single user fault test.
 *
 * @param path Path to the test program on disk.
 * @param name Human-readable test name.
 * @return true if test passed, false otherwise.
 */
static bool run_fault_test(const char *path, const char *name)
{
    serial::puts("\n[userfault_test] Running ");
    serial::puts(name);
    serial::puts("...\n");

    // Spawn the fault test program (no parent - we'll poll its state directly)
    loader::SpawnResult result = loader::spawn_process(path, name, nullptr);
    if (!result.success)
    {
        test_fail(name, "failed to spawn process");
        return false;
    }

    serial::puts("[userfault_test] Spawned process, pid=");
    serial::put_dec(result.viper ? result.viper->id : 0);
    serial::puts(", task=");
    serial::put_dec(static_cast<u32>(result.task_id));
    serial::puts("\n");

    // Poll for the process to become a zombie (fault terminates it)
    // Give it up to 5 seconds to complete
    i32 status = 0;
    bool found_zombie = false;
    for (int i = 0; i < 50; i++)
    {
        poll::sleep_ms(100);

        if (result.viper->state == viper::ViperState::Zombie)
        {
            status = result.viper->exit_code;
            found_zombie = true;
            break;
        }
    }

    if (!found_zombie)
    {
        test_fail(name, "process did not terminate within timeout");
        return false;
    }

    serial::puts("[userfault_test] Process terminated with exit code ");
    serial::put_dec(status);
    serial::puts("\n");

    // Clean up the zombie
    viper::reap(result.viper);

    // Fault exits should use exit code -1
    if (status == -1)
    {
        test_pass(name);
        return true;
    }
    else
    {
        test_fail(name, "unexpected exit code (expected -1)");
        return false;
    }
}

void run_userfault_tests()
{
    serial::puts("\n========================================\n");
    serial::puts("  User Fault Recovery Tests\n");
    serial::puts("========================================\n");

    userfault_tests_passed = 0;
    userfault_tests_failed = 0;

    // Test 1: Null pointer dereference
    run_fault_test("/faulttest_null.prg", "null_deref");

    // Verify kernel is still alive
    serial::puts("[userfault_test] Kernel still alive after null deref test\n");

    // Test 2: Illegal instruction
    run_fault_test("/faulttest_illegal.prg", "illegal_insn");

    // Verify kernel is still alive
    serial::puts("[userfault_test] Kernel still alive after illegal insn test\n");

    // Summary
    serial::puts("\n========================================\n");
    serial::puts("  User Fault Tests Complete\n");
    serial::puts("  Passed: ");
    serial::put_dec(userfault_tests_passed);
    serial::puts("\n  Failed: ");
    serial::put_dec(userfault_tests_failed);
    serial::puts("\n========================================\n");

    if (userfault_tests_failed == 0)
    {
        serial::puts("[RESULT] ALL USERFAULT TESTS PASSED\n");
    }
    else
    {
        serial::puts("[RESULT] USERFAULT TESTS FAILED\n");
    }
}

/**
 * @brief Kernel task entry point for user fault tests.
 */
static void userfault_test_task_entry(void *)
{
    // Small delay to let other init tasks complete
    poll::sleep_ms(100);

    run_userfault_tests();

    // Exit the test task
    task::exit(0);
}

void create_userfault_test_task()
{
    serial::puts("[kernel] Creating user fault test task...\n");

    task::Task *test_task = task::create("userfault_test", userfault_test_task_entry, nullptr);
    if (test_task)
    {
        // Set lower priority so other init tasks run first
        test_task->priority = 2;
        scheduler::enqueue(test_task);
        serial::puts("[kernel] User fault test task created (tid=");
        serial::put_dec(test_task->id);
        serial::puts(")\n");
    }
    else
    {
        serial::puts("[kernel] Failed to create user fault test task\n");
    }
}

} // namespace tests
