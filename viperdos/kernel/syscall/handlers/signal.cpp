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

#include "handlers_internal.hpp"
#include "../../console/serial.hpp"
#include "../../sched/signal.hpp"
#include "../../sched/task.hpp"

namespace syscall
{

SyscallResult sys_sigaction(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 signum = static_cast<i32>(a0);
    const signal::SigAction *act = reinterpret_cast<const signal::SigAction *>(a1);
    signal::SigAction *oldact = reinterpret_cast<signal::SigAction *>(a2);

    // Validate signal number
    if (signum <= 0 || signum >= signal::sig::NSIG)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // SIGKILL and SIGSTOP cannot be caught or ignored
    if (signum == signal::sig::SIGKILL || signum == signal::sig::SIGSTOP)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // Validate user pointers
    if (act && !validate_user_read(act, sizeof(signal::SigAction)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (oldact && !validate_user_write(oldact, sizeof(signal::SigAction)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Store old action if requested
    if (oldact)
    {
        oldact->handler = t->signals.handlers[signum];
        oldact->flags = t->signals.handler_flags[signum];
        oldact->mask = t->signals.handler_mask[signum];
    }

    // Set new action if provided
    if (act)
    {
        t->signals.handlers[signum] = act->handler;
        t->signals.handler_flags[signum] = act->flags;
        t->signals.handler_mask[signum] = act->mask;
    }

    return SyscallResult::ok();
}

SyscallResult sys_sigprocmask(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 how = static_cast<i32>(a0);
    const u32 *set = reinterpret_cast<const u32 *>(a1);
    u32 *oldset = reinterpret_cast<u32 *>(a2);

    // Validate user pointers
    if (set && !validate_user_read(set, sizeof(u32)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (oldset && !validate_user_write(oldset, sizeof(u32)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Store old mask if requested
    if (oldset)
    {
        *oldset = t->signals.blocked;
    }

    // Apply new mask if provided
    if (set)
    {
        u32 new_mask = *set;

        // Cannot block SIGKILL or SIGSTOP
        new_mask &= ~((1u << signal::sig::SIGKILL) | (1u << signal::sig::SIGSTOP));

        switch (how)
        {
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
                return SyscallResult::err(error::VERR_INVALID_ARG);
        }
    }

    return SyscallResult::ok();
}

SyscallResult sys_sigreturn(u64, u64, u64, u64, u64, u64)
{
    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Check if we have a saved frame from signal delivery
    if (!t->signals.saved_frame)
    {
        serial::puts("[signal] sigreturn with no saved frame\n");
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // Restore the original context
    serial::puts("[signal] sigreturn - restoring context\n");
    t->signals.saved_frame = nullptr;

    return SyscallResult::ok();
}

SyscallResult sys_kill(u64 a0, u64 a1, u64, u64, u64, u64)
{
    i64 pid = static_cast<i64>(a0);
    i32 signum = static_cast<i32>(a1);

    // Validate signal number
    if (signum <= 0 || signum >= signal::sig::NSIG)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // Special cases for pid
    if (pid == 0)
    {
        // Send to all processes in caller's process group (not implemented)
        return SyscallResult::err(error::VERR_NOT_SUPPORTED);
    }
    else if (pid == -1)
    {
        // Send to all processes (not implemented)
        return SyscallResult::err(error::VERR_NOT_SUPPORTED);
    }
    else if (pid < -1)
    {
        // Send to process group (not implemented)
        return SyscallResult::err(error::VERR_NOT_SUPPORTED);
    }

    // Find target task
    task::Task *target = task::get_by_id(static_cast<u32>(pid));
    if (!target)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Permission check: caller can only signal tasks in the same process
    // (same viper), or kernel tasks can signal anyone
    task::Task *caller = task::current();
    if (caller && caller->viper)
    {
        // User task - must be same viper process or signaling self
        if (target->viper != caller->viper && target->id != caller->id)
        {
            // Check if caller is parent (allowed to signal children)
            if (target->parent_id != caller->id)
            {
                return SyscallResult::err(error::VERR_PERMISSION);
            }
        }
    }
    // Kernel tasks (no viper) can signal anyone

    // Send the signal
    i32 result = signal::send_signal(target, signum);
    if (result < 0)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    return SyscallResult::ok();
}

SyscallResult sys_sigpending(u64 a0, u64, u64, u64, u64, u64)
{
    u32 *set = reinterpret_cast<u32 *>(a0);

    if (!validate_user_write(set, sizeof(u32)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    *set = t->signals.pending;
    return SyscallResult::ok();
}

} // namespace syscall
