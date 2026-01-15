//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Compatibility shim for ilc-style usage printing.
/// @details Provides a legacy `usage()` function expected by older entry points
///          and forwards to the shared Zia usage printer.

#include "usage.hpp"

/// @brief Legacy usage hook for ilc compatibility.
/// @details Calls @ref zia::printUsage so older code paths can continue
///          to emit the standard help text without duplicating strings.
void usage()
{
    zia::printUsage();
}
