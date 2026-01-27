//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../fs/viperfs/viperfs.hpp"
#include "object.hpp"

/**
 * @file file.hpp
 * @brief Reference-counted file object for capability-based I/O.
 *
 * @details
 * A @ref kobj::FileObject represents an open file in the kernel capability
 * system. It is the file counterpart to @ref kobj::DirObject and is intended
 * to back the handle-based I/O syscalls (FsOpen, IORead, IOWrite, IOSeek).
 *
 * The object stores:
 * - The on-disk inode number of the file.
 * - A current byte offset used for sequential reads/writes.
 * - Open flags describing allowed access (read/write) and behaviors such as
 *   append.
 *
 * The file object does not permanently pin the inode in memory; it loads the
 * inode from disk as needed to perform I/O operations and releases it
 * afterwards.
 */
namespace kobj {

/** @name Open flags
 *  @brief File open flags used by the handle-based API.
 *
 *  @details
 *  These values intentionally mirror the bring-up VFS flags so user-space can
 *  share constants between the descriptor-based and handle-based APIs.
 *  @{
 */
namespace file_flags {
constexpr u32 O_RDONLY = 0x0000;
constexpr u32 O_WRONLY = 0x0001;
constexpr u32 O_RDWR = 0x0002;
constexpr u32 O_CREAT = 0x0040;
constexpr u32 O_TRUNC = 0x0200;
constexpr u32 O_APPEND = 0x0400;
} // namespace file_flags

/** @} */

/** @name Seek origins
 *  @brief Whence values accepted by @ref FileObject::seek.
 *
 *  @details
 *  These values are designed to match conventional POSIX semantics.
 *  @{
 */
namespace seek_origin {
constexpr i32 SET = 0; // Absolute position
constexpr i32 CUR = 1; // Relative to current
constexpr i32 END = 2; // Relative to end
} // namespace seek_origin

/** @} */

/**
 * @brief Reference-counted file object.
 *
 * @details
 * A file object provides sequential and positioned I/O by maintaining an
 * internal byte offset. Calls to @ref read and @ref write advance the offset
 * by the number of bytes successfully transferred. Calls to @ref seek update
 * the offset according to the chosen origin.
 *
 * The object enforces basic access policy using the open flags:
 * - Read operations are rejected when opened write-only.
 * - Write operations are rejected when opened read-only.
 */
class FileObject : public Object {
  public:
    static constexpr cap::Kind KIND = cap::Kind::File;

    /**
     * @brief Create a new file object.
     *
     * @param inode_num Inode number of the file.
     * @param flags Open flags (O_RDONLY, O_WRONLY, O_RDWR, etc.).
     * @return Pointer to a new file object, or nullptr on failure.
     */
    static FileObject *create(u64 inode_num, u32 flags);

    ~FileObject() override = default;

    /**
     * @brief Return the on-disk inode number backing this file object.
     *
     * @details
     * This value is used internally when reading/writing file data and is not
     * intended as a stable user-space identifier.
     */
    u64 inode_num() const {
        return inode_num_;
    }

    /**
     * @brief Return the current file position (byte offset).
     *
     * @details
     * The position is updated by reads/writes/seeks.
     */
    u64 offset() const {
        return offset_;
    }

    /**
     * @brief Set the current file position (byte offset).
     *
     * @param off New offset value in bytes.
     */
    void set_offset(u64 off) {
        offset_ = off;
    }

    /**
     * @brief Return the open flags used to create the file object.
     *
     * @details
     * The flags influence access checks and initial offset behavior (append).
     */
    u32 flags() const {
        return flags_;
    }

    /**
     * @brief Read data from the file.
     *
     * @details
     * Reads up to `len` bytes from the current file offset into `buf`. On
     * success, the file offset is advanced by the number of bytes read.
     *
     * Errors are currently returned as negative values; the exact error mapping
     * is a bring-up detail and may evolve to use the shared kernel error codes.
     *
     * @param buf Destination buffer.
     * @param len Maximum bytes to read.
     * @return Bytes read (0 at EOF), or negative on error.
     */
    i64 read(void *buf, usize len);

    /**
     * @brief Write data to the file.
     *
     * @details
     * Writes up to `len` bytes from `buf` at the current file offset. On
     * success, the offset is advanced by the number of bytes written and the
     * inode metadata is flushed back to disk.
     *
     * @param buf Source buffer.
     * @param len Bytes to write.
     * @return Bytes written, or negative on error.
     */
    i64 write(const void *buf, usize len);

    /**
     * @brief Seek within the file.
     *
     * @details
     * Updates the current file offset according to `whence`:
     * - @ref seek_origin::SET: set position to `offset`.
     * - @ref seek_origin::CUR: add `offset` to the current position.
     * - @ref seek_origin::END: add `offset` to the end-of-file position.
     *
     * Negative resulting positions are rejected.
     *
     * @param offset Offset value.
     * @param whence Origin (SET, CUR, END).
     * @return New position, or negative on error.
     */
    i64 seek(i64 offset, i32 whence);

    /**
     * @brief Return whether the file was opened with read permission.
     */
    bool can_read() const {
        u32 access = flags_ & 0x3;
        return access != file_flags::O_WRONLY;
    }

    /**
     * @brief Return whether the file was opened with write permission.
     */
    bool can_write() const {
        u32 access = flags_ & 0x3;
        return access != file_flags::O_RDONLY;
    }

  private:
    FileObject(u64 inode_num, u32 flags)
        : Object(KIND), inode_num_(inode_num), offset_(0), flags_(flags) {}

    u64 inode_num_; // Inode number on disk
    u64 offset_;    // Current read/write position
    u32 flags_;     // Open flags
};

} // namespace kobj
