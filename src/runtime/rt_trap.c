//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC runtime's fatal trap helpers.  These routines print a
// diagnostic describing the failure before terminating the hosting process.
// Centralising the logic keeps trap text and exit codes consistent between the
// VM and native code paths.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Fatal trap helpers shared by the runtime C ABI.
/// @details Provides @ref rt_trap_div0, mirroring the VM's behaviour when a
///          division by zero occurs.  The helper writes a deterministic message
///          to stderr and terminates the process with exit code 1.

#include <stdio.h>
#include <stdlib.h>

#include "rt_trap.h"

/// @brief Report a division-by-zero trap and terminate the process.
/// @details Prints a fixed diagnostic to stderr, flushes the stream to ensure
///          embedders observe the message, and exits with status code 1.  The
///          behaviour mirrors the VM trap hook so test suites observe consistent
///          failure semantics across execution modes.
void rt_trap_div0(void)
{
    fprintf(stderr, "Viper runtime trap: division by zero\n");
    fflush(stderr);
    exit(1); // Match VM behavior if your VM uses a specific code; adjust here later if needed.
}
