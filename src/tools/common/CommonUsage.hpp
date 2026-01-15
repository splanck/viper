//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/common/CommonUsage.hpp
// Purpose: Shared CLI option text for frontend tools (vbasic, vpascal, zia).
// Key invariants: All frontend tools share the same option descriptions.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ostream>

namespace viper::tools
{

/// @brief Print shared CLI option descriptions to an output stream.
/// @details Outputs the standard option descriptions used by all frontend tools.
///          This ensures consistency across vbasic, vpascal, and zia help text.
/// @param os Output stream to write to (typically std::cerr).
inline void printSharedOptions(std::ostream &os)
{
    os << "  -o, --output FILE              Output file for IL\n"
       << "  --emit-il                      Emit IL instead of running\n"
       << "  --trace[=il|src]               Enable execution tracing\n"
       << "  --bounds-checks                Enable array bounds checking\n"
       << "  --stdin-from FILE              Redirect stdin from file\n"
       << "  --max-steps N                  Limit execution steps\n"
       << "  --dump-trap                    Show detailed trap diagnostics\n"
       << "  -h, --help                     Show this help message\n"
       << "  --version                      Show version information\n";
}

} // namespace viper::tools
