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

#include "vm/DiagFormat.hpp"

namespace il::vm::diag
{

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
