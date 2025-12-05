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

#include <sstream>

namespace il::vm::diag
{

std::string formatUnsupportedKind(std::string_view operation, il::core::Type::Kind kind)
{
    std::ostringstream os;
    os << "runtime bridge does not support " << operation << " kind '"
       << il::core::kindToString(kind) << "'";
    return os.str();
}

std::string formatUnknownRuntimeHelper(std::string_view name)
{
    std::ostringstream os;
    os << "attempted to call unknown runtime helper '" << name << '\'';
    return os.str();
}

std::string formatArgumentCountMismatch(std::string_view functionName,
                                         std::size_t expected,
                                         std::size_t received)
{
    std::ostringstream os;
    os << "argument count mismatch for function " << functionName << ": expected " << expected
       << " argument" << (expected == 1 ? "" : "s") << ", received " << received;
    return os.str();
}

std::string formatBranchArgMismatch(std::string_view targetLabel,
                                     std::string_view sourceLabel,
                                     std::size_t expected,
                                     std::size_t provided)
{
    std::ostringstream os;
    os << "branch argument count mismatch targeting '" << targetLabel << '\'';
    if (!sourceLabel.empty())
        os << " from '" << sourceLabel << '\'';
    os << ": expected " << expected << ", got " << provided;
    return os.str();
}

} // namespace il::vm::diag
