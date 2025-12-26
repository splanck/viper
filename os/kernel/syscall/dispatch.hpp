#pragma once

#include "../arch/aarch64/exceptions.hpp"
#include "../include/types.hpp"

/**
 * @file dispatch.hpp
 * @brief Kernel syscall dispatcher interface.
 *
 * @details
 * Syscalls on AArch64 are issued via `svc #0`. The exception handler captures
 * the CPU register state into an @ref exceptions::ExceptionFrame and then hands
 * control to the syscall dispatcher.
 *
 * The dispatcher interprets:
 * - x8 as the syscall number.
 * - x0-x5 as up to six syscall arguments.
 *
 * Results are returned by writing back to the saved register frame (typically
 * `x0` for the primary return value).
 */
namespace syscall
{

/**
 * @brief Dispatch a syscall based on the supplied exception frame.
 *
 * @details
 * Looks up the syscall number in the shared syscall number table and executes
 * the corresponding kernel implementation. The dispatcher is responsible for
 * interpreting raw argument registers as pointers/integers and for returning
 * result codes in the ABI-defined registers.
 *
 * Callers are expected to invoke this only for SVC exceptions (i.e. syscalls),
 * not for arbitrary synchronous exceptions.
 *
 * @param frame Saved register state at the point of the SVC.
 */
void dispatch(exceptions::ExceptionFrame *frame);

} // namespace syscall
