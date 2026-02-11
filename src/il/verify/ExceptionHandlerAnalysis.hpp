//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/verify/ExceptionHandlerAnalysis.hpp
// Purpose: Analysis utilities for identifying and validating exception handler
//          basic blocks. Handler blocks must declare exactly two parameters
//          (Error + ResumeTok) with specific naming (%err, %tok). Returns a
//          three-way result: not-a-handler, valid handler, or malformed handler.
// Key invariants:
//   - Handler blocks have exactly two parameters: Error and ResumeTok.
//   - std::nullopt means "not a handler"; error means "malformed handler".
// Ownership/Lifetime: Stateless free function operating on caller-owned IL
//          structures.
// Links: support/diag_expected.hpp
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
