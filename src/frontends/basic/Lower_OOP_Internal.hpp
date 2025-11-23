//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Lower_OOP_Internal.hpp
// Purpose: Internal shared declarations for OOP lowering implementation.
// Key invariants: For use only by OOP lowering translation units.
// Ownership/Lifetime: Non-owning references to Lowerer and OOP metadata.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/OopLoweringContext.hpp"

namespace il::frontends::basic
{

// Internal helper functions shared between OOP lowering translation units.
// These are implementation details and should not be exposed in the public Lowerer interface.

} // namespace il::frontends::basic