/**
 * @file fs_protocol.hpp
 * @brief Filesystem server IPC protocol definitions.
 *
 * @details
 * Defines the message formats for filesystem operations between clients
 * and the filesystem server (fsd).
 *
 * Protocol Overview:
 * - Clients send requests via IPC channel
 * - Server responds with reply messages
 * - Small data transfers are inline (up to 200 bytes)
 * - Large data transfers use shared memory handles
 * - All messages fit within 256-byte IPC limit
 */
#pragma once

#include "../../syscall.hpp"

namespace fs
{

/**
 * @brief Filesystem request message types.
 */
enum MsgType : u32
{
    // File operations (client -> server)
    FS_OPEN = 1,
    FS_CLOSE = 2,
    FS_READ = 3,
    FS_WRITE = 4,
    FS_SEEK = 5,
    FS_STAT = 6,
    FS_FSTAT = 7,
    FS_FSYNC = 8,

    // Directory operations
    FS_READDIR = 10,
    FS_MKDIR = 11,
    FS_RMDIR = 12,
    FS_UNLINK = 13,
    FS_RENAME = 14,

    // Symlink operations
    FS_SYMLINK = 20,
    FS_READLINK = 21,

    // Filesystem info
    FS_STATFS = 30,

    // Replies (server -> client)
    FS_OPEN_REPLY = 0x81,
    FS_CLOSE_REPLY = 0x82,
    FS_READ_REPLY = 0x83,
    FS_WRITE_REPLY = 0x84,
    FS_SEEK_REPLY = 0x85,
    FS_STAT_REPLY = 0x86,
    FS_FSTAT_REPLY = 0x87,
    FS_FSYNC_REPLY = 0x88,

    FS_READDIR_REPLY = 0x8A,
    FS_MKDIR_REPLY = 0x8B,
    FS_RMDIR_REPLY = 0x8C,
    FS_UNLINK_REPLY = 0x8D,
    FS_RENAME_REPLY = 0x8E,

    FS_SYMLINK_REPLY = 0x94,
    FS_READLINK_REPLY = 0x95,

    FS_STATFS_REPLY = 0x9E,
};

/**
 * @brief Maximum path length in a single message.
 */
constexpr usize MAX_PATH_LEN = 200;

/**
 * @brief Maximum inline data in read/write replies.
 */
constexpr usize MAX_INLINE_DATA = 200;

/**
 * @brief Open flags (matches kernel open_flags).
 */
namespace open_flags
{
constexpr u32 O_RDONLY = 0;
constexpr u32 O_WRONLY = 1;
constexpr u32 O_RDWR = 2;
constexpr u32 O_CREAT = 0x40;
constexpr u32 O_TRUNC = 0x200;
constexpr u32 O_APPEND = 0x400;
} // namespace open_flags

/**
 * @brief Seek whence values.
 */
namespace seek_whence
{
constexpr i32 SET = 0;
constexpr i32 CUR = 1;
constexpr i32 END = 2;
} // namespace seek_whence

/**
 * @brief File type constants for stat/readdir.
 */
namespace file_type
{
constexpr u8 UNKNOWN = 0;
constexpr u8 FILE = 1;
constexpr u8 DIR = 2;
constexpr u8 LINK = 7;
} // namespace file_type

// ============================================================================
// Request Messages
// ============================================================================

/**
 * @brief FS_OPEN request message.
 */
struct OpenRequest
{
    u32 type;            ///< FS_OPEN
    u32 request_id;      ///< For matching replies
    u32 flags;           ///< Open flags
    u16 path_len;        ///< Length of path
    char path[MAX_PATH_LEN]; ///< Path (not null-terminated)
};

/**
 * @brief FS_CLOSE request message.
 */
struct CloseRequest
{
    u32 type;       ///< FS_CLOSE
    u32 request_id; ///< For matching replies
    u32 file_id;    ///< Server-side file ID
    u32 _pad;
};

/**
 * @brief FS_READ request message.
 */
struct ReadRequest
{
    u32 type;       ///< FS_READ
    u32 request_id; ///< For matching replies
    u32 file_id;    ///< Server-side file ID
    u32 count;      ///< Max bytes to read
    i64 offset;     ///< Offset (-1 = use current position)
};

/**
 * @brief FS_WRITE request message.
 *
 * For small writes (<=MAX_INLINE_DATA), data is inline.
 * For large writes, handle[0] contains shared memory.
 */
struct WriteRequest
{
    u32 type;       ///< FS_WRITE
    u32 request_id; ///< For matching replies
    u32 file_id;    ///< Server-side file ID
    u32 count;      ///< Bytes to write
    i64 offset;     ///< Offset (-1 = use current position)
    u8 data[MAX_INLINE_DATA]; ///< Inline data for small writes
};

/**
 * @brief FS_SEEK request message.
 */
struct SeekRequest
{
    u32 type;       ///< FS_SEEK
    u32 request_id; ///< For matching replies
    u32 file_id;    ///< Server-side file ID
    i32 whence;     ///< SEEK_SET, SEEK_CUR, SEEK_END
    i64 offset;     ///< Offset value
};

/**
 * @brief FS_STAT request message.
 */
struct StatRequest
{
    u32 type;            ///< FS_STAT
    u32 request_id;      ///< For matching replies
    u16 path_len;        ///< Length of path
    char path[MAX_PATH_LEN]; ///< Path
};

/**
 * @brief FS_FSTAT request message.
 */
struct FstatRequest
{
    u32 type;       ///< FS_FSTAT
    u32 request_id; ///< For matching replies
    u32 file_id;    ///< Server-side file ID
    u32 _pad;
};

/**
 * @brief FS_FSYNC request message.
 */
struct FsyncRequest
{
    u32 type;       ///< FS_FSYNC
    u32 request_id; ///< For matching replies
    u32 file_id;    ///< Server-side file ID
    u32 _pad;
};

/**
 * @brief FS_READDIR request message.
 */
struct ReaddirRequest
{
    u32 type;       ///< FS_READDIR
    u32 request_id; ///< For matching replies
    u32 file_id;    ///< Server-side directory file ID
    u32 max_entries; ///< Max entries to return
};

/**
 * @brief FS_MKDIR request message.
 */
struct MkdirRequest
{
    u32 type;            ///< FS_MKDIR
    u32 request_id;      ///< For matching replies
    u16 path_len;        ///< Length of path
    char path[MAX_PATH_LEN]; ///< Path
};

/**
 * @brief FS_RMDIR request message.
 */
struct RmdirRequest
{
    u32 type;            ///< FS_RMDIR
    u32 request_id;      ///< For matching replies
    u16 path_len;        ///< Length of path
    char path[MAX_PATH_LEN]; ///< Path
};

/**
 * @brief FS_UNLINK request message.
 */
struct UnlinkRequest
{
    u32 type;            ///< FS_UNLINK
    u32 request_id;      ///< For matching replies
    u16 path_len;        ///< Length of path
    char path[MAX_PATH_LEN]; ///< Path
};

/**
 * @brief FS_RENAME request message.
 */
struct RenameRequest
{
    u32 type;            ///< FS_RENAME
    u32 request_id;      ///< For matching replies
    u16 old_path_len;    ///< Length of old path
    u16 new_path_len;    ///< Length of new path
    char paths[200];     ///< old_path followed by new_path
};

// ============================================================================
// Reply Messages
// ============================================================================

/**
 * @brief FS_OPEN reply message.
 */
struct OpenReply
{
    u32 type;       ///< FS_OPEN_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 file_id;    ///< Server-side file ID (if success)
};

/**
 * @brief FS_CLOSE reply message.
 */
struct CloseReply
{
    u32 type;       ///< FS_CLOSE_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

/**
 * @brief FS_READ reply message.
 *
 * For small reads, data is inline.
 * For large reads, handle[0] contains shared memory.
 */
struct ReadReply
{
    u32 type;       ///< FS_READ_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 bytes_read; ///< Bytes actually read
    u8 data[MAX_INLINE_DATA]; ///< Inline data for small reads
};

/**
 * @brief FS_WRITE reply message.
 */
struct WriteReply
{
    u32 type;          ///< FS_WRITE_REPLY
    u32 request_id;    ///< Matches request
    i32 status;        ///< 0 = success, negative = error
    u32 bytes_written; ///< Bytes actually written
};

/**
 * @brief FS_SEEK reply message.
 */
struct SeekReply
{
    u32 type;         ///< FS_SEEK_REPLY
    u32 request_id;   ///< Matches request
    i32 status;       ///< 0 = success, negative = error
    u32 _pad;
    i64 new_offset;   ///< New file position
};

/**
 * @brief Stat structure returned in stat/fstat replies.
 */
struct StatInfo
{
    u64 inode;       ///< Inode number
    u64 size;        ///< File size
    u64 blocks;      ///< Blocks allocated
    u32 mode;        ///< File mode/type
    u32 _pad;
    u64 atime;       ///< Access time
    u64 mtime;       ///< Modification time
    u64 ctime;       ///< Creation time
};

/**
 * @brief FS_STAT reply message.
 */
struct StatReply
{
    u32 type;       ///< FS_STAT_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
    StatInfo stat;  ///< File statistics
};

/**
 * @brief FS_FSTAT reply message.
 */
struct FstatReply
{
    u32 type;       ///< FS_FSTAT_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
    StatInfo stat;  ///< File statistics
};

/**
 * @brief FS_FSYNC reply message.
 */
struct FsyncReply
{
    u32 type;       ///< FS_FSYNC_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

/**
 * @brief Directory entry in readdir reply.
 */
struct DirEntryInfo
{
    u64 inode;        ///< Inode number
    u8 type;          ///< File type
    u8 name_len;      ///< Name length
    char name[62];    ///< Entry name (not null-terminated)
};

/**
 * @brief FS_READDIR reply message.
 */
struct ReaddirReply
{
    u32 type;          ///< FS_READDIR_REPLY
    u32 request_id;    ///< Matches request
    i32 status;        ///< 0 = success, negative = error
    u32 entry_count;   ///< Number of entries returned
    DirEntryInfo entries[2]; ///< Directory entries (variable count)
};

/**
 * @brief FS_MKDIR reply message.
 */
struct MkdirReply
{
    u32 type;       ///< FS_MKDIR_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

/**
 * @brief FS_RMDIR reply message.
 */
struct RmdirReply
{
    u32 type;       ///< FS_RMDIR_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

/**
 * @brief FS_UNLINK reply message.
 */
struct UnlinkReply
{
    u32 type;       ///< FS_UNLINK_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

/**
 * @brief FS_RENAME reply message.
 */
struct RenameReply
{
    u32 type;       ///< FS_RENAME_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

} // namespace fs
