/**
 * @file signal.cpp
 * @brief POSIX-like signal handling implementation.
 *
 * @details
 * Implements signal delivery for hardware faults and software signals.
 * Currently, most signals result in task termination since user-space
 * signal handlers are not yet implemented.
 */
#include "signal.hpp"
#include "../arch/aarch64/exceptions.hpp"
#include "../console/gcon.hpp"
#include "../console/serial.hpp"
#include "../mm/vmm.hpp"
#include "../viper/viper.hpp"
#include "scheduler.hpp"
#include "task.hpp"

namespace signal {

char default_action(i32 signum) {
    switch (signum) {
        // Terminate (core dump in real UNIX)
        case sig::SIGQUIT:
        case sig::SIGILL:
        case sig::SIGTRAP:
        case sig::SIGABRT:
        case sig::SIGBUS:
        case sig::SIGFPE:
        case sig::SIGSEGV:
        case sig::SIGXCPU:
        case sig::SIGXFSZ:
        case sig::SIGSYS:
            return 'T';

        // Terminate
        case sig::SIGHUP:
        case sig::SIGINT:
        case sig::SIGKILL:
        case sig::SIGPIPE:
        case sig::SIGALRM:
        case sig::SIGTERM:
        case sig::SIGUSR1:
        case sig::SIGUSR2:
        case sig::SIGVTALRM:
        case sig::SIGPROF:
        case sig::SIGIO:
            return 'T';

        // Ignore
        case sig::SIGCHLD:
        case sig::SIGURG:
        case sig::SIGWINCH:
            return 'I';

        // Stop
        case sig::SIGSTOP:
        case sig::SIGTSTP:
        case sig::SIGTTIN:
        case sig::SIGTTOU:
            return 'S';

        // Continue
        case sig::SIGCONT:
            return 'C';

        default:
            return 'T'; // Default to terminate
    }
}

const char *signal_name(i32 signum) {
    switch (signum) {
        case sig::SIGHUP:
            return "SIGHUP";
        case sig::SIGINT:
            return "SIGINT";
        case sig::SIGQUIT:
            return "SIGQUIT";
        case sig::SIGILL:
            return "SIGILL";
        case sig::SIGTRAP:
            return "SIGTRAP";
        case sig::SIGABRT:
            return "SIGABRT";
        case sig::SIGBUS:
            return "SIGBUS";
        case sig::SIGFPE:
            return "SIGFPE";
        case sig::SIGKILL:
            return "SIGKILL";
        case sig::SIGUSR1:
            return "SIGUSR1";
        case sig::SIGSEGV:
            return "SIGSEGV";
        case sig::SIGUSR2:
            return "SIGUSR2";
        case sig::SIGPIPE:
            return "SIGPIPE";
        case sig::SIGALRM:
            return "SIGALRM";
        case sig::SIGTERM:
            return "SIGTERM";
        case sig::SIGCHLD:
            return "SIGCHLD";
        case sig::SIGCONT:
            return "SIGCONT";
        case sig::SIGSTOP:
            return "SIGSTOP";
        case sig::SIGTSTP:
            return "SIGTSTP";
        case sig::SIGTTIN:
            return "SIGTTIN";
        case sig::SIGTTOU:
            return "SIGTTOU";
        case sig::SIGURG:
            return "SIGURG";
        case sig::SIGXCPU:
            return "SIGXCPU";
        case sig::SIGXFSZ:
            return "SIGXFSZ";
        case sig::SIGVTALRM:
            return "SIGVTALRM";
        case sig::SIGPROF:
            return "SIGPROF";
        case sig::SIGWINCH:
            return "SIGWINCH";
        case sig::SIGIO:
            return "SIGIO";
        case sig::SIGSYS:
            return "SIGSYS";
        default:
            return "SIG???";
    }
}

i32 send_signal(task::Task *t, i32 signum) {
    if (!t)
        return -1;

    if (signum <= 0 || signum >= sig::NSIG)
        return -1;

    // Log the signal
    serial::puts("[signal] Sending ");
    serial::puts(signal_name(signum));
    serial::puts(" to task '");
    serial::puts(t->name);
    serial::puts("' (pid=");
    serial::put_dec(t->id);
    serial::puts(")\n");

    // Check for user handler
    u64 handler = t->signals.handlers[signum];

    // SIGKILL and SIGSTOP cannot be caught or ignored
    if (signum == sig::SIGKILL || signum == sig::SIGSTOP) {
        // Immediate termination for SIGKILL
        if (signum == sig::SIGKILL) {
            return task::kill(t->id, signum);
        }
        // SIGSTOP - not fully implemented
        return 0;
    }

    // SIG_IGN - ignore the signal
    if (handler == 1) {
        return 0;
    }

    // If there's a user handler (not SIG_DFL), set pending and wake if blocked
    if (handler > 1) {
        __atomic_fetch_or(&t->signals.pending, 1u << signum, __ATOMIC_SEQ_CST);
        // If task is blocked, wake it to deliver the signal
        if (t->state == task::TaskState::Blocked) {
            task::wakeup(t);
        }
        return 0;
    }

    // SIG_DFL (handler == 0) - use default action
    char action = default_action(signum);

    switch (action) {
        case 'T': // Terminate
            // Use existing kill mechanism
            return task::kill(t->id, signum);

        case 'I': // Ignore
            return 0;

        case 'S': // Stop
            // Not implemented - ignore for now
            return 0;

        case 'C': // Continue
            // Not implemented - ignore for now
            return 0;

        default:
            return task::kill(t->id, sig::SIGTERM);
    }
}

void deliver_fault_signal(i32 signum, const FaultInfo *info) {
    task::Task *current = task::current();
    if (!current) {
        serial::puts("[signal] ERROR: No current task for fault signal\n");
        return;
    }

    // Get task info for logging
    u32 tid = current->id;
    u32 pid = tid;
    const char *task_name = current->name;

    // If this is a user task with viper, use viper's id as pid
    if (current->viper) {
        auto *v = reinterpret_cast<viper::Viper *>(current->viper);
        pid = static_cast<u32>(v->id);
    }

    // Log in USERFAULT format for debugging
    serial::puts("USERFAULT pid=");
    serial::put_dec(pid);
    serial::puts(" tid=");
    serial::put_dec(tid);
    serial::puts(" signal=");
    serial::puts(signal_name(signum));
    if (info) {
        serial::puts(" pc=");
        serial::put_hex(info->fault_pc);
        serial::puts(" addr=");
        serial::put_hex(info->fault_addr);
        serial::puts(" esr=");
        serial::put_hex(info->fault_esr);
        if (info->kind) {
            serial::puts(" kind=");
            serial::puts(info->kind);
        }
    }
    serial::puts("\n");

    // Also display on graphics console
    if (gcon::is_available()) {
        gcon::puts("\n[signal] Task '");
        gcon::puts(task_name);
        gcon::puts("' received ");
        gcon::puts(signal_name(signum));
        if (info && info->kind) {
            gcon::puts(" (");
            gcon::puts(info->kind);
            gcon::puts(")");
        }
        gcon::puts("\n");
    }

    // Terminate the task
    // In the future, we could deliver to a user signal handler here
    task::exit(-(128 + signum)); // Exit code follows shell convention
}

bool has_pending(task::Task *t) {
    if (!t)
        return false;
    // Check if any pending signals are not blocked
    return (t->signals.pending & ~t->signals.blocked) != 0;
}

void process_pending() {
    task::Task *t = task::current();
    if (!t)
        return;

    // Get the set of deliverable signals (pending & ~blocked)
    u32 deliverable = t->signals.pending & ~t->signals.blocked;
    if (deliverable == 0)
        return;

    // Find the lowest numbered pending signal
    i32 signum = 0;
    for (i32 i = 1; i < sig::NSIG; i++) {
        if (deliverable & (1u << i)) {
            signum = i;
            break;
        }
    }

    if (signum == 0)
        return;

    // Clear this signal from pending (atomic to prevent races)
    __atomic_fetch_and(&t->signals.pending, ~(1u << signum), __ATOMIC_SEQ_CST);

    u64 handler = t->signals.handlers[signum];

    // SIG_DFL (0) - apply default action
    if (handler == 0) {
        char action = default_action(signum);
        if (action == 'T') {
            serial::puts("[signal] Delivering ");
            serial::puts(signal_name(signum));
            serial::puts(" (default: terminate) to '");
            serial::puts(t->name);
            serial::puts("'\n");
            task::exit(-(128 + signum));
        }
        // Ignore 'I', 'S', 'C' for now
        return;
    }

    // SIG_IGN (1) - ignore
    if (handler == 1) {
        return;
    }

    // User signal handler (handler > 1)
    // In a full implementation, we would:
    // 1. Save the current trap frame to t->signals.saved_frame
    // 2. Set up a signal trampoline on the user stack
    // 3. Modify the trap frame to jump to the handler
    // 4. When handler calls sigreturn, restore the saved frame
    //
    // For now, log that we would call the handler and apply default action

    serial::puts("[signal] Would call user handler at 0x");
    serial::put_hex(handler);
    serial::puts(" for ");
    serial::puts(signal_name(signum));
    serial::puts(" - user handlers not yet implemented, using default action\n");

    // Apply default action since we can't call user handlers yet
    char action = default_action(signum);
    if (action == 'T') {
        task::exit(-(128 + signum));
    }
}

bool setup_signal_delivery(exceptions::ExceptionFrame *frame) {
    task::Task *t = task::current();
    if (!t || !frame)
        return false;

    // Check for deliverable signals
    u32 deliverable = t->signals.pending & ~t->signals.blocked;
    if (deliverable == 0)
        return false;

    // Find the lowest numbered pending signal
    i32 signum = 0;
    for (i32 i = 1; i < sig::NSIG; i++) {
        if (deliverable & (1u << i)) {
            signum = i;
            break;
        }
    }

    if (signum == 0)
        return false;

    // Get handler
    u64 handler = t->signals.handlers[signum];

    // SIG_DFL (0) - apply default action (don't set up handler)
    if (handler == 0) {
        char action = default_action(signum);
        if (action == 'T') {
            // Clear the pending signal and terminate
            __atomic_fetch_and(&t->signals.pending, ~(1u << signum), __ATOMIC_SEQ_CST);
            serial::puts("[signal] Delivering ");
            serial::puts(signal_name(signum));
            serial::puts(" (default: terminate) to '");
            serial::puts(t->name);
            serial::puts("'\n");
            task::exit(-(128 + signum));
            // Never returns
        }
        // For 'I', 'S', 'C' - clear signal and continue
        __atomic_fetch_and(&t->signals.pending, ~(1u << signum), __ATOMIC_SEQ_CST);
        return false;
    }

    // SIG_IGN (1) - just clear and continue
    if (handler == 1) {
        __atomic_fetch_and(&t->signals.pending, ~(1u << signum), __ATOMIC_SEQ_CST);
        return false;
    }

    // User handler - set up signal frame on user stack
    serial::puts("[signal] Delivering ");
    serial::puts(signal_name(signum));
    serial::puts(" to handler at 0x");
    serial::put_hex(handler);
    serial::puts(" for task '");
    serial::puts(t->name);
    serial::puts("'\n");

    // Calculate new stack pointer (must be 16-byte aligned)
    u64 user_sp = frame->sp;
    u64 frame_size = sizeof(SignalFrame);
    frame_size = (frame_size + 15) & ~15ULL; // Align to 16 bytes
    u64 new_sp = user_sp - frame_size;

    // Validate the new stack is in user space
    if (new_sp < 0x1000 || new_sp >= 0x800000000000ULL) {
        serial::puts("[signal] Invalid user stack for signal frame\n");
        __atomic_fetch_and(&t->signals.pending, ~(1u << signum), __ATOMIC_SEQ_CST);
        task::exit(-sig::SIGSEGV);
        return false;
    }

    // Create the signal frame on the user stack
    SignalFrame *sig_frame = reinterpret_cast<SignalFrame *>(new_sp);

    // Save current context
    for (int i = 0; i < 30; i++) {
        sig_frame->x[i] = frame->x[i];
    }
    sig_frame->x[30] = frame->lr;
    sig_frame->sp = frame->sp;
    sig_frame->elr = frame->elr;
    sig_frame->spsr = frame->spsr;
    sig_frame->signum = static_cast<u32>(signum);
    sig_frame->blocked_old = t->signals.blocked;

    // Set up the signal return trampoline
    // This is ARM64 code to call sigreturn:
    //   mov x8, #SYS_SIGRETURN (0x90 = 144)
    //   svc #0
    // Encoded as:
    //   0xD2801200  // mov x8, #0x90
    //   0xD4000001  // svc #0
    sig_frame->trampoline[0] = 0xD2801200D4000001ULL; // Combined as little-endian
    sig_frame->trampoline[1] = 0; // Padding

    // Clear this signal from pending
    __atomic_fetch_and(&t->signals.pending, ~(1u << signum), __ATOMIC_SEQ_CST);

    // Block signals during handler execution (add handler's mask)
    u32 handler_mask = t->signals.handler_mask[signum];
    if (!(t->signals.handler_flags[signum] & sa_flags::SA_NODEFER)) {
        // Also block the current signal
        handler_mask |= (1u << signum);
    }
    t->signals.blocked |= handler_mask;

    // Reset handler to default if SA_RESETHAND is set
    if (t->signals.handler_flags[signum] & sa_flags::SA_RESETHAND) {
        t->signals.handlers[signum] = SIG_DFL;
    }

    // Modify exception frame to call the handler
    // x0 = signal number
    // x30 (LR) = address of trampoline (for when handler returns)
    // SP = new stack pointer (below signal frame)
    // ELR = handler address (where to jump)
    frame->x[0] = static_cast<u64>(signum);
    frame->lr = reinterpret_cast<u64>(&sig_frame->trampoline[0]);
    frame->sp = new_sp;
    frame->elr = handler;

    return true;
}

bool restore_signal_context(exceptions::ExceptionFrame *frame) {
    task::Task *t = task::current();
    if (!t || !frame)
        return false;

    // The signal frame should be at the current SP
    SignalFrame *sig_frame = reinterpret_cast<SignalFrame *>(frame->sp);

    // Basic validation
    u64 sp_addr = reinterpret_cast<u64>(sig_frame);
    if (sp_addr < 0x1000 || sp_addr >= 0x800000000000ULL) {
        serial::puts("[signal] Invalid signal frame address in sigreturn\n");
        return false;
    }

    serial::puts("[signal] sigreturn - restoring context for signal ");
    serial::put_dec(sig_frame->signum);
    serial::puts("\n");

    // Restore registers
    for (int i = 0; i < 30; i++) {
        frame->x[i] = sig_frame->x[i];
    }
    frame->lr = sig_frame->x[30];
    frame->sp = sig_frame->sp;
    frame->elr = sig_frame->elr;
    // Restore SPSR but preserve EL bits for security
    u64 safe_spsr = (sig_frame->spsr & 0xFFFFFFFF0FFFFFFFUL) | (frame->spsr & 0x0000000F0000000UL);
    frame->spsr = safe_spsr;

    // Restore blocked signal mask
    t->signals.blocked = sig_frame->blocked_old;

    return true;
}

} // namespace signal
