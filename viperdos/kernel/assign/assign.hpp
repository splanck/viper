#pragma once

/**
 * @file assign.hpp
 * @brief "Assign" name-to-directory mapping system (v0.2.0).
 *
 * @details
 * The Assign system provides a user-facing way to refer to filesystem locations
 * using short logical prefixes, similar to "devices" or "volumes":
 *
 * - `SYS:` typically refers to the system/root directory.
 * - `D0:` refers to a boot disk or physical drive.
 *
 * Paths using assigns have the form `NAME:rest/of/path`. The prefix is resolved
 * to a directory handle, and the remainder is walked relative to that base.
 *
 * In v0.2.0 the implementation is intentionally simple:
 * - Assign names are matched case-insensitively.
 * - The backing handle type is a capability handle. During bring-up this maps
 *   directly to ViperFS inode numbers for filesystem objects.
 * - Multi-directory assigns are represented as a linked chain of entries.
 *
 * This subsystem is used by syscalls (ASSIGN_* numbers) and by higher-level
 * path resolution routines that want to support `SYS:`-style addressing.
 */

#include "../cap/handle.hpp"
#include "../include/types.hpp"

// Forward declaration
namespace fs::viperfs
{
class ViperFS;
}

namespace viper::assign
{

/**
 * @brief Handle type used by the Assign system.
 *
 * @details
 * This is a capability handle that can be returned to userspace.
 */
using Handle = cap::Handle;

/** @brief Maximum number of active assign entries (including multi-assign chain nodes). */
constexpr int MAX_ASSIGNS = 64;

/** @brief Maximum length of an assign name excluding the trailing colon. */
constexpr int MAX_ASSIGN_NAME = 31;

/**
 * @brief Flags describing assign behavior.
 *
 * @details
 * Flags are stored on each assign entry and may influence whether the entry is
 * mutable, how it is resolved, and whether multiple directories participate in
 * a search path.
 */
enum AssignFlags : u32
{
    ASSIGN_NONE = 0,            /**< No special behavior. */
    ASSIGN_SYSTEM = (1 << 0),   /**< System assign (read-only, e.g., SYS:, D0:). */
    ASSIGN_DEFERRED = (1 << 1), /**< Deferred resolution (path-based; reserved for future). */
    ASSIGN_MULTI = (1 << 2),    /**< Multi-directory assign (search path / chained entries). */
    ASSIGN_SERVICE = (1 << 3),  /**< Service assign (stores channel handle, e.g., BLKD:, NETD:). */
};

/**
 * @brief Internal representation of an assign mapping.
 *
 * @details
 * Entries live in a fixed-size table. For multi-directory assigns, multiple
 * table entries share the same name and are connected via @ref next, forming a
 * simple chain. Only the head node should be returned to userspace by listing
 * routines.
 *
 * The assign stores the inode number of the directory. When a path is resolved,
 * a fresh DirObject or FileObject is created and inserted into the caller's
 * cap_table.
 */
struct AssignEntry
{
    char name[MAX_ASSIGN_NAME + 1]; /**< Assign name (without colon). */

    union
    {
        u64 dir_inode;  /**< Inode number of the directory (for directory assigns). */
        u32 channel_id; /**< Global channel ID (for service assigns with ASSIGN_SERVICE). */
    };

    ::fs::viperfs::ViperFS *fs; /**< Filesystem this inode belongs to (nullptr = system disk). */
    u32 flags;                  /**< Bitmask of @ref AssignFlags. */
    AssignEntry *next;          /**< Next directory in a multi-assign chain. */
    bool active;                /**< Whether this table entry is in use. */
};

/**
 * @brief Assign information record returned to callers during listing.
 *
 * @details
 * This structure is intended to match the syscall ABI representation for
 * listing assigns, so its size and field ordering should remain stable.
 */
struct AssignInfo
{
    char name[32];    /**< Assign name (without colon). */
    u32 handle;       /**< Directory handle value (ABI-sized). */
    u32 flags;        /**< Flags (`ASSIGN_SYSTEM`, `ASSIGN_DEFERRED`, etc.). */
    u8 _reserved[24]; /**< Reserved for future expansion; must be zeroed by producer. */
};

/**
 * @brief Assign-specific error codes.
 *
 * @details
 * Negative values are used to make it easy to forward errors through syscall
 * return paths while keeping `OK` at 0.
 */
enum class AssignError
{
    OK = 0,             /**< Operation completed successfully. */
    NotFound = -1,      /**< The requested assign name does not exist. */
    AlreadyExists = -2, /**< Name already exists (used by future APIs). */
    InvalidName = -3,   /**< Name is empty or exceeds @ref MAX_ASSIGN_NAME. */
    ReadOnly = -4,      /**< Attempted to modify/remove a system assign. */
    TableFull = -5,     /**< No free slot remains in the assign table. */
    InvalidHandle = -6, /**< Provided handle is not valid for the operation. */
};

/**
 * @brief Initialize the assign subsystem and install default system assigns.
 *
 * @details
 * Clears the assign table and then installs well-known system assigns such as
 * `SYS:` and `D0:`. During bring-up these typically map to the root directory
 * of the boot filesystem.
 */
void init();

/**
 * @brief Set up standard directory assigns.
 *
 * @details
 * Called after the filesystem is mounted to create assigns for standard
 * directories like C: (commands), S: (startup), L: (libs), and T: (temp).
 * This function looks up the directories by path and creates assigns for
 * those that exist.
 */
void setup_standard_assigns();

/**
 * @brief Create or update an assign mapping.
 *
 * @details
 * If an assign with the given name already exists and is not a system assign,
 * its directory inode and flags are replaced. If it does not exist, a new
 * entry is allocated from the fixed-size table.
 *
 * Names are case-insensitive for lookup. The supplied `name` must not include a
 * colon; callers should pass `"SYS"`, not `"SYS:"`.
 *
 * @param name Assign name without a colon.
 * @param dir_inode Inode number of the directory to associate with the name.
 * @param flags Flags describing behavior; use @ref ASSIGN_SYSTEM for boot-time assigns.
 * @param fs Filesystem this inode belongs to (nullptr = system disk).
 * @return An @ref AssignError describing success/failure.
 */
AssignError set(const char *name, u64 dir_inode, u32 flags = ASSIGN_NONE,
                ::fs::viperfs::ViperFS *fs = nullptr);

/**
 * @brief Create or update an assign mapping from a directory handle.
 *
 * @details
 * Looks up the inode from the directory handle in the current viper's cap_table
 * and stores the inode number in the assign entry.
 *
 * @param name Assign name without a colon.
 * @param dir_handle Directory capability handle.
 * @param flags Flags describing behavior.
 * @return An @ref AssignError describing success/failure.
 */
AssignError set_from_handle(const char *name, Handle dir_handle, u32 flags = ASSIGN_NONE);

/**
 * @brief Create or update a service assign mapping from a channel handle.
 *
 * @details
 * Stores a channel handle for service discovery. Services register themselves
 * using this function so clients can find them via assign_get(). The
 * ASSIGN_SERVICE flag is automatically set.
 *
 * @param name Assign name without a colon (e.g., "BLKD", "NETD", "FSD").
 * @param channel_handle Channel capability handle for the service.
 * @param flags Additional flags (ASSIGN_SERVICE is added automatically).
 * @return An @ref AssignError describing success/failure.
 */
AssignError set_channel(const char *name, Handle channel_handle, u32 flags = ASSIGN_NONE);

/**
 * @brief Get the channel handle for a service assign.
 *
 * @details
 * Looks up a service assign and returns the channel handle. Returns
 * HANDLE_INVALID if the assign doesn't exist or is not a service assign.
 *
 * @param name Assign name without a colon.
 * @return Channel handle, or HANDLE_INVALID if not found or not a service.
 */
Handle get_channel(const char *name);

/**
 * @brief Add a directory to a multi-directory assign.
 *
 * @details
 * Adds `dir_inode` to the end of the chain for `name`. If the assign does not
 * exist, it is created and marked as @ref ASSIGN_MULTI.
 *
 * Multi-directory assigns are intended to represent "search paths" where the
 * system may later look up a file in multiple directories. The current
 * resolution function walks the remainder relative to the base directory and
 * does not yet implement search-order resolution across all chain entries.
 *
 * @param name Assign name without a colon.
 * @param dir_inode Inode number of directory to add.
 * @return An @ref AssignError describing success/failure.
 */
AssignError add(const char *name, u64 dir_inode);

/**
 * @brief Remove an assign and any chained entries.
 *
 * @details
 * Clears the assign entry from the table. If the assign is a multi-directory
 * assign, the entire chain is removed. System assigns are protected and cannot
 * be removed.
 *
 * @param name Assign name without a colon.
 * @return An @ref AssignError describing success/failure.
 */
AssignError remove(const char *name);

/**
 * @brief Look up the directory inode for an assign name.
 *
 * @details
 * Performs a case-insensitive lookup of `name`. Only the head entry for a
 * multi-directory assign is returned.
 *
 * @param name Assign name without a colon.
 * @return Directory inode number, or 0 if not found.
 */
u64 get_inode(const char *name);

/**
 * @brief Create a directory handle for an assign and insert it into the caller's cap_table.
 *
 * @details
 * Looks up the assign, creates a DirObject for it, and inserts it into the
 * current viper's cap_table.
 *
 * @param name Assign name without a colon.
 * @return Directory handle in the caller's cap_table, or HANDLE_INVALID if not found.
 */
Handle get(const char *name);

/**
 * @brief Check whether an assign name exists.
 *
 * @param name Assign name without a colon.
 * @return `true` if present, otherwise `false`.
 */
bool exists(const char *name);

/**
 * @brief Check whether an assign is a system (read-only) assign.
 *
 * @param name Assign name without a colon.
 * @return `true` if the assign exists and is flagged @ref ASSIGN_SYSTEM.
 */
bool is_system(const char *name);

/**
 * @brief List active assigns into a caller-provided buffer.
 *
 * @details
 * Writes up to `max_count` @ref AssignInfo entries describing active assigns.
 * For multi-directory assigns, only the head entry is listed (chain nodes are
 * skipped).
 *
 * @param buffer Output array of @ref AssignInfo.
 * @param max_count Maximum number of entries to write.
 * @return Number of entries written into `buffer`.
 */
int list(AssignInfo *buffer, int max_count);

/**
 * @brief Resolve an assign-prefixed path to a filesystem object handle.
 *
 * @details
 * Parses the assign prefix (e.g. `SYS:`), looks up the base directory inode,
 * then walks the remainder of the path component-by-component using viperfs
 * lookups. Creates a FileObject or DirObject for the final component and
 * inserts it into the current viper's cap_table.
 *
 * Both `/` and `\\` are accepted as path separators. If `path` contains no
 * colon, it is treated as a relative path and currently resolves to invalid.
 *
 * @param path Full path such as `SYS:dir/file.txt` or `D0:\\boot\\init`.
 * @param flags Open flags for files (O_RDONLY, O_WRONLY, O_RDWR).
 * @return Handle to the resolved file/directory in the caller's cap_table,
 *         or an invalid handle on error.
 */
Handle resolve_path(const char *path, u32 flags = 0);

/**
 * @brief Parse an assign prefix from a path string.
 *
 * @details
 * If the input contains a colon, copies the assign name (without colon) into
 * `assign_out` and returns a pointer to the remainder of the path (the
 * substring after the colon). If no colon is present, returns `false` and does
 * not modify outputs.
 *
 * @param path Input path such as `C:file.txt`.
 * @param assign_out Output buffer for the assign name (size at least @ref MAX_ASSIGN_NAME + 1).
 * @param remainder_out Output pointer set to the substring after the colon.
 * @return `true` if an assign prefix was parsed, otherwise `false`.
 */
bool parse_assign(const char *path, char *assign_out, const char **remainder_out);

/**
 * @brief Print all active assigns to the kernel console.
 *
 * @details
 * Debug-only helper that prints the assign table contents, including flags and
 * chained multi-directory entries.
 */
void debug_dump();

} // namespace viper::assign
