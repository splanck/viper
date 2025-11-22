//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/tco.hpp
// Purpose: Tail-call optimisation helper for reusing the current frame. 
// Key invariants: To be documented.
// Ownership/Lifetime: Modifies the provided ExecState in place; no allocation beyond vectors.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include "vm/VM.hpp"

namespace il::vm
{

/// @brief Try to perform a tail call by reusing the current frame.
/// @param vm Owning VM used to locate the current execution state.
/// @param callee Function to enter.
/// @param args Evaluated argument slots for the callee's entry parameters.
/// @return true if the frame was reused and control transferred to callee.
bool tryTailCall(VM &vm, const il::core::Function *callee, std::span<const Slot> args);

} // namespace il::vm
