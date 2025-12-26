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
 * - User-mode exception handling (syscalls and fault reporting during bring-up).
 *
 * Many EL0 paths are still development-oriented: fatal user faults currently
 * print diagnostics and halt rather than terminating/isolating the offending
 * task.
 */
#include "exceptions.hpp"
#include "../../../include/viperos/cap_info.hpp"
#include "../../../include/viperos/mem_info.hpp"
#include "../../cap/handle.hpp"
#include "../../cap/rights.hpp"
#include "../../cap/table.hpp"
#include "../../console/gcon.hpp"
#include "../../console/serial.hpp"
#include "../../fs/vfs/vfs.hpp"
#include "../../fs/viperfs/viperfs.hpp"
#include "../../include/error.hpp"
#include "../../input/input.hpp"
#include "../../ipc/channel.hpp"
#include "../../ipc/poll.hpp"
#include "../../ipc/pollset.hpp"
#include "../../kobj/dir.hpp"
#include "../../kobj/file.hpp"
#include "../../mm/pmm.hpp"
#include "../../sched/task.hpp"
#include "../../viper/viper.hpp"
#include "../aarch64/timer.hpp"

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
            // Get syscall number from x8
            u64 syscall_num = frame->x[8];

            // Handle exit syscall (0x01) specially - terminates the task
            if (syscall_num == 0x01)
            {
                serial::puts("[syscall] User exit with code ");
                serial::put_dec(frame->x[0]);
                serial::puts("\n");

                // Terminate the task - this will not return
                // task::exit() marks the task as Exited and schedules another task
                task::exit(static_cast<i32>(frame->x[0]));

                // Should never get here
                serial::puts("[kernel] PANIC: task::exit returned!\n");
                for (;;)
                    asm volatile("wfi");
            }

            // Handle debug_print syscall (0xF0) - print user string
            if (syscall_num == 0xF0)
            {
                // x0 = pointer to string in user space
                const char *msg = reinterpret_cast<const char *>(frame->x[0]);
                if (msg)
                {
                    serial::puts(msg);
                    if (gcon::is_available())
                    {
                        gcon::puts(msg);
                    }
                }
                frame->x[0] = 0; // Success
                return;
            }

            // Handle getchar syscall (0xF1) - read a character from console (non-blocking)
            if (syscall_num == 0xF1)
            {
                // Poll keyboard to check for input
                input::poll();
                i32 c = input::getchar();
                if (c >= 0)
                {
                    frame->x[0] = static_cast<u64>(static_cast<u8>(c));
                    return;
                }
                // Check serial (non-blocking)
                if (serial::has_char())
                {
                    c = static_cast<i32>(static_cast<u8>(serial::getc()));
                    frame->x[0] = static_cast<u64>(static_cast<u8>(c));
                    return;
                }
                // No character available - return WOULD_BLOCK
                frame->x[0] = static_cast<u64>(static_cast<i64>(-300)); // VERR_WOULD_BLOCK
                return;
            }

            // Handle putchar syscall (0xF2) - write a character to console
            if (syscall_num == 0xF2)
            {
                char c = static_cast<char>(frame->x[0] & 0xFF);
                serial::putc(c);
                if (gcon::is_available())
                {
                    gcon::putc(c);
                }
                frame->x[0] = 0; // Success
                return;
            }

            // Handle uptime syscall (0xF3) - get system uptime in milliseconds
            if (syscall_num == 0xF3)
            {
                frame->x[0] = timer::get_ticks(); // 1 tick = 1ms
                return;
            }

            // Handle channel_create syscall (0x10)
            // Returns: x[0] = send_handle, x[1] = recv_handle (or error in x[0])
            if (syscall_num == 0x10)
            {
                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    // No viper context - use legacy ID-based API for kernel tasks
                    i64 result = channel::create();
                    frame->x[0] = static_cast<u64>(result);
                    frame->x[1] = static_cast<u64>(result); // Same ID for legacy
                    return;
                }

                // Create the channel with both endpoint handles
                channel::ChannelPair pair;
                i64 result = channel::create(&pair);
                if (result < 0)
                {
                    frame->x[0] = static_cast<u64>(result);
                    return;
                }

                frame->x[0] = static_cast<u64>(pair.send_handle);
                frame->x[1] = static_cast<u64>(pair.recv_handle);
                return;
            }

            // Handle channel_send syscall (0x11)
            // Args: x[0]=handle, x[1]=data, x[2]=size, x[3]=handles_to_transfer, x[4]=handle_count
            if (syscall_num == 0x11)
            {
                cap::Handle ch_handle = static_cast<cap::Handle>(frame->x[0]);
                const void *data = reinterpret_cast<const void *>(frame->x[1]);
                u32 size = static_cast<u32>(frame->x[2]);
                const cap::Handle *handles = reinterpret_cast<const cap::Handle *>(frame->x[3]);
                u32 handle_count = static_cast<u32>(frame->x[4]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    // Legacy: no viper, use ID directly (no handle transfer)
                    i64 result = channel::try_send(ch_handle, data, size);
                    frame->x[0] = static_cast<u64>(result);
                    return;
                }

                // Look up the channel handle (requires CAP_WRITE for send endpoint)
                cap::Entry *entry =
                    ct->get_with_rights(ch_handle, cap::Kind::Channel, cap::CAP_WRITE);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                channel::Channel *ch = static_cast<channel::Channel *>(entry->object);

                // Call the new try_send with handle transfer support
                i64 result = channel::try_send(ch, data, size, handles, handle_count);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle channel_recv syscall (0x12)
            // Args: x[0]=handle, x[1]=buffer, x[2]=buffer_size, x[3]=out_handles,
            // x[4]=out_handle_count_ptr Returns: x[0]=message_size or error, received handles
            // written to x[3] array
            if (syscall_num == 0x12)
            {
                cap::Handle ch_handle = static_cast<cap::Handle>(frame->x[0]);
                void *buffer = reinterpret_cast<void *>(frame->x[1]);
                u32 buffer_size = static_cast<u32>(frame->x[2]);
                cap::Handle *out_handles = reinterpret_cast<cap::Handle *>(frame->x[3]);
                u32 *out_handle_count = reinterpret_cast<u32 *>(frame->x[4]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    // Legacy: no viper, use ID directly (no handle transfer)
                    i64 result = channel::try_recv(ch_handle, buffer, buffer_size);
                    frame->x[0] = static_cast<u64>(result);
                    return;
                }

                // Look up the channel handle (requires CAP_READ for recv endpoint)
                cap::Entry *entry =
                    ct->get_with_rights(ch_handle, cap::Kind::Channel, cap::CAP_READ);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                channel::Channel *ch = static_cast<channel::Channel *>(entry->object);

                // Call the new try_recv with handle transfer support
                i64 result =
                    channel::try_recv(ch, buffer, buffer_size, out_handles, out_handle_count);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle channel_close syscall (0x13)
            if (syscall_num == 0x13)
            {
                cap::Handle ch_handle = static_cast<cap::Handle>(frame->x[0]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    // Legacy: no viper, use ID directly
                    i64 result = channel::close(ch_handle);
                    frame->x[0] = static_cast<u64>(result);
                    return;
                }

                // Look up the channel handle
                cap::Entry *entry = ct->get_checked(ch_handle, cap::Kind::Channel);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                channel::Channel *ch = static_cast<channel::Channel *>(entry->object);

                // Determine if this is send or recv endpoint based on rights
                bool is_send = cap::has_rights(entry->rights, cap::CAP_WRITE);

                // Close the endpoint (decrements ref count)
                i64 result = channel::close_endpoint(ch, is_send);

                // Remove from cap_table
                ct->remove(ch_handle);

                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle cap_derive syscall (0x70) - derive a new handle with reduced rights
            if (syscall_num == 0x70)
            {
                cap::Handle parent_handle = static_cast<cap::Handle>(frame->x[0]);
                cap::Rights new_rights = static_cast<cap::Rights>(frame->x[1]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                cap::Handle derived = ct->derive(parent_handle, new_rights);
                if (derived == cap::HANDLE_INVALID)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_PERMISSION);
                    return;
                }

                frame->x[0] = static_cast<u64>(derived);
                return;
            }

            // Handle cap_revoke syscall (0x71) - revoke/remove a handle
            if (syscall_num == 0x71)
            {
                cap::Handle handle = static_cast<cap::Handle>(frame->x[0]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                // Check if handle exists
                cap::Entry *entry = ct->get(handle);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                ct->remove(handle);
                frame->x[0] = static_cast<u64>(error::VOK);
                return;
            }

            // Handle cap_query syscall (0x72) - query handle info (kind, rights, generation)
            if (syscall_num == 0x72)
            {
                cap::Handle handle = static_cast<cap::Handle>(frame->x[0]);
                CapInfo *info_out = reinterpret_cast<CapInfo *>(frame->x[1]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                if (!info_out)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_ARG);
                    return;
                }

                cap::Entry *entry = ct->get(handle);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                info_out->handle = handle;
                info_out->kind = static_cast<unsigned short>(entry->kind);
                info_out->generation = entry->generation;
                info_out->_reserved = 0;
                info_out->rights = entry->rights;

                frame->x[0] = static_cast<u64>(error::VOK);
                return;
            }

            // Handle cap_list syscall (0x73) - list all capabilities in the table
            if (syscall_num == 0x73)
            {
                CapListEntry *buffer = reinterpret_cast<CapListEntry *>(frame->x[0]);
                u32 max_count = static_cast<u32>(frame->x[1]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                if (!buffer || max_count == 0)
                {
                    // Return count only
                    frame->x[0] = static_cast<u64>(ct->count());
                    return;
                }

                // Enumerate valid entries using efficient iteration
                u32 count = 0;
                usize capacity = ct->capacity();

                for (usize idx = 0; idx < capacity && count < max_count; idx++)
                {
                    cap::Entry *entry = ct->entry_at(idx);
                    if (entry && entry->kind != cap::Kind::Invalid)
                    {
                        // Construct valid handle from index and generation
                        cap::Handle h = cap::make_handle(static_cast<u32>(idx), entry->generation);
                        buffer[count].handle = h;
                        buffer[count].kind = static_cast<unsigned short>(entry->kind);
                        buffer[count].generation = entry->generation;
                        buffer[count]._reserved = 0;
                        buffer[count].rights = entry->rights;
                        count++;
                    }
                }

                frame->x[0] = static_cast<u64>(count);
                return;
            }

            // Handle poll_create syscall (0x20)
            if (syscall_num == 0x20)
            {
                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    // No viper context - use legacy ID-based API for kernel tasks
                    i64 result = pollset::create();
                    frame->x[0] = static_cast<u64>(result);
                    return;
                }

                // Create the pollset
                i64 result = pollset::create();
                if (result < 0)
                {
                    frame->x[0] = static_cast<u64>(result);
                    return;
                }

                // Get the pollset pointer and insert into cap_table
                pollset::PollSet *ps = pollset::get(static_cast<u32>(result));
                if (!ps)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_FOUND);
                    return;
                }

                cap::Handle h = ct->insert(ps, cap::Kind::Poll, cap::CAP_READ | cap::CAP_WRITE);
                if (h == cap::HANDLE_INVALID)
                {
                    pollset::destroy(static_cast<u32>(result));
                    frame->x[0] = static_cast<u64>(error::VERR_OUT_OF_MEMORY);
                    return;
                }

                frame->x[0] = static_cast<u64>(h);
                return;
            }

            // Handle poll_add syscall (0x21)
            if (syscall_num == 0x21)
            {
                cap::Handle poll_handle = static_cast<cap::Handle>(frame->x[0]);
                u32 target_handle = static_cast<u32>(frame->x[1]);
                u32 mask = static_cast<u32>(frame->x[2]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    // Legacy: no viper, use ID directly
                    i64 result = pollset::add(poll_handle, target_handle, mask);
                    frame->x[0] = static_cast<u64>(result);
                    return;
                }

                // Look up the pollset handle
                cap::Entry *entry =
                    ct->get_with_rights(poll_handle, cap::Kind::Poll, cap::CAP_WRITE);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                pollset::PollSet *ps = static_cast<pollset::PollSet *>(entry->object);
                i64 result = pollset::add(ps->id, target_handle, mask);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle poll_remove syscall (0x22)
            if (syscall_num == 0x22)
            {
                cap::Handle poll_handle = static_cast<cap::Handle>(frame->x[0]);
                u32 target_handle = static_cast<u32>(frame->x[1]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    // Legacy: no viper, use ID directly
                    i64 result = pollset::remove(poll_handle, target_handle);
                    frame->x[0] = static_cast<u64>(result);
                    return;
                }

                // Look up the pollset handle
                cap::Entry *entry =
                    ct->get_with_rights(poll_handle, cap::Kind::Poll, cap::CAP_WRITE);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                pollset::PollSet *ps = static_cast<pollset::PollSet *>(entry->object);
                i64 result = pollset::remove(ps->id, target_handle);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle poll_wait syscall (0x23)
            if (syscall_num == 0x23)
            {
                cap::Handle poll_handle = static_cast<cap::Handle>(frame->x[0]);
                poll::PollEvent *out_events = reinterpret_cast<poll::PollEvent *>(frame->x[1]);
                u32 max_events = static_cast<u32>(frame->x[2]);
                i64 timeout_ms = static_cast<i64>(frame->x[3]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    // Legacy: no viper, use ID directly
                    i64 result = pollset::wait(poll_handle, out_events, max_events, timeout_ms);
                    frame->x[0] = static_cast<u64>(result);
                    return;
                }

                // Look up the pollset handle
                cap::Entry *entry =
                    ct->get_with_rights(poll_handle, cap::Kind::Poll, cap::CAP_READ);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                pollset::PollSet *ps = static_cast<pollset::PollSet *>(entry->object);
                i64 result = pollset::wait(ps->id, out_events, max_events, timeout_ms);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle open syscall (0x40)
            if (syscall_num == 0x40)
            {
                const char *path = reinterpret_cast<const char *>(frame->x[0]);
                u32 flags = static_cast<u32>(frame->x[1]);
                i32 fd = fs::vfs::open(path, flags);
                frame->x[0] = static_cast<u64>(fd);
                return;
            }

            // Handle close syscall (0x41)
            if (syscall_num == 0x41)
            {
                i32 fd = static_cast<i32>(frame->x[0]);
                i32 result = fs::vfs::close(fd);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle read syscall (0x42)
            if (syscall_num == 0x42)
            {
                i32 fd = static_cast<i32>(frame->x[0]);
                void *buf = reinterpret_cast<void *>(frame->x[1]);
                usize len = static_cast<usize>(frame->x[2]);
                i64 result = fs::vfs::read(fd, buf, len);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle write syscall (0x43)
            if (syscall_num == 0x43)
            {
                i32 fd = static_cast<i32>(frame->x[0]);
                const void *buf = reinterpret_cast<const void *>(frame->x[1]);
                usize len = static_cast<usize>(frame->x[2]);
                i64 result = fs::vfs::write(fd, buf, len);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle lseek syscall (0x44)
            if (syscall_num == 0x44)
            {
                i32 fd = static_cast<i32>(frame->x[0]);
                i64 offset = static_cast<i64>(frame->x[1]);
                i32 whence = static_cast<i32>(frame->x[2]);
                i64 result = fs::vfs::lseek(fd, offset, whence);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle stat syscall (0x45)
            if (syscall_num == 0x45)
            {
                const char *path = reinterpret_cast<const char *>(frame->x[0]);
                fs::vfs::Stat *st = reinterpret_cast<fs::vfs::Stat *>(frame->x[1]);
                i32 result = fs::vfs::stat(path, st);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle fstat syscall (0x46)
            if (syscall_num == 0x46)
            {
                i32 fd = static_cast<i32>(frame->x[0]);
                fs::vfs::Stat *st = reinterpret_cast<fs::vfs::Stat *>(frame->x[1]);
                i32 result = fs::vfs::fstat(fd, st);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle readdir syscall (0x60) - read directory entries
            if (syscall_num == 0x60)
            {
                i32 fd = static_cast<i32>(frame->x[0]);
                void *buf = reinterpret_cast<void *>(frame->x[1]);
                usize len = static_cast<usize>(frame->x[2]);
                i64 result = fs::vfs::getdents(fd, buf, len);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle mkdir syscall (0x61)
            if (syscall_num == 0x61)
            {
                const char *path = reinterpret_cast<const char *>(frame->x[0]);
                i32 result = fs::vfs::mkdir(path);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle rmdir syscall (0x62)
            if (syscall_num == 0x62)
            {
                const char *path = reinterpret_cast<const char *>(frame->x[0]);
                i32 result = fs::vfs::rmdir(path);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle unlink syscall (0x63)
            if (syscall_num == 0x63)
            {
                const char *path = reinterpret_cast<const char *>(frame->x[0]);
                i32 result = fs::vfs::unlink(path);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle rename syscall (0x64)
            if (syscall_num == 0x64)
            {
                const char *old_path = reinterpret_cast<const char *>(frame->x[0]);
                const char *new_path = reinterpret_cast<const char *>(frame->x[1]);
                i32 result = fs::vfs::rename(old_path, new_path);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // Handle task_list syscall (0x05)
            if (syscall_num == 0x05)
            {
                TaskInfo *buffer = reinterpret_cast<TaskInfo *>(frame->x[0]);
                u32 max_count = static_cast<u32>(frame->x[1]);
                u32 count = task::list_tasks(buffer, max_count);
                frame->x[0] = static_cast<u64>(count);
                return;
            }

            // Handle mem_info syscall (0xE0)
            if (syscall_num == 0xE0)
            {
                MemInfo *info = reinterpret_cast<MemInfo *>(frame->x[0]);
                if (info)
                {
                    info->total_pages = pmm::get_total_pages();
                    info->free_pages = pmm::get_free_pages();
                    info->used_pages = pmm::get_used_pages();
                    info->page_size = 4096;
                    info->total_bytes = info->total_pages * info->page_size;
                    info->free_bytes = info->free_pages * info->page_size;
                    info->used_bytes = info->used_pages * info->page_size;
                    frame->x[0] = 0; // Success
                }
                else
                {
                    frame->x[0] = static_cast<u64>(-1); // Error
                }
                return;
            }

            // =========================================================================
            // Handle-based Filesystem API (0x80-0x87)
            // =========================================================================

            // FsOpenRoot syscall (0x80) - Get a handle to the root directory
            // Returns: x[0] = directory handle or error
            if (syscall_num == 0x80)
            {
                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                // Create directory object for root
                kobj::DirObject *dir = kobj::DirObject::create(fs::viperfs::ROOT_INODE);
                if (!dir)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_FOUND);
                    return;
                }

                // Insert into cap_table with read/traverse rights
                cap::Rights rights = cap::CAP_READ | cap::CAP_TRAVERSE;
                cap::Handle h = ct->insert(dir, cap::Kind::Directory, rights);
                if (h == cap::HANDLE_INVALID)
                {
                    delete dir;
                    frame->x[0] = static_cast<u64>(error::VERR_OUT_OF_MEMORY);
                    return;
                }

                frame->x[0] = static_cast<u64>(h);
                return;
            }

            // FsOpen syscall (0x81) - Open a file or directory relative to a dir handle
            // Args: x[0]=dir_handle, x[1]=name, x[2]=name_len, x[3]=flags
            // Returns: x[0] = file/dir handle or error
            if (syscall_num == 0x81)
            {
                cap::Handle dir_h = static_cast<cap::Handle>(frame->x[0]);
                const char *name = reinterpret_cast<const char *>(frame->x[1]);
                usize name_len = static_cast<usize>(frame->x[2]);
                u32 flags = static_cast<u32>(frame->x[3]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                // Look up the directory handle
                cap::Entry *entry = ct->get_checked(dir_h, cap::Kind::Directory);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                kobj::DirObject *dir = static_cast<kobj::DirObject *>(entry->object);

                // Lookup the child entry
                u64 child_inode;
                u8 child_type;
                if (!dir->lookup(name, name_len, &child_inode, &child_type))
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_FOUND);
                    return;
                }

                // Determine if it's a file or directory
                if (child_type == fs::viperfs::file_type::DIR)
                {
                    // Create directory object
                    kobj::DirObject *child_dir = kobj::DirObject::create(child_inode);
                    if (!child_dir)
                    {
                        frame->x[0] = static_cast<u64>(error::VERR_OUT_OF_MEMORY);
                        return;
                    }

                    cap::Rights rights = cap::CAP_READ | cap::CAP_TRAVERSE;
                    cap::Handle h = ct->insert(child_dir, cap::Kind::Directory, rights);
                    if (h == cap::HANDLE_INVALID)
                    {
                        delete child_dir;
                        frame->x[0] = static_cast<u64>(error::VERR_OUT_OF_MEMORY);
                        return;
                    }

                    frame->x[0] = static_cast<u64>(h);
                }
                else
                {
                    // Create file object
                    kobj::FileObject *file = kobj::FileObject::create(child_inode, flags);
                    if (!file)
                    {
                        frame->x[0] = static_cast<u64>(error::VERR_OUT_OF_MEMORY);
                        return;
                    }

                    // Determine rights based on open flags
                    u32 access = flags & 0x3;
                    cap::Rights rights = cap::CAP_NONE;
                    if (access == kobj::file_flags::O_RDONLY || access == kobj::file_flags::O_RDWR)
                    {
                        rights = rights | cap::CAP_READ;
                    }
                    if (access == kobj::file_flags::O_WRONLY || access == kobj::file_flags::O_RDWR)
                    {
                        rights = rights | cap::CAP_WRITE;
                    }

                    cap::Handle h = ct->insert(file, cap::Kind::File, rights);
                    if (h == cap::HANDLE_INVALID)
                    {
                        delete file;
                        frame->x[0] = static_cast<u64>(error::VERR_OUT_OF_MEMORY);
                        return;
                    }

                    frame->x[0] = static_cast<u64>(h);
                }
                return;
            }

            // IORead syscall (0x82) - Read from a file handle
            // Args: x[0]=file_handle, x[1]=buffer, x[2]=len
            // Returns: x[0] = bytes read or error
            if (syscall_num == 0x82)
            {
                cap::Handle file_h = static_cast<cap::Handle>(frame->x[0]);
                void *buffer = reinterpret_cast<void *>(frame->x[1]);
                usize len = static_cast<usize>(frame->x[2]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                // Look up the file handle (requires CAP_READ)
                cap::Entry *entry = ct->get_with_rights(file_h, cap::Kind::File, cap::CAP_READ);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                kobj::FileObject *file = static_cast<kobj::FileObject *>(entry->object);
                i64 result = file->read(buffer, len);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // IOWrite syscall (0x83) - Write to a file handle
            // Args: x[0]=file_handle, x[1]=buffer, x[2]=len
            // Returns: x[0] = bytes written or error
            if (syscall_num == 0x83)
            {
                cap::Handle file_h = static_cast<cap::Handle>(frame->x[0]);
                const void *buffer = reinterpret_cast<const void *>(frame->x[1]);
                usize len = static_cast<usize>(frame->x[2]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                // Look up the file handle (requires CAP_WRITE)
                cap::Entry *entry = ct->get_with_rights(file_h, cap::Kind::File, cap::CAP_WRITE);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                kobj::FileObject *file = static_cast<kobj::FileObject *>(entry->object);
                i64 result = file->write(buffer, len);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // IOSeek syscall (0x84) - Seek within a file handle
            // Args: x[0]=file_handle, x[1]=offset, x[2]=whence
            // Returns: x[0] = new position or error
            if (syscall_num == 0x84)
            {
                cap::Handle file_h = static_cast<cap::Handle>(frame->x[0]);
                i64 offset = static_cast<i64>(frame->x[1]);
                i32 whence = static_cast<i32>(frame->x[2]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                // Look up the file handle (no special rights needed for seek)
                cap::Entry *entry = ct->get_checked(file_h, cap::Kind::File);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                kobj::FileObject *file = static_cast<kobj::FileObject *>(entry->object);
                i64 result = file->seek(offset, whence);
                frame->x[0] = static_cast<u64>(result);
                return;
            }

            // FsReadDir syscall (0x85) - Read next directory entry
            // Args: x[0]=dir_handle, x[1]=out_entry (FsDirEnt*)
            // Returns: x[0] = 1 if entry returned, 0 if end, negative on error
            if (syscall_num == 0x85)
            {
                cap::Handle dir_h = static_cast<cap::Handle>(frame->x[0]);
                kobj::FsDirEnt *out_ent = reinterpret_cast<kobj::FsDirEnt *>(frame->x[1]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                // Look up the directory handle (requires CAP_READ)
                cap::Entry *entry = ct->get_with_rights(dir_h, cap::Kind::Directory, cap::CAP_READ);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                kobj::DirObject *dir = static_cast<kobj::DirObject *>(entry->object);
                bool has_more = dir->read_next(out_ent);
                frame->x[0] = has_more ? 1 : 0;
                return;
            }

            // FsClose syscall (0x86) - Close a file or directory handle
            // Args: x[0]=handle
            // Returns: x[0] = 0 on success or error
            if (syscall_num == 0x86)
            {
                cap::Handle h = static_cast<cap::Handle>(frame->x[0]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                // Look up the handle (could be File or Directory)
                cap::Entry *entry = ct->get(h);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                // Check that it's a file or directory
                if (entry->kind != cap::Kind::File && entry->kind != cap::Kind::Directory)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                // Release the object
                kobj::Object *obj = static_cast<kobj::Object *>(entry->object);
                kobj::release(obj);

                // Remove from cap_table
                ct->remove(h);

                frame->x[0] = 0;
                return;
            }

            // FsRewindDir syscall (0x87) - Reset directory enumeration to beginning
            // Args: x[0]=dir_handle
            // Returns: x[0] = 0 on success or error
            if (syscall_num == 0x87)
            {
                cap::Handle dir_h = static_cast<cap::Handle>(frame->x[0]);

                cap::Table *ct = viper::current_cap_table();
                if (!ct)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_NOT_SUPPORTED);
                    return;
                }

                // Look up the directory handle
                cap::Entry *entry = ct->get_checked(dir_h, cap::Kind::Directory);
                if (!entry)
                {
                    frame->x[0] = static_cast<u64>(error::VERR_INVALID_HANDLE);
                    return;
                }

                kobj::DirObject *dir = static_cast<kobj::DirObject *>(entry->object);
                dir->rewind();
                frame->x[0] = 0;
                return;
            }

            // Unknown syscall
            serial::puts("[syscall] Unknown user syscall: ");
            serial::put_hex(syscall_num);
            serial::puts("\n");
            frame->x[0] = static_cast<u64>(-1); // Error
            return;
        }

        // Data abort from user space
        if (ec == exceptions::ec::DATA_ABORT_LOWER)
        {
            serial::puts("\n!!! USER DATA ABORT !!!\n");
            serial::puts("FAR: ");
            serial::put_hex(frame->far);
            serial::puts(" PC: ");
            serial::put_hex(frame->elr);
            serial::puts("\n");
            exceptions::print_frame(frame);
            // TODO: Send signal to user process or terminate
            for (;;)
                asm volatile("wfi");
        }

        // Instruction abort from user space
        if (ec == exceptions::ec::INST_ABORT_LOWER)
        {
            serial::puts("\n!!! USER INSTRUCTION ABORT !!!\n");
            serial::puts("FAR: ");
            serial::put_hex(frame->far);
            serial::puts(" PC: ");
            serial::put_hex(frame->elr);
            serial::puts("\n");
            exceptions::print_frame(frame);
            for (;;)
                asm volatile("wfi");
        }

        // Other exception from user space
        serial::puts("\n!!! USER EXCEPTION !!!\n");
        serial::puts("EC: ");
        serial::put_hex(ec);
        serial::puts(" (");
        serial::puts(exceptions::exception_class_name(ec));
        serial::puts(")\n");
        exceptions::print_frame(frame);
        for (;;)
            asm volatile("wfi");
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
        serial::puts("\n!!! USER SERROR !!!\n");
        exceptions::print_frame(frame);
        // TODO: Terminate user process
        for (;;)
            asm volatile("wfi");
    }

} // extern "C"
