//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Compatibility shims for viper CLI functions used by cmd_run_il.cpp
//
//===----------------------------------------------------------------------===//

#include <iostream>

/// @brief Stub for usage() called from cmd_run_il.cpp
///
/// @details cmdRunIL doesn't actually call usage() in normal operation,
///          but it's referenced from the compilation unit. This stub
///          provides a minimal implementation.
void usage()
{
    std::cerr << "ilrun: internal error - usage() called\n";
    std::cerr << "Please use: ilrun --help\n";
}
