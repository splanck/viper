// File: src/il/verify/ExceptionHandlerAnalysis.hpp
// Purpose: Declare helpers that analyse exception-handler blocks and EH stack balance.
// Key invariants: Handler blocks must declare (%err:Error, %tok:ResumeTok) and EH pushes/pops nest
// properly. Ownership/Lifetime: Operates on verifier-supplied IL references without owning them.
// Links: docs/il-guide.md#reference
#pragma once

#include "support/diag_expected.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace il::core
{
struct BasicBlock;
struct Function;
} // namespace il::core

namespace il::verify
{

/// @brief Captures the parameter IDs associated with a handler's %err and %tok values.
struct HandlerSignature
{
    unsigned errorParam = 0;
    unsigned resumeTokenParam = 0;
};

/// @brief Inspect @p bb and determine whether it is a handler block with a valid signature.
/// @param fn Function providing diagnostic context.
/// @param bb Basic block to analyse.
/// @return Empty optional when @p bb is not a handler, signature when valid, diagnostic otherwise.
il::support::Expected<std::optional<HandlerSignature>> analyzeHandlerBlock(
    const il::core::Function &fn, const il::core::BasicBlock &bb);

/// @brief Validate that EH push/pop usage across the function maintains stack balance.
/// @param fn Function being analysed.
/// @param blockMap Lookup table for successors by label.
/// @return Success when balanced; diagnostic when unmatched push/pop pairs are discovered.
il::support::Expected<void> checkEhStackBalance(
    const il::core::Function &fn,
    const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap);

} // namespace il::verify
