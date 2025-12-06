//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/RuntimeSignatures_Handlers.hpp
// Purpose: Handler templates and adapter function declarations for runtime
//          descriptor marshalling. These templates bridge the VM's generic
//          void** argument arrays to typed C runtime function calls.
// Key invariants: Handler templates must match the RuntimeHandler signature.
// Ownership/Lifetime: Templates are instantiated at compile time; adapters
//                     have static duration.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "viper/runtime/rt.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace il::runtime
{

/// @brief Adapter that invokes a concrete runtime function from VM call stubs.
///
/// @details Each instantiation binds a runtime C function pointer and translates
///          the generic `void **` argument array provided by the VM into typed
///          parameters.  The @ref invoke entry point matches the signature
///          expected by @ref RuntimeDescriptor.
template <auto Fn, typename Ret, typename... Args> struct DirectHandler
{
    /// @brief Dispatch the runtime call using the supplied argument array.
    ///
    /// @details Expands the argument pack through @ref call using an index
    ///          sequence so each operand is reinterpreted to the function's
    ///          declared type.  Results are written into the provided buffer when
    ///          applicable.
    ///
    /// @param args Pointer to the marshalled argument array.
    /// @param result Optional pointer to storage for the return value.
    static void invoke(void **args, void *result)
    {
        call(args, result, std::index_sequence_for<Args...>{});
    }

  private:
    /// @brief Helper that expands the argument pack and forwards to the target.
    ///
    /// @details Uses `reinterpret_cast` to view each `void *` slot as the
    ///          appropriate type.  When the function returns a value the helper
    ///          stores it through @p result.
    template <std::size_t... I>
    static void call(void **args, void *result, std::index_sequence<I...>)
    {
        if constexpr (std::is_void_v<Ret>)
        {
            Fn(*reinterpret_cast<Args *>(args[I])...);
        }
        else
        {
            Ret value = Fn(*reinterpret_cast<Args *>(args[I])...);
            *reinterpret_cast<Ret *>(result) = value;
        }
    }
};

/// @brief Handler that retains string arguments before invoking the runtime.
///
/// @details Some runtime entry points consume string handles without
///          retaining them, so the VM must increment reference counts before
///          the call to keep values alive.  The wrapper first retains any
///          string arguments and then delegates to @ref DirectHandler to
///          perform the actual call/return marshalling.
template <auto Fn, typename Ret, typename... Args> struct ConsumingStringHandler
{
    /// @brief Invoke a runtime helper after retaining string arguments.
    /// @param args VM-supplied argument array.
    /// @param result Optional pointer to storage for the return value.
    static void invoke(void **args, void *result)
    {
        retainStrings(args, std::index_sequence_for<Args...>{});
        DirectHandler<Fn, Ret, Args...>::invoke(args, result);
    }

  private:
    /// @brief Retain every string argument present in the parameter pack.
    template <std::size_t... I> static void retainStrings(void **args, std::index_sequence<I...>)
    {
        (retainArg<Args>(args, I), ...);
    }

    /// @brief Retain a single argument when it is of type @c rt_string.
    template <typename T> static void retainArg(void **args, std::size_t index)
    {
        if constexpr (std::is_same_v<std::remove_cv_t<T>, rt_string>)
        {
            if (!args)
                return;
            void *slot = args[index];
            if (!slot)
                return;
            rt_string value = *reinterpret_cast<rt_string *>(slot);
            if (value)
                rt_string_ref(value);
        }
    }
};

// ============================================================================
// Adapter function declarations
// These bridge VM calling conventions to specific runtime helpers that require
// custom argument/result marshalling beyond what DirectHandler provides.
// ============================================================================

/// @brief Bridge runtime string trap requests into the VM trap mechanism.
void trapFromRuntimeString(void **args, void *result);

/// @brief Mutate a string handle in place for debugger bridge tests.
void testMutateStringNoStack(void **args, void *result);

/// @brief Adapter that narrows @ref rt_lof_ch results to 32 bits.
int32_t rt_lof_ch_i32(int32_t channel);

/// @brief Adapter that narrows @ref rt_loc_ch results to 32 bits.
int32_t rt_loc_ch_i32(int32_t channel);

// --- Integer (i32) array helpers ---
void invokeRtArrI32New(void **args, void *result);
void invokeRtArrI32Len(void **args, void *result);
void invokeRtArrI32Get(void **args, void *result);
void invokeRtArrI32Set(void **args, void *result);
void invokeRtArrI32Resize(void **args, void *result);

// --- LONG (i64) array helpers ---
void invokeRtArrI64New(void **args, void *result);
void invokeRtArrI64Len(void **args, void *result);
void invokeRtArrI64Get(void **args, void *result);
void invokeRtArrI64Set(void **args, void *result);
void invokeRtArrI64Resize(void **args, void *result);

// --- SINGLE/DOUBLE (f64) array helpers ---
void invokeRtArrF64New(void **args, void *result);
void invokeRtArrF64Len(void **args, void *result);
void invokeRtArrF64Get(void **args, void *result);
void invokeRtArrF64Set(void **args, void *result);
void invokeRtArrF64Resize(void **args, void *result);

// --- Object array helpers ---
void invokeRtArrObjNew(void **args, void *result);
void invokeRtArrObjLen(void **args, void *result);
void invokeRtArrObjGet(void **args, void *result);
void invokeRtArrObjPut(void **args, void *result);
void invokeRtArrObjResize(void **args, void *result);

// --- String array helpers ---
void invokeRtArrStrAlloc(void **args, void *result);
void invokeRtArrStrRelease(void **args, void *result);
void invokeRtArrStrGet(void **args, void *result);
void invokeRtArrStrPut(void **args, void *result);
void invokeRtArrStrLen(void **args, void *result);

// --- Bounds checking ---
void invokeRtArrOobPanic(void **args, void *result);

// --- Conversion helpers ---
void invokeRtCintFromDouble(void **args, void *result);
void invokeRtClngFromDouble(void **args, void *result);
void invokeRtCsngFromDouble(void **args, void *result);
void invokeRtStrFAlloc(void **args, void *result);
void invokeRtRoundEven(void **args, void *result);
void invokeRtPowF64Chkdom(void **args, void *result);

} // namespace il::runtime
