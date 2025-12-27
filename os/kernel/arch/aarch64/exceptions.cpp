/**
 * @file exceptions.cpp
 * @brief C++ exception handlers for AArch64.
 *
 * @details
 * The assembly vector table in `exceptions.S` saves CPU state into an
 * @ref exceptions::ExceptionFrame and then calls into the handler functions
 * implemented here.
 *
 * Responsibilities covered by this translation unit:
 * - Installing the vector base (VBAR_EL1) during initialization.
 * - Helpers for masking/unmasking IRQs at EL1.
 * - Kernel-mode exception handling (panic diagnostics and syscall dispatch).
 * - User-mode exception handling (syscalls and graceful fault termination).
 *
 * User-mode faults (data aborts, instruction aborts, alignment faults, etc.)
 * are handled gracefully: the faulting task is terminated and the system
 * continues running. Only kernel-mode faults cause a full system panic.
 */
#include "exceptions.hpp"
#include "../../console/gcon.hpp"
#include "../../console/serial.hpp"
#include "../../mm/fault.hpp"
#include "../../sched/scheduler.hpp"
#include "../../sched/task.hpp"
#include "../../viper/viper.hpp"

// Forward declaration for GIC handler
namespace gic
{
void handle_irq();
}

// Forward declaration for syscall dispatcher
namespace syscall
{
void dispatch(exceptions::ExceptionFrame *frame);
}

/**
 * @brief Terminate a user task that caused a fatal fault.
 *
 * @details
 * Called when a user-mode task triggers a fatal exception (data abort,
 * instruction abort, etc.). Instead of panicking the kernel, this function:
 * 1. Logs the fault details in USERFAULT format for debugging
 * 2. Terminates just the faulting task with exit code -1
 * 3. Schedules the next runnable task
 *
 * This allows the system to continue running even when a user process crashes.
 *
 * @param frame Exception frame with saved registers.
 * @param reason Human-readable description of the fault type (kind).
 */
static void terminate_faulting_task(exceptions::ExceptionFrame *frame, const char *reason)
{
    // Get current task info for logging
    task::Task *current = task::current();
    u32 tid = current ? current->id : 0;
    u32 pid = tid; // In single-threaded model, pid == tid
    const char *task_name = current ? current->name : "<unknown>";

    // If this is a user task with viper, use viper's id as pid
    if (current && current->viper)
    {
        // The viper field is an opaque pointer to viper::Viper
        auto *v = reinterpret_cast<viper::Viper *>(current->viper);
        pid = static_cast<u32>(v->id);
    }

    // Log in USERFAULT format: USERFAULT pid=<id> tid=<id> pc=0x... far=0x... esr=0x... kind=<...>
    serial::puts("USERFAULT pid=");
    serial::put_dec(pid);
    serial::puts(" tid=");
    serial::put_dec(tid);
    serial::puts(" pc=");
    serial::put_hex(frame->elr);
    serial::puts(" far=");
    serial::put_hex(frame->far);
    serial::puts(" esr=");
    serial::put_hex(frame->esr);
    serial::puts(" kind=");
    serial::puts(reason);
    serial::puts("\n");

    // Also log task name for clarity
    serial::puts("[fault] Task '");
    serial::puts(task_name);
    serial::puts("' terminated\n");

    // Display on graphics console if available
    if (gcon::is_available())
    {
        gcon::set_colors(gcon::colors::VIPER_YELLOW, gcon::colors::BLACK);
        gcon::puts("\n[fault] Task '");
        gcon::puts(task_name);
        gcon::puts("' crashed: ");
        gcon::puts(reason);
        gcon::puts("\n");
        gcon::set_colors(gcon::colors::VIPER_WHITE, gcon::colors::BLACK);
    }

    // Terminate the task - this marks it as Exited and removes from run queue
    // task::exit() will call scheduler::schedule() internally
    task::exit(-1);

    // Should never reach here - task::exit() doesn't return
    serial::puts("[fault] PANIC: task::exit returned!\n");
    for (;;)
        asm volatile("wfi");
}

namespace exceptions
{

/** @copydoc exceptions::init */
void init()
{
    serial::puts("[exceptions] Installing exception vectors\n");
    exceptions_init_asm();
    serial::puts("[exceptions] Exception vectors installed\n");
}

/** @copydoc exceptions::enable_interrupts */
void enable_interrupts()
{
    asm volatile("msr daifclr, #0x2" ::: "memory"); // Clear IRQ mask
}

/** @copydoc exceptions::disable_interrupts */
void disable_interrupts()
{
    asm volatile("msr daifset, #0x2" ::: "memory"); // Set IRQ mask
}

/** @copydoc exceptions::interrupts_enabled */
bool interrupts_enabled()
{
    u64 daif;
    asm volatile("mrs %0, daif" : "=r"(daif));
    return (daif & (1 << 7)) == 0; // IRQ bit is bit 7
}

/**
 * @brief Print the contents of an exception frame for debugging.
 *
 * @details
 * Dumps general-purpose registers and key EL1 system registers to the serial
 * console in a human-readable format. This is primarily used in fatal paths
 * (kernel panics, unexpected exceptions, user faults during bring-up) to aid
 * post-mortem debugging.
 *
 * @param frame Saved register state from the exception trampoline.
 */
static void print_frame(ExceptionFrame *frame)
{
    serial::puts("\n=== EXCEPTION FRAME ===\n");

    // Print special registers
    serial::puts("ELR:  ");
    serial::put_hex(frame->elr);
    serial::puts("\n");
    serial::puts("SPSR: ");
    serial::put_hex(frame->spsr);
    serial::puts("\n");
    serial::puts("ESR:  ");
    serial::put_hex(frame->esr);
    serial::puts("\n");
    serial::puts("FAR:  ");
    serial::put_hex(frame->far);
    serial::puts("\n");
    serial::puts("SP:   ");
    serial::put_hex(frame->sp);
    serial::puts("\n");
    serial::puts("LR:   ");
    serial::put_hex(frame->lr);
    serial::puts("\n");

    // Print general registers
    for (int i = 0; i < 30; i += 2)
    {
        serial::puts("x");
        if (i < 10)
            serial::putc('0');
        serial::put_dec(i);
        serial::puts(": ");
        serial::put_hex(frame->x[i]);
        serial::puts("  x");
        if (i + 1 < 10)
            serial::putc('0');
        serial::put_dec(i + 1);
        serial::puts(": ");
        serial::put_hex(frame->x[i + 1]);
        serial::puts("\n");
    }

    serial::puts("=======================\n");
}

/**
 * @brief Map an exception class code to a human-readable name.
 *
 * @details
 * Converts the `ESR_EL1.EC` field value into a short descriptive string for
 * diagnostics. The set of recognized codes is intentionally small and focused
 * on the exception types currently encountered during kernel bring-up.
 *
 * @param ec Exception class value (ESR_EL1.EC).
 * @return Constant string describing the exception class.
 */
static const char *exception_class_name(u32 ec)
{
    switch (ec)
    {
        case ec::UNKNOWN:
            return "Unknown";
        case ec::WFI_WFE:
            return "WFI/WFE";
        case ec::SVC_A64:
            return "SVC (AArch64)";
        case ec::INST_ABORT_LOWER:
            return "Instruction abort (lower EL)";
        case ec::INST_ABORT_SAME:
            return "Instruction abort (same EL)";
        case ec::PC_ALIGN:
            return "PC alignment fault";
        case ec::DATA_ABORT_LOWER:
            return "Data abort (lower EL)";
        case ec::DATA_ABORT_SAME:
            return "Data abort (same EL)";
        case ec::SP_ALIGN:
            return "SP alignment fault";
        case ec::BRK_A64:
            return "BRK (AArch64)";
        default:
            return "Other";
    }
}

} // namespace exceptions

extern "C"
{
    /** @copydoc handle_sync_exception */
    void handle_sync_exception(exceptions::ExceptionFrame *frame)
    {
        u32 ec = (frame->esr >> 26) & 0x3F; // Exception class

        // Check for SVC (syscall)
        if (ec == exceptions::ec::SVC_A64)
        {
            // Route to syscall dispatcher
            syscall::dispatch(frame);
            return; // Return to caller after syscall
        }

        // Data abort from kernel - route to page fault handler (will panic)
        if (ec == exceptions::ec::DATA_ABORT_SAME)
        {
            mm::handle_page_fault(frame, false /* is_instruction */);
            // Never returns - handler panics for kernel faults
        }

        // Instruction abort from kernel - route to page fault handler (will panic)
        if (ec == exceptions::ec::INST_ABORT_SAME)
        {
            mm::handle_page_fault(frame, true /* is_instruction */);
            // Never returns - handler panics for kernel faults
        }

        // Other synchronous exceptions are errors
        serial::puts("\n!!! SYNCHRONOUS EXCEPTION !!!\n");
        serial::puts("Exception class: ");
        serial::put_hex(ec);
        serial::puts(" (");
        serial::puts(exceptions::exception_class_name(ec));
        serial::puts(")\n");

        exceptions::print_frame(frame);

        // Display on graphics console if available
        if (gcon::is_available())
        {
            gcon::set_colors(gcon::colors::VIPER_RED, gcon::colors::BLACK);
            gcon::puts("\n\n  !!! KERNEL PANIC !!!\n");
            gcon::puts("  Synchronous Exception\n");
            gcon::set_colors(gcon::colors::VIPER_WHITE, gcon::colors::BLACK);
            gcon::puts("  EC: ");
            // Simple hex output
            const char hex[] = "0123456789ABCDEF";
            gcon::putc(hex[(ec >> 4) & 0xF]);
            gcon::putc(hex[ec & 0xF]);
            gcon::puts(" - ");
            gcon::puts(exceptions::exception_class_name(ec));
            gcon::puts("\n");
        }

        // Halt
        serial::puts("\nSystem halted.\n");
        for (;;)
        {
            asm volatile("wfi");
        }
    }

    /** @copydoc handle_irq */
    void handle_irq(exceptions::ExceptionFrame *frame)
    {
        (void)frame;
        // Call GIC to handle the interrupt
        gic::handle_irq();
    }

    /** @copydoc handle_fiq */
    void handle_fiq(exceptions::ExceptionFrame *frame)
    {
        (void)frame;
        serial::puts("\n!!! FIQ (unexpected) !!!\n");
        // FIQs are not used
    }

    /** @copydoc handle_serror */
    void handle_serror(exceptions::ExceptionFrame *frame)
    {
        serial::puts("\n!!! SERROR (System Error) !!!\n");
        exceptions::print_frame(frame);

        if (gcon::is_available())
        {
            gcon::set_colors(gcon::colors::VIPER_RED, gcon::colors::BLACK);
            gcon::puts("\n\n  !!! KERNEL PANIC !!!\n");
            gcon::puts("  System Error (SError)\n");
        }

        // Halt
        for (;;)
        {
            asm volatile("wfi");
        }
    }

    /** @copydoc handle_invalid_exception */
    void handle_invalid_exception(exceptions::ExceptionFrame *frame)
    {
        serial::puts("\n!!! INVALID EXCEPTION !!!\n");
        serial::puts("This exception type should not occur.\n");
        exceptions::print_frame(frame);

        // Halt
        for (;;)
        {
            asm volatile("wfi");
        }
    }

    // EL0 (user mode) exception handlers

    /** @copydoc handle_el0_sync */
    void handle_el0_sync(exceptions::ExceptionFrame *frame)
    {
        u32 ec = (frame->esr >> 26) & 0x3F; // Exception class

        // Check for SVC (syscall from user space)
        if (ec == exceptions::ec::SVC_A64)
        {
            // Route to centralized syscall dispatcher
            syscall::dispatch(frame);
            return;
        }

        // Data abort from user space - route to page fault handler
        if (ec == exceptions::ec::DATA_ABORT_LOWER)
        {
            mm::handle_page_fault(frame, false /* is_instruction */);
            return; // Never reached - handler terminates task or panics
        }

        // Instruction abort from user space - route to page fault handler
        if (ec == exceptions::ec::INST_ABORT_LOWER)
        {
            mm::handle_page_fault(frame, true /* is_instruction */);
            return; // Never reached - handler terminates task or panics
        }

        // PC alignment fault from user space
        if (ec == exceptions::ec::PC_ALIGN)
        {
            terminate_faulting_task(frame, "pc_alignment");
            return; // Never reached
        }

        // SP alignment fault from user space
        if (ec == exceptions::ec::SP_ALIGN)
        {
            terminate_faulting_task(frame, "sp_alignment");
            return; // Never reached
        }

        // Illegal instruction / unknown instruction from user space
        // EC=0x00 is used for instructions that can't be decoded
        if (ec == exceptions::ec::UNKNOWN)
        {
            terminate_faulting_task(frame, "illegal_instruction");
            return; // Never reached
        }

        // Illegal execution state (e.g., PSTATE.IL set)
        if (ec == exceptions::ec::ILLEGAL_STATE)
        {
            terminate_faulting_task(frame, "illegal_state");
            return; // Never reached
        }

        // BRK instruction (breakpoint) from user space
        if (ec == exceptions::ec::BRK_A64)
        {
            terminate_faulting_task(frame, "breakpoint");
            return; // Never reached
        }

        // Other user-mode exception - terminate with generic message
        serial::puts("[fault] Unknown user exception EC=0x");
        serial::put_hex(ec);
        serial::puts(" (");
        serial::puts(exceptions::exception_class_name(ec));
        serial::puts(")\n");
        terminate_faulting_task(frame, "unknown");
    }

    /** @copydoc handle_el0_irq */
    void handle_el0_irq(exceptions::ExceptionFrame *frame)
    {
        (void)frame;
        // Handle IRQ while in user mode - same as kernel IRQ
        gic::handle_irq();
    }

    /** @copydoc handle_el0_serror */
    void handle_el0_serror(exceptions::ExceptionFrame *frame)
    {
        // User-mode SError - terminate the faulting task instead of panicking
        terminate_faulting_task(frame, "system error (SError)");
    }

} // extern "C"
