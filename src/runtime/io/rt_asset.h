//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_asset.h
// Purpose: Runtime asset manager for loading embedded and packed resources.
//          Provides transparent asset resolution across embedded data (.rodata),
//          mounted .zpak pack files, and the filesystem.
//
// Key invariants:
//   - Resolution order: embedded → mounted packs (LIFO) → filesystem.
//   - Auto-discovery of .zpak files next to the executable on first use.
//   - Assets.Load() returns typed objects based on file extension.
//   - Assets.LoadBytes() always returns raw Bytes.
//   - Thread-safe initialization via single-init guard.
//
// Ownership/Lifetime:
//   - Returned objects are GC-managed.
//   - Mounted pack handles are held until unmount or process exit.
//
// Links: rt_zpak_reader.h (ZPAK parser), rt_path_exe.c (exe dir detection)
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Initialize the asset manager with an optional embedded ZPAK blob.
/// Called from codegen-generated main trampoline when assets are embedded.
/// Safe to call multiple times (idempotent).
void rt_asset_init(const uint8_t *blob, uint64_t size);

/// @brief Load an asset by name, returning a typed object based on extension.
/// Resolution: embedded → mounted packs → filesystem.
/// Returns NULL if not found.
void *rt_asset_load(rt_string name);

/// @brief Load an asset by name as raw Bytes, regardless of extension.
/// Returns NULL if not found.
void *rt_asset_load_bytes(rt_string name);

/// @brief Load an asset by name into a malloc-owned raw buffer.
/// @details Runtime-internal convenience for asset-aware decoders that need to
///          parse bytes directly. Resolution matches LoadBytes().
/// @param name Asset name. The optional asset:// scheme is stripped before lookup.
/// @param out_size Receives the byte count on success, 0 on failure.
/// @return malloc-owned byte buffer, or NULL if not found/invalid. Caller frees.
uint8_t *rt_asset_load_raw(rt_string name, size_t *out_size);

/// @brief Check if an asset exists (embedded, in pack, or on disk).
/// @return 1 if found, 0 otherwise.
int64_t rt_asset_exists(rt_string name);

/// @brief Get the size of an asset in bytes.
/// @return Size in bytes, or 0 if not found.
int64_t rt_asset_size(rt_string name);

/// @brief List all available asset names (embedded + all mounted packs).
/// @return seq<str> of asset names.
void *rt_asset_list(void);

/// @brief Mount a .zpak pack file for asset resolution.
/// @return 1 on success, 0 on failure.
int64_t rt_asset_mount(rt_string path);

/// @brief Unmount a previously mounted .zpak pack file.
/// @return 1 on success, 0 on failure.
int64_t rt_asset_unmount(rt_string path);

#ifdef __cplusplus
}
#endif
