/**
 * @file dispatch.cpp
 * @brief Minimal syscall dispatch entry point.
 *
 * @details
 * This file provides the syscall dispatch() function called from the
 * AArch64 exception handler. It extracts syscall number and arguments
 * from the exception frame and delegates to the table-driven dispatcher.
 *
 * ## ViperDOS Syscall ABI (AArch64)
 *
 * **Input registers:**
 * - x8: Syscall number (SYS_* constant)
 * - x0-x5: Up to 6 input arguments
 *
 * **Output registers:**
 * - x0: VError code (0 = success, negative = error)
 * - x1: Result value 0 (if syscall produces a result)
 * - x2: Result value 1 (if syscall produces multiple results)
 * - x3: Result value 2 (if syscall produces multiple results)
 */
#include "dispatch.hpp"
#include "table.hpp"

namespace syscall
{

/**
 * @brief Dispatch the syscall described by the exception frame.
 *
 * @details
 * Extracts syscall number and arguments from the saved registers and
 * delegates to the table-driven dispatcher. Results are placed back
 * into the exception frame for return to the caller.
 *
 * @param frame Exception frame captured at the SVC instruction.
 */
void dispatch(exceptions::ExceptionFrame *frame)
{
    // Get syscall number from x8
    u32 syscall_num = static_cast<u32>(frame->x[8]);

    // Get arguments from x0-x5
    u64 a0 = frame->x[0];
    u64 a1 = frame->x[1];
    u64 a2 = frame->x[2];
    u64 a3 = frame->x[3];
    u64 a4 = frame->x[4];
    u64 a5 = frame->x[5];

    // Dispatch via table
    SyscallResult result = dispatch_syscall(syscall_num, a0, a1, a2, a3, a4, a5);

    // Store results per ABI: x0=VError, x1-x3=results
    frame->x[0] = static_cast<u64>(result.verr);
    frame->x[1] = result.res0;
    frame->x[2] = result.res1;
    frame->x[3] = result.res2;
}

} // namespace syscall
