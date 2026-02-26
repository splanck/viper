//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_box.h
// Purpose: Boxing and unboxing primitives for storing primitive types (i64, f64, i1, str) in
// heap-allocated objects for use in generic collections.
//
// Key invariants:
//   - Boxed values carry a type tag: 0=i64, 1=f64, 2=i1, 3=str.
//   - rt_unbox_* traps if the type tag does not match the requested type.
//   - Boxed values participate in reference counting.
//   - rt_box_value_type boxes a struct/entity by copying all fields into a heap object.
//
// Ownership/Lifetime:
//   - Boxed objects are heap-allocated with refcount 1; callers own the initial reference.
//   - Unboxing does not consume the boxed object; caller must release separately.
//
// Links: src/runtime/oop/rt_box.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Type tags for boxed values
    typedef enum rt_box_type
    {
        RT_BOX_I64 = 0,
        RT_BOX_F64 = 1,
        RT_BOX_I1 = 2,
        RT_BOX_STR = 3
    } rt_box_type_t;

    /// @brief Box a 64-bit integer.
    /// @param val The integer value to box.
    /// @return Heap-allocated boxed object (refcount = 1).
    void *rt_box_i64(int64_t val);

    /// @brief Box a 64-bit float.
    /// @param val The float value to box.
    /// @return Heap-allocated boxed object (refcount = 1).
    void *rt_box_f64(double val);

    /// @brief Box a boolean.
    /// @param val The boolean value (0 = false, non-zero = true).
    /// @return Heap-allocated boxed object (refcount = 1).
    void *rt_box_i1(int64_t val);

    /// @brief Box a string.
    /// @param val The string to box.
    /// @return Heap-allocated boxed object (refcount = 1).
    void *rt_box_str(rt_string val);

    /// @brief Unbox to integer.
    /// @param box Boxed value (must be RT_BOX_I64).
    /// @return The unboxed integer value.
    /// @note Traps if box is NULL or wrong type.
    int64_t rt_unbox_i64(void *box);

    /// @brief Unbox to float.
    /// @param box Boxed value (must be RT_BOX_F64).
    /// @return The unboxed float value.
    /// @note Traps if box is NULL or wrong type.
    double rt_unbox_f64(void *box);

    /// @brief Unbox to boolean.
    /// @param box Boxed value (must be RT_BOX_I1).
    /// @return The unboxed boolean (0 or 1).
    /// @note Traps if box is NULL or wrong type.
    int64_t rt_unbox_i1(void *box);

    /// @brief Unbox to string.
    /// @param box Boxed value (must be RT_BOX_STR).
    /// @return The unboxed string (retained).
    /// @note Traps if box is NULL or wrong type.
    rt_string rt_unbox_str(void *box);

    /// @brief Get the type tag of a boxed value.
    /// @param box Boxed value.
    /// @return Type tag (0=i64, 1=f64, 2=i1, 3=str), or -1 if NULL.
    int64_t rt_box_type(void *box);

    /// @brief Check if a boxed value equals an integer.
    /// @param box Boxed value.
    /// @param val Integer to compare.
    /// @return 1 if equal, 0 otherwise.
    int64_t rt_box_eq_i64(void *box, int64_t val);

    /// @brief Check if a boxed value equals a float.
    /// @param box Boxed value.
    /// @param val Float to compare.
    /// @return 1 if equal, 0 otherwise.
    int64_t rt_box_eq_f64(void *box, double val);

    /// @brief Check if a boxed value equals a string.
    /// @param box Boxed value.
    /// @param val String to compare.
    /// @return 1 if equal, 0 otherwise.
    int64_t rt_box_eq_str(void *box, rt_string val);

    /// @brief Allocate heap memory for boxing a value type (struct).
    /// @param size Size in bytes to allocate.
    /// @return Heap-allocated zero-initialized memory.
    /// @note The compiler copies struct fields into this memory.
    void *rt_box_value_type(int64_t size);

    /// @brief Content-aware hash for an element.
    /// Boxed values (RT_ELEM_BOX) are hashed by content using FNV-1a;
    /// non-boxed objects fall back to pointer identity hashing.
    /// @param elem Element pointer (may be NULL).
    /// @return Hash value.
    size_t rt_box_hash(void *elem);

    /// @brief Content-aware equality for two elements.
    /// Boxed values (RT_ELEM_BOX) are compared by content (tag + data);
    /// non-boxed objects fall back to pointer identity.
    /// @param a First element (may be NULL).
    /// @param b Second element (may be NULL).
    /// @return 1 if equal, 0 otherwise.
    int rt_box_equal(void *a, void *b);

#ifdef __cplusplus
}
#endif
