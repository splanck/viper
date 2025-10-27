//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Registry.cpp
// Purpose: Implement the lightweight signature registry used for debug
//          validation of runtime helper metadata.
// Key invariants: Registration preserves insertion order and exposes stable
//                 references for the lifetime of the process.
// Ownership/Lifetime: Stored signatures have static storage managed by this
//                     translation unit.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/runtime/signatures/Registry.hpp"

#include <unordered_set>

namespace il::runtime::signatures
{
namespace
{
std::vector<Signature> &registry()
{
    static std::vector<Signature> g_signatures;
    return g_signatures;
}
} // namespace

void register_signature(const Signature &signature)
{
    registry().push_back(signature);
}

const std::vector<Signature> &all_signatures()
{
    return registry();
}

} // namespace il::runtime::signatures

