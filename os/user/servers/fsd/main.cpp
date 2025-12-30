/**
 * @file main.cpp
 * @brief Filesystem server (fsd) main entry point.
 *
 * @details
 * This server provides filesystem access to other user-space processes
 * via IPC. It:
 * - Connects to the block device server (blkd)
 * - Mounts the ViperFS filesystem
 * - Creates a service channel
 * - Registers with the assign system as "FSD:"
 * - Handles file/directory operation requests
 */

#include "../../syscall.hpp"
#include "fs_protocol.hpp"
#include "viperfs.hpp"

// Debug output helper
static void debug_print(const char *msg)
{
    sys::print(msg);
}

static void debug_print_dec(u64 val)
{
    if (val == 0)
    {
        sys::print("0");
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    while (val > 0 && i > 0)
    {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    sys::print(&buf[i]);
}

// Global state
static viperfs::BlkClient g_blk_client;
static viperfs::ViperFS g_viperfs;
static i32 g_service_channel = -1;

// File descriptor table for this server
// Maps client file_ids to internal file state
struct OpenFile
{
    bool in_use;
    u64 inode_num;
    u64 offset;
    u32 flags;
};

static constexpr usize MAX_OPEN_FILES = 64;
static OpenFile g_open_files[MAX_OPEN_FILES];

static i32 alloc_file()
{
    for (usize i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (!g_open_files[i].in_use)
        {
            g_open_files[i].in_use = true;
            return static_cast<i32>(i);
        }
    }
    return -1;
}

static void free_file(i32 file_id)
{
    if (file_id >= 0 && static_cast<usize>(file_id) < MAX_OPEN_FILES)
    {
        g_open_files[file_id].in_use = false;
    }
}

static OpenFile *get_file(i32 file_id)
{
    if (file_id >= 0 && static_cast<usize>(file_id) < MAX_OPEN_FILES &&
        g_open_files[file_id].in_use)
    {
        return &g_open_files[file_id];
    }
    return nullptr;
}

// Path resolution
static u64 resolve_path(const char *path, usize len)
{
    // Start from root
    u64 ino = g_viperfs.root_inode();

    // Skip leading slashes
    usize i = 0;
    while (i < len && path[i] == '/')
        i++;

    // Walk path components
    while (i < len)
    {
        // Find end of component
        usize start = i;
        while (i < len && path[i] != '/')
            i++;
        usize comp_len = i - start;

        if (comp_len == 0)
        {
            // Skip empty components (consecutive slashes)
            while (i < len && path[i] == '/')
                i++;
            continue;
        }

        // Look up component
        viperfs::Inode *dir = g_viperfs.read_inode(ino);
        if (!dir || !viperfs::is_directory(dir))
        {
            g_viperfs.release_inode(dir);
            return 0;
        }

        u64 child_ino = g_viperfs.lookup(dir, path + start, comp_len);
        g_viperfs.release_inode(dir);

        if (child_ino == 0)
            return 0;

        ino = child_ino;

        // Skip trailing slashes
        while (i < len && path[i] == '/')
            i++;
    }

    return ino;
}

// Get parent directory and basename from path
static bool split_path(
    const char *path, usize len, u64 *parent_ino, const char **name, usize *name_len)
{
    // Find last slash
    usize last_slash = 0;
    bool found_slash = false;
    for (usize i = 0; i < len; i++)
    {
        if (path[i] == '/')
        {
            last_slash = i;
            found_slash = true;
        }
    }

    if (!found_slash || last_slash == 0)
    {
        // Root directory case
        *parent_ino = g_viperfs.root_inode();
        // Skip leading slashes
        usize start = 0;
        while (start < len && path[start] == '/')
            start++;
        *name = path + start;
        *name_len = len - start;
    }
    else
    {
        // Resolve parent
        *parent_ino = resolve_path(path, last_slash);
        if (*parent_ino == 0)
            return false;
        *name = path + last_slash + 1;
        *name_len = len - last_slash - 1;
    }

    // Strip trailing slashes from name
    while (*name_len > 0 && (*name)[*name_len - 1] == '/')
        (*name_len)--;

    return *name_len > 0;
}

// ============================================================================
// Request Handlers
// ============================================================================

static void handle_open(const fs::OpenRequest *req, i32 reply_channel)
{
    fs::OpenReply reply;
    reply.type = fs::FS_OPEN_REPLY;
    reply.request_id = req->request_id;

    // Resolve path
    u64 ino = resolve_path(req->path, req->path_len);

    if (ino == 0 && (req->flags & fs::open_flags::O_CREAT))
    {
        // Create new file
        u64 parent_ino;
        const char *name;
        usize name_len;
        if (split_path(req->path, req->path_len, &parent_ino, &name, &name_len))
        {
            viperfs::Inode *parent = g_viperfs.read_inode(parent_ino);
            if (parent)
            {
                ino = g_viperfs.create_file(parent, name, name_len);
                g_viperfs.release_inode(parent);
            }
        }
    }

    if (ino == 0)
    {
        reply.status = -2; // VERR_NOT_FOUND
        reply.file_id = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Allocate file descriptor
    i32 file_id = alloc_file();
    if (file_id < 0)
    {
        reply.status = -4; // VERR_OUT_OF_MEMORY
        reply.file_id = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    OpenFile *file = get_file(file_id);
    file->inode_num = ino;
    file->offset = 0;
    file->flags = req->flags;

    // Handle truncate
    if (req->flags & fs::open_flags::O_TRUNC)
    {
        viperfs::Inode *inode = g_viperfs.read_inode(ino);
        if (inode && viperfs::is_file(inode))
        {
            // TODO: Implement truncate
            g_viperfs.release_inode(inode);
        }
    }

    reply.status = 0;
    reply.file_id = static_cast<u32>(file_id);
    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static void handle_close(const fs::CloseRequest *req, i32 reply_channel)
{
    fs::CloseReply reply;
    reply.type = fs::FS_CLOSE_REPLY;
    reply.request_id = req->request_id;

    OpenFile *file = get_file(static_cast<i32>(req->file_id));
    if (!file)
    {
        reply.status = -1; // VERR_INVALID_HANDLE
    }
    else
    {
        free_file(static_cast<i32>(req->file_id));
        reply.status = 0;
    }

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static void handle_read(const fs::ReadRequest *req, i32 reply_channel)
{
    fs::ReadReply reply;
    reply.type = fs::FS_READ_REPLY;
    reply.request_id = req->request_id;

    OpenFile *file = get_file(static_cast<i32>(req->file_id));
    if (!file)
    {
        reply.status = -1;
        reply.bytes_read = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    viperfs::Inode *inode = g_viperfs.read_inode(file->inode_num);
    if (!inode)
    {
        reply.status = -1;
        reply.bytes_read = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Determine offset
    u64 offset = (req->offset < 0) ? file->offset : static_cast<u64>(req->offset);

    // Limit read to inline data size for now
    u32 count = req->count;
    if (count > fs::MAX_INLINE_DATA)
        count = fs::MAX_INLINE_DATA;

    i64 bytes = g_viperfs.read_data(inode, offset, reply.data, count);
    g_viperfs.release_inode(inode);

    if (bytes >= 0)
    {
        reply.status = 0;
        reply.bytes_read = static_cast<u32>(bytes);
        if (req->offset < 0)
            file->offset = offset + static_cast<u64>(bytes);
    }
    else
    {
        reply.status = static_cast<i32>(bytes);
        reply.bytes_read = 0;
    }

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static void handle_write(const fs::WriteRequest *req, i32 reply_channel)
{
    fs::WriteReply reply;
    reply.type = fs::FS_WRITE_REPLY;
    reply.request_id = req->request_id;

    OpenFile *file = get_file(static_cast<i32>(req->file_id));
    if (!file)
    {
        reply.status = -1;
        reply.bytes_written = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    viperfs::Inode *inode = g_viperfs.read_inode(file->inode_num);
    if (!inode)
    {
        reply.status = -1;
        reply.bytes_written = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Determine offset
    u64 offset = (req->offset < 0) ? file->offset : static_cast<u64>(req->offset);

    // Handle append mode
    if (file->flags & fs::open_flags::O_APPEND)
        offset = inode->size;

    // Limit write to inline data size for now
    u32 count = req->count;
    if (count > fs::MAX_INLINE_DATA)
        count = fs::MAX_INLINE_DATA;

    i64 bytes = g_viperfs.write_data(inode, offset, req->data, count);
    g_viperfs.release_inode(inode);

    if (bytes >= 0)
    {
        reply.status = 0;
        reply.bytes_written = static_cast<u32>(bytes);
        if (req->offset < 0)
            file->offset = offset + static_cast<u64>(bytes);
    }
    else
    {
        reply.status = static_cast<i32>(bytes);
        reply.bytes_written = 0;
    }

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static void handle_seek(const fs::SeekRequest *req, i32 reply_channel)
{
    fs::SeekReply reply;
    reply.type = fs::FS_SEEK_REPLY;
    reply.request_id = req->request_id;

    OpenFile *file = get_file(static_cast<i32>(req->file_id));
    if (!file)
    {
        reply.status = -1;
        reply.new_offset = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    i64 new_offset;
    switch (req->whence)
    {
        case fs::seek_whence::SET:
            new_offset = req->offset;
            break;
        case fs::seek_whence::CUR:
            new_offset = static_cast<i64>(file->offset) + req->offset;
            break;
        case fs::seek_whence::END:
        {
            viperfs::Inode *inode = g_viperfs.read_inode(file->inode_num);
            if (!inode)
            {
                reply.status = -1;
                reply.new_offset = 0;
                sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
                return;
            }
            new_offset = static_cast<i64>(inode->size) + req->offset;
            g_viperfs.release_inode(inode);
            break;
        }
        default:
            reply.status = -1;
            reply.new_offset = 0;
            sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
            return;
    }

    if (new_offset < 0)
    {
        reply.status = -1;
        reply.new_offset = 0;
    }
    else
    {
        file->offset = static_cast<u64>(new_offset);
        reply.status = 0;
        reply.new_offset = new_offset;
    }

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static void handle_stat(const fs::StatRequest *req, i32 reply_channel)
{
    fs::StatReply reply;
    reply.type = fs::FS_STAT_REPLY;
    reply.request_id = req->request_id;

    u64 ino = resolve_path(req->path, req->path_len);
    if (ino == 0)
    {
        reply.status = -2; // VERR_NOT_FOUND
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    viperfs::Inode *inode = g_viperfs.read_inode(ino);
    if (!inode)
    {
        reply.status = -1;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    reply.status = 0;
    reply.stat.inode = inode->inode_num;
    reply.stat.size = inode->size;
    reply.stat.blocks = inode->blocks;
    reply.stat.mode = inode->mode;
    reply.stat.atime = inode->atime;
    reply.stat.mtime = inode->mtime;
    reply.stat.ctime = inode->ctime;

    g_viperfs.release_inode(inode);
    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static void handle_fstat(const fs::FstatRequest *req, i32 reply_channel)
{
    fs::FstatReply reply;
    reply.type = fs::FS_FSTAT_REPLY;
    reply.request_id = req->request_id;

    OpenFile *file = get_file(static_cast<i32>(req->file_id));
    if (!file)
    {
        reply.status = -1;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    viperfs::Inode *inode = g_viperfs.read_inode(file->inode_num);
    if (!inode)
    {
        reply.status = -1;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    reply.status = 0;
    reply.stat.inode = inode->inode_num;
    reply.stat.size = inode->size;
    reply.stat.blocks = inode->blocks;
    reply.stat.mode = inode->mode;
    reply.stat.atime = inode->atime;
    reply.stat.mtime = inode->mtime;
    reply.stat.ctime = inode->ctime;

    g_viperfs.release_inode(inode);
    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static void handle_mkdir(const fs::MkdirRequest *req, i32 reply_channel)
{
    fs::MkdirReply reply;
    reply.type = fs::FS_MKDIR_REPLY;
    reply.request_id = req->request_id;

    u64 parent_ino;
    const char *name;
    usize name_len;
    if (!split_path(req->path, req->path_len, &parent_ino, &name, &name_len))
    {
        reply.status = -1;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    viperfs::Inode *parent = g_viperfs.read_inode(parent_ino);
    if (!parent)
    {
        reply.status = -1;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    u64 ino = g_viperfs.create_dir(parent, name, name_len);
    g_viperfs.release_inode(parent);

    reply.status = (ino != 0) ? 0 : -1;
    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static void handle_rmdir(const fs::RmdirRequest *req, i32 reply_channel)
{
    fs::RmdirReply reply;
    reply.type = fs::FS_RMDIR_REPLY;
    reply.request_id = req->request_id;

    u64 parent_ino;
    const char *name;
    usize name_len;
    if (!split_path(req->path, req->path_len, &parent_ino, &name, &name_len))
    {
        reply.status = -1;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    viperfs::Inode *parent = g_viperfs.read_inode(parent_ino);
    if (!parent)
    {
        reply.status = -1;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    bool ok = g_viperfs.rmdir(parent, name, name_len);
    g_viperfs.release_inode(parent);

    reply.status = ok ? 0 : -1;
    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static void handle_unlink(const fs::UnlinkRequest *req, i32 reply_channel)
{
    fs::UnlinkReply reply;
    reply.type = fs::FS_UNLINK_REPLY;
    reply.request_id = req->request_id;

    u64 parent_ino;
    const char *name;
    usize name_len;
    if (!split_path(req->path, req->path_len, &parent_ino, &name, &name_len))
    {
        reply.status = -1;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    viperfs::Inode *parent = g_viperfs.read_inode(parent_ino);
    if (!parent)
    {
        reply.status = -1;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    bool ok = g_viperfs.unlink_file(parent, name, name_len);
    g_viperfs.release_inode(parent);

    reply.status = ok ? 0 : -1;
    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

static void handle_request(const u8 *msg, usize len, i32 reply_channel)
{
    if (len < 4)
        return;

    u32 type = *reinterpret_cast<const u32 *>(msg);

    switch (type)
    {
        case fs::FS_OPEN:
            if (len >= sizeof(fs::OpenRequest))
                handle_open(reinterpret_cast<const fs::OpenRequest *>(msg), reply_channel);
            break;

        case fs::FS_CLOSE:
            if (len >= sizeof(fs::CloseRequest))
                handle_close(reinterpret_cast<const fs::CloseRequest *>(msg), reply_channel);
            break;

        case fs::FS_READ:
            if (len >= sizeof(fs::ReadRequest))
                handle_read(reinterpret_cast<const fs::ReadRequest *>(msg), reply_channel);
            break;

        case fs::FS_WRITE:
            if (len >= sizeof(fs::WriteRequest))
                handle_write(reinterpret_cast<const fs::WriteRequest *>(msg), reply_channel);
            break;

        case fs::FS_SEEK:
            if (len >= sizeof(fs::SeekRequest))
                handle_seek(reinterpret_cast<const fs::SeekRequest *>(msg), reply_channel);
            break;

        case fs::FS_STAT:
            if (len >= sizeof(fs::StatRequest))
                handle_stat(reinterpret_cast<const fs::StatRequest *>(msg), reply_channel);
            break;

        case fs::FS_FSTAT:
            if (len >= sizeof(fs::FstatRequest))
                handle_fstat(reinterpret_cast<const fs::FstatRequest *>(msg), reply_channel);
            break;

        case fs::FS_MKDIR:
            if (len >= sizeof(fs::MkdirRequest))
                handle_mkdir(reinterpret_cast<const fs::MkdirRequest *>(msg), reply_channel);
            break;

        case fs::FS_RMDIR:
            if (len >= sizeof(fs::RmdirRequest))
                handle_rmdir(reinterpret_cast<const fs::RmdirRequest *>(msg), reply_channel);
            break;

        case fs::FS_UNLINK:
            if (len >= sizeof(fs::UnlinkRequest))
                handle_unlink(reinterpret_cast<const fs::UnlinkRequest *>(msg), reply_channel);
            break;

        default:
            debug_print("[fsd] Unknown request type: ");
            debug_print_dec(type);
            debug_print("\n");
            break;
    }
}

/**
 * @brief Server main loop.
 */
static void server_loop()
{
    debug_print("[fsd] Entering server loop\n");

    while (true)
    {
        // Receive a message
        u8 msg_buf[256];
        u32 handles[4];
        u32 handle_count = 4;

        i64 len =
            sys::channel_recv(g_service_channel, msg_buf, sizeof(msg_buf), handles, &handle_count);
        if (len < 0)
        {
            // Would block or error, yield and retry
            sys::yield();
            continue;
        }

        // First handle should be the reply channel
        if (handle_count < 1)
        {
            debug_print("[fsd] No reply channel in request\n");
            continue;
        }

        i32 reply_channel = static_cast<i32>(handles[0]);

        // Handle the request
        handle_request(msg_buf, static_cast<usize>(len), reply_channel);

        // Close the reply channel
        sys::channel_close(reply_channel);
    }
}

/**
 * @brief Main entry point.
 */
extern "C" void _start()
{
    debug_print("[fsd] Filesystem server starting\n");

    // Connect to block device server
    debug_print("[fsd] Connecting to blkd...\n");
    if (!g_blk_client.connect())
    {
        debug_print("[fsd] Failed to connect to blkd\n");
        sys::exit(1);
    }
    debug_print("[fsd] Connected to blkd\n");

    // Mount filesystem
    debug_print("[fsd] Mounting filesystem...\n");
    if (!g_viperfs.mount(&g_blk_client))
    {
        debug_print("[fsd] Failed to mount filesystem\n");
        sys::exit(1);
    }

    debug_print("[fsd] Mounted: ");
    debug_print(g_viperfs.label());
    debug_print(" (");
    debug_print_dec(g_viperfs.total_blocks());
    debug_print(" blocks, ");
    debug_print_dec(g_viperfs.free_blocks());
    debug_print(" free)\n");

    // Create service channel
    auto result = sys::channel_create();
    if (result.error != 0)
    {
        debug_print("[fsd] Failed to create channel\n");
        sys::exit(1);
    }
    g_service_channel = static_cast<i32>(result.val0);

    debug_print("[fsd] Service channel created: ");
    debug_print_dec(static_cast<u64>(g_service_channel));
    debug_print("\n");

    // Register with assign system
    i32 err = sys::assign_set("FSD", static_cast<u32>(g_service_channel));
    if (err != 0)
    {
        debug_print("[fsd] Failed to register assign: ");
        debug_print_dec(static_cast<u64>(-err));
        debug_print("\n");
    }
    else
    {
        debug_print("[fsd] Registered as FSD:\n");
    }

    // Enter the server loop
    server_loop();

    // Should never reach here
    sys::exit(0);
}
