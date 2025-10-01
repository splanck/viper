// File: src/il/verify/InstructionStrategies.hpp
// Purpose: Declare factory helpers that register instruction verification strategies.
// Key invariants: Strategies partition opcode handling between control-flow and generic instruction
// checks. Ownership/Lifetime: Returns unique_ptr-owned strategies transferred to the caller
// (FunctionVerifier). Links: docs/il-guide.md#reference
#pragma once

#include "il/verify/FunctionVerifier.hpp"

#include <memory>
#include <vector>

namespace il::verify
{

/// @brief Construct the default set of instruction strategies used by FunctionVerifier.
std::vector<std::unique_ptr<FunctionVerifier::InstructionStrategy>>
makeDefaultInstructionStrategies();

} // namespace il::verify
