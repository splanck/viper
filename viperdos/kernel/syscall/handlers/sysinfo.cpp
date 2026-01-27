//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/sysinfo.cpp
// Purpose: System info syscall handlers (0xE0-0xEF).
//
//===----------------------------------------------------------------------===//

#include "../../drivers/virtio/rng.hpp"
#include "../../include/config.hpp"
#include "../../include/viperdos/mem_info.hpp"
#include "../../include/viperdos/net_stats.hpp"
#include "../../mm/pmm.hpp"
#include "handlers_internal.hpp"

#if VIPER_KERNEL_ENABLE_NET
#include "../../net/netstack.hpp"
#endif

namespace syscall {

SyscallResult sys_mem_info(u64 a0, u64, u64, u64, u64, u64) {
    MemInfo *info = reinterpret_cast<MemInfo *>(a0);

    VALIDATE_USER_WRITE(info, sizeof(MemInfo));

    info->total_pages = pmm::get_total_pages();
    info->free_pages = pmm::get_free_pages();
    info->used_pages = info->total_pages - info->free_pages;
    info->page_size = 4096;

    // Populate byte fields from page counts
    info->total_bytes = info->total_pages * info->page_size;
    info->free_bytes = info->free_pages * info->page_size;
    info->used_bytes = info->used_pages * info->page_size;

    return SyscallResult::ok();
}

#if VIPER_KERNEL_ENABLE_NET
SyscallResult sys_net_stats(u64 a0, u64, u64, u64, u64, u64) {
    NetStats *stats = reinterpret_cast<NetStats *>(a0);

    VALIDATE_USER_WRITE(stats, sizeof(NetStats));

    net::get_stats(stats);
    return SyscallResult::ok();
}

SyscallResult sys_ping(u64 a0, u64 a1, u64, u64, u64, u64) {
    u32 ip_be = static_cast<u32>(a0);
    u32 timeout_ms = static_cast<u32>(a1);

    if (timeout_ms == 0)
        timeout_ms = 5000; // Default 5 second timeout

    // Convert from big-endian to our Ipv4Addr format
    net::Ipv4Addr dst;
    dst.bytes[0] = (ip_be >> 24) & 0xFF;
    dst.bytes[1] = (ip_be >> 16) & 0xFF;
    dst.bytes[2] = (ip_be >> 8) & 0xFF;
    dst.bytes[3] = ip_be & 0xFF;

    i32 rtt = net::icmp::ping(dst, timeout_ms);
    if (rtt < 0) {
        return SyscallResult::err(rtt);
    }
    return SyscallResult::ok(static_cast<u64>(rtt));
}
#else
SyscallResult sys_net_stats(u64, u64, u64, u64, u64, u64) {
    return err_not_supported();
}

SyscallResult sys_ping(u64, u64, u64, u64, u64, u64) {
    return err_not_supported();
}
#endif

SyscallResult sys_device_list(u64 a0, u64 a1, u64, u64, u64, u64) {
    struct DeviceInfo {
        char name[32];
        char type[16];
        u32 flags;
        u32 irq;
    };

    // Helper to copy a string into a fixed-size buffer
    auto copy_str = [](char *dst, const char *src, usize max) {
        usize i = 0;
        while (i < max - 1 && src[i]) {
            dst[i] = src[i];
            i++;
        }
        dst[i] = '\0';
    };

    DeviceInfo *buf = reinterpret_cast<DeviceInfo *>(a0);
    usize max_count = static_cast<usize>(a1);

    if (max_count == 0) {
        // Just return the count
        return SyscallResult::ok(3); // RAM, timer, serial
    }

    if (!validate_user_write(buf, max_count * sizeof(DeviceInfo))) {
        return err_invalid_arg();
    }

    usize count = 0;

    // System RAM
    if (count < max_count) {
        copy_str(buf[count].name, "System RAM", sizeof(buf[count].name));
        copy_str(buf[count].type, "memory", sizeof(buf[count].type));
        buf[count].flags = 1;
        buf[count].irq = 0;
        count++;
    }

    // ARM timer
    if (count < max_count) {
        copy_str(buf[count].name, "ARM Timer", sizeof(buf[count].name));
        copy_str(buf[count].type, "timer", sizeof(buf[count].type));
        buf[count].flags = 1;
        buf[count].irq = 30;
        count++;
    }

    // PL011 UART
    if (count < max_count) {
        copy_str(buf[count].name, "PL011 UART", sizeof(buf[count].name));
        copy_str(buf[count].type, "serial", sizeof(buf[count].type));
        buf[count].flags = 1;
        buf[count].irq = 33;
        count++;
    }

    return SyscallResult::ok(count);
}

SyscallResult sys_getrandom(u64 a0, u64 a1, u64, u64, u64, u64) {
    u8 *buf = reinterpret_cast<u8 *>(a0);
    usize len = static_cast<usize>(a1);

    if (len == 0) {
        return SyscallResult::ok(0);
    }

    if (!validate_user_write(buf, len)) {
        return err_invalid_arg();
    }

    if (!virtio::rng::is_available()) {
        return SyscallResult::err(error::VERR_NOT_SUPPORTED);
    }

    usize got = virtio::rng::get_bytes(buf, len);
    return SyscallResult::ok(static_cast<u64>(got));
}

} // namespace syscall
