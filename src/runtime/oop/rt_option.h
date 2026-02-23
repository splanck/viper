//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_option.h
// Purpose: Option type representing either Some(value) or None, with typed extraction, transformation (map, flat_map), and conversion to Result.
//
// Key invariants:
//   - An Option is always exactly one of Some or None.
//   - rt_option_unwrap traps on None; use rt_option_get_or for safe extraction.
//   - Typed accessors (get_i64, get_f64, get_str, get_obj) must match the stored variant.
//   - rt_option_map applies a function to Some's value; returns None for None.
//
// Ownership/Lifetime:
//   - Option objects are GC-managed opaque pointers.
//   - Values inside Option objects are retained while stored.
//
// Links: src/runtime/oop/rt_option.c (implementation), src/runtime/core/rt_string.h
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
    // Option Creation
    //=========================================================================

    /// @brief Create Some option with the given value.
    /// @param value The value (may be NULL, but will still be Some).
    /// @return Opaque Option object pointer.
    void *rt_option_some(void *value);

    /// @brief Create Some option with a string value.
    /// @param value The string value.
    /// @return Opaque Option object pointer.
    void *rt_option_some_str(rt_string value);

    /// @brief Create Some option with an integer value.
    /// @param value The integer value.
    /// @return Opaque Option object pointer.
    void *rt_option_some_i64(int64_t value);

    /// @brief Create Some option with a float value.
    /// @param value The float value.
    /// @return Opaque Option object pointer.
    void *rt_option_some_f64(double value);

    /// @brief Create None option.
    /// @return Opaque Option object pointer.
    void *rt_option_none(void);

    //=========================================================================
    // Option Inspection
    //=========================================================================

    /// @brief Check if the option is Some.
    /// @param obj Opaque Option object pointer.
    /// @return 1 if Some, 0 if None or NULL.
    int8_t rt_option_is_some(void *obj);

    /// @brief Check if the option is None.
    /// @param obj Opaque Option object pointer.
    /// @return 1 if None, 0 if Some.
    int8_t rt_option_is_none(void *obj);

    //=========================================================================
    // Value Extraction
    //=========================================================================

    /// @brief Get the Some value, traps if None.
    /// @param obj Opaque Option object pointer.
    /// @return The value.
    void *rt_option_unwrap(void *obj);

    /// @brief Get the Some value as a string, traps if None.
    /// @param obj Opaque Option object pointer.
    /// @return The string value.
    rt_string rt_option_unwrap_str(void *obj);

    /// @brief Get the Some value as an integer, traps if None.
    /// @param obj Opaque Option object pointer.
    /// @return The integer value.
    int64_t rt_option_unwrap_i64(void *obj);

    /// @brief Get the Some value as a float, traps if None.
    /// @param obj Opaque Option object pointer.
    /// @return The float value.
    double rt_option_unwrap_f64(void *obj);

    /// @brief Get the Some value or a default if None.
    /// @param obj Opaque Option object pointer.
    /// @param def Default value to return if None.
    /// @return The value or default.
    void *rt_option_unwrap_or(void *obj, void *def);

    /// @brief Get the Some string value or a default if None.
    /// @param obj Opaque Option object pointer.
    /// @param def Default string to return if None.
    /// @return The string value or default.
    rt_string rt_option_unwrap_or_str(void *obj, rt_string def);

    /// @brief Get the Some integer value or a default if None.
    /// @param obj Opaque Option object pointer.
    /// @param def Default value to return if None.
    /// @return The integer value or default.
    int64_t rt_option_unwrap_or_i64(void *obj, int64_t def);

    /// @brief Get the Some float value or a default if None.
    /// @param obj Opaque Option object pointer.
    /// @param def Default value to return if None.
    /// @return The float value or default.
    double rt_option_unwrap_or_f64(void *obj, double def);

    /// @brief Get the value if Some, NULL otherwise.
    /// @param obj Opaque Option object pointer.
    /// @return The value or NULL.
    void *rt_option_value(void *obj);

    //=========================================================================
    // Expect (with custom error messages)
    //=========================================================================

    /// @brief Get the Some value or trap with a custom message.
    /// @param obj Opaque Option object pointer.
    /// @param msg Error message to show if None.
    /// @return The value.
    void *rt_option_expect(void *obj, rt_string msg);

    //=========================================================================
    // Transformation
    //=========================================================================

    /// @brief Transform the Some value using a function.
    /// @param obj Opaque Option object pointer.
    /// @param fn Function to apply to Some value.
    /// @return New Option with transformed value, or None.
    void *rt_option_map(void *obj, void *(*fn)(void *));

    /// @brief Chain Option operations (flatMap/andThen).
    /// @param obj Opaque Option object pointer.
    /// @param fn Function that returns a new Option.
    /// @return Option from fn if Some, or None.
    void *rt_option_and_then(void *obj, void *(*fn)(void *));

    /// @brief Provide fallback Option if None (orElse).
    /// @param obj Opaque Option object pointer.
    /// @param fn Function that returns a fallback Option.
    /// @return Same Some, or Option from fn if None.
    void *rt_option_or_else(void *obj, void *(*fn)(void));

    /// @brief Filter: return Some if predicate passes, None otherwise.
    /// @param obj Opaque Option object pointer.
    /// @param pred Predicate function.
    /// @return Same Some if predicate returns true, None otherwise.
    void *rt_option_filter(void *obj, int8_t (*pred)(void *));

    //=========================================================================
    // Conversion
    //=========================================================================

    /// @brief Convert Option to Result (Some->Ok, None->Err).
    /// @param obj Opaque Option object pointer.
    /// @param err Error value to use if None.
    /// @return Result with Ok(value) or Err(err).
    void *rt_option_ok_or(void *obj, void *err);

    /// @brief Convert Option to Result with error string.
    /// @param obj Opaque Option object pointer.
    /// @param err Error message if None.
    /// @return Result with Ok(value) or Err(err).
    void *rt_option_ok_or_str(void *obj, rt_string err);

    //=========================================================================
    // Utility
    //=========================================================================

    /// @brief Check equality of two Options.
    /// @param a First Option.
    /// @param b Second Option.
    /// @return 1 if both have same variant and value, 0 otherwise.
    int8_t rt_option_equals(void *a, void *b);

    /// @brief Get a string representation of the Option.
    /// @param obj Opaque Option object pointer.
    /// @return String like "Some(value)" or "None".
    rt_string rt_option_to_string(void *obj);

#ifdef __cplusplus
}
#endif
