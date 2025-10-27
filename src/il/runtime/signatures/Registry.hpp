//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Registry.hpp
// Purpose: Declares a lightweight registry capturing expected runtime signature
//          shapes for debug verification.
// Key invariants: Registered signatures remain immutable once recorded and are
//                 only populated during startup by subsystem registrars.
// Ownership/Lifetime: Signatures have static storage and live for the duration
//                     of the process.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

namespace il::runtime::signatures
{

/// @brief Describes a simple parameter or result tag for debug verification.
struct SigParam
{
    /// @brief Coarse-grained classification of IL runtime value categories.
    enum Kind
    {
        I32, ///< 32-bit or smaller integers and booleans.
        I64, ///< 64-bit integers.
        F32, ///< 32-bit floating point values.
        F64, ///< 64-bit floating point values.
        Ptr, ///< Raw pointers and handles.
    } kind{Ptr}; ///< Stored kind for this parameter.
};

/// @brief Captures the expected shape of a runtime helper signature.
struct Signature
{
    std::string name;            ///< Runtime helper name.
    std::vector<SigParam> params; ///< Parameter kinds in declaration order.
    std::vector<SigParam> rets;   ///< Return value kinds (empty when void).
};

/// @brief Record an expected signature in the debug registry.
/// @param sig Signature definition to store.
void register_signature(const Signature &sig);

/// @brief Access the collected set of debug signature definitions.
/// @return Reference to the registry populated during subsystem registration.
const std::vector<Signature> &all_signatures();

} // namespace il::runtime::signatures

