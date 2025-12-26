/**
 * @file file.cpp
 * @brief Implementation of @ref kobj::FileObject for handle-based I/O.
 *
 * @details
 * This file implements the kernel object used to back handle-based file I/O.
 * The object:
 * - Validates that a provided inode refers to a regular file.
 * - Maintains a current offset for sequential reads/writes.
 * - Delegates actual disk reads/writes to the ViperFS driver.
 *
 * Error handling is currently bring-up oriented. Several methods return `-1`
 * for failure rather than a fully specified `error::Code`. As the syscall ABI
 * for capability-based I/O stabilizes, these return values should be mapped to
 * the shared kernel error codes.
 */
#include "file.hpp"
#include "../console/serial.hpp"

namespace kobj
{

/** @copydoc kobj::FileObject::create */
FileObject *FileObject::create(u64 inode_num, u32 flags)
{
    // Verify the inode exists
    fs::viperfs::Inode *inode = fs::viperfs::viperfs().read_inode(inode_num);
    if (!inode)
    {
        return nullptr;
    }

    // Verify it's a file (not a directory)
    if (fs::viperfs::is_directory(inode))
    {
        fs::viperfs::viperfs().release_inode(inode);
        return nullptr;
    }

    u64 initial_offset = 0;

    // Handle O_APPEND
    if (flags & file_flags::O_APPEND)
    {
        initial_offset = inode->size;
    }

    fs::viperfs::viperfs().release_inode(inode);

    // Allocate the file object
    FileObject *file = new FileObject(inode_num, flags);
    if (!file)
    {
        return nullptr;
    }

    file->offset_ = initial_offset;

    serial::puts("[kobj::file] Created file object for inode ");
    serial::put_dec(inode_num);
    serial::puts("\n");

    return file;
}

/** @copydoc kobj::FileObject::read */
i64 FileObject::read(void *buf, usize len)
{
    if (!can_read())
    {
        return -1; // Not readable
    }

    fs::viperfs::Inode *inode = fs::viperfs::viperfs().read_inode(inode_num_);
    if (!inode)
    {
        return -1;
    }

    i64 bytes = fs::viperfs::viperfs().read_data(inode, offset_, buf, len);
    if (bytes > 0)
    {
        offset_ += bytes;
    }

    fs::viperfs::viperfs().release_inode(inode);
    return bytes;
}

/** @copydoc kobj::FileObject::write */
i64 FileObject::write(const void *buf, usize len)
{
    if (!can_write())
    {
        return -1; // Not writable
    }

    fs::viperfs::Inode *inode = fs::viperfs::viperfs().read_inode(inode_num_);
    if (!inode)
    {
        return -1;
    }

    i64 bytes = fs::viperfs::viperfs().write_data(inode, offset_, buf, len);
    if (bytes > 0)
    {
        offset_ += bytes;
        fs::viperfs::viperfs().write_inode(inode);
    }

    fs::viperfs::viperfs().release_inode(inode);
    return bytes;
}

/** @copydoc kobj::FileObject::seek */
i64 FileObject::seek(i64 offset, i32 whence)
{
    i64 new_offset;

    switch (whence)
    {
        case seek_origin::SET:
            new_offset = offset;
            break;

        case seek_origin::CUR:
            new_offset = static_cast<i64>(offset_) + offset;
            break;

        case seek_origin::END:
        {
            fs::viperfs::Inode *inode = fs::viperfs::viperfs().read_inode(inode_num_);
            if (!inode)
            {
                return -1;
            }
            new_offset = static_cast<i64>(inode->size) + offset;
            fs::viperfs::viperfs().release_inode(inode);
            break;
        }

        default:
            return -1;
    }

    if (new_offset < 0)
    {
        return -1;
    }

    offset_ = static_cast<u64>(new_offset);
    return new_offset;
}

} // namespace kobj
