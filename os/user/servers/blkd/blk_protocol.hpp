/**
 * @file blk_protocol.hpp
 * @brief Block device server IPC protocol definitions.
 *
 * @details
 * Defines the message formats for block device operations between clients
 * and the block device server (blkd).
 *
 * Protocol Overview:
 * - Clients send requests via IPC channel
 * - Server responds with reply messages
 * - Large data transfers use shared memory handles
 * - All messages fit within 256-byte IPC limit
 */
#pragma once

#include "../../syscall.hpp"

namespace blk
{

/**
 * @brief Block request message types.
 */
enum MsgType : u32
{
    // Requests (client -> server)
    BLK_READ = 1,
    BLK_WRITE = 2,
    BLK_FLUSH = 3,
    BLK_INFO = 4,

    // Replies (server -> client)
    BLK_READ_REPLY = 0x81,
    BLK_WRITE_REPLY = 0x82,
    BLK_FLUSH_REPLY = 0x83,
    BLK_INFO_REPLY = 0x84,
};

/**
 * @brief BLK_READ request message.
 *
 * Requests reading sectors from the block device.
 * Reply will include a shared memory handle with the data.
 */
struct ReadRequest
{
    u32 type;       ///< BLK_READ
    u32 request_id; ///< For matching replies
    u64 sector;     ///< Starting sector
    u32 count;      ///< Number of sectors
    u32 _pad;
};

/**
 * @brief BLK_READ reply message.
 *
 * Contains status and bytes read.
 * If successful, handle[0] contains shared memory with data.
 */
struct ReadReply
{
    u32 type;       ///< BLK_READ_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 bytes_read; ///< Number of bytes read
};

/**
 * @brief BLK_WRITE request message.
 *
 * Requests writing sectors to the block device.
 * handle[0] must contain shared memory with data to write.
 */
struct WriteRequest
{
    u32 type;       ///< BLK_WRITE
    u32 request_id; ///< For matching replies
    u64 sector;     ///< Starting sector
    u32 count;      ///< Number of sectors
    u32 _pad;
    // handle[0] = shared memory with data to write
};

/**
 * @brief BLK_WRITE reply message.
 */
struct WriteReply
{
    u32 type;          ///< BLK_WRITE_REPLY
    u32 request_id;    ///< Matches request
    i32 status;        ///< 0 = success, negative = error
    u32 bytes_written; ///< Number of bytes written
};

/**
 * @brief BLK_FLUSH request message.
 */
struct FlushRequest
{
    u32 type;       ///< BLK_FLUSH
    u32 request_id; ///< For matching replies
};

/**
 * @brief BLK_FLUSH reply message.
 */
struct FlushReply
{
    u32 type;       ///< BLK_FLUSH_REPLY
    u32 request_id; ///< Matches request
    i32 status;     ///< 0 = success, negative = error
    u32 _pad;
};

/**
 * @brief BLK_INFO request message.
 */
struct InfoRequest
{
    u32 type;       ///< BLK_INFO
    u32 request_id; ///< For matching replies
};

/**
 * @brief BLK_INFO reply message.
 */
struct InfoReply
{
    u32 type;          ///< BLK_INFO_REPLY
    u32 request_id;    ///< Matches request
    i32 status;        ///< 0 = success, negative = error
    u32 sector_size;   ///< Bytes per sector (usually 512)
    u64 total_sectors; ///< Total sector count
    u32 max_request;   ///< Max sectors per request
    u32 readonly;      ///< 1 if device is read-only
};

/**
 * @brief Maximum sectors in a single request.
 */
constexpr u32 MAX_SECTORS_PER_REQUEST = 128;

/**
 * @brief Default sector size.
 */
constexpr u32 SECTOR_SIZE = 512;

} // namespace blk
