/**
 * @file fault.cpp
 * @brief Page fault handling implementation.
 *
 * @details
 * Handles data aborts and instruction aborts on AArch64. This provides the
 * foundation for future demand paging, copy-on-write, and other virtual
 * memory features.
 *
 * ## AArch64 Fault Status Codes (DFSC/IFSC)
 *
 * The fault status code is in ESR_EL1[5:0] and indicates the cause:
 * - 0b0000xx: Address size fault at level xx
 * - 0b0001xx: Translation fault at level xx
 * - 0b0010xx: Access flag fault at level xx
 * - 0b0011xx: Permission fault at level xx
 * - 0b010000: Synchronous external abort
 * - 0b011000: Synchronous parity/ECC error
 * - 0b100001: Alignment fault
 * - 0b110000: TLB conflict abort
 *
 * ## ESR_EL1 Fields for Data Abort (EC=0x24/0x25)
 *
 * - [5:0]   DFSC: Data Fault Status Code
 * - [6]     WnR: Write not Read (1=write, 0=read)
 * - [7]     S1PTW: Stage 1 translation table walk fault
 * - [8]     CM: Cache maintenance operation fault
 * - [9]     EA: External abort type
 * - [10]    FnV: FAR not valid (1=FAR invalid)
 * - [11]    SET: Synchronous error type
 * - [12]    VNCR: VNCR_EL2 register trap
 * - [13]    AR: Acquire/Release semantics
 * - [14]    SF: 64-bit register transfer
 * - [23:22] SSE: Syndrome Sign Extend
 * - [24]    ISV: Instruction Syndrome Valid
 */

#include "fault.hpp"
#include "../console/gcon.hpp"
#include "../console/serial.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"

namespace mm
{

// ESR field extraction helpers
namespace esr_fields
{

/// Extract fault status code (DFSC/IFSC) from ESR
constexpr u32 fault_status(u64 esr)
{
    return static_cast<u32>(esr & 0x3F);
}

/// Extract Write-not-Read bit (1=write, 0=read) - only valid for data aborts
constexpr bool is_write(u64 esr)
{
    return (esr & (1 << 6)) != 0;
}

/// Extract FAR-not-Valid bit (1=FAR is invalid)
constexpr bool far_not_valid(u64 esr)
{
    return (esr & (1 << 10)) != 0;
}

/// Extract exception class from ESR
constexpr u32 exception_class(u64 esr)
{
    return static_cast<u32>((esr >> 26) & 0x3F);
}

/// Extract page table level from fault status (for faults that include level)
constexpr i8 fault_level(u32 fsc)
{
    // Level is encoded in bits [1:0] for address/translation/access/permission faults
    u32 type = (fsc >> 2) & 0xF;
    if (type <= 3)
    {
        return static_cast<i8>(fsc & 0x3);
    }
    return -1; // Not applicable
}

} // namespace esr_fields

const char *fault_type_name(FaultType type)
{
    switch (type)
    {
        case FaultType::ADDRESS_SIZE:
            return "address size fault";
        case FaultType::TRANSLATION:
            return "translation fault";
        case FaultType::ACCESS_FLAG:
            return "access flag fault";
        case FaultType::PERMISSION:
            return "permission fault";
        case FaultType::EXTERNAL:
            return "external abort";
        case FaultType::PARITY:
            return "parity/ECC error";
        case FaultType::ALIGNMENT:
            return "alignment fault";
        case FaultType::TLB_CONFLICT:
            return "TLB conflict";
        default:
            return "unknown fault";
    }
}

/**
 * @brief Classify the fault type from the fault status code.
 */
static FaultType classify_fault(u32 fsc)
{
    // Upper 4 bits of FSC determine the fault class
    u32 fault_class = (fsc >> 2) & 0xF;

    switch (fault_class)
    {
        case 0b0000: // Address size fault
            return FaultType::ADDRESS_SIZE;
        case 0b0001: // Translation fault
            return FaultType::TRANSLATION;
        case 0b0010: // Access flag fault
            return FaultType::ACCESS_FLAG;
        case 0b0011: // Permission fault
            return FaultType::PERMISSION;
        default:
            break;
    }

    // Check specific codes
    switch (fsc)
    {
        case 0b010000: // Synchronous external abort, not on translation table walk
        case 0b010001: // Synchronous external abort on translation table walk, level 0-3
        case 0b010010:
        case 0b010011:
        case 0b010100:
        case 0b010101:
            return FaultType::EXTERNAL;

        case 0b011000: // Synchronous parity/ECC error
        case 0b011001:
        case 0b011010:
        case 0b011011:
        case 0b011100:
        case 0b011101:
            return FaultType::PARITY;

        case 0b100001: // Alignment fault
            return FaultType::ALIGNMENT;

        case 0b110000: // TLB conflict abort
            return FaultType::TLB_CONFLICT;

        default:
            return FaultType::UNKNOWN;
    }
}

FaultInfo parse_fault(u64 fault_addr, u64 esr, u64 elr, bool is_instruction, bool is_user)
{
    FaultInfo info;

    info.fault_addr = fault_addr;
    info.pc = elr;
    info.esr = esr;
    info.is_instruction_fault = is_instruction;
    info.is_user = is_user;

    u32 fsc = esr_fields::fault_status(esr);
    info.type = classify_fault(fsc);
    info.level = esr_fields::fault_level(fsc);

    // Write bit is only meaningful for data aborts
    info.is_write = !is_instruction && esr_fields::is_write(esr);

    return info;
}

/**
 * @brief Log fault details to serial and graphics console.
 */
static void log_fault(const FaultInfo &info, const char *task_name)
{
    // Serial console output
    serial::puts("\n[page_fault] ");
    serial::puts(info.is_user ? "User" : "Kernel");
    serial::puts(" ");
    serial::puts(info.is_instruction_fault ? "instruction" : "data");
    serial::puts(" fault\n");

    serial::puts("[page_fault] Task: ");
    serial::puts(task_name);
    serial::puts("\n");

    serial::puts("[page_fault] Type: ");
    serial::puts(fault_type_name(info.type));
    if (info.level >= 0)
    {
        serial::puts(" (level ");
        serial::put_dec(info.level);
        serial::puts(")");
    }
    serial::puts("\n");

    serial::puts("[page_fault] Address: ");
    serial::put_hex(info.fault_addr);
    serial::puts("\n");

    serial::puts("[page_fault] PC: ");
    serial::put_hex(info.pc);
    serial::puts("\n");

    if (!info.is_instruction_fault)
    {
        serial::puts("[page_fault] Access: ");
        serial::puts(info.is_write ? "write" : "read");
        serial::puts("\n");
    }

    serial::puts("[page_fault] ESR: ");
    serial::put_hex(info.esr);
    serial::puts("\n");

    // Graphics console output
    if (gcon::is_available())
    {
        gcon::set_colors(gcon::colors::VIPER_YELLOW, gcon::colors::BLACK);
        gcon::puts("\n[page_fault] ");
        gcon::puts(info.is_user ? "User" : "Kernel");
        gcon::puts(" ");
        gcon::puts(fault_type_name(info.type));
        gcon::puts(" at ");

        // Simple hex output for graphics console
        const char hex[] = "0123456789ABCDEF";
        gcon::puts("0x");
        for (int i = 60; i >= 0; i -= 4)
        {
            gcon::putc(hex[(info.fault_addr >> i) & 0xF]);
        }
        gcon::puts("\n");
        gcon::set_colors(gcon::colors::VIPER_WHITE, gcon::colors::BLACK);
    }
}

/**
 * @brief Kernel panic for unrecoverable faults.
 */
[[noreturn]] static void kernel_panic(const FaultInfo &info)
{
    serial::puts("\n!!! KERNEL PANIC: Unhandled page fault in kernel mode !!!\n");
    serial::puts("Type: ");
    serial::puts(fault_type_name(info.type));
    serial::puts(" at address ");
    serial::put_hex(info.fault_addr);
    serial::puts("\n");

    if (gcon::is_available())
    {
        gcon::set_colors(gcon::colors::VIPER_RED, gcon::colors::BLACK);
        gcon::puts("\n\n  !!! KERNEL PANIC !!!\n");
        gcon::puts("  ");
        gcon::puts(fault_type_name(info.type));
        gcon::puts(" in kernel mode\n");
        gcon::set_colors(gcon::colors::VIPER_WHITE, gcon::colors::BLACK);
    }

    serial::puts("\nSystem halted.\n");
    for (;;)
    {
        asm volatile("wfi");
    }
}

void handle_page_fault(exceptions::ExceptionFrame *frame, bool is_instruction)
{
    // Determine if fault is from user mode by checking SPSR.M[3:0]
    // EL0 = 0b0000, EL1 = 0b0100/0b0101
    u32 spsr_el = (frame->spsr & 0xF);
    bool is_user = (spsr_el == 0);

    // Parse the fault information
    FaultInfo info = parse_fault(frame->far, frame->esr, frame->elr, is_instruction, is_user);

    // Get task name for logging
    task::Task *current = task::current();
    const char *task_name = current ? current->name : "<unknown>";

    // Log the fault
    log_fault(info, task_name);

    // TODO: Future demand paging implementation
    // =========================================
    // For TRANSLATION faults in user space:
    //   1. Check if address is in a valid VMA (virtual memory area)
    //   2. If valid, allocate a physical page and map it
    //   3. Return to retry the faulting instruction
    //
    // For PERMISSION faults (potential copy-on-write):
    //   1. Check if page is marked COW
    //   2. If COW, copy the page and update mapping to writable
    //   3. Return to retry the faulting instruction
    //
    // For faults near stack:
    //   1. Check if address is in stack growth region
    //   2. If so, extend the stack and map new pages
    //   3. Return to retry the faulting instruction

    // For now, all faults are fatal

    if (!is_user)
    {
        // Kernel fault - panic
        kernel_panic(info);
    }

    // User fault - terminate the task gracefully
    serial::puts("[page_fault] Terminating user task\n");

    // Terminate the task
    task::exit(-1);

    // Should never reach here
    serial::puts("[page_fault] PANIC: task::exit returned!\n");
    for (;;)
    {
        asm volatile("wfi");
    }
}

} // namespace mm
