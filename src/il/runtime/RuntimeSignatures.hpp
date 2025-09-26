// File: src/il/runtime/RuntimeSignatures.hpp
// Purpose: Declares the shared registry of runtime helper signatures.
// Key invariants: Entries mirror the runtime C ABI and remain stable for consumers.
// Ownership/Lifetime: Registry data lives for the duration of the process.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Type.hpp"
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::runtime
{

/// @brief Enumerates optional runtime helpers that lowering may request.
enum class RuntimeFeature : std::size_t
{
    Concat,
    InputLine,
    ToInt,
    IntToStr,
    F64ToStr,
    Val,
    Str,
    Alloc,
    StrEq,
    Left,
    Right,
    Mid2,
    Mid3,
    Instr2,
    Instr3,
    Ltrim,
    Rtrim,
    Trim,
    Ucase,
    Lcase,
    Chr,
    Asc,
    Sqrt,
    AbsI64,
    AbsF64,
    Floor,
    Ceil,
    Sin,
    Cos,
    Pow,
    RandomizeI64,
    Rnd,
    Count,
};

/// @brief Lowering requirement classification for runtime helpers.
enum class RuntimeLoweringKind
{
    Always,
    BoundsChecked,
    Feature,
    Manual,
};

/// @brief Metadata describing when lowering should declare a runtime helper.
struct RuntimeLowering
{
    RuntimeLoweringKind kind{RuntimeLoweringKind::Manual}; ///< Dispatch strategy.
    RuntimeFeature feature{RuntimeFeature::Count};          ///< Feature identifier when @ref kind is Feature.
    bool ordered{false};                                    ///< Preserve request order when true.
};

/// @brief Handler invocation adapter signature used by the VM bridge.
using RuntimeHandler = void (*)(void **args, void *result);

/// @brief Describes the IL signature for a runtime helper function.
/// @notes Parameter order matches the runtime C ABI.
struct RuntimeSignature
{
    il::core::Type retType;                  ///< Return type of the helper.
    std::vector<il::core::Type> paramTypes;  ///< Parameter types in declaration order.
};

/// @brief Aggregated descriptor covering signature, handler, and lowering metadata.
struct RuntimeDescriptor
{
    std::string_view name;       ///< Symbol exported by the runtime library.
    RuntimeSignature signature;  ///< Canonical IL signature for the helper.
    RuntimeHandler handler;      ///< Adapter that invokes the C implementation.
    RuntimeLowering lowering;    ///< Lowering metadata controlling declaration.
};

/// @brief Access the ordered registry of runtime descriptors.
/// @return Stable list of descriptors mirroring the runtime ABI.
const std::vector<RuntimeDescriptor> &runtimeRegistry();

/// @brief Lookup runtime descriptor by exported symbol name.
/// @return Descriptor pointer when registered, nullptr otherwise.
const RuntimeDescriptor *findRuntimeDescriptor(std::string_view name);

/// @brief Lookup runtime descriptor by lowering feature identifier.
/// @return Descriptor pointer when @p feature is published, nullptr otherwise.
const RuntimeDescriptor *findRuntimeDescriptor(RuntimeFeature feature);

/// @brief Access the registry of runtime signatures keyed by symbol name.
/// @return Mapping from runtime symbol to its IL signature metadata.
const std::unordered_map<std::string_view, RuntimeSignature> &runtimeSignatures();

/// @brief Look up the signature for a runtime helper if it exists.
/// @param name Runtime symbol name, e.g., "rt_print_str".
/// @return Pointer to the signature when registered; nullptr otherwise.
const RuntimeSignature *findRuntimeSignature(std::string_view name);

} // namespace il::runtime
