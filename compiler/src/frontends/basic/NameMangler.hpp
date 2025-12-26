//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the NameMangler class, which generates deterministic,
// unique names for IL symbols, temporaries, and basic blocks during the
// lowering process.
//
// This file re-exports the common NameMangler from the shared frontend library.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/common/NameMangler.hpp"

namespace il::frontends::basic
{

/// @brief Alias for the common NameMangler.
/// @details BASIC uses the standard common NameMangler with default settings.
using NameMangler = ::il::frontends::common::NameMangler;

} // namespace il::frontends::basic
