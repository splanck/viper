//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/thread.cpp
// Purpose: Thread syscall handlers (0xB0-0xB4).
//
//===----------------------------------------------------------------------===//

#include "../../include/error.hpp"
#include "../../sched/scheduler.hpp"
#include "../../sched/task.hpp"
#include "../../sched/wait.hpp"
#include "../../viper/viper.hpp"
#include "handlers_internal.hpp"

namespace syscall {

SyscallResult sys_thread_create(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    u64 entry = a0;
    u64 stack_top = a1;
    u64 tls_base = a2;

    task::Task *caller = task::current();
    if (!caller || !caller->viper) {
        return err_invalid_arg();
    }

    viper::Viper *v = reinterpret_cast<viper::Viper *>(caller->viper);

    // Check thread limit
    if (v->task_count >= v->task_limit) {
        return SyscallResult::err(error::VERR_NO_RESOURCE);
    }

    // Create the thread in the same process
    task::Task *t = task::create_thread("thread", v, entry, stack_top, tls_base);
    if (!t) {
        return err_out_of_memory();
    }

    // Enqueue on the scheduler
    scheduler::enqueue(t);

    return SyscallResult::ok(static_cast<u64>(t->id));
}

SyscallResult sys_thread_exit(u64 a0, u64, u64, u64, u64, u64) {
    task::Task *t = task::current();
    if (!t) {
        return err_invalid_arg();
    }

    // Store the return value
    t->thread.retval = a0;

    // Wake any joiners
    if (t->thread.join_waiters) {
        sched::wait_wake_all(
            static_cast<sched::WaitQueue *>(t->thread.join_waiters));
    }

    // Decrement process thread count
    if (t->viper) {
        viper::Viper *v = reinterpret_cast<viper::Viper *>(t->viper);
        if (v->task_count > 0)
            v->task_count--;
    }

    // Mark as exited and schedule away (don't call viper::exit)
    t->exit_code = static_cast<i32>(a0);
    t->state = task::TaskState::Exited;
    scheduler::schedule();

    // Should not return
    return SyscallResult::ok();
}

SyscallResult sys_thread_join(u64 a0, u64, u64, u64, u64, u64) {
    u32 target_id = static_cast<u32>(a0);

    task::Task *caller = task::current();
    if (!caller || !caller->viper) {
        return err_invalid_arg();
    }

    // Look up target task
    task::Task *target = task::get_by_id(target_id);
    if (!target) {
        return SyscallResult::err(error::VERR_TASK_NOT_FOUND);
    }

    // Must be in the same process and must be a thread
    if (target->viper != caller->viper || !target->thread.is_thread) {
        return err_invalid_arg();
    }

    // Can't join a detached thread
    if (target->thread.detached) {
        return err_invalid_arg();
    }

    // Mark as joined
    target->thread.joined = true;

    // If already exited, return immediately
    if (target->state == task::TaskState::Exited) {
        return SyscallResult::ok(target->thread.retval);
    }

    // Block until the thread exits
    if (target->thread.join_waiters) {
        sched::WaitQueue *wq =
            static_cast<sched::WaitQueue *>(target->thread.join_waiters);
        sched::wait_enqueue(wq, caller);
        scheduler::schedule();
    }

    // Woken up - thread has exited
    return SyscallResult::ok(target->thread.retval);
}

SyscallResult sys_thread_detach(u64 a0, u64, u64, u64, u64, u64) {
    u32 target_id = static_cast<u32>(a0);

    task::Task *caller = task::current();
    if (!caller || !caller->viper) {
        return err_invalid_arg();
    }

    // Look up target task
    task::Task *target = task::get_by_id(target_id);
    if (!target) {
        return SyscallResult::err(error::VERR_TASK_NOT_FOUND);
    }

    // Must be in the same process and must be a thread
    if (target->viper != caller->viper || !target->thread.is_thread) {
        return err_invalid_arg();
    }

    // Already joined?
    if (target->thread.joined) {
        return err_invalid_arg();
    }

    target->thread.detached = true;

    return SyscallResult::ok();
}

SyscallResult sys_thread_self(u64, u64, u64, u64, u64, u64) {
    task::Task *t = task::current();
    if (!t) {
        return err_invalid_arg();
    }
    return SyscallResult::ok(static_cast<u64>(t->id));
}

} // namespace syscall
