#pragma once

#include "../fs/viperfs/viperfs.hpp"
#include "object.hpp"

/**
 * @file dir.hpp
 * @brief Reference-counted directory object for capability-based filesystem.
 *
 * @details
 * A @ref kobj::DirObject represents an open directory in the kernel capability
 * system. Rather than exposing raw inodes to user-space, the handle-based
 * filesystem API (FsOpenRoot, FsOpen, FsReadDir) operates on opaque handles
 * that refer to kernel objects.
 *
 * The directory object stores:
 * - The on-disk inode number of the directory.
 * - A logical enumeration cursor (`offset_`) used by `FsReadDir` to return
 *   entries one-at-a-time.
 *
 * The object does not permanently pin the inode in memory; it loads the inode
 * from disk when required for operations such as lookup and enumeration.
 */
namespace kobj {

/**
 * @brief Directory entry returned by FsReadDir.
 *
 * @details
 * This structure is the user/kernel ABI for the handle-based directory
 * enumeration syscall. Each call to `FsReadDir` fills one instance of this
 * structure with the next entry in the directory.
 *
 * Field semantics:
 * - `inode`: inode number of the entry.
 * - `type`: simple type tag for display and fast checks (file vs directory).
 * - `name_len`: length in bytes of `name` excluding the NUL terminator.
 * - `name`: NUL-terminated name; entries longer than 255 bytes are truncated.
 */
struct FsDirEnt {
    u64 inode;      /**< Inode number of the entry. */
    u8 type;        /**< Entry type (filesystem-defined; typically 1=file, 2=dir). */
    u8 name_len;    /**< Name length in bytes excluding NUL. */
    char name[256]; /**< NUL-terminated name (truncated if necessary). */
};

/**
 * @brief Reference-counted directory object.
 *
 * @details
 * A directory object provides two core behaviors for the handle-based
 * filesystem API:
 * - Lookup: resolve a child name within the directory to an inode/type.
 * - Enumeration: return directory entries sequentially using an internal
 *   cursor.
 *
 * The cursor is a logical index rather than a byte offset into the on-disk
 * directory record stream. This keeps the user/kernel API stable even if the
 * on-disk directory record layout changes.
 */
class DirObject : public Object {
  public:
    static constexpr cap::Kind KIND = cap::Kind::Directory;

    /**
     * @brief Create a new directory object.
     *
     * @param inode_num Inode number of the directory.
     * @return Pointer to a new directory object, or nullptr on failure.
     */
    static DirObject *create(u64 inode_num);

    ~DirObject() override = default;

    /**
     * @brief Return the on-disk inode number backing this directory object.
     *
     * @details
     * This value is used internally when loading directory metadata. It is not
     * intended to be a stable user-space identifier.
     */
    u64 inode_num() const {
        return inode_num_;
    }

    /**
     * @brief Return the current enumeration cursor.
     *
     * @details
     * The cursor is advanced by @ref read_next and can be manipulated by user
     * operations like rewind.
     */
    u64 offset() const {
        return offset_;
    }

    /**
     * @brief Set the enumeration cursor.
     *
     * @details
     * This is primarily used by syscall wrappers implementing `FsRewindDir`.
     * Callers should treat the cursor as opaque and only set values obtained
     * from previous reads (or 0).
     *
     * @param off New cursor value.
     */
    void set_offset(u64 off) {
        offset_ = off;
    }

    /**
     * @brief Reset enumeration to the beginning of the directory.
     *
     * @details
     * After calling this, the next @ref read_next returns the first entry again.
     */
    void rewind() {
        offset_ = 0;
    }

    /**
     * @brief Look up a child entry by name.
     *
     * @details
     * Resolves a single path component within this directory. The name is
     * provided as a pointer + length pair so user-space does not need to
     * allocate a temporary NUL-terminated string.
     *
     * On success:
     * - `*out_inode` is set to the inode number of the child.
     * - If `out_type` is non-null, it is set to a simple type tag for the
     *   entry (file vs directory).
     *
     * @param name Entry name (not null-terminated).
     * @param name_len Length of name.
     * @param out_inode Output: inode number if found.
     * @param out_type Output: entry type (1=file, 2=dir).
     * @return true if found, false otherwise.
     */
    bool lookup(const char *name, usize name_len, u64 *out_inode, u8 *out_type);

    /**
     * @brief Read the next directory entry.
     *
     * @details
     * Advances the internal cursor and fills `out_ent` with the next entry. The
     * enumeration order is defined by the underlying filesystem directory
     * record stream.
     *
     * @param out_ent Output: directory entry.
     * @return true if an entry was returned, false if at end.
     */
    bool read_next(FsDirEnt *out_ent);

    /**
     * @brief Validate that the backing inode is still a directory.
     *
     * @details
     * Handles can become stale if the underlying filesystem entry is replaced
     * or corrupted. This method reloads the inode and checks its mode bits.
     *
     * @return True if the inode exists and is a directory.
     */
    bool is_valid_dir();

  private:
    DirObject(u64 inode_num) : Object(KIND), inode_num_(inode_num), offset_(0) {}

    u64 inode_num_; // Inode number on disk
    u64 offset_;    // Current enumeration position
};

} // namespace kobj
