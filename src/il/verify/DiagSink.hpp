// File: src/il/verify/DiagSink.hpp
// Purpose: Declares diagnostic sinks used by verifier components to collect warnings.
// Key invariants: Sinks accept diagnostics in the order reported and decide ownership policy.
// Ownership/Lifetime: Implementations own stored diagnostics or forward them immediately.
// Links: docs/il-guide.md#reference
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
    Unknown = 0,               ///< Unclassified diagnostic.
    EhStackUnderflow,          ///< Encountered eh.pop with an empty handler stack.
    EhStackLeak,               ///< Execution left a function with handlers still active.
    EhResumeTokenMissing,      ///< Resume.* executed without an active resume token.
    EhResumeLabelInvalidTarget ///< resume.label target does not postdominate the faulting block.
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
