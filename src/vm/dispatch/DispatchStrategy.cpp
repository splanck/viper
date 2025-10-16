// File: src/vm/dispatch/DispatchStrategy.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements factory logic for selecting interpreter dispatch strategies.
// Key invariants: Returns a non-null strategy for all supported dispatch kinds and
//                 falls back to switch dispatch when threaded dispatch is unavailable.
// Ownership/Lifetime: Strategies are allocated per VM instance and owned via
//                     std::unique_ptr.
// Links: docs/il-guide.md#reference

#include "vm/dispatch/DispatchStrategy.hpp"

namespace il::vm
{

std::unique_ptr<DispatchStrategy> createDispatchStrategy(VM::DispatchKind kind)
{
    switch (kind)
    {
        case VM::DispatchKind::FnTable:
            return detail::createFnTableDispatchStrategy();
        case VM::DispatchKind::Switch:
            return detail::createSwitchDispatchStrategy();
        case VM::DispatchKind::Threaded:
        {
#if VIPER_THREADING_SUPPORTED
            return detail::createThreadedDispatchStrategy();
#else
            return detail::createSwitchDispatchStrategy();
#endif
        }
    }
    return detail::createSwitchDispatchStrategy();
}

} // namespace il::vm
