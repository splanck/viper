//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/RuntimeSignatures_Handlers.cpp
// Purpose: Adapter function implementations for runtime descriptor marshalling.
//          These functions bridge the VM's void** argument arrays to typed C
//          runtime function calls for helpers requiring custom marshalling.
// Key invariants: Each adapter matches the RuntimeHandler signature.
// Ownership/Lifetime: Functions have static duration; called via function ptr.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "RuntimeSignatures_Handlers.hpp"

#include "rt.hpp"
#include "rt_array_f64.h"
#include "rt_array_i64.h"
#include "rt_array_obj.h"
#include "rt_fp.h"
#include "rt_internal.h"
#include "rt_math.h"
#include "viper/runtime/rt.h"

#include <cstdint>
#include <limits>

namespace il::runtime
{
namespace
{

constexpr const char kTestBridgeMutatedText[] = "bridge-mutated";

/// @brief Clamp a 64-bit runtime file position/length to a 32-bit IL value.
///
/// @details Runtime helpers such as @ref rt_lof_ch and @ref rt_loc_ch expose
///          64-bit offsets so large files can be supported.  BASIC however
///          models these builtins as returning 32-bit signed integers.  The
///          bridge narrows the runtime result using saturation so overflow
///          produces INT32_MAX/INT32_MIN instead of wrapping.
int32_t clampRuntimeOffset(int64_t value)
{
    if (value > std::numeric_limits<int32_t>::max())
        return std::numeric_limits<int32_t>::max();
    if (value < std::numeric_limits<int32_t>::min())
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(value);
}

} // namespace

// ============================================================================
// File I/O adapters
// ============================================================================

int32_t rt_lof_ch_i32(int32_t channel)
{
    return clampRuntimeOffset(rt_lof_ch(channel));
}

int32_t rt_loc_ch_i32(int32_t channel)
{
    return clampRuntimeOffset(rt_loc_ch(channel));
}

// ============================================================================
// Debug/test adapters
// ============================================================================

void trapFromRuntimeString(void **args, void * /*result*/)
{
    rt_string str = args ? *reinterpret_cast<rt_string *>(args[0]) : nullptr;
    const char *msg = (str && str->data) ? str->data : "trap";
    rt_trap(msg);
}

void testMutateStringNoStack(void **args, void * /*result*/)
{
    if (!args)
        return;
    auto *slot = reinterpret_cast<rt_string *>(args[0]);
    if (!slot)
        return;
    rt_string updated =
        rt_string_from_bytes(kTestBridgeMutatedText, sizeof(kTestBridgeMutatedText) - 1);
    *slot = updated;
}

// ============================================================================
// Integer array adapters
// ============================================================================

void invokeRtArrI32New(void **args, void *result)
{
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    int32_t *arr = rt_arr_i32_new(len);
    if (result)
        *reinterpret_cast<void **>(result) = arr;
}

void invokeRtArrI32Len(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void *const *>(args[0]) : nullptr;
    int32_t *arr = arrPtr ? *reinterpret_cast<int32_t *const *>(arrPtr) : nullptr;
    const size_t len = rt_arr_i32_len(arr);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(len);
}

void invokeRtArrI32Get(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void *const *>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    int32_t *arr = arrPtr ? *reinterpret_cast<int32_t *const *>(arrPtr) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const int32_t value = rt_arr_i32_get(arr, idx);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(value);
}

void invokeRtArrI32Set(void **args, void * /*result*/)
{
    const auto arrPtr = args ? reinterpret_cast<void **>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const auto valPtr = args ? reinterpret_cast<const int64_t *>(args[2]) : nullptr;
    int32_t *arr = arrPtr ? *reinterpret_cast<int32_t **>(arrPtr) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const int32_t value = valPtr ? static_cast<int32_t>(*valPtr) : 0;
    rt_arr_i32_set(arr, idx, value);
}

void invokeRtArrI32Resize(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void **>(args[0]) : nullptr;
    const auto newLenPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    int32_t *arr = arrPtr ? *reinterpret_cast<int32_t **>(arrPtr) : nullptr;
    const size_t newLen = newLenPtr ? static_cast<size_t>(*newLenPtr) : 0;
    int32_t *local = arr;
    int32_t **handle = arrPtr ? reinterpret_cast<int32_t **>(arrPtr) : &local;
    int rc = rt_arr_i32_resize(handle, newLen);
    int32_t *resized = (rc == 0) ? *handle : nullptr;
    if (arrPtr && rc == 0)
        *reinterpret_cast<int32_t **>(arrPtr) = resized;
    if (result)
        *reinterpret_cast<void **>(result) = resized;
}

// ============================================================================
// I64 (LONG) array adapters
// ============================================================================

void invokeRtArrI64New(void **args, void *result)
{
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    int64_t *arr = rt_arr_i64_new(len);
    if (result)
        *reinterpret_cast<void **>(result) = arr;
}

void invokeRtArrI64Len(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void *const *>(args[0]) : nullptr;
    int64_t *arr = arrPtr ? *reinterpret_cast<int64_t *const *>(arrPtr) : nullptr;
    const size_t len = rt_arr_i64_len(arr);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(len);
}

void invokeRtArrI64Get(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void *const *>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    int64_t *arr = arrPtr ? *reinterpret_cast<int64_t *const *>(arrPtr) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const int64_t value = rt_arr_i64_get(arr, idx);
    if (result)
        *reinterpret_cast<int64_t *>(result) = value;
}

void invokeRtArrI64Set(void **args, void * /*result*/)
{
    const auto arrPtr = args ? reinterpret_cast<void **>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const auto valPtr = args ? reinterpret_cast<const int64_t *>(args[2]) : nullptr;
    int64_t *arr = arrPtr ? *reinterpret_cast<int64_t **>(arrPtr) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const int64_t value = valPtr ? *valPtr : 0;
    rt_arr_i64_set(arr, idx, value);
}

void invokeRtArrI64Resize(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void **>(args[0]) : nullptr;
    const auto newLenPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    int64_t *arr = arrPtr ? *reinterpret_cast<int64_t **>(arrPtr) : nullptr;
    const size_t newLen = newLenPtr ? static_cast<size_t>(*newLenPtr) : 0;
    int64_t *local = arr;
    int64_t **handle = arrPtr ? reinterpret_cast<int64_t **>(arrPtr) : &local;
    int rc = rt_arr_i64_resize(handle, newLen);
    int64_t *resized = (rc == 0) ? *handle : nullptr;
    if (arrPtr && rc == 0)
        *reinterpret_cast<int64_t **>(arrPtr) = resized;
    if (result)
        *reinterpret_cast<void **>(result) = resized;
}

// ============================================================================
// F64 (SINGLE/DOUBLE) array adapters
// ============================================================================

void invokeRtArrF64New(void **args, void *result)
{
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    double *arr = rt_arr_f64_new(len);
    if (result)
        *reinterpret_cast<void **>(result) = arr;
}

void invokeRtArrF64Len(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void *const *>(args[0]) : nullptr;
    double *arr = arrPtr ? *reinterpret_cast<double *const *>(arrPtr) : nullptr;
    const size_t len = rt_arr_f64_len(arr);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(len);
}

void invokeRtArrF64Get(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void *const *>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    double *arr = arrPtr ? *reinterpret_cast<double *const *>(arrPtr) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const double value = rt_arr_f64_get(arr, idx);
    if (result)
        *reinterpret_cast<double *>(result) = value;
}

void invokeRtArrF64Set(void **args, void * /*result*/)
{
    const auto arrPtr = args ? reinterpret_cast<void **>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const auto valPtr = args ? reinterpret_cast<const double *>(args[2]) : nullptr;
    double *arr = arrPtr ? *reinterpret_cast<double **>(arrPtr) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const double value = valPtr ? *valPtr : 0.0;
    rt_arr_f64_set(arr, idx, value);
}

void invokeRtArrF64Resize(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void **>(args[0]) : nullptr;
    const auto newLenPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    double *arr = arrPtr ? *reinterpret_cast<double **>(arrPtr) : nullptr;
    const size_t newLen = newLenPtr ? static_cast<size_t>(*newLenPtr) : 0;
    double *local = arr;
    double **handle = arrPtr ? reinterpret_cast<double **>(arrPtr) : &local;
    int rc = rt_arr_f64_resize(handle, newLen);
    double *resized = (rc == 0) ? *handle : nullptr;
    if (arrPtr && rc == 0)
        *reinterpret_cast<double **>(arrPtr) = resized;
    if (result)
        *reinterpret_cast<void **>(result) = resized;
}

// ============================================================================
// Object array adapters
// ============================================================================

void invokeRtArrObjNew(void **args, void *result)
{
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    void **arr = rt_arr_obj_new(len);
    if (result)
        *reinterpret_cast<void **>(result) = arr;
}

void invokeRtArrObjLen(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void ***>(args[0]) : nullptr;
    void **arr = arrPtr ? *arrPtr : nullptr;
    const size_t len = rt_arr_obj_len(arr);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(len);
}

void invokeRtArrObjGet(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void ***>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    void **arr = arrPtr ? *arrPtr : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    void *ptr = rt_arr_obj_get(arr, idx);
    if (result)
        *reinterpret_cast<void **>(result) = ptr;
}

void invokeRtArrObjPut(void **args, void * /*result*/)
{
    const auto arrPtr = args ? reinterpret_cast<void ***>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const auto valPtr = args ? reinterpret_cast<void **>(args[2]) : nullptr;
    void **arr = arrPtr ? *arrPtr : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    void *val = valPtr ? *valPtr : nullptr;
    rt_arr_obj_put(arr, idx, val);
}

void invokeRtArrObjResize(void **args, void *result)
{
    const auto arrPtr = args ? reinterpret_cast<void ***>(args[0]) : nullptr;
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    void **arr = arrPtr ? *arrPtr : nullptr;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    void **res = rt_arr_obj_resize(arr, len);
    if (result)
        *reinterpret_cast<void **>(result) = res;
}

// ============================================================================
// String array adapters
// ============================================================================

void invokeRtArrStrAlloc(void **args, void *result)
{
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    rt_string *arr = rt_arr_str_alloc(len);
    if (result)
        *reinterpret_cast<void **>(result) = arr;
}

void invokeRtArrStrRelease(void **args, void * /*result*/)
{
    // Param 0 is a pointer-typed IL value; args[0] points to storage containing the pointer.
    rt_string *arr = args ? *reinterpret_cast<rt_string **>(args[0]) : nullptr;
    const auto sizePtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const size_t size = sizePtr ? static_cast<size_t>(*sizePtr) : 0;
    rt_arr_str_release(arr, size);
}

void invokeRtArrStrGet(void **args, void *result)
{
    // Param 0 (ptr): args[0] -> storage holding the array payload pointer
    rt_string *arr = args ? *reinterpret_cast<rt_string **>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    rt_string value = rt_arr_str_get(arr, idx);
    if (result)
        *reinterpret_cast<rt_string *>(result) = value;
}

void invokeRtArrStrPut(void **args, void * /*result*/)
{
    // Param 0 (ptr): args[0] -> storage holding the array payload pointer
    rt_string *arr = args ? *reinterpret_cast<rt_string **>(args[0]) : nullptr;
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    // Param 2 (string): args[2] -> storage holding the string handle directly
    rt_string value = args ? *reinterpret_cast<rt_string *>(args[2]) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    rt_arr_str_put(arr, idx, value);
}

void invokeRtArrStrLen(void **args, void *result)
{
    // Param 0 (ptr): args[0] -> storage holding the array payload pointer
    rt_string *arr = args ? *reinterpret_cast<rt_string **>(args[0]) : nullptr;
    const size_t len = rt_arr_str_len(arr);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(len);
}

// ============================================================================
// Bounds checking
// ============================================================================

void invokeRtArrOobPanic(void **args, void * /*result*/)
{
    const auto idxPtr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const auto lenPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const size_t idx = idxPtr ? static_cast<size_t>(*idxPtr) : 0;
    const size_t len = lenPtr ? static_cast<size_t>(*lenPtr) : 0;
    rt_arr_oob_panic(idx, len);
}

// ============================================================================
// Conversion adapters
// ============================================================================

void invokeRtCintFromDouble(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const auto okPtr = args ? reinterpret_cast<bool *const *>(args[1]) : nullptr;
    const double x = xPtr ? *xPtr : 0.0;
    bool *ok = okPtr ? *okPtr : nullptr;
    const int16_t value = rt_cint_from_double(x, ok);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(value);
}

void invokeRtClngFromDouble(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const auto okPtr = args ? reinterpret_cast<bool *const *>(args[1]) : nullptr;
    const double x = xPtr ? *xPtr : 0.0;
    bool *ok = okPtr ? *okPtr : nullptr;
    const int32_t value = rt_clng_from_double(x, ok);
    if (result)
        *reinterpret_cast<int64_t *>(result) = static_cast<int64_t>(value);
}

void invokeRtCsngFromDouble(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const auto okPtr = args ? reinterpret_cast<bool *const *>(args[1]) : nullptr;
    const double x = xPtr ? *xPtr : 0.0;
    bool *ok = okPtr ? *okPtr : nullptr;
    const float value = rt_csng_from_double(x, ok);
    if (result)
        *reinterpret_cast<double *>(result) = static_cast<double>(value);
}

void invokeRtStrFAlloc(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const float value = xPtr ? static_cast<float>(*xPtr) : 0.0f;
    rt_string str = rt_str_f_alloc(value);
    if (result)
        *reinterpret_cast<rt_string *>(result) = str;
}

void invokeRtRoundEven(void **args, void *result)
{
    const auto xPtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const auto digitsPtr = args ? reinterpret_cast<const int64_t *>(args[1]) : nullptr;
    const double x = xPtr ? *xPtr : 0.0;
    const int ndigits = digitsPtr ? static_cast<int>(*digitsPtr) : 0;
    const double rounded = rt_round_even(x, ndigits);
    if (result)
        *reinterpret_cast<double *>(result) = rounded;
}

void invokeRtPowF64Chkdom(void **args, void *result)
{
    const auto basePtr = args ? reinterpret_cast<const double *>(args[0]) : nullptr;
    const auto expPtr = args ? reinterpret_cast<const double *>(args[1]) : nullptr;
    const auto okPtrPtr = args ? reinterpret_cast<bool *const *>(args[2]) : nullptr;
    const double base = basePtr ? *basePtr : 0.0;
    const double exponent = expPtr ? *expPtr : 0.0;
    bool *ok = okPtrPtr ? *okPtrPtr : nullptr;
    const double value = rt_pow_f64_chkdom(base, exponent, ok);
    if (result)
        *reinterpret_cast<double *>(result) = value;
}

} // namespace il::runtime
