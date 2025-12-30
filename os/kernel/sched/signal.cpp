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
#include "../console/serial.hpp"
#include "../console/gcon.hpp"
#include "../viper/viper.hpp"
#include "scheduler.hpp"
#include "task.hpp"

namespace signal
{

char default_action(i32 signum)
{
    switch (signum)
    {
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

const char *signal_name(i32 signum)
{
    switch (signum)
    {
        case sig::SIGHUP:    return "SIGHUP";
        case sig::SIGINT:    return "SIGINT";
        case sig::SIGQUIT:   return "SIGQUIT";
        case sig::SIGILL:    return "SIGILL";
        case sig::SIGTRAP:   return "SIGTRAP";
        case sig::SIGABRT:   return "SIGABRT";
        case sig::SIGBUS:    return "SIGBUS";
        case sig::SIGFPE:    return "SIGFPE";
        case sig::SIGKILL:   return "SIGKILL";
        case sig::SIGUSR1:   return "SIGUSR1";
        case sig::SIGSEGV:   return "SIGSEGV";
        case sig::SIGUSR2:   return "SIGUSR2";
        case sig::SIGPIPE:   return "SIGPIPE";
        case sig::SIGALRM:   return "SIGALRM";
        case sig::SIGTERM:   return "SIGTERM";
        case sig::SIGCHLD:   return "SIGCHLD";
        case sig::SIGCONT:   return "SIGCONT";
        case sig::SIGSTOP:   return "SIGSTOP";
        case sig::SIGTSTP:   return "SIGTSTP";
        case sig::SIGTTIN:   return "SIGTTIN";
        case sig::SIGTTOU:   return "SIGTTOU";
        case sig::SIGURG:    return "SIGURG";
        case sig::SIGXCPU:   return "SIGXCPU";
        case sig::SIGXFSZ:   return "SIGXFSZ";
        case sig::SIGVTALRM: return "SIGVTALRM";
        case sig::SIGPROF:   return "SIGPROF";
        case sig::SIGWINCH:  return "SIGWINCH";
        case sig::SIGIO:     return "SIGIO";
        case sig::SIGSYS:    return "SIGSYS";
        default:             return "SIG???";
    }
}

i32 send_signal(task::Task *t, i32 signum)
{
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

    // Get default action
    char action = default_action(signum);

    switch (action)
    {
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

void deliver_fault_signal(i32 signum, const FaultInfo *info)
{
    task::Task *current = task::current();
    if (!current)
    {
        serial::puts("[signal] ERROR: No current task for fault signal\n");
        return;
    }

    // Get task info for logging
    u32 tid = current->id;
    u32 pid = tid;
    const char *task_name = current->name;

    // If this is a user task with viper, use viper's id as pid
    if (current->viper)
    {
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
    if (info)
    {
        serial::puts(" pc=");
        serial::put_hex(info->fault_pc);
        serial::puts(" addr=");
        serial::put_hex(info->fault_addr);
        serial::puts(" esr=");
        serial::put_hex(info->fault_esr);
        if (info->kind)
        {
            serial::puts(" kind=");
            serial::puts(info->kind);
        }
    }
    serial::puts("\n");

    // Also display on graphics console
    if (gcon::is_available())
    {
        gcon::set_colors(gcon::colors::VIPER_YELLOW, gcon::colors::BLACK);
        gcon::puts("\n[signal] Task '");
        gcon::puts(task_name);
        gcon::puts("' received ");
        gcon::puts(signal_name(signum));
        if (info && info->kind)
        {
            gcon::puts(" (");
            gcon::puts(info->kind);
            gcon::puts(")");
        }
        gcon::puts("\n");
        gcon::set_colors(gcon::colors::VIPER_WHITE, gcon::colors::BLACK);
    }

    // Terminate the task
    // In the future, we could deliver to a user signal handler here
    task::exit(-(128 + signum)); // Exit code follows shell convention
}

bool has_pending(task::Task *task)
{
    // No pending signal infrastructure yet
    (void)task;
    return false;
}

void process_pending()
{
    // No pending signal infrastructure yet
    // In a full implementation, this would check for pending signals
    // and either call user handlers or apply default actions
}

} // namespace signal
