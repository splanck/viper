// File: src/il/runtime/RuntimeSignatures.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Defines the shared runtime signature registry for IL producers and consumers.
// Key invariants: Registry contents reflect the runtime C ABI signatures.
// Ownership/Lifetime: Uses static storage duration; entries are immutable after construction.
// Links: docs/il-spec.md

#include "il/runtime/RuntimeSignatures.hpp"
#include <initializer_list>

namespace il::runtime
{
namespace
{
using Kind = il::core::Type::Kind;
using SignatureMap = std::unordered_map<std::string_view, RuntimeSignature>;

/// @brief Construct a runtime signature from the provided type kinds.
/// @param ret Kind of the return type exposed by the runtime helper.
/// @param params Parameter kinds in the order expected by the runtime ABI.
/// @return Fully materialized signature with concrete il::core::Type entries.
RuntimeSignature makeSignature(Kind ret, std::initializer_list<Kind> params)
{
    RuntimeSignature sig;
    sig.retType = il::core::Type(ret);
    sig.paramTypes.reserve(params.size());
    for (Kind p : params)
        sig.paramTypes.emplace_back(p);
    return sig;
}

/// @brief Populate the runtime signature registry with known helper declarations.
/// @note Keep this table synchronized with the exported helpers in runtime/rt.hpp and
///       corresponding C sources so IL producers/consumers stay ABI-compatible.
/// @return Mapping from runtime symbol names to their IL signatures.
SignatureMap buildRegistry()
{
    SignatureMap map;
    map.reserve(40);
    auto add = [&](std::string_view name, Kind ret, std::initializer_list<Kind> params)
    {
        map.emplace(name, makeSignature(ret, params));
    };

    add("rt_abort", Kind::Void, {Kind::Ptr});
    add("rt_trap", Kind::Void, {Kind::Str});
    add("rt_print_str", Kind::Void, {Kind::Str});
    add("rt_print_i64", Kind::Void, {Kind::I64});
    add("rt_print_f64", Kind::Void, {Kind::F64});
    add("rt_input_line", Kind::Str, {});
    add("rt_len", Kind::I64, {Kind::Str});
    add("rt_concat", Kind::Str, {Kind::Str, Kind::Str});
    add("rt_substr", Kind::Str, {Kind::Str, Kind::I64, Kind::I64});
    add("rt_left", Kind::Str, {Kind::Str, Kind::I64});
    add("rt_right", Kind::Str, {Kind::Str, Kind::I64});
    add("rt_mid2", Kind::Str, {Kind::Str, Kind::I64});
    add("rt_mid3", Kind::Str, {Kind::Str, Kind::I64, Kind::I64});
    add("rt_instr3", Kind::I64, {Kind::I64, Kind::Str, Kind::Str});
    add("rt_instr2", Kind::I64, {Kind::Str, Kind::Str});
    add("rt_ltrim", Kind::Str, {Kind::Str});
    add("rt_rtrim", Kind::Str, {Kind::Str});
    add("rt_trim", Kind::Str, {Kind::Str});
    add("rt_ucase", Kind::Str, {Kind::Str});
    add("rt_lcase", Kind::Str, {Kind::Str});
    add("rt_chr", Kind::Str, {Kind::I64});
    add("rt_asc", Kind::I64, {Kind::Str});
    add("rt_str_eq", Kind::I1, {Kind::Str, Kind::Str});
    add("rt_to_int", Kind::I64, {Kind::Str});
    add("rt_int_to_str", Kind::Str, {Kind::I64});
    add("rt_f64_to_str", Kind::Str, {Kind::F64});
    add("rt_val", Kind::F64, {Kind::Str});
    add("rt_str", Kind::Str, {Kind::F64});
    add("rt_sqrt", Kind::F64, {Kind::F64});
    add("rt_floor", Kind::F64, {Kind::F64});
    add("rt_ceil", Kind::F64, {Kind::F64});
    add("rt_sin", Kind::F64, {Kind::F64});
    add("rt_cos", Kind::F64, {Kind::F64});
    add("rt_pow", Kind::F64, {Kind::F64, Kind::F64});
    add("rt_abs_i64", Kind::I64, {Kind::I64});
    add("rt_abs_f64", Kind::F64, {Kind::F64});
    add("rt_randomize_i64", Kind::Void, {Kind::I64});
    add("rt_rnd", Kind::F64, {});
    add("rt_alloc", Kind::Ptr, {Kind::I64});
    add("rt_const_cstr", Kind::Str, {Kind::Ptr});

    return map;
}

} // namespace

/// @brief Access the lazily constructed runtime signature registry.
/// @return Immutable mapping shared across IL components.
const std::unordered_map<std::string_view, RuntimeSignature> &runtimeSignatures()
{
    static const SignatureMap table = buildRegistry();
    return table;
}

/// @brief Look up a runtime helper signature by symbol name.
/// @param name Runtime helper identifier, e.g., "rt_print_str".
/// @return Pointer to the signature if registered; nullptr otherwise.
const RuntimeSignature *findRuntimeSignature(std::string_view name)
{
    const auto &table = runtimeSignatures();
    const auto it = table.find(name);
    if (it == table.end())
        return nullptr;
    return &it->second;
}

} // namespace il::runtime
