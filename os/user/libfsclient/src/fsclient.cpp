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

    usize path_len = bounded_strlen(path, fs::MAX_PATH_LEN);
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

i32 Client::file_size(u32 file_id, u64 *out_size)
{
    i32 err = connect();
    if (err != 0)
    {
        return err;
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

    if (reply.status == 0 && out_size)
    {
        *out_size = reply.stat.size;
    }
    return reply.status;
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
