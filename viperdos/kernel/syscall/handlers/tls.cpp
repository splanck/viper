//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/tls.cpp
// Purpose: TLS syscall handlers (0xD0-0xDF).
//
//===----------------------------------------------------------------------===//

#include "handlers_internal.hpp"
#include "../../include/config.hpp"
#include "../../lib/spinlock.hpp"

#if VIPER_KERNEL_ENABLE_TLS
#include "../../viper/tls.hpp"
#include "../../include/viperdos/tls_info.hpp"
#endif

namespace syscall
{

#if VIPER_KERNEL_ENABLE_TLS

static constexpr int MAX_TLS_SESSIONS = 16;
static viper::tls::TlsSession tls_sessions[MAX_TLS_SESSIONS];
static bool tls_session_active[MAX_TLS_SESSIONS] = {false};
static kernel::Spinlock tls_lock;

SyscallResult sys_tls_create(u64 a0, u64, u64, u64, u64, u64)
{
    i32 socket_fd = static_cast<i32>(a0);

    kernel::SpinlockGuard lock(tls_lock);

    int slot = -1;
    for (int i = 0; i < MAX_TLS_SESSIONS; i++)
    {
        if (!tls_session_active[i])
        {
            slot = i;
            break;
        }
    }

    if (slot < 0)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    if (!viper::tls::tls_init(&tls_sessions[slot], socket_fd, nullptr))
    {
        return SyscallResult::err(error::VERR_IO);
    }

    tls_session_active[slot] = true;
    return SyscallResult::ok(static_cast<u64>(slot));
}

SyscallResult sys_tls_handshake(u64 a0, u64 a1, u64, u64, u64, u64)
{
    i32 session_id = static_cast<i32>(a0);
    const char *hostname = reinterpret_cast<const char *>(a1);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (hostname && validate_user_string(hostname, 256) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (viper::tls::tls_handshake(&tls_sessions[session_id]))
    {
        return SyscallResult::ok();
    }
    return SyscallResult::err(error::VERR_IO);
}

SyscallResult sys_tls_send(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 session_id = static_cast<i32>(a0);
    const void *data = reinterpret_cast<const void *>(a1);
    usize len = static_cast<usize>(a2);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (!validate_user_read(data, len))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = viper::tls::tls_send(&tls_sessions[session_id], data, len);
    if (result < 0)
    {
        return SyscallResult::err(error::VERR_IO);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

SyscallResult sys_tls_recv(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 session_id = static_cast<i32>(a0);
    void *buf = reinterpret_cast<void *>(a1);
    usize len = static_cast<usize>(a2);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (!validate_user_write(buf, len))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = viper::tls::tls_recv(&tls_sessions[session_id], buf, len);
    if (result < 0)
    {
        return SyscallResult::err(error::VERR_IO);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

SyscallResult sys_tls_close(u64 a0, u64, u64, u64, u64, u64)
{
    i32 session_id = static_cast<i32>(a0);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    kernel::SpinlockGuard lock(tls_lock);
    viper::tls::tls_close(&tls_sessions[session_id]);
    tls_session_active[session_id] = false;
    return SyscallResult::ok();
}

SyscallResult sys_tls_info(u64 a0, u64 a1, u64, u64, u64, u64)
{
    i32 session_id = static_cast<i32>(a0);
    TLSInfo *out_info = reinterpret_cast<TLSInfo *>(a1);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (!validate_user_write(out_info, sizeof(TLSInfo)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (viper::tls::tls_get_info(&tls_sessions[session_id], out_info))
    {
        return SyscallResult::ok();
    }
    return SyscallResult::err(error::VERR_IO);
}

#else // !VIPER_KERNEL_ENABLE_TLS

SyscallResult sys_tls_create(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

SyscallResult sys_tls_handshake(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

SyscallResult sys_tls_send(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

SyscallResult sys_tls_recv(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

SyscallResult sys_tls_close(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

SyscallResult sys_tls_info(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

#endif // VIPER_KERNEL_ENABLE_TLS

} // namespace syscall
