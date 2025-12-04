//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/il/Module.hpp
// Purpose: Stable public entry point for IL core aggregates used by frontends.
// Key invariants: Re-exports only supported IL core structures; avoid leaking internals.
// Ownership/Lifetime: Types mirror definitions in il::core and retain their semantics.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

/// @file include/viper/il/Module.hpp
/// @brief Public aggregation header exposing IL core types for clients such as
///        language frontends.  Only stable surface structures are re-exported;
///        helper utilities remain under src/il/internal.
/// @notes See docs/il-guide.md#reference for semantics of the contained types.

namespace il
{
namespace core
{
// Forward declarations live in il/core/fwd.hpp; this header intentionally
// includes concrete definitions so downstream clients receive full type
// information without depending on src/ paths.
} // namespace core
} // namespace il
