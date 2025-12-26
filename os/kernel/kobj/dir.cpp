/**
 * @file dir.cpp
 * @brief Implementation of @ref kobj::DirObject for the handle-based filesystem API.
 *
 * @details
 * This file implements directory operations for the capability-based
 * filesystem model:
 * - Creating directory objects from an inode number.
 * - Looking up a child entry within a directory.
 * - Enumerating directory entries one-at-a-time using an internal cursor.
 *
 * The underlying on-disk directory format is provided by the ViperFS driver.
 * `DirObject` does not permanently hold an inode reference; it reads/releases
 * the inode as needed for each operation.
 */
#include "dir.hpp"
#include "../console/serial.hpp"

namespace kobj
{

/** @copydoc kobj::DirObject::create */
DirObject *DirObject::create(u64 inode_num)
{
    // Verify the inode exists and is a directory
    fs::viperfs::Inode *inode = fs::viperfs::viperfs().read_inode(inode_num);
    if (!inode)
    {
        return nullptr;
    }

    if (!fs::viperfs::is_directory(inode))
    {
        fs::viperfs::viperfs().release_inode(inode);
        return nullptr;
    }

    fs::viperfs::viperfs().release_inode(inode);

    // Allocate the directory object
    DirObject *dir = new DirObject(inode_num);
    if (!dir)
    {
        return nullptr;
    }

    serial::puts("[kobj::dir] Created directory object for inode ");
    serial::put_dec(inode_num);
    serial::puts("\n");

    return dir;
}

/** @copydoc kobj::DirObject::lookup */
bool DirObject::lookup(const char *name, usize name_len, u64 *out_inode, u8 *out_type)
{
    if (!name || name_len == 0 || !out_inode)
    {
        return false;
    }

    fs::viperfs::Inode *inode = fs::viperfs::viperfs().read_inode(inode_num_);
    if (!inode)
    {
        return false;
    }

    if (!fs::viperfs::is_directory(inode))
    {
        fs::viperfs::viperfs().release_inode(inode);
        return false;
    }

    // Use viperfs lookup
    u64 child_ino = fs::viperfs::viperfs().lookup(inode, name, name_len);
    fs::viperfs::viperfs().release_inode(inode);

    if (child_ino == 0)
    {
        return false;
    }

    *out_inode = child_ino;

    // Determine the type of the child
    if (out_type)
    {
        fs::viperfs::Inode *child = fs::viperfs::viperfs().read_inode(child_ino);
        if (child)
        {
            *out_type = fs::viperfs::mode_to_file_type(child->mode);
            fs::viperfs::viperfs().release_inode(child);
        }
        else
        {
            *out_type = fs::viperfs::file_type::UNKNOWN;
        }
    }

    return true;
}

/**
 * @brief Callback context used by @ref read_next_callback.
 *
 * @details
 * ViperFS directory enumeration currently uses a callback-style API. `DirObject`
 * adapts that model to a one-entry-at-a-time iterator by:
 * - Scanning entries from the beginning of the directory.
 * - Skipping until it reaches the logical index stored in `offset_`.
 * - Capturing the next entry into an @ref FsDirEnt output structure.
 *
 * This context structure tracks the current scan position and whether a
 * suitable entry has been found.
 */
struct ReadNextCtx
{
    u64 target_offset;  // Offset we're looking for
    u64 current_offset; // Current position in enumeration
    FsDirEnt *out_ent;  // Where to write the result
    bool found;         // Set to true when we find an entry at/after target
    u64 next_offset;    // Updated offset for next call
};

/**
 * @brief Directory enumeration callback used to implement @ref DirObject::read_next.
 *
 * @details
 * This callback is invoked once for each directory entry encountered by the
 * ViperFS `readdir` implementation. The callback updates the @ref ReadNextCtx:
 * - If the current entry index is below `target_offset`, it is skipped.
 * - When the current index reaches the target, the entry is copied into the
 *   output @ref FsDirEnt and the callback records `next_offset` for the next
 *   read.
 *
 * The callback does not attempt to stop enumeration early; it simply sets
 * `found` so later invocations become no-ops.
 *
 * @param name Entry name pointer.
 * @param name_len Length of the entry name in bytes.
 * @param ino Inode number for the entry.
 * @param file_type Entry type tag.
 * @param ctx Opaque pointer to a @ref ReadNextCtx structure.
 */
static void read_next_callback(const char *name, usize name_len, u64 ino, u8 file_type, void *ctx)
{
    auto *rctx = static_cast<ReadNextCtx *>(ctx);

    // Skip until we reach or pass the target offset
    if (rctx->found)
    {
        // We already found one entry; record offset for next iteration
        return;
    }

    if (rctx->current_offset >= rctx->target_offset)
    {
        // This is the entry we want
        rctx->out_ent->inode = ino;
        rctx->out_ent->type = file_type;
        rctx->out_ent->name_len = static_cast<u8>(name_len > 255 ? 255 : name_len);

        for (usize i = 0; i < rctx->out_ent->name_len; i++)
        {
            rctx->out_ent->name[i] = name[i];
        }
        rctx->out_ent->name[rctx->out_ent->name_len] = '\0';

        rctx->found = true;
        rctx->next_offset = rctx->current_offset + 1;
    }

    rctx->current_offset++;
}

/** @copydoc kobj::DirObject::read_next */
bool DirObject::read_next(FsDirEnt *out_ent)
{
    if (!out_ent)
    {
        return false;
    }

    fs::viperfs::Inode *inode = fs::viperfs::viperfs().read_inode(inode_num_);
    if (!inode)
    {
        return false;
    }

    if (!fs::viperfs::is_directory(inode))
    {
        fs::viperfs::viperfs().release_inode(inode);
        return false;
    }

    // Set up context
    ReadNextCtx ctx;
    ctx.target_offset = offset_;
    ctx.current_offset = 0;
    ctx.out_ent = out_ent;
    ctx.found = false;
    ctx.next_offset = offset_;

    // Read all directory entries (viperfs reads from byte offset 0)
    fs::viperfs::viperfs().readdir(inode, 0, read_next_callback, &ctx);

    fs::viperfs::viperfs().release_inode(inode);

    if (ctx.found)
    {
        offset_ = ctx.next_offset;
        return true;
    }

    return false; // End of directory
}

/** @copydoc kobj::DirObject::is_valid_dir */
bool DirObject::is_valid_dir()
{
    fs::viperfs::Inode *inode = fs::viperfs::viperfs().read_inode(inode_num_);
    if (!inode)
    {
        return false;
    }

    bool is_dir = fs::viperfs::is_directory(inode);
    fs::viperfs::viperfs().release_inode(inode);
    return is_dir;
}

} // namespace kobj
