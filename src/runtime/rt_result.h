//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_result.h
// Purpose: Result type for error handling - represents Ok(value) or Err(error).
// Key invariants: A Result is always exactly one of Ok or Err; unwrap traps on wrong variant.
// Ownership/Lifetime: Result objects are runtime-managed; values inside follow normal refcounting.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Result Creation
    //=========================================================================

    /// @brief Create an Ok result with the given value.
    /// @param value The success value (may be NULL).
    /// @return Opaque Result object pointer.
    void *rt_result_ok(void *value);

    /// @brief Create an Ok result with a string value.
    /// @param value The string success value.
    /// @return Opaque Result object pointer.
    void *rt_result_ok_str(rt_string value);

    /// @brief Create an Ok result with an integer value.
    /// @param value The integer success value.
    /// @return Opaque Result object pointer.
    void *rt_result_ok_i64(int64_t value);

    /// @brief Create an Ok result with a float value.
    /// @param value The float success value.
    /// @return Opaque Result object pointer.
    void *rt_result_ok_f64(double value);

    /// @brief Create an Err result with the given error.
    /// @param error The error value (may be NULL).
    /// @return Opaque Result object pointer.
    void *rt_result_err(void *error);

    /// @brief Create an Err result with a string error message.
    /// @param message The error message.
    /// @return Opaque Result object pointer.
    void *rt_result_err_str(rt_string message);

    //=========================================================================
    // Result Inspection
    //=========================================================================

    /// @brief Check if the result is Ok.
    /// @param obj Opaque Result object pointer.
    /// @return 1 if Ok, 0 if Err or NULL.
    int8_t rt_result_is_ok(void *obj);

    /// @brief Check if the result is Err.
    /// @param obj Opaque Result object pointer.
    /// @return 1 if Err, 0 if Ok or NULL.
    int8_t rt_result_is_err(void *obj);

    //=========================================================================
    // Value Extraction
    //=========================================================================

    /// @brief Get the Ok value, traps if Err.
    /// @param obj Opaque Result object pointer.
    /// @return The success value.
    void *rt_result_unwrap(void *obj);

    /// @brief Get the Ok value as a string, traps if Err.
    /// @param obj Opaque Result object pointer.
    /// @return The string success value.
    rt_string rt_result_unwrap_str(void *obj);

    /// @brief Get the Ok value as an integer, traps if Err.
    /// @param obj Opaque Result object pointer.
    /// @return The integer success value.
    int64_t rt_result_unwrap_i64(void *obj);

    /// @brief Get the Ok value as a float, traps if Err.
    /// @param obj Opaque Result object pointer.
    /// @return The float success value.
    double rt_result_unwrap_f64(void *obj);

    /// @brief Get the Ok value or a default if Err.
    /// @param obj Opaque Result object pointer.
    /// @param def Default value to return if Err.
    /// @return The success value or default.
    void *rt_result_unwrap_or(void *obj, void *def);

    /// @brief Get the Ok string value or a default if Err.
    /// @param obj Opaque Result object pointer.
    /// @param def Default string to return if Err.
    /// @return The success string or default.
    rt_string rt_result_unwrap_or_str(void *obj, rt_string def);

    /// @brief Get the Ok integer value or a default if Err.
    /// @param obj Opaque Result object pointer.
    /// @param def Default value to return if Err.
    /// @return The success integer or default.
    int64_t rt_result_unwrap_or_i64(void *obj, int64_t def);

    /// @brief Get the Ok float value or a default if Err.
    /// @param obj Opaque Result object pointer.
    /// @param def Default value to return if Err.
    /// @return The success float or default.
    double rt_result_unwrap_or_f64(void *obj, double def);

    /// @brief Get the Err value, traps if Ok.
    /// @param obj Opaque Result object pointer.
    /// @return The error value.
    void *rt_result_unwrap_err(void *obj);

    /// @brief Get the Err value as a string, traps if Ok.
    /// @param obj Opaque Result object pointer.
    /// @return The string error value.
    rt_string rt_result_unwrap_err_str(void *obj);

    /// @brief Get the Ok value if present, NULL otherwise.
    /// @param obj Opaque Result object pointer.
    /// @return The success value or NULL.
    void *rt_result_ok_value(void *obj);

    /// @brief Get the Err value if present, NULL otherwise.
    /// @param obj Opaque Result object pointer.
    /// @return The error value or NULL.
    void *rt_result_err_value(void *obj);

    //=========================================================================
    // Expect (with custom error messages)
    //=========================================================================

    /// @brief Get the Ok value or trap with a custom message.
    /// @param obj Opaque Result object pointer.
    /// @param msg Error message to show if Err.
    /// @return The success value.
    void *rt_result_expect(void *obj, rt_string msg);

    /// @brief Get the Err value or trap with a custom message.
    /// @param obj Opaque Result object pointer.
    /// @param msg Error message to show if Ok.
    /// @return The error value.
    void *rt_result_expect_err(void *obj, rt_string msg);

    //=========================================================================
    // Transformation
    //=========================================================================

    /// @brief Transform the Ok value using a function.
    /// @param obj Opaque Result object pointer.
    /// @param fn Function to apply to Ok value.
    /// @return New Result with transformed value, or same Err.
    void *rt_result_map(void *obj, void *(*fn)(void *));

    /// @brief Transform the Err value using a function.
    /// @param obj Opaque Result object pointer.
    /// @param fn Function to apply to Err value.
    /// @return Same Ok, or new Result with transformed error.
    void *rt_result_map_err(void *obj, void *(*fn)(void *));

    /// @brief Chain Result operations (flatMap/andThen).
    /// @param obj Opaque Result object pointer.
    /// @param fn Function that returns a new Result.
    /// @return Result from fn if Ok, or same Err.
    void *rt_result_and_then(void *obj, void *(*fn)(void *));

    /// @brief Provide fallback Result if Err (orElse).
    /// @param obj Opaque Result object pointer.
    /// @param fn Function that returns a fallback Result.
    /// @return Same Ok, or Result from fn if Err.
    void *rt_result_or_else(void *obj, void *(*fn)(void *));

    //=========================================================================
    // Utility
    //=========================================================================

    /// @brief Check equality of two Results.
    /// @param a First Result.
    /// @param b Second Result.
    /// @return 1 if both have same variant and value, 0 otherwise.
    int8_t rt_result_equals(void *a, void *b);

    /// @brief Get a string representation of the Result.
    /// @param obj Opaque Result object pointer.
    /// @return String like "Ok(value)" or "Err(error)".
    rt_string rt_result_to_string(void *obj);

#ifdef __cplusplus
}
#endif
