//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Registry.hpp
// Purpose: Declare a lightweight registry describing expected runtime helper
//          signatures for debug cross-checks.
// Key invariants: Entries mirror the runtime ABI symbol names and expose a
//                 simplified type classification for comparison.
// Ownership/Lifetime: Registered signatures are stored with static lifetime
//                     and remain valid for the duration of the process.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace il::runtime::signatures
{

/// @brief Describe a parameter or result using a coarse type bucket.
struct SigParam
{
    /// @brief Enumerate supported coarse-grained type categories.
    enum Kind
    {
        I1,  ///< Boolean value.
        I32, ///< 32-bit integral value.
        I64, ///< 64-bit integral value.
        F32, ///< 32-bit floating-point value.
        F64, ///< 64-bit floating-point value.
        Ptr  ///< Pointer-like value, including runtime strings.
    } kind; ///< Active type classification.
};

/// @brief Capture the expected signature shape for a runtime helper.
struct Signature
{
    std::string name;          ///< Canonical runtime symbol name.
    std::vector<SigParam> params; ///< Parameter type sequence.
    std::vector<SigParam> rets;   ///< Result type sequence (empty for void).
};

/// @brief Register an expected runtime signature in the debug registry.
/// @param signature Signature metadata to append.
void register_signature(const Signature &signature);

/// @brief Access the registered runtime signature expectations.
/// @return Reference to the stable list of registered signatures.
const std::vector<Signature> &all_signatures();

/// @brief Helper to construct a signature from initializer lists.
/// @param name Runtime symbol name to register.
/// @param params Ordered parameter kinds.
/// @param returns Ordered result kinds.
/// @return Materialised @ref Signature for registration.
inline Signature make_signature(std::string name,
                                std::initializer_list<SigParam::Kind> params,
                                std::initializer_list<SigParam::Kind> returns = {})
{
    Signature signature;
    signature.name = std::move(name);
    signature.params.reserve(params.size());
    for (auto kind : params)
        signature.params.push_back(SigParam{kind});
    signature.rets.reserve(returns.size());
    for (auto kind : returns)
        signature.rets.push_back(SigParam{kind});
    return signature;
}

} // namespace il::runtime::signatures

