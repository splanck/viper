//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/Diagnostics.hpp
// Purpose: Target-independent diagnostic sink for codegen passes.
// Key invariants: Errors trigger pipeline short-circuit; warnings do not.
// Ownership/Lifetime: Owned by caller; typically lives for the duration of a
//                     single pass-manager run.
// Links: codegen/common/PassManager.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diagnostics.hpp"

#include <ostream>
#include <string>
#include <vector>

namespace viper::codegen::common {

/// @brief Diagnostic sink used by passes to surface errors and warnings.
/// @details Errors are fatal and cause the pass manager to short-circuit.
///          Warnings are advisory and do not stop the pipeline.
class Diagnostics {
  public:
    /// @brief Record an error message and mark the diagnostic stream as failed.
    void error(std::string message);

    /// @brief Record a structured error diagnostic.
    void error(std::string code, std::string message, il::support::SourceLoc loc = {});

    /// @brief Record a non-fatal warning message.
    void warning(std::string message);

    /// @brief Record a structured warning diagnostic.
    void warning(std::string code, std::string message, il::support::SourceLoc loc = {});

    /// @brief Query whether any error has been recorded.
    [[nodiscard]] bool hasErrors() const noexcept;

    /// @brief Query whether any warnings were recorded.
    [[maybe_unused]] [[nodiscard]] bool hasWarnings() const noexcept;

    /// @brief Emit accumulated diagnostics to the provided streams.
    void flush(std::ostream &err, std::ostream *warn = nullptr) const;

    /// @brief Direct read-only access to stored error messages (for testing).
    [[maybe_unused]] [[nodiscard]] const std::vector<std::string> &errors() const noexcept;

    /// @brief Direct read-only access to stored warning messages (for testing).
    [[maybe_unused]] [[nodiscard]] const std::vector<std::string> &warnings() const noexcept;

    /// @brief Direct read-only access to structured diagnostics.
    [[maybe_unused]] [[nodiscard]] const std::vector<il::support::Diagnostic> &
    diagnostics() const noexcept;

  private:
    std::vector<std::string> errors_{};
    std::vector<std::string> warnings_{};
    std::vector<il::support::Diagnostic> diagnostics_{};
};

} // namespace viper::codegen::common
