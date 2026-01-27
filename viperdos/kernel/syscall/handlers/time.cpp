//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/time.cpp
// Purpose: Time syscall handlers (0x30-0x3F).
//
//===----------------------------------------------------------------------===//

#include "../../arch/aarch64/timer.hpp"
#include "../../drivers/pl031.hpp"
#include "../../ipc/poll.hpp"
#include "../../sched/task.hpp"
#include "handlers_internal.hpp"

namespace syscall {

SyscallResult sys_time_now(u64, u64, u64, u64, u64, u64) {
    return SyscallResult::ok(timer::get_ms());
}

SyscallResult sys_sleep(u64 a0, u64, u64, u64, u64, u64) {
    u64 ms = a0;
    if (ms == 0) {
        task::yield();
    } else {
        poll::sleep_ms(ms);
    }
    return SyscallResult::ok();
}

SyscallResult sys_time_now_ns(u64, u64, u64, u64, u64, u64) {
    return SyscallResult::ok(timer::get_ns());
}

SyscallResult sys_rtc_read(u64, u64, u64, u64, u64, u64) {
    if (!pl031::is_available()) {
        return SyscallResult::err(error::VERR_NOT_SUPPORTED);
    }
    return SyscallResult::ok(pl031::read_time());
}

} // namespace syscall
