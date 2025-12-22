//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/DiagFormat.cpp
// Purpose: Implementation of centralized VM diagnostic string builders.
// Key invariants: All helpers are cold-path only; constructs strings on demand.
// Ownership/Lifetime: Returns std::string by value.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Centralized helpers for formatting VM diagnostic strings.
/// @details Builds descriptive error messages for runtime bridge and verifier
///          failures. All helpers are cold-path and allocate strings on demand.

#include "vm/DiagFormat.hpp"

namespace il::vm::diag
{

/// @brief Format a message for an unsupported runtime kind.
/// @details Produces: "runtime bridge does not support <op> kind '<kind>'".
/// @param operation Operation name being performed.
/// @param kind IL type kind that is unsupported.
/// @return Newly constructed diagnostic string.
std::string formatUnsupportedKind(std::string_view operation, il::core::Type::Kind kind)
{
    // Pre-allocate buffer to avoid reallocation
    // Format: "runtime bridge does not support <op> kind '<kind>'"
    const auto kindStr = il::core::kindToString(kind);
    std::string result;
    result.reserve(48 + operation.size() + kindStr.size());
    result.append("runtime bridge does not support ");
    result.append(operation);
    result.append(" kind '");
    result.append(kindStr);
    result.push_back('\'');
    return result;
}

/// @brief Format a message for an unknown runtime helper.
/// @details Produces: "attempted to call unknown runtime helper '<name>'".
/// @param name Runtime helper name supplied by the caller.
/// @return Newly constructed diagnostic string.
std::string formatUnknownRuntimeHelper(std::string_view name)
{
    // Format: "attempted to call unknown runtime helper '<name>'"
    std::string result;
    result.reserve(48 + name.size());
    result.append("attempted to call unknown runtime helper '");
    result.append(name);
    result.push_back('\'');
    return result;
}

/// @brief Format an argument count mismatch message.
/// @details Produces: "argument count mismatch for function <name>: expected N
///          argument(s), received M".
/// @param functionName Name of the function being called.
/// @param expected Expected argument count.
/// @param received Observed argument count.
/// @return Newly constructed diagnostic string.
std::string formatArgumentCountMismatch(std::string_view functionName,
                                        std::size_t expected,
                                        std::size_t received)
{
    // Format: "argument count mismatch for function <name>: expected N argument(s), received M"
    std::string result;
    result.reserve(72 + functionName.size());
    result.append("argument count mismatch for function ");
    result.append(functionName);
    result.append(": expected ");
    result.append(std::to_string(expected));
    result.append(expected == 1 ? " argument" : " arguments");
    result.append(", received ");
    result.append(std::to_string(received));
    return result;
}

/// @brief Format a branch argument mismatch message.
/// @details Produces: "branch argument count mismatch targeting '<target>'
///          [from '<source>']: expected N, got M".
/// @param targetLabel Target block label.
/// @param sourceLabel Source block label, if known.
/// @param expected Expected argument count.
/// @param provided Provided argument count.
/// @return Newly constructed diagnostic string.
std::string formatBranchArgMismatch(std::string_view targetLabel,
                                    std::string_view sourceLabel,
                                    std::size_t expected,
                                    std::size_t provided)
{
    // Format: "branch argument count mismatch targeting '<target>' [from '<source>']: expected N,
    // got M"
    std::string result;
    result.reserve(80 + targetLabel.size() + sourceLabel.size());
    result.append("branch argument count mismatch targeting '");
    result.append(targetLabel);
    result.push_back('\'');
    if (!sourceLabel.empty())
    {
        result.append(" from '");
        result.append(sourceLabel);
        result.push_back('\'');
    }
    result.append(": expected ");
    result.append(std::to_string(expected));
    result.append(", got ");
    result.append(std::to_string(provided));
    return result;
}

} // namespace il::vm::diag
