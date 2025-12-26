//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the diagnostic infrastructure for the IL verifier, providing
// interfaces for reporting and collecting verification errors and warnings. The
// DiagSink abstraction decouples verification logic from diagnostic storage and
// output strategies.
//
// The IL verifier must report both hard errors (verification failures) and
// warnings (suspicious patterns that don't violate the spec). Rather than coupling
// verification code to specific output mechanisms, this file defines a sink
// interface that verification passes can report to. Different sink implementations
// can collect diagnostics for batch processing, forward them immediately to stderr,
// or integrate with IDE error reporting systems.
//
// Key Responsibilities:
// - Define structured diagnostic codes for verifier-specific errors
// - Provide DiagSink interface for decoupled diagnostic reporting
// - Implement CollectingDiagSink for in-memory diagnostic accumulation
// - Offer factory functions for constructing verifier diagnostics
//
// Design Rationale:
// The DiagSink pattern follows the observer pattern, allowing verification passes
// to remain agnostic about diagnostic consumption. The VerifyDiagCode enum provides
// structured error identification enabling programmatic diagnostic filtering and
// tooling integration. The distinction between errors (verification failures) and
// warnings (potential issues) supports both strict validation and best-practice
// linting workflows.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diag_expected.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace il::verify
{

/// @brief Identifier for structured verifier diagnostics.
enum class VerifyDiagCode
{
    Unknown = 0,                ///< Unclassified diagnostic.
    EhStackUnderflow,           ///< Encountered eh.pop with an empty handler stack.
    EhStackLeak,                ///< Execution left a function with handlers still active.
    EhResumeTokenMissing,       ///< Resume.* executed without an active resume token.
    EhResumeLabelInvalidTarget, ///< resume.label target does not postdominate the faulting block.
    EhHandlerNotDominant,       ///< Handler block does not dominate a protected faulting block.
    EhHandlerUnreachable        ///< Handler block is not reachable from function entry.
};

/// @brief Convert a verifier diagnostic code to its textual prefix.
/// @param code Diagnostic code to translate.
/// @return Stable string view describing @p code.
std::string_view toString(VerifyDiagCode code);

/// @brief Construct a diagnostic tagged with a verifier code.
/// @param code Structured diagnostic code.
/// @param severity Diagnostic severity classification.
/// @param loc Optional source location associated with the diagnostic.
/// @param message Human readable payload appended after the code prefix.
/// @return Diagnostic ready for reporting through sinks or Expected values.
il::support::Diag makeVerifierDiag(VerifyDiagCode code,
                                   il::support::Severity severity,
                                   il::support::SourceLoc loc,
                                   std::string message);

/// @brief Convenience wrapper that constructs an error diagnostic for verifier failures.
/// @param code Structured diagnostic code.
/// @param loc Optional source location associated with the diagnostic.
/// @param message Human readable description appended after the code prefix.
/// @return Error severity diagnostic referencing the provided verifier code.
il::support::Diag makeVerifierError(VerifyDiagCode code,
                                    il::support::SourceLoc loc,
                                    std::string message);

/// @brief Interface for verifier components to report diagnostics without coupling to storage.
class DiagSink
{
  public:
    virtual ~DiagSink() = default;

    /// @brief Report a diagnostic to the sink.
    /// @param diag Diagnostic to forward or store.
    virtual void report(il::support::Diag diag) = 0;
};

/// @brief Concrete sink that stores diagnostics in-memory for later inspection.
class CollectingDiagSink : public DiagSink
{
  public:
    /// @brief Append @p diag to the collection.
    /// @param diag Diagnostic to store.
    void report(il::support::Diag diag) override;

    /// @brief Access the accumulated diagnostics.
    /// @return Immutable view of the recorded diagnostics.
    [[nodiscard]] const std::vector<il::support::Diag> &diagnostics() const;

    /// @brief Remove all stored diagnostics.
    void clear();

  private:
    std::vector<il::support::Diag> diags_;
};

} // namespace il::verify
