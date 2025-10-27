// File: src/il/verify/ExceptionHandlerAnalysis.hpp
// Purpose: Declare helpers that analyse exception-handler blocks and signatures.
// Key invariants: Handler blocks must declare (%err:Error, %tok:ResumeTok) with
// the expected parameter identifiers.
// Ownership/Lifetime: Operates on verifier-supplied IL references without owning
// them.
// Links: docs/il-guide.md#reference
#pragma once

#include "support/diag_expected.hpp"

#include <optional>

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

} // namespace il::verify

