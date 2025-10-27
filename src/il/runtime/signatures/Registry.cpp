//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Registry.cpp
// Purpose: Implements the debug signature registry used for runtime cross-checks.
// Key invariants: Entries are appended during process initialisation and remain
//                 stable thereafter.
// Ownership/Lifetime: Stored signatures have static storage duration.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/runtime/signatures/Registry.hpp"

#include <unordered_set>

namespace il::runtime::signatures
{
namespace
{
    /// @brief Backing storage for registered signatures.
    static std::vector<Signature> g_sigs;
}

void register_signature(const Signature &sig)
{
    g_sigs.push_back(sig);
}

const std::vector<Signature> &all_signatures()
{
    return g_sigs;
}

} // namespace il::runtime::signatures

