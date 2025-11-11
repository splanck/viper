// File: src/il/runtime/RuntimeSignatures.hpp
// Purpose: Declares the shared registry of runtime helper signatures.
// Key invariants: Entries mirror the runtime C ABI and remain stable for consumers.
// Ownership/Lifetime: Registry data lives for the duration of the process.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Type.hpp"
#include <cstddef>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::runtime
{

/// @brief Enumerates runtime signatures covered by the generated registry subset.
enum class RtSig : std::size_t
{
#define SIG(name, type) name,
#include "il/runtime/RuntimeSigs.def"
#undef SIG
    Count,
};

/// @brief Enumerates optional runtime helpers that lowering may request.
enum class RuntimeFeature : std::size_t
{
    Concat,
    CsvQuote,
    InputLine,
    SplitFields,
    ToInt,
    ToDouble,
    ParseInt64,
    ParseDouble,
    CintFromDouble,
    ClngFromDouble,
    CsngFromDouble,
    CdblFromAny,
    IntToStr,
    F64ToStr,
    StrFromI16,
    StrFromI32,
    StrFromSingle,
    StrFromDouble,
    Val,
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
    IntFloor,
    FixTrunc,
    RoundEven,
    RandomizeI64,
    Rnd,
    TermCls,
    TermColor,
    TermLocate,
    TermCursor,
    InKey,
    GetKey,
    ObjNew,
    ObjRetainMaybe,
    ObjReleaseChk0,
    ObjFree,
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
    RuntimeFeature feature{
        RuntimeFeature::Count}; ///< Feature identifier when @ref kind is Feature.
    bool ordered{false};        ///< Preserve request order when true.
};

/// @brief Handler invocation adapter signature used by the VM bridge.
using RuntimeHandler = void (*)(void **args, void *result);

/// @brief Describes the IL signature for a runtime helper function.
/// @notes Parameter order matches the runtime C ABI.
enum class RuntimeHiddenParamKind
{
    None,
    PowStatusPointer,
};

/// @brief Hidden runtime argument injected by the VM bridge.
struct RuntimeHiddenParam
{
    RuntimeHiddenParamKind kind{
        RuntimeHiddenParamKind::None}; ///< Classification for bridge marshalling.
};

/// @brief Classification guiding runtime trap handling for helper invocations.
enum class RuntimeTrapClass
{
    None,
    PowDomainOverflow,
};

struct RuntimeSignature
{
    il::core::Type retType;                       ///< Return type of the helper.
    std::vector<il::core::Type> paramTypes;       ///< Parameter types in declaration order.
    std::vector<RuntimeHiddenParam> hiddenParams; ///< Hidden arguments appended by the bridge.
    RuntimeTrapClass trapClass{RuntimeTrapClass::None}; ///< Trap semantics for the helper.
    bool nothrow{false};                                ///< Helper is guaranteed not to throw.
    bool readonly{false}; ///< Helper may read from memory without writes.
    bool pure{false};     ///< Helper has no observable side effects.
};

/// @brief Aggregated descriptor covering signature, handler, and lowering metadata.
struct RuntimeDescriptor
{
    std::string_view name;      ///< Symbol exported by the runtime library.
    RuntimeSignature signature; ///< Canonical IL signature for the helper.
    RuntimeHandler handler;     ///< Adapter that invokes the C implementation.
    RuntimeLowering lowering;   ///< Lowering metadata controlling declaration.
    RuntimeTrapClass trapClass{RuntimeTrapClass::None}; ///< Trap classification for VM bridge.
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

/// @brief Look up the generated runtime signature identifier for a symbol.
/// @param name Runtime symbol name to query.
/// @return Runtime signature identifier when tracked, std::nullopt otherwise.
std::optional<RtSig> findRuntimeSignatureId(std::string_view name);

/// @brief Retrieve signature metadata by generated runtime signature identifier.
/// @param sig Enumerated runtime signature identifier.
/// @return Pointer to the signature metadata when defined; nullptr otherwise.
const RuntimeSignature *findRuntimeSignature(RtSig sig);

} // namespace il::runtime
