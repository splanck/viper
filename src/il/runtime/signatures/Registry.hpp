//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
        Ptr, ///< Generic pointer value.
        Str  ///< String value (IL str type, runtime rt_string).
    } kind;  ///< Active type classification.
};

/// @brief Capture the expected signature shape for a runtime helper.
struct Signature
{
    std::string name;             ///< Canonical runtime symbol name.
    std::vector<SigParam> params; ///< Parameter type sequence.
    std::vector<SigParam> rets;   ///< Result type sequence (empty for void).
    bool nothrow = false;         ///< Helper is guaranteed not to throw.
    bool readonly = false;        ///< Helper may read memory but performs no writes.
    bool pure = false;            ///< Helper is free of side effects and memory access.
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
                                std::initializer_list<SigParam::Kind> returns = {},
                                bool nothrow = false,
                                bool readonly = false,
                                bool pure = false)
{
    Signature signature;
    signature.name = std::move(name);
    signature.params.reserve(params.size());
    for (auto kind : params)
        signature.params.push_back(SigParam{kind});
    signature.rets.reserve(returns.size());
    for (auto kind : returns)
        signature.rets.push_back(SigParam{kind});
    signature.nothrow = nothrow;
    signature.readonly = readonly;
    signature.pure = pure;
    return signature;
}

} // namespace il::runtime::signatures
