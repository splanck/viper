#pragma once

/**
 * @file cap_info.hpp
 * @brief Shared capability metadata structures for capability-related syscalls.
 *
 * @details
 * ViperOS uses a capability/handle model for many kernel objects (channels,
 * timers, tasks, files, directories, etc.). User-space typically holds a
 * 32-bit "handle" value which indexes into a per-process capability table in
 * the kernel.
 *
 * This header defines the user/kernel ABI used by capability inspection
 * syscalls such as `SYS_CAP_QUERY` and `SYS_CAP_LIST`. The kernel fills
 * @ref CapInfo / @ref CapListEntry structures so user-space tooling can display
 * the current capability table and debug rights issues.
 *
 * The constants in this file mirror kernel values. They are intended to be
 * stable ABI and should be kept in sync with the kernel capability subsystem.
 */

/** @name Capability Kinds
 *  @brief Kind identifiers stored in @ref CapInfo::kind.
 *
 *  @details
 *  Each handle refers to a kernel object of a particular kind. The kernel uses
 *  this kind to validate syscalls (e.g., passing a timer handle to a channel
 *  syscall should fail with an invalid-handle error).
 *
 *  The numeric values are shared with the kernel's `cap::Kind` enum.
 *  @{
 */
#define CAP_KIND_INVALID 0    /**< Invalid/unused handle slot. */
#define CAP_KIND_STRING 1     /**< Kernel-owned string object. */
#define CAP_KIND_ARRAY 2      /**< Kernel-owned array object. */
#define CAP_KIND_BLOB 3       /**< Kernel-owned binary blob object. */
#define CAP_KIND_CHANNEL 16   /**< IPC channel endpoint. */
#define CAP_KIND_POLL 17      /**< Poll set used for event multiplexing. */
#define CAP_KIND_TIMER 18     /**< Timer object that can signal poll events. */
#define CAP_KIND_TASK 19      /**< Task/process handle. */
#define CAP_KIND_VIPER 20     /**< "Viper" process container/instance handle. */
#define CAP_KIND_FILE 21      /**< File object (handle-based filesystem API). */
#define CAP_KIND_DIRECTORY 22 /**< Directory object (handle-based filesystem API). */
#define CAP_KIND_SURFACE 23   /**< Graphics surface/framebuffer object. */
#define CAP_KIND_INPUT 24     /**< Input device/stream object. */
#define CAP_KIND_SHARED_MEMORY 25 /**< Shared memory object. */
#define CAP_KIND_DEVICE 26        /**< Device capability (microkernel). */
/** @} */

/** @name Capability Rights
 *  @brief Bitmask flags stored in @ref CapInfo::rights.
 *
 *  @details
 *  Rights encode what operations a handle can be used for. The kernel validates
 *  rights on each syscall that consumes a handle.
 *
 *  Rights are intentionally coarse-grained: they communicate policy decisions
 *  between components and support least-privilege patterns (e.g., derive a
 *  read-only handle and pass it to an untrusted component).
 *
 *  The values mirror the kernel's `cap::Rights` bitmask.
 *  @{
 */
#define CAP_RIGHT_NONE 0            /**< No rights granted. */
#define CAP_RIGHT_READ (1 << 0)     /**< Read bytes / receive data / query state. */
#define CAP_RIGHT_WRITE (1 << 1)    /**< Write bytes / send data / mutate state. */
#define CAP_RIGHT_EXECUTE (1 << 2)  /**< Execute/launch behavior where applicable. */
#define CAP_RIGHT_LIST (1 << 3)     /**< Enumerate contents (e.g., directory listing). */
#define CAP_RIGHT_CREATE (1 << 4)   /**< Create new entries/objects under this handle. */
#define CAP_RIGHT_DELETE (1 << 5)   /**< Delete/unlink entries/objects under this handle. */
#define CAP_RIGHT_DERIVE (1 << 6)   /**< Derive a new handle with reduced rights. */
#define CAP_RIGHT_TRANSFER (1 << 7) /**< Transfer/duplicate handle to another party. */
#define CAP_RIGHT_SPAWN (1 << 8)    /**< Spawn tasks/processes using this handle/context. */

/** @brief Allow mapping device MMIO regions via `SYS_MAP_DEVICE`. */
#define CAP_RIGHT_DEVICE_ACCESS (1 << 10)
/** @brief Allow registering/waiting/acking IRQs via `SYS_IRQ_*`. */
#define CAP_RIGHT_IRQ_ACCESS (1 << 11)
/** @brief Allow allocating/using DMA buffers via `SYS_DMA_*` and `SYS_VIRT_TO_PHYS`. */
#define CAP_RIGHT_DMA_ACCESS (1 << 12)

/** @} */

/**
 * @brief Metadata describing one capability handle.
 *
 * @details
 * The kernel fills this structure when user-space calls `SYS_CAP_QUERY`.
 * Together the fields allow user-space to:
 * - Determine what kind of object a handle refers to (`kind`).
 * - Inspect the current rights mask (`rights`).
 * - Detect stale/reused handles using the `generation` counter.
 *
 * The `handle` field is included for convenience when the structure appears in
 * arrays or logs; user-space usually already knows which handle it queried.
 */
struct CapInfo
{
    unsigned int handle;      /**< Handle value being described. */
    unsigned short kind;      /**< Object kind (`CAP_KIND_*`). */
    unsigned char generation; /**< Generation counter to detect stale handles. */
    unsigned char _reserved;  /**< Reserved/padding for alignment; set to 0. */
    unsigned int rights;      /**< Rights bitmask (`CAP_RIGHT_*`). */
};

/**
 * @brief One entry in the capability table returned by `SYS_CAP_LIST`.
 *
 * @details
 * `SYS_CAP_LIST` typically returns an array of these entries so user-space can
 * display all handles owned by the current process. The layout largely matches
 * @ref CapInfo but is optimized for enumeration rather than querying a single
 * handle.
 */
struct CapListEntry
{
    unsigned int handle;      /**< Capability handle value. */
    unsigned short kind;      /**< Object kind (`CAP_KIND_*`). */
    unsigned char generation; /**< Generation counter. */
    unsigned char _reserved;  /**< Reserved/padding. */
    unsigned int rights;      /**< Rights bitmask (`CAP_RIGHT_*`). */
};
