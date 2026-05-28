//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/Diagnostics.cpp
// Purpose: Implementation of the target-independent diagnostic sink.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/Diagnostics.hpp"

#include "support/diag_expected.hpp"

#include <ostream>
#include <utility>

namespace viper::codegen::common {

void Diagnostics::error(std::string message) {
    error("V-CG-ERROR", std::move(message));
}

void Diagnostics::error(std::string code, std::string message, il::support::SourceLoc loc) {
    errors_.push_back(message);
    il::support::Diagnostic diag{
        il::support::Severity::Error, std::move(message), loc, std::move(code)};
    diag.stage = "codegen";
    diagnostics_.push_back(std::move(diag));
}

void Diagnostics::warning(std::string message) {
    warning("V-CG-WARN", std::move(message));
}

void Diagnostics::warning(std::string code, std::string message, il::support::SourceLoc loc) {
    warnings_.push_back(message);
    il::support::Diagnostic diag{
        il::support::Severity::Warning, std::move(message), loc, std::move(code)};
    diag.stage = "codegen";
    diagnostics_.push_back(std::move(diag));
}

bool Diagnostics::hasErrors() const noexcept {
    return !errors_.empty();
}

bool Diagnostics::hasWarnings() const noexcept {
    return !warnings_.empty();
}

const std::vector<std::string> &Diagnostics::errors() const noexcept {
    return errors_;
}

const std::vector<std::string> &Diagnostics::warnings() const noexcept {
    return warnings_;
}

const std::vector<il::support::Diagnostic> &Diagnostics::diagnostics() const noexcept {
    return diagnostics_;
}

void Diagnostics::flush(std::ostream &err, std::ostream *warn) const {
    for (const auto &diag : diagnostics_) {
        if (diag.severity == il::support::Severity::Warning) {
            if (warn)
                il::support::printDiag(diag, *warn);
        } else {
            il::support::printDiag(diag, err);
        }
    }
}

} // namespace viper::codegen::common
