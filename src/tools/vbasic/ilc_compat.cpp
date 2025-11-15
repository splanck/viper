//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Compatibility shims for ilc functions used by cmd_front_basic.cpp
//
//===----------------------------------------------------------------------===//

#include "usage.hpp"

/// @brief Compatibility wrapper for usage() called from cmd_front_basic.cpp
///
/// @details The cmdFrontBasic implementation calls the global usage() function
///          when argument parsing fails. For vbasic, we redirect to our own
///          vbasic-specific help text.
void usage()
{
    vbasic::printUsage();
}
