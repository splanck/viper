#pragma once

#include "syscall.hpp"

namespace fsclient
{

class Client
{
  public:
    Client() = default;
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    ~Client()
    {
        if (channel_ >= 0)
        {
            sys::channel_close(channel_);
            channel_ = -1;
        }
    }

    i32 connect();

    i32 open(const char *path, u32 flags, u32 *out_file_id);
    i32 close(u32 file_id);

    i32 stat(const char *path, sys::Stat *out);
    i32 fstat(u32 file_id, sys::Stat *out);

    i32 mkdir(const char *path);
    i32 rmdir(const char *path);
    i32 unlink(const char *path);
    i32 rename(const char *old_path, const char *new_path);

    i32 readdir_one(u32 dir_file_id, u64 *out_ino, u8 *out_type, char *name_out, u32 name_cap);

    i32 file_size(u32 file_id, u64 *out_size);

    i64 read(u32 file_id, void *buf, u32 count);
    i64 write(u32 file_id, const void *buf, u32 count);

    i64 seek(u32 file_id, i64 offset, i32 whence, i64 *out_new_offset);

    i32 fsync(u32 file_id);

  private:
    i32 channel_ = -1;
    u32 next_request_id_ = 1;
};

} // namespace fsclient
