#include "fsclient.hpp"

#include "servers/fsd/fs_protocol.hpp"

namespace fsclient
{

static usize bounded_strlen(const char *s, usize max_len)
{
    if (!s)
        return 0;
    usize n = 0;
    while (n < max_len && s[n])
    {
        n++;
    }
    return n;
}

static i64 recv_reply_blocking(i32 ch, void *buf, usize buf_len)
{
    while (true)
    {
        u32 handles[4];
        u32 handle_count = 4;
        i64 n = sys::channel_recv(ch, buf, buf_len, handles, &handle_count);
        if (n == VERR_WOULD_BLOCK)
        {
            sys::yield();
            continue;
        }
        if (n >= 0 && handle_count != 0)
        {
            // This client currently only supports inline replies. Close any unexpected
            // transferred handles to avoid capability table exhaustion.
            for (u32 i = 0; i < handle_count; i++)
            {
                if (handles[i] == 0)
                    continue;
                i32 close_err = sys::shm_close(handles[i]);
                if (close_err != 0)
                {
                    (void)sys::cap_revoke(handles[i]);
                }
            }
            return VERR_NOT_SUPPORTED;
        }
        return n;
    }
}

i32 Client::connect()
{
    if (channel_ >= 0)
    {
        return 0;
    }

    u32 handle = 0;
    i32 err = sys::assign_get("FSD", &handle);
    if (err != 0)
    {
        return err;
    }

    channel_ = static_cast<i32>(handle);
    return 0;
}

i32 Client::open(const char *path, u32 flags, u32 *out_file_id)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    usize path_len = bounded_strlen(path, fs::MAX_PATH_LEN + 1);
    if (path_len == 0 || path_len > fs::MAX_PATH_LEN)
    {
        return VERR_INVALID_ARG;
    }

    fs::OpenRequest req = {};
    req.type = fs::FS_OPEN;
    req.request_id = next_request_id_++;
    req.flags = flags;
    req.path_len = static_cast<u16>(path_len);
    for (usize i = 0; i < path_len; i++)
    {
        req.path[i] = path[i];
    }

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    fs::OpenReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }

    if (reply.status == 0 && out_file_id)
    {
        *out_file_id = reply.file_id;
    }
    return reply.status;
}

i32 Client::close(u32 file_id)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    fs::CloseRequest req = {};
    req.type = fs::FS_CLOSE;
    req.request_id = next_request_id_++;
    req.file_id = file_id;

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    fs::CloseReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }

    return reply.status;
}

i32 Client::stat(const char *path, sys::Stat *out)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }
    if (!out)
    {
        return VERR_INVALID_ARG;
    }

    usize path_len = bounded_strlen(path, fs::MAX_PATH_LEN + 1);
    if (path_len == 0 || path_len > fs::MAX_PATH_LEN)
    {
        return VERR_INVALID_ARG;
    }

    fs::StatRequest req = {};
    req.type = fs::FS_STAT;
    req.request_id = next_request_id_++;
    req.path_len = static_cast<u16>(path_len);
    for (usize i = 0; i < path_len; i++)
    {
        req.path[i] = path[i];
    }

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    fs::StatReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }
    if (reply.status != 0)
    {
        return reply.status;
    }

    out->ino = reply.stat.inode;
    out->mode = reply.stat.mode;
    out->size = reply.stat.size;
    out->blocks = reply.stat.blocks;
    out->atime = reply.stat.atime;
    out->mtime = reply.stat.mtime;
    out->ctime = reply.stat.ctime;

    return 0;
}

i32 Client::fstat(u32 file_id, sys::Stat *out)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }
    if (!out)
    {
        return VERR_INVALID_ARG;
    }

    fs::FstatRequest req = {};
    req.type = fs::FS_FSTAT;
    req.request_id = next_request_id_++;
    req.file_id = file_id;

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    fs::FstatReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }
    if (reply.status != 0)
    {
        return reply.status;
    }

    out->ino = reply.stat.inode;
    out->mode = reply.stat.mode;
    out->size = reply.stat.size;
    out->blocks = reply.stat.blocks;
    out->atime = reply.stat.atime;
    out->mtime = reply.stat.mtime;
    out->ctime = reply.stat.ctime;

    return 0;
}

i32 Client::mkdir(const char *path)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    usize path_len = bounded_strlen(path, fs::MAX_PATH_LEN + 1);
    if (path_len == 0 || path_len > fs::MAX_PATH_LEN)
    {
        return VERR_INVALID_ARG;
    }

    fs::MkdirRequest req = {};
    req.type = fs::FS_MKDIR;
    req.request_id = next_request_id_++;
    req.path_len = static_cast<u16>(path_len);
    for (usize i = 0; i < path_len; i++)
    {
        req.path[i] = path[i];
    }

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    fs::MkdirReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }

    return reply.status;
}

i32 Client::rmdir(const char *path)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    usize path_len = bounded_strlen(path, fs::MAX_PATH_LEN + 1);
    if (path_len == 0 || path_len > fs::MAX_PATH_LEN)
    {
        return VERR_INVALID_ARG;
    }

    fs::RmdirRequest req = {};
    req.type = fs::FS_RMDIR;
    req.request_id = next_request_id_++;
    req.path_len = static_cast<u16>(path_len);
    for (usize i = 0; i < path_len; i++)
    {
        req.path[i] = path[i];
    }

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    fs::RmdirReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }

    return reply.status;
}

i32 Client::unlink(const char *path)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    usize path_len = bounded_strlen(path, fs::MAX_PATH_LEN + 1);
    if (path_len == 0 || path_len > fs::MAX_PATH_LEN)
    {
        return VERR_INVALID_ARG;
    }

    fs::UnlinkRequest req = {};
    req.type = fs::FS_UNLINK;
    req.request_id = next_request_id_++;
    req.path_len = static_cast<u16>(path_len);
    for (usize i = 0; i < path_len; i++)
    {
        req.path[i] = path[i];
    }

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    fs::UnlinkReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }

    return reply.status;
}

i32 Client::rename(const char *old_path, const char *new_path)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    usize old_len = bounded_strlen(old_path, fs::MAX_PATH_LEN + 1);
    usize new_len = bounded_strlen(new_path, fs::MAX_PATH_LEN + 1);
    if (old_len == 0 || new_len == 0 || old_len > fs::MAX_PATH_LEN || new_len > fs::MAX_PATH_LEN)
    {
        return VERR_INVALID_ARG;
    }

    fs::RenameRequest req = {};
    if (old_len + new_len > sizeof(req.paths))
    {
        return VERR_INVALID_ARG;
    }
    req.type = fs::FS_RENAME;
    req.request_id = next_request_id_++;
    req.old_path_len = static_cast<u16>(old_len);
    req.new_path_len = static_cast<u16>(new_len);
    for (usize i = 0; i < old_len; i++)
    {
        req.paths[i] = old_path[i];
    }
    for (usize i = 0; i < new_len; i++)
    {
        req.paths[old_len + i] = new_path[i];
    }

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    fs::RenameReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }

    return reply.status;
}

i32 Client::readdir_one(u32 dir_file_id, u64 *out_ino, u8 *out_type, char *name_out, u32 name_cap)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    fs::ReaddirRequest req = {};
    req.type = fs::FS_READDIR;
    req.request_id = next_request_id_++;
    req.file_id = dir_file_id;
    req.max_entries = 1;

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return static_cast<i32>(ch.error);
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return static_cast<i32>(send_err);
    }

    fs::ReaddirReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return static_cast<i32>(n);
    }
    if (reply.status != 0)
    {
        return reply.status;
    }
    if (reply.entry_count == 0)
    {
        return 0;
    }

    const fs::DirEntryInfo &ent = reply.entries[0];
    if (out_ino)
        *out_ino = ent.inode;
    if (out_type)
        *out_type = ent.type;

    if (name_out && name_cap > 0)
    {
        u32 to_copy = ent.name_len;
        if (to_copy >= name_cap)
            to_copy = name_cap - 1;
        if (to_copy > sizeof(ent.name))
            to_copy = sizeof(ent.name);
        for (u32 i = 0; i < to_copy; i++)
        {
            name_out[i] = ent.name[i];
        }
        name_out[to_copy] = '\0';
    }

    return 1;
}

i32 Client::file_size(u32 file_id, u64 *out_size)
{
    sys::Stat st = {};
    i32 err = fstat(file_id, &st);
    if (err == 0 && out_size)
    {
        *out_size = st.size;
    }
    return err;
}

i64 Client::read(u32 file_id, void *buf, u32 count)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }
    if (!buf && count != 0)
    {
        return VERR_INVALID_ARG;
    }

    u8 *out = static_cast<u8 *>(buf);
    u32 total = 0;

    while (total < count)
    {
        u32 chunk = count - total;
        if (chunk > fs::MAX_INLINE_DATA)
        {
            chunk = fs::MAX_INLINE_DATA;
        }

        fs::ReadRequest req = {};
        req.type = fs::FS_READ;
        req.request_id = next_request_id_++;
        req.file_id = file_id;
        req.count = chunk;
        req.offset = -1;

        auto ch = sys::channel_create();
        if (!ch.ok())
        {
            return ch.error;
        }
        i32 reply_send = static_cast<i32>(ch.val0);
        i32 reply_recv = static_cast<i32>(ch.val1);

        u32 send_handles[1] = {static_cast<u32>(reply_send)};
        i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
        if (send_err != 0)
        {
            sys::channel_close(reply_send);
            sys::channel_close(reply_recv);
            return send_err;
        }

        fs::ReadReply reply = {};
        i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
        sys::channel_close(reply_recv);
        if (n < 0)
        {
            return n;
        }
        if (reply.status != 0)
        {
            return reply.status;
        }

        u32 got = reply.bytes_read;
        if (got > chunk)
        {
            got = chunk;
        }
        for (u32 i = 0; i < got; i++)
        {
            out[total + i] = reply.data[i];
        }

        total += got;
        if (got == 0 || got < chunk)
        {
            break;
        }
    }

    return static_cast<i64>(total);
}

i64 Client::write(u32 file_id, const void *buf, u32 count)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }
    if (!buf && count != 0)
    {
        return VERR_INVALID_ARG;
    }

    const u8 *in = static_cast<const u8 *>(buf);
    u32 total = 0;

    while (total < count)
    {
        u32 chunk = count - total;
        if (chunk > fs::MAX_INLINE_DATA)
        {
            chunk = fs::MAX_INLINE_DATA;
        }

        fs::WriteRequest req = {};
        req.type = fs::FS_WRITE;
        req.request_id = next_request_id_++;
        req.file_id = file_id;
        req.count = chunk;
        req.offset = -1;
        for (u32 i = 0; i < chunk; i++)
        {
            req.data[i] = in[total + i];
        }

        auto ch = sys::channel_create();
        if (!ch.ok())
        {
            return ch.error;
        }
        i32 reply_send = static_cast<i32>(ch.val0);
        i32 reply_recv = static_cast<i32>(ch.val1);

        u32 send_handles[1] = {static_cast<u32>(reply_send)};
        i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
        if (send_err != 0)
        {
            sys::channel_close(reply_send);
            sys::channel_close(reply_recv);
            return send_err;
        }

        fs::WriteReply reply = {};
        i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
        sys::channel_close(reply_recv);
        if (n < 0)
        {
            return n;
        }
        if (reply.status != 0)
        {
            return reply.status;
        }

        u32 wrote = reply.bytes_written;
        if (wrote > chunk)
        {
            wrote = chunk;
        }
        total += wrote;
        if (wrote == 0 || wrote < chunk)
        {
            break;
        }
    }

    return static_cast<i64>(total);
}

i64 Client::seek(u32 file_id, i64 offset, i32 whence, i64 *out_new_offset)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
    }

    fs::SeekRequest req = {};
    req.type = fs::FS_SEEK;
    req.request_id = next_request_id_++;
    req.file_id = file_id;
    req.whence = whence;
    req.offset = offset;

    auto ch = sys::channel_create();
    if (!ch.ok())
    {
        return ch.error;
    }
    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(channel_, &req, sizeof(req), send_handles, 1);
    if (send_err != 0)
    {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        return send_err;
    }

    fs::SeekReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    if (n < 0)
    {
        return n;
    }
    if (reply.status != 0)
    {
        return reply.status;
    }

    if (out_new_offset)
    {
        *out_new_offset = reply.new_offset;
    }
    return reply.new_offset;
}

} // namespace fsclient
