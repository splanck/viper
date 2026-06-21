//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements helpers for the Symbol handle returned by the string interner.
// Symbols wrap a 32-bit identifier where zero represents an invalid handle.
// The utilities defined here provide comparisons and hashing used throughout
// the support library.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Compatibility translation unit for `il::support::Symbol`.
/// @details Symbol operations are intentionally defined inline in
///          `support/symbol.hpp` because they are tiny hot-path value operations.
///          This file remains in the support library target so existing build
///          structure and install manifests do not need to special-case the
///          symbol component.

#include "support/symbol.hpp"
