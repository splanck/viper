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

namespace tests
{

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

} // namespace tests
