// File: src/vm/dispatch/DispatchStrategy.hpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Declares interpreter dispatch strategy interface for executing VM loops.
// Key invariants: Strategies operate on valid VMContext and ExecState references
//                 without owning VM lifetime.
// Ownership/Lifetime: Strategies are owned by the VM and hold no global state.
// Links: docs/il-guide.md#reference
#pragma once

#include "vm/VMContext.hpp"

#include <memory>

namespace il::vm
{

/// @brief Interface implemented by interpreter dispatch strategies.
class DispatchStrategy
{
  public:
    virtual ~DispatchStrategy() = default;

    /// @brief Execute the interpreter loop using a specific dispatch mechanism.
    /// @param context Helper exposing VM evaluation utilities.
    /// @param state   Execution state for the active frame.
    /// @return True when the strategy stored a result in @p state.pendingResult.
    virtual bool run(VMContext &context, VM::ExecState &state) = 0;
};

/// @brief Factory selecting a dispatch strategy for the requested kind.
std::unique_ptr<DispatchStrategy> createDispatchStrategy(VM::DispatchKind kind);

namespace detail
{
std::unique_ptr<DispatchStrategy> createFnTableDispatchStrategy();
std::unique_ptr<DispatchStrategy> createSwitchDispatchStrategy();
#if VIPER_THREADING_SUPPORTED
std::unique_ptr<DispatchStrategy> createThreadedDispatchStrategy();
#endif
} // namespace detail

} // namespace il::vm
