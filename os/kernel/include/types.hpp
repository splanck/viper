//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/include/types.hpp
// Purpose: Fundamental fixed-width type aliases for the kernel.
// Key invariants: Re-exports shared types; adds nullptr for freestanding builds.
// Ownership/Lifetime: Header-only; no runtime state.
// Links: include/viperos/types.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

/**
 * @file types.hpp
 * @brief Fundamental fixed-width type aliases for the kernel.
 *
 * @details
 * This header re-exports the shared ViperOS types and adds kernel-specific
 * definitions (such as the nullptr macro for freestanding builds).
 *
 * Kernel code should include this header rather than the shared types header
 * directly to ensure all kernel-specific definitions are available.
 */

/* Include shared types (also used by user-space) */
#include "../../include/viperos/types.hpp"

/**
 * @brief Null pointer constant for environments without a standard definition.
 *
 * @details
 * This project defines `nullptr` as `0` to provide a consistent null pointer
 * constant in freestanding code. In hosted C++ environments `nullptr` is a
 * language keyword; this macro exists for the kernel's constrained build setup
 * and should be treated as a compatibility shim.
 */
#define nullptr 0
