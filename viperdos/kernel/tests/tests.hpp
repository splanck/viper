#pragma once

/**
 * @file tests.hpp
 * @brief Kernel subsystem test functions.
 *
 * @details
 * This header declares test functions for various kernel subsystems.
 * These tests run during early boot to validate basic functionality
 * before the system enters normal operation.
 */

namespace tests {

/**
 * @brief Run block device and filesystem tests.
 *
 * @details
 * Tests virtio-blk read/write, block cache, ViperFS mount and file operations,
 * VFS layer, and the Assign name resolution system.
 */
void run_storage_tests();

/**
 * @brief Run Viper subsystem tests.
 *
 * @details
 * Tests Viper creation, address space mapping, capability tables,
 * and kernel object (blob, channel) creation.
 */
void run_viper_tests();

/**
 * @brief Create ping-pong IPC test tasks.
 *
 * @details
 * Creates two tasks that demonstrate bidirectional channel-based IPC.
 * The ping task sends PING messages and waits for PONG responses.
 * The pong task receives PING messages and sends PONG responses.
 */
void create_ipc_test_tasks();

/**
 * @brief Run syscall dispatch tests.
 *
 * @details
 * Tests the table-driven syscall dispatch mechanism:
 * 1. Valid syscall (task_yield) returns success
 * 2. Invalid syscall number returns VERR_NOT_SUPPORTED
 * 3. Invalid pointer returns VERR_INVALID_ARG
 */
void run_syscall_tests();

/**
 * @brief Create user fault test task.
 *
 * @details
 * Creates a kernel task that tests user-mode fault recovery:
 * 1. Spawns faulttest_null.prg which dereferences NULL
 * 2. Spawns faulttest_illegal.prg which executes undefined instruction
 * 3. Verifies kernel continues running after each fault
 * 4. Checks that faulted processes exit with code -1
 *
 * The task runs under the scheduler since it uses viper::wait().
 */
void create_userfault_test_task();

} // namespace tests
