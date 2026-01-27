//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../cap/table.hpp"
#include "../fs/viperfs/viperfs.hpp"
#include "../viper/viper.hpp"

/**
 * @file boot_diagnostics.hpp
 * @brief Boot-time diagnostic functions.
 *
 * @details
 * These functions provide verbose diagnostic output during boot for
 * debugging and validation. They are separate from the unit tests
 * in tests.hpp which have pass/fail tracking.
 */

namespace boot_diag {

/**
 * @brief Test block device read/write operations.
 */
void test_block_device();

/**
 * @brief Test block cache operations.
 */
void test_block_cache();

/**
 * @brief Test ViperFS root directory and file operations.
 */
void test_viperfs_root(fs::viperfs::Inode *root);

/**
 * @brief Test file creation and writing on ViperFS.
 */
void test_viperfs_write(fs::viperfs::Inode *root);

/**
 * @brief Test VFS operations (open, read, write).
 */
void test_vfs_operations();

/**
 * @brief Test capability table operations.
 */
void test_cap_table(cap::Table *ct);

/**
 * @brief Test sbrk syscall implementation.
 */
void test_sbrk(viper::Viper *vp);

/**
 * @brief Test address space mapping operations.
 */
void test_address_space(viper::Viper *vp);

} // namespace boot_diag
