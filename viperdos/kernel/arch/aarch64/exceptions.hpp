#pragma once

#include "../../include/types.hpp"

/**
 * @file exceptions.hpp
 * @brief AArch64 exception handling interfaces and frame definitions.
 *
 * @details
 * When the CPU takes an exception (synchronous fault, IRQ, FIQ, SError), the
 * assembly vector code saves register state into an @ref exceptions::ExceptionFrame
 * and then calls into C++ handlers declared here.
 *
 * This header defines:
 * - The saved register frame layout shared with the assembly trampoline.
 * - Exception class constants extracted from ESR_EL1.
 * - Kernel-side helpers for installing vectors and controlling interrupts.
 * - C-linkage handler entry points called directly from assembly.
 */
namespace exceptions {

/**
 * @brief Saved register state for an exception.
 *
 * @details
 * The assembly exception vectors save general-purpose registers and key system
 * registers into this frame. The layout must match the save/restore sequence
 * in `exceptions.S` exactly, because the assembly code treats the frame as a
 * raw memory block at a fixed size/offset.
 *
 * The `sp` field captures the interrupted context's stack pointer:
 * - For exceptions taken from EL1, it stores the kernel SP value prior to
 *   frame allocation.
 * - For exceptions taken from EL0, it stores SP_EL0 (the user stack pointer).
 */
struct ExceptionFrame {
    u64 x[30]; ///< General-purpose registers x0-x29.
    u64 lr;    ///< Saved x30 (link register).
    u64 sp;    ///< Saved interrupted SP (kernel SP or SP_EL0 depending on origin).
    u64 elr;   ///< ELR_EL1: return address for `eret`.
    u64 spsr;  ///< SPSR_EL1: saved program status for `eret`.
    u64 esr;   ///< ESR_EL1: exception syndrome (class/ISS).
    u64 far;   ///< FAR_EL1: faulting address for aborts.
};

// Exception classes (from ESR_EL1.EC field)
/**
 * @brief Exception class values extracted from `ESR_EL1.EC`.
 *
 * @details
 * The EC field identifies the high-level cause of a synchronous exception
 * (e.g. SVC, instruction abort, data abort). Handlers commonly use EC to
 * decide whether an exception is a syscall, a fault, or an unexpected event.
 */
namespace ec {
constexpr u32 UNKNOWN = 0x00;
constexpr u32 WFI_WFE = 0x01;
constexpr u32 CP15_MCR_MRC = 0x03;
constexpr u32 CP15_MCRR_MRRC = 0x04;
constexpr u32 CP14_MCR_MRC = 0x05;
constexpr u32 CP14_LDC_STC = 0x06;
constexpr u32 SVE_ASIMD_FP = 0x07;
constexpr u32 CP10_MRC = 0x08;
constexpr u32 PAC = 0x09;
constexpr u32 CP14_MRRC = 0x0C;
constexpr u32 ILLEGAL_STATE = 0x0E;
constexpr u32 SVC_A32 = 0x11;
constexpr u32 SVC_A64 = 0x15;
constexpr u32 SYS_A64 = 0x18;
constexpr u32 SVE = 0x19;
constexpr u32 INST_ABORT_LOWER = 0x20;
constexpr u32 INST_ABORT_SAME = 0x21;
constexpr u32 PC_ALIGN = 0x22;
constexpr u32 DATA_ABORT_LOWER = 0x24;
constexpr u32 DATA_ABORT_SAME = 0x25;
constexpr u32 SP_ALIGN = 0x26;
constexpr u32 FP_A32 = 0x28;
constexpr u32 FP_A64 = 0x2C;
constexpr u32 SERROR = 0x2F;
constexpr u32 BREAKPOINT_LOWER = 0x30;
constexpr u32 BREAKPOINT_SAME = 0x31;
constexpr u32 SOFTWARE_STEP_LOWER = 0x32;
constexpr u32 SOFTWARE_STEP_SAME = 0x33;
constexpr u32 WATCHPOINT_LOWER = 0x34;
constexpr u32 WATCHPOINT_SAME = 0x35;
constexpr u32 BRK_A32 = 0x38;
constexpr u32 BRK_A64 = 0x3C;
} // namespace ec

/**
 * @brief Install exception vectors and initialize exception handling.
 *
 * @details
 * Loads the exception vector base address (VBAR_EL1) via the assembly helper.
 * This must be called before enabling interrupts so that IRQs and synchronous
 * faults have valid handler targets.
 */
void init();

/**
 * @brief Enable IRQ delivery at EL1.
 *
 * @details
 * Clears the IRQ mask bit in DAIF.
 */
void enable_interrupts();
/**
 * @brief Disable IRQ delivery at EL1.
 *
 * @details
 * Sets the IRQ mask bit in DAIF. This is typically used to protect critical
 * sections during early bring-up when fine-grained locking is not available.
 */
void disable_interrupts();

/**
 * @brief Check whether IRQs are currently enabled at EL1.
 *
 * @return `true` if the IRQ mask is clear in DAIF, otherwise `false`.
 */
bool interrupts_enabled();

} // namespace exceptions

// C linkage for assembly handlers
extern "C" {
// Kernel (EL1) exception handlers
/**
 * @brief Handle a synchronous exception taken at EL1.
 *
 * @param frame Pointer to the saved register frame.
 */
void handle_sync_exception(exceptions::ExceptionFrame *frame);
/** @brief Handle an IRQ exception taken at EL1. */
void handle_irq(exceptions::ExceptionFrame *frame);
/** @brief Handle an FIQ exception taken at EL1 (currently unexpected). */
void handle_fiq(exceptions::ExceptionFrame *frame);
/** @brief Handle an SError exception taken at EL1. */
void handle_serror(exceptions::ExceptionFrame *frame);
/** @brief Handle an exception routed to an invalid vector. */
void handle_invalid_exception(exceptions::ExceptionFrame *frame);

// User (EL0) exception handlers
/**
 * @brief Handle a synchronous exception taken from EL0 (user mode).
 *
 * @details
 * This is used for user syscalls (SVC) as well as user-mode faults. The
 * handler is responsible for returning results to the user via the saved
 * register frame (typically by setting `x0`) or terminating the task on
 * fatal faults.
 *
 * @param frame Pointer to the saved register frame.
 */
void handle_el0_sync(exceptions::ExceptionFrame *frame);
/** @brief Handle an IRQ taken while executing in EL0. */
void handle_el0_irq(exceptions::ExceptionFrame *frame);
/** @brief Handle an SError taken while executing in EL0. */
void handle_el0_serror(exceptions::ExceptionFrame *frame);

// Assembly functions
/**
 * @brief Install the exception vector table by setting `VBAR_EL1`.
 *
 * @details
 * Implemented in `exceptions.S`. This is separated so the C++ side does not
 * need to embed address calculations or privileged register writes inline.
 */
void exceptions_init_asm();

// Enter user mode for the first time
/**
 * @brief Transition from EL1 into EL0 and begin executing user code.
 *
 * @details
 * Implemented in `exceptions.S`. Programs SP_EL0 (user stack pointer),
 * ELR_EL1 (return address), and SPSR_EL1 (target EL/mode), sets the initial
 * user argument in x0, clears remaining registers, and executes `eret`.
 *
 * This function does not return.
 *
 * @param entry User entry point virtual address.
 * @param stack User stack pointer (typically top of user stack).
 * @param arg Initial argument passed to user in x0.
 */
[[noreturn]] void enter_user_mode(u64 entry, u64 stack, u64 arg);
}
