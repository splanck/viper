//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/signal.cpp
// Purpose: Signal syscall handlers (0x90-0x9F).
//
//===----------------------------------------------------------------------===//

#include "../../sched/signal.hpp"
#include "../../console/serial.hpp"
#include "../../sched/task.hpp"
#include "handlers_internal.hpp"

namespace syscall {

SyscallResult sys_sigaction(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    i32 signum = static_cast<i32>(a0);
    const signal::SigAction *act = reinterpret_cast<const signal::SigAction *>(a1);
    signal::SigAction *oldact = reinterpret_cast<signal::SigAction *>(a2);

    // Validate signal number
    if (signum <= 0 || signum >= signal::sig::NSIG) {
        return err_invalid_arg();
    }

    // SIGKILL and SIGSTOP cannot be caught or ignored
    if (signum == signal::sig::SIGKILL || signum == signal::sig::SIGSTOP) {
        return err_invalid_arg();
    }

    // Validate user pointers
    if (act && !validate_user_read(act, sizeof(signal::SigAction))) {
        return err_invalid_arg();
    }
    if (oldact && !validate_user_write(oldact, sizeof(signal::SigAction))) {
        return err_invalid_arg();
    }

    task::Task *t = task::current();
    if (!t) {
        return err_not_found();
    }

    // Store old action if requested
    if (oldact) {
        oldact->handler = t->signals.handlers[signum];
        oldact->flags = t->signals.handler_flags[signum];
        oldact->mask = t->signals.handler_mask[signum];
    }

    // Set new action if provided
    if (act) {
        t->signals.handlers[signum] = act->handler;
        t->signals.handler_flags[signum] = act->flags;
        t->signals.handler_mask[signum] = act->mask;
    }

    return SyscallResult::ok();
}

SyscallResult sys_sigprocmask(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    i32 how = static_cast<i32>(a0);
    const u32 *set = reinterpret_cast<const u32 *>(a1);
    u32 *oldset = reinterpret_cast<u32 *>(a2);

    // Validate user pointers
    if (set && !validate_user_read(set, sizeof(u32))) {
        return err_invalid_arg();
    }
    if (oldset && !validate_user_write(oldset, sizeof(u32))) {
        return err_invalid_arg();
    }

    task::Task *t = task::current();
    if (!t) {
        return err_not_found();
    }

    // Store old mask if requested
    if (oldset) {
        *oldset = t->signals.blocked;
    }

    // Apply new mask if provided
    if (set) {
        u32 new_mask = *set;

        // Cannot block SIGKILL or SIGSTOP
        new_mask &= ~((1u << signal::sig::SIGKILL) | (1u << signal::sig::SIGSTOP));

        switch (how) {
            case 0: // SIG_BLOCK - add signals to blocked set
                t->signals.blocked |= new_mask;
                break;
            case 1: // SIG_UNBLOCK - remove signals from blocked set
                t->signals.blocked &= ~new_mask;
                break;
            case 2: // SIG_SETMASK - set blocked set to new mask
                t->signals.blocked = new_mask;
                break;
            default:
                return err_invalid_arg();
        }
    }

    return SyscallResult::ok();
}

SyscallResult sys_sigreturn(u64, u64, u64, u64, u64, u64) {
    task::Task *t = task::current();
    if (!t) {
        return err_not_found();
    }

    // Check if we have a saved frame from signal delivery
    if (!t->signals.saved_frame) {
        serial::puts("[signal] sigreturn with no saved frame\n");
        return err_invalid_arg();
    }

    // Restore the original context
    serial::puts("[signal] sigreturn - restoring context\n");
    t->signals.saved_frame = nullptr;

    return SyscallResult::ok();
}

SyscallResult sys_kill(u64 a0, u64 a1, u64, u64, u64, u64) {
    i64 pid = static_cast<i64>(a0);
    i32 signum = static_cast<i32>(a1);

    // Validate signal number
    if (signum <= 0 || signum >= signal::sig::NSIG) {
        return err_invalid_arg();
    }

    // Special cases for pid
    if (pid == 0) {
        // Send to all processes in caller's process group (not implemented)
        return err_not_supported();
    } else if (pid == -1) {
        // Send to all processes (not implemented)
        return err_not_supported();
    } else if (pid < -1) {
        // Send to process group (not implemented)
        return err_not_supported();
    }

    // Find target task
    task::Task *target = task::get_by_id(static_cast<u32>(pid));
    if (!target) {
        return err_not_found();
    }

    // Permission check: caller can only signal tasks in the same process
    // (same viper), or kernel tasks can signal anyone
    task::Task *caller = task::current();
    if (caller && caller->viper) {
        // User task - must be same viper process or signaling self
        if (target->viper != caller->viper && target->id != caller->id) {
            // Check if caller is parent (allowed to signal children)
            if (target->parent_id != caller->id) {
                return err_permission();
            }
        }
    }
    // Kernel tasks (no viper) can signal anyone

    // Send the signal
    i32 result = signal::send_signal(target, signum);
    if (result < 0) {
        return err_permission();
    }

    return SyscallResult::ok();
}

SyscallResult sys_sigpending(u64 a0, u64, u64, u64, u64, u64) {
    u32 *set = reinterpret_cast<u32 *>(a0);

    VALIDATE_USER_WRITE(set, sizeof(u32));

    task::Task *t = task::current();
    if (!t) {
        return err_not_found();
    }

    *set = t->signals.pending;
    return SyscallResult::ok();
}

} // namespace syscall
