//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/drivers/fwcfg.hpp
// Purpose: QEMU fw_cfg interface for firmware-provided configuration data.
// Key invariants: MMIO interface matches QEMU virt machine spec.
// Ownership/Lifetime: Global interface; initialized once.
// Links: kernel/drivers/fwcfg.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"

/**
 * @file fwcfg.hpp
 * @brief QEMU `fw_cfg` interface for firmware-provided configuration data.
 *
 * @details
 * QEMU exposes a firmware configuration device (`fw_cfg`) that allows the guest
 * to read configuration items and small "files" provided by the virtual
 * machine. On the AArch64 `virt` machine this is accessed via an MMIO
 * interface.
 *
 * This module provides:
 * - Initialization/feature probing (`init`).
 * - File directory lookup (`find_file`).
 * - Raw selector-based access (`select`, `read`, `write`).
 * - DMA-based writes required by some devices (e.g. `ramfb` configuration).
 *
 * Reference: https://www.qemu.org/docs/master/specs/fw_cfg.html
 */
namespace fwcfg {

// QEMU fw_cfg interface for virt machine
// Reference: https://www.qemu.org/docs/master/specs/fw_cfg.html

/**
 * @brief Probe and initialize the fw_cfg interface.
 *
 * @details
 * Reads the fw_cfg signature and ID to determine whether the device is present
 * and whether the file directory interface is supported. Prints diagnostics to
 * the serial console.
 */
void init();

/**
 * @brief Look up a file entry in the fw_cfg directory.
 *
 * @details
 * Searches the fw_cfg file directory for a file named `name`. If found, writes
 * its selector value into `selector` and returns the file size in bytes.
 *
 * @param name NUL-terminated file name (e.g. `"etc/ramfb"`).
 * @param selector Output: selector value used to access the file contents.
 * @return File size in bytes, or 0 if not found or unsupported.
 */
u32 find_file(const char *name, u16 *selector);

/**
 * @brief Select the fw_cfg item to access.
 *
 * @details
 * Programs the selector register to choose which item subsequent
 * @ref read/@ref write operations will access.
 *
 * @param selector Selector value.
 */
void select(u16 selector);

/**
 * @brief Read bytes from the currently selected fw_cfg item.
 *
 * @param buf Destination buffer.
 * @param size Number of bytes to read.
 */
void read(void *buf, u32 size);

/**
 * @brief Write bytes to the currently selected fw_cfg item.
 *
 * @details
 * Only some fw_cfg items are writable. For others, writes may be ignored or
 * may not be supported. Some items require DMA writes instead.
 *
 * @param buf Source buffer.
 * @param size Number of bytes to write.
 */
void write(const void *buf, u32 size);

/**
 * @brief Write bytes to an fw_cfg item using the DMA interface.
 *
 * @details
 * Certain fw_cfg-backed devices require DMA-based access. This helper builds a
 * DMA descriptor, writes its address to the fw_cfg DMA register, and waits for
 * completion.
 *
 * @param sel Selector identifying the target item.
 * @param buf Source buffer to write.
 * @param size Number of bytes to write.
 */
void dma_write(u16 sel, const void *buf, u32 size);

} // namespace fwcfg
