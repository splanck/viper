// File: src/il/verify/DiagSink.hpp
// Purpose: Declares diagnostic sinks used by verifier components to collect warnings.
// Key invariants: Sinks accept diagnostics in the order reported and decide ownership policy.
// Ownership/Lifetime: Implementations own stored diagnostics or forward them immediately.
// Links: docs/il-reference.md
#pragma once

#include "support/diag_expected.hpp"

#include <utility>
#include <vector>

namespace il::verify
{

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
    void report(il::support::Diag diag) override
    {
        diags_.push_back(std::move(diag));
    }

    /// @brief Access the accumulated diagnostics.
    /// @return Immutable view of the recorded diagnostics.
    [[nodiscard]] const std::vector<il::support::Diag> &diagnostics() const
    {
        return diags_;
    }

    /// @brief Remove all stored diagnostics.
    void clear()
    {
        diags_.clear();
    }

  private:
    std::vector<il::support::Diag> diags_;
};

} // namespace il::verify
