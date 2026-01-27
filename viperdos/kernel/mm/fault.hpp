//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

/**
 * @file fault.hpp
 * @brief Page fault handling interface.
 *
 * @details
 * This module provides the foundation for handling memory faults (data aborts
 * and instruction aborts) on AArch64. Currently it logs faults and terminates
 * the faulting task, but is designed to be extended with:
 * - Demand paging (lazy page allocation)
 * - Copy-on-write (COW) for forked processes
 * - Stack growth (automatic stack expansion)
 * - Memory-mapped file handling
 */

#include "../arch/aarch64/exceptions.hpp"
#include "../include/types.hpp"

namespace mm {

/**
 * @brief Classification of page fault types.
 *
 * @details
 * Derived from the ESR_EL1.DFSC (Data Fault Status Code) or IFSC (Instruction
 * Fault Status Code) fields. These codes help determine the appropriate
 * response to a fault.
 */
enum class FaultType : u8 {
    /// Address size fault (virtual address too large for configured levels)
    ADDRESS_SIZE = 0,

    /// Translation fault - no valid page table entry at some level
    /// This is the primary fault type for demand paging
    TRANSLATION = 1,

    /// Access flag fault - page table entry exists but access flag not set
    ACCESS_FLAG = 2,

    /// Permission fault - page exists but access not permitted
    /// Used for copy-on-write detection (write to read-only page)
    PERMISSION = 3,

    /// Synchronous external abort (hardware error)
    EXTERNAL = 4,

    /// Synchronous parity/ECC error
    PARITY = 5,

    /// Alignment fault
    ALIGNMENT = 6,

    /// TLB conflict abort
    TLB_CONFLICT = 7,

    /// Unknown or reserved fault type
    UNKNOWN = 255,
};

/**
 * @brief Additional information about a page fault.
 */
struct FaultInfo {
    /// Virtual address that caused the fault (from FAR_EL1)
    u64 fault_addr;

    /// Faulting instruction address (from ELR_EL1)
    u64 pc;

    /// Raw ESR value for detailed analysis
    u64 esr;

    /// Classified fault type
    FaultType type;

    /// True if fault was a write access, false for read
    bool is_write;

    /// True if fault was during instruction fetch
    bool is_instruction_fault;

    /// True if fault originated from EL0 (user mode)
    bool is_user;

    /// Page table level where fault occurred (0-3, or -1 if N/A)
    i8 level;
};

/**
 * @brief Convert a FaultType to a human-readable string.
 */
const char *fault_type_name(FaultType type);

/**
 * @brief Parse ESR_EL1 to extract fault information.
 *
 * @param fault_addr Virtual address from FAR_EL1.
 * @param esr Exception Syndrome Register value.
 * @param elr Exception Link Register (faulting PC).
 * @param is_instruction True if this is an instruction abort.
 * @param is_user True if fault originated from EL0.
 * @return Parsed fault information.
 */
FaultInfo parse_fault(u64 fault_addr, u64 esr, u64 elr, bool is_instruction, bool is_user);

/**
 * @brief Handle a page fault (data abort or instruction abort).
 *
 * @details
 * This is the main entry point for page fault handling. Currently it:
 * 1. Parses the fault information from ESR/FAR
 * 2. Logs the fault details for debugging
 * 3. Terminates the faulting task (user mode) or panics (kernel mode)
 *
 * TODO: Future enhancements:
 * - Demand paging: For translation faults, allocate and map a zero page
 * - Copy-on-write: For permission faults on COW pages, copy and remap
 * - Stack growth: For faults near stack guard page, grow the stack
 * - Swap: For translation faults on swapped pages, bring page back in
 *
 * @param frame Exception frame with saved registers.
 * @param is_instruction True if this is an instruction abort (EC 0x20/0x21).
 */
void handle_page_fault(exceptions::ExceptionFrame *frame, bool is_instruction);

} // namespace mm
