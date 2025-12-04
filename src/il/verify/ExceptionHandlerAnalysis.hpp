//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares analysis utilities for identifying and validating exception
// handler basic blocks within IL functions. Handler blocks have a special signature
// structure that must be verified to ensure correct exception handling semantics.
//
// The IL exception handling model requires that handler blocks (targets of eh.push
// instructions) declare exactly two parameters: an Error value representing the
// caught exception and a ResumeTok enabling resume operations. These parameters
// must follow a specific naming convention (%err and %tok) to support runtime
// exception dispatch.
//
// Key Responsibilities:
// - Identify handler blocks by analyzing their parameter structure
// - Validate handler blocks have exactly two parameters with correct types
// - Verify handler parameters use the required identifiers (%err, %tok)
// - Extract parameter IDs for use in exception handling verification
//
// Design Notes:
// The analyzeHandlerBlock function returns an optional HandlerSignature, using
// std::nullopt to indicate a block is not a handler and Expected<>::error() to
// indicate a handler with invalid structure. This three-way result enables the
// verifier to distinguish between non-handlers, valid handlers, and malformed
// handlers requiring different verification logic.
//
//===----------------------------------------------------------------------===//

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
[[nodiscard]] il::support::Expected<std::optional<HandlerSignature>> analyzeHandlerBlock(
    const il::core::Function &fn, const il::core::BasicBlock &bb);

} // namespace il::verify
