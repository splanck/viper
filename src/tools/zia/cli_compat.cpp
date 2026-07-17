//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Compatibility shim for zanna CLI-style usage printing.
/// @details Provides a `usage()` function expected by shared command entry points
///          and forwards to the Zia-specific usage printer.

#include "usage.hpp"

/// @brief Usage hook for zanna CLI compatibility.
/// @details Calls @ref zia::printUsage so shared code paths can
///          emit the Zia-specific help text.
void usage() {
    zia::printUsage();
}
