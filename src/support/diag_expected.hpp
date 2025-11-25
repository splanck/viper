//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/diag_expected.hpp
// Purpose: Provides diagnostic helpers and a lightweight Expected container for CLI tools.
// Key invariants: Diagnostics encapsulate a single severity, message, and location; Expected holds
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <optional>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

namespace il::support
{
using Diag = Diagnostic;

/// @brief Tag type selecting the value constructor for Expected<Diag>.
struct SuccessDiagTag
{
    explicit SuccessDiagTag() = default;
};

/// @brief Inline tag instance used to disambiguate success payload construction.
inline constexpr SuccessDiagTag kSuccessDiag{};

/// @brief Expected-style container pairing a value with a diagnostic on error.
/// @tparam T Stored value type when the operation succeeds.
/// @note Mirrors a subset of std::expected for tool-only usage until the
///       standard type becomes universally available on our toolchain.
template <class T> class Expected
{
  public:
    /// @brief Construct a successful result containing @p value.
    /// @param value Value produced by a successful computation.
    /// @details Enabled only when the provided value does not decay to Diag to
    ///          avoid colliding with the diagnostic constructor below.
    template <class U = T, class = std::enable_if_t<!std::is_same_v<std::decay_t<U>, Diag>>>
    Expected(U &&value) : value_(std::forward<U>(value))
    {
    }

    /// @brief Construct an error result holding diagnostic @p diag.
    /// @param diag Diagnostic to return to the caller.
    Expected(Diag diag) : error_(std::move(diag)) {}

    /// @brief Construct a successful result containing a Diag payload.
    /// @param tag Tag used to disambiguate value-vs-error construction.
    /// @param value Diagnostic stored as the success payload.
    template <class U = T, std::enable_if_t<std::is_same_v<U, Diag>, int> = 0>
    Expected(SuccessDiagTag, Diag value) : value_(std::move(value))
    {
    }

    /// @brief Create a successful Expected<Diag> from a diagnostic payload.
    /// @param tag Tag selecting the success overload.
    /// @param value Diagnostic stored as the success payload.
    template <class U = T, std::enable_if_t<std::is_same_v<U, Diag>, int> = 0>
    static Expected success(SuccessDiagTag tag, Diag value)
    {
        return Expected(tag, std::move(value));
    }

    /// @brief Check whether a value is present.
    /// @return True when the Expected stores a value.
    [[nodiscard]] bool hasValue() const
    {
        return value_.has_value();
    }

    /// @brief Allow use in boolean contexts to test success.
    explicit operator bool() const
    {
        return hasValue();
    }

    /// @brief Access the stored value; requires hasValue().
    T &value()
    {
        return *value_;
    }

    /// @brief Access the stored value; requires hasValue().
    const T &value() const
    {
        return *value_;
    }

    /// @brief Access the diagnostic describing the failure.
    const Diag &error() const &
    {
        return *error_;
    }

  private:
    std::optional<T> value_;
    std::optional<Diag> error_;
};

/// @brief Expected specialization for void success type.
template <> class Expected<void>
{
  public:
    /// @brief Construct a successful result with no payload.
    Expected() = default;

    /// @brief Construct an error result holding diagnostic @p diag.
    /// @param diag Diagnostic describing the failure.
    Expected(Diag diag);

    /// @brief Check whether the Expected represents success.
    [[nodiscard]] bool hasValue() const;

    /// @brief Allow use in boolean contexts to test success.
    explicit operator bool() const;

    /// @brief Access the diagnostic describing the failure.
    const Diag &error() const &;

  private:
    std::optional<Diag> error_;
};

namespace detail
{
/// @brief Convert diagnostic severity to lowercase string.
const char *diagSeverityToString(Severity severity);
} // namespace detail

/// @brief Create an error diagnostic with location and message.
/// @param loc Optional source location associated with the diagnostic.
/// @param msg Human-readable diagnostic message.
/// @return Diagnostic marked as an error severity.
Diag makeError(SourceLoc loc, std::string msg);

/// @brief Print a single diagnostic to the provided stream.
/// @param diag Diagnostic to format.
/// @param os Output stream receiving the text.
/// @param sm Optional source manager to resolve file paths.
/// @note Follows DiagnosticEngine::printAll formatting for consistency.
void printDiag(const Diag &diag, std::ostream &os, const SourceManager *sm = nullptr);
} // namespace il::support
