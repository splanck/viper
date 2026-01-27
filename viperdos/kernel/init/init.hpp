//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

/**
 * @file init.hpp
 * @brief Kernel subsystem initialization functions.
 *
 * @details
 * This header declares the initialization functions called during early boot
 * to bring up kernel subsystems in the correct order.
 */

namespace init {

/**
 * @brief Print the boot banner to serial console.
 */
void print_boot_banner();

/**
 * @brief Initialize framebuffer (UEFI GOP or ramfb fallback).
 * @return true if framebuffer was initialized.
 */
bool init_framebuffer();

/**
 * @brief Initialize memory management subsystems (PMM, VMM, heap, slab).
 */
void init_memory_subsystem();

/**
 * @brief Initialize exception handlers, GIC, timer, and enable interrupts.
 */
void init_interrupts();

/**
 * @brief Initialize task, scheduler, channel, and poll subsystems.
 */
void init_task_subsystem();

/**
 * @brief Initialize virtio subsystem and device drivers.
 */
void init_virtio_subsystem();

/**
 * @brief Initialize network stack and run connectivity tests.
 */
void init_network_subsystem();

/**
 * @brief Initialize filesystems and run storage tests.
 */
void init_filesystem_subsystem();

/**
 * @brief Initialize Viper subsystem and create/test vinit process.
 */
void init_viper_subsystem();

} // namespace init
