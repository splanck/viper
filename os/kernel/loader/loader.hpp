#pragma once

/**
 * @file loader.hpp
 * @brief ELF loader interface for creating runnable user-space images.
 *
 * @details
 * The loader is responsible for mapping an ELF image into a Viper process's
 * address space and returning the information required to start execution:
 * - The final entry point address.
 * - The base load address used for PIE (ET_DYN) images.
 * - The initial program break (`brk`) used to initialize the user heap.
 *
 * Loader operations are currently designed for early bring-up and favor
 * simplicity over completeness:
 * - Only ELF64 for AArch64 is supported.
 * - Only PT_LOAD segments are interpreted.
 * - No relocations are applied; ET_DYN is supported only as a fixed-base PIE.
 * - File I/O uses the kernel VFS and reads the entire image into memory.
 */

#include "../include/types.hpp"
#include "../viper/viper.hpp"

namespace loader
{

/**
 * @brief Result of loading an ELF image into an address space.
 *
 * @details
 * When loading succeeds, the loader computes:
 * - `entry_point`: where the CPU should begin execution in EL0.
 * - `base_addr`: the base address applied to PIE images (0 for ET_EXEC).
 * - `brk`: an initial program break value aligned to a page boundary.
 *
 * The caller typically uses this information to set up the initial user stack
 * and to configure a userspace `sbrk`/`brk` implementation.
 */
struct LoadResult
{
    bool success;    /**< Whether the load completed successfully. */
    u64 entry_point; /**< Final entry point virtual address. */
    u64 base_addr;   /**< Base address applied to PIE images (0 for ET_EXEC). */
    u64 brk;         /**< Page-aligned initial break (end of loaded segments). */
};

/**
 * @brief Load an ELF image from memory into a Viper's address space.
 *
 * @details
 * Validates the ELF header, then iterates program headers and loads each
 * `PT_LOAD` segment by:
 * - Allocating and mapping pages in the target AddressSpace.
 * - Zeroing the full mapped region (to cover BSS).
 * - Copying the file-backed portion (`p_filesz`) from the ELF image.
 * - Flushing caches for executable mappings to ensure I-cache coherency.
 *
 * For ET_DYN (PIE) images, the loader applies a fixed base address (currently
 * `viper::layout::USER_CODE_BASE`) and returns the relocated entry point.
 *
 * The implementation assumes the kernel can directly write to the physical
 * pages returned by `AddressSpace::translate` (identity-mapped bring-up model).
 *
 * @param v Target process that owns the destination address space.
 * @param elf_data Pointer to the ELF image in memory.
 * @param elf_size Size of the ELF image in bytes.
 * @return A @ref LoadResult. `success=false` indicates load failure.
 */
LoadResult load_elf(viper::Viper *v, const void *elf_data, usize elf_size);

/**
 * @brief Convenience wrapper to load an ELF image from a memory blob.
 *
 * @details
 * Equivalent to calling @ref load_elf directly; provided to make call sites
 * that operate on generic "blobs" more self-documenting.
 *
 * @param v Target process.
 * @param data Pointer to the blob (ELF image).
 * @param size Size of the blob in bytes.
 * @return Load result; see @ref load_elf for details.
 */
LoadResult load_elf_from_blob(viper::Viper *v, const void *data, usize size);

/**
 * @brief Load an ELF image from the VFS into a Viper's address space.
 *
 * @details
 * Opens the file at `path`, reads the entire contents into a kernel heap
 * buffer, then delegates to @ref load_elf. The temporary buffer is freed before
 * returning.
 *
 * This routine is useful for boot-time loading of user programs stored on the
 * filesystem.
 *
 * @param v Target process.
 * @param path NUL-terminated filesystem path to the ELF file.
 * @return Load result; `success=false` indicates open/read/parse/load failure.
 */
LoadResult load_elf_from_disk(viper::Viper *v, const char *path);

/**
 * @brief Result of spawning a new process.
 */
struct SpawnResult
{
    bool success;        /**< Whether spawn completed successfully. */
    viper::Viper *viper; /**< The newly created process (nullptr on failure). */
    u64 task_id;         /**< The task ID of the main thread (0 on failure). */
};

/**
 * @brief Spawn a new process from an ELF file on disk.
 *
 * @details
 * Creates a complete new process by:
 * 1. Creating a new Viper (process) with its own address space
 * 2. Loading the ELF binary from the filesystem
 * 3. Setting up the user stack
 * 4. Creating a user task with the ELF entry point
 * 5. Enqueueing the task in the scheduler
 *
 * The process starts executing immediately when the scheduler selects it.
 *
 * @param path Filesystem path to the ELF executable.
 * @param name Human-readable process name (for debugging).
 * @param parent Parent process (for process hierarchy), or nullptr.
 * @return SpawnResult with success status and process/task info.
 */
SpawnResult spawn_process(const char *path, const char *name, viper::Viper *parent = nullptr);

/**
 * @brief Spawn a new process from an in-memory ELF blob.
 *
 * @details
 * Same as spawn_process(path, name) but loads from memory instead of disk.
 * Useful for embedded binaries or network-loaded programs.
 *
 * @param elf_data Pointer to the ELF image in memory.
 * @param elf_size Size of the ELF image.
 * @param name Human-readable process name.
 * @param parent Parent process, or nullptr.
 * @return SpawnResult with success status and process/task info.
 */
SpawnResult spawn_process_from_blob(const void *elf_data,
                                    usize elf_size,
                                    const char *name,
                                    viper::Viper *parent = nullptr);

} // namespace loader
