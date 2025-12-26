//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the top-level Verifier class, which provides the public
// interface for validating IL modules. This is the main entry point for IL
// verification, coordinating all verification passes.
//
// The IL specification defines structural and semantic constraints that every
// valid IL module must satisfy: unique symbol names, well-typed expressions,
// properly structured control flow, balanced exception handlers, and valid
// references between entities. The Verifier orchestrates the specialized verifier
// components (ExternVerifier, GlobalVerifier, FunctionVerifier, EhVerifier) to
// enforce these constraints.
//
// Key Responsibilities:
// - Provide the public verify() interface for IL module validation
// - Orchestrate the verification pipeline (externs -> globals -> functions -> EH)
// - Report the first verification error encountered
// - Ensure verification is stateless and thread-safe
//
// Design Notes:
// The Verifier class is a simple facade with only a static verify() method. It
// delegates to specialized verifier components, each responsible for one aspect
// of module validation. Verification proceeds in dependency order: externs must
// be validated before functions (since functions reference externs), functions
// must be validated before exception handling (since EH analysis examines function
// bodies). The first error encountered stops verification and returns immediately,
// avoiding cascading errors from invalid IL.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diag_expected.hpp"

namespace il::core
{
struct Module;
}

namespace il::verify
{

/// @brief Verifies structural and type rules for a module.
class Verifier
{
  public:
    /// @brief Verify module @p m against the IL specification.
    /// @param m Module to verify.
    /// @return Expected success or diagnostic on failure.
    [[nodiscard]] static il::support::Expected<void> verify(const il::core::Module &m);
};

} // namespace il::verify
