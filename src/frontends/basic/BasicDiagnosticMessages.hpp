// File: src/frontends/basic/BasicDiagnosticMessages.hpp
// Purpose: Defines reusable diagnostic message identifiers for the BASIC front end.
// Key invariants: Message identifiers are stable and uniquely map to human-readable text.
// Ownership/Lifetime: Header-only constants; no dynamic ownership.
// Links: docs/codemap.md
#pragma once

#include <string_view>

namespace il::frontends::basic::diag
{

/// @brief Compile-time description of a diagnostic message.
struct Message
{
    std::string_view id;   ///< Stable identifier printed by the diagnostic emitter.
    std::string_view text; ///< Primary human-readable message text.
};

/// @brief SELECT CASE selector must be integer-compatible.
inline constexpr Message ERR_SelectCase_NonIntegerSelector{
    "ERR_SelectCase_NonIntegerSelector",
    "SELECT CASE selector must be integer-compatible"};

/// @brief Duplicate CASE label encountered inside a SELECT CASE.
inline constexpr Message ERR_SelectCase_DuplicateLabel{
    "ERR_SelectCase_DuplicateLabel",
    "Duplicate CASE label"};

/// @brief Multiple CASE ELSE arms were found in the same SELECT CASE.
inline constexpr Message ERR_SelectCase_DuplicateElse{
    "ERR_SelectCase_DuplicateElse",
    "Duplicate CASE ELSE arm"};

/// @brief SELECT CASE statement was not terminated by END SELECT.
inline constexpr Message ERR_SelectCase_MissingEndSelect{
    "ERR_SelectCase_MissingEndSelect",
    "SELECT CASE missing END SELECT terminator"};

/// @brief CASE statement lacked any labels before the body.
inline constexpr Message ERR_Case_EmptyLabelList{
    "ERR_Case_EmptyLabelList",
    "CASE arm requires at least one label"};

} // namespace il::frontends::basic::diag
