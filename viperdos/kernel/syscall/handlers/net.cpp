//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/net.cpp
// Purpose: Networking syscall handlers (0x50-0x5F).
//
//===----------------------------------------------------------------------===//

#include "../../console/serial.hpp"
#include "../../include/config.hpp"
#include "../../lib/log.hpp"
#include "../../viper/viper.hpp"
#include "handlers_internal.hpp"

#if VIPER_KERNEL_ENABLE_NET
#include "../../net/netstack.hpp"
#endif

namespace syscall {

#if VIPER_KERNEL_ENABLE_NET

SyscallResult sys_socket_create(u64, u64, u64, u64, u64, u64) {
    viper::Viper *v = viper::current();
    if (!v) {
        return err_not_found();
    }

    i64 result = net::tcp::socket_create(static_cast<u32>(v->id));
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_socket_connect(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    i32 sock = static_cast<i32>(a0);
    u32 ip_raw = static_cast<u32>(a1);
    u16 port_be = static_cast<u16>(a2);

    u16 port = net::ntohs(port_be);

    viper::Viper *v = viper::current();
    if (!v || !net::tcp::socket_owned_by(sock, static_cast<u32>(v->id))) {
        return err_invalid_handle();
    }

    net::Ipv4Addr ip;
    ip.bytes[0] = ip_raw & 0xFF;
    ip.bytes[1] = (ip_raw >> 8) & 0xFF;
    ip.bytes[2] = (ip_raw >> 16) & 0xFF;
    ip.bytes[3] = (ip_raw >> 24) & 0xFF;

#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    if (log::get_level() == log::Level::Debug) {
        serial::puts("[syscall] socket_connect: sock=");
        serial::put_dec(sock);
        serial::puts(" ip=");
        serial::put_ipv4(ip.bytes);
        serial::puts(" port=");
        serial::put_dec(port);
        serial::putc('\n');
    }
#endif

    bool result = net::tcp::socket_connect(sock, ip, port);

#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    if (log::get_level() == log::Level::Debug) {
        serial::puts("[syscall] socket_connect: result=");
        serial::puts(result ? "true" : "false");
        serial::putc('\n');
    }
#endif

    if (!result) {
        return err_code(error::VERR_CONNECTION);
    }
    return SyscallResult::ok();
}

SyscallResult sys_socket_send(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    i32 sock = static_cast<i32>(a0);
    const void *buf = reinterpret_cast<const void *>(a1);
    usize len = static_cast<usize>(a2);

    viper::Viper *v = viper::current();
    if (!v || !net::tcp::socket_owned_by(sock, static_cast<u32>(v->id))) {
        return err_invalid_handle();
    }

#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    if (log::get_level() == log::Level::Debug) {
        serial::puts("[syscall] socket_send: sock=");
        serial::put_dec(sock);
        serial::puts(" len=");
        serial::put_dec(len);
        serial::putc('\n');
    }
#endif

    VALIDATE_USER_READ(buf, len);

    i64 result = net::tcp::socket_send(sock, buf, len);

#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    if (log::get_level() == log::Level::Debug) {
        serial::puts("[syscall] socket_send: result=");
        serial::put_dec(result);
        serial::putc('\n');
    }
#endif

    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_socket_recv(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    i32 sock = static_cast<i32>(a0);
    void *buf = reinterpret_cast<void *>(a1);
    usize len = static_cast<usize>(a2);

    viper::Viper *v = viper::current();
    if (!v || !net::tcp::socket_owned_by(sock, static_cast<u32>(v->id))) {
        return err_invalid_handle();
    }

#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    if (log::get_level() == log::Level::Debug) {
        serial::puts("[syscall] socket_recv: sock=");
        serial::put_dec(sock);
        serial::puts(" len=");
        serial::put_dec(len);
        serial::putc('\n');
    }
#endif

    VALIDATE_USER_WRITE(buf, len);

    net::network_poll();

    i64 result = net::tcp::socket_recv(sock, buf, len);

#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    if (log::get_level() == log::Level::Debug) {
        serial::puts("[syscall] socket_recv: result=");
        serial::put_dec(result);
        serial::putc('\n');
    }
#endif

    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_socket_close(u64 a0, u64, u64, u64, u64, u64) {
    i32 sock = static_cast<i32>(a0);

    viper::Viper *v = viper::current();
    if (!v || !net::tcp::socket_owned_by(sock, static_cast<u32>(v->id))) {
        return err_invalid_handle();
    }

    net::tcp::socket_close(sock);
    return SyscallResult::ok();
}

SyscallResult sys_dns_resolve(u64 a0, u64 a1, u64, u64, u64, u64) {
    const char *hostname = reinterpret_cast<const char *>(a0);
    u32 *ip_out = reinterpret_cast<u32 *>(a1);

    VALIDATE_USER_STRING(hostname, 256);
    VALIDATE_USER_WRITE(ip_out, sizeof(u32));

    net::Ipv4Addr result_ip;
    if (!net::dns::resolve(hostname, &result_ip, 5000)) {
        return err_not_found();
    }

    *ip_out = static_cast<u32>(result_ip.bytes[0]) | (static_cast<u32>(result_ip.bytes[1]) << 8) |
              (static_cast<u32>(result_ip.bytes[2]) << 16) |
              (static_cast<u32>(result_ip.bytes[3]) << 24);
    return SyscallResult::ok();
}

SyscallResult sys_socket_poll(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    i32 sock = static_cast<i32>(a0);
    u32 *out_flags = reinterpret_cast<u32 *>(a1);
    u32 *out_rx_available = reinterpret_cast<u32 *>(a2);

    viper::Viper *v = viper::current();
    if (!v || !net::tcp::socket_owned_by(sock, static_cast<u32>(v->id))) {
        return err_invalid_handle();
    }

    VALIDATE_USER_WRITE(out_flags, sizeof(u32));
    VALIDATE_USER_WRITE(out_rx_available, sizeof(u32));

    net::network_poll();

    i32 result = net::tcp::socket_status(sock, out_flags, out_rx_available);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok();
}

#else // !VIPER_KERNEL_ENABLE_NET

SyscallResult sys_socket_create(u64, u64, u64, u64, u64, u64) {
    return err_code(error::VERR_NOT_SUPPORTED);
}

SyscallResult sys_socket_connect(u64, u64, u64, u64, u64, u64) {
    return err_code(error::VERR_NOT_SUPPORTED);
}

SyscallResult sys_socket_send(u64, u64, u64, u64, u64, u64) {
    return err_code(error::VERR_NOT_SUPPORTED);
}

SyscallResult sys_socket_recv(u64, u64, u64, u64, u64, u64) {
    return err_code(error::VERR_NOT_SUPPORTED);
}

SyscallResult sys_socket_close(u64, u64, u64, u64, u64, u64) {
    return err_code(error::VERR_NOT_SUPPORTED);
}

SyscallResult sys_dns_resolve(u64, u64, u64, u64, u64, u64) {
    return err_code(error::VERR_NOT_SUPPORTED);
}

SyscallResult sys_socket_poll(u64, u64, u64, u64, u64, u64) {
    return err_code(error::VERR_NOT_SUPPORTED);
}

#endif // VIPER_KERNEL_ENABLE_NET

} // namespace syscall
