//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Compatibility shims for viper CLI functions used by cmd_front_pascal.cpp
//
//===----------------------------------------------------------------------===//

#include "usage.hpp"

/// @brief Compatibility wrapper for usage() called from cmd_front_pascal.cpp
///
/// @details The cmdFrontPascal implementation calls the global usage() function
///          when argument parsing fails. For vpascal, we redirect to our own
///          vpascal-specific help text.
void usage()
{
    vpascal::printUsage();
}
