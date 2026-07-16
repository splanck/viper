//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_numbuf.h
// Purpose: Packed numeric collection APIs for F64Buffer and I64Buffer.
// Key invariants:
//   - Buffers are fixed-length, GC-managed objects; indices are 0-based and
//     bounds-checked (out-of-range access traps).
//   - I64 arithmetic traps on signed overflow instead of wrapping; F64
//     arithmetic follows IEEE-754.
// Ownership/Lifetime:
//   - Constructors return GC-managed handles; callers must not free directly.
//   - Slice/conversion results are fresh allocations owned by the caller.
// Links: src/runtime/collections/rt_numbuf.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Allocate a zero-filled F64Buffer of @p len elements (traps when len < 0).
void *rt_f64buf_new(int64_t len);

/// @brief Build an F64Buffer from a Seq of boxed floats (traps on non-numeric elements).
void *rt_f64buf_from_seq(void *seq);

/// @brief Number of elements in the buffer (0 for NULL).
int64_t rt_f64buf_len(void *buf);

/// @brief Read the element at @p index (traps when out of range).
double rt_f64buf_get(void *buf, int64_t index);

/// @brief Write @p value at @p index (traps when out of range).
void rt_f64buf_set(void *buf, int64_t index, double value);

/// @brief Set every element to @p value.
void rt_f64buf_fill(void *buf, double value);

/// @brief Copy all elements of @p src into @p dst (traps on length mismatch).
void rt_f64buf_copy_from(void *dst, void *src);

/// @brief Return a fresh buffer holding elements [start, end) (bounds clamped).
void *rt_f64buf_slice(void *buf, int64_t start, int64_t end);

/// @brief Add @p value to every element in place.
void rt_f64buf_add_scalar(void *buf, double value);

/// @brief Multiply every element by @p value in place.
void rt_f64buf_mul_scalar(void *buf, double value);

/// @brief Element-wise add @p other into @p buf (traps on length mismatch).
void rt_f64buf_add_buffer(void *buf, void *other);

/// @brief Sum of all elements (0.0 for an empty buffer).
double rt_f64buf_sum(void *buf);

/// @brief Dot product with @p other (traps on length mismatch).
double rt_f64buf_dot(void *buf, void *other);

/// @brief Smallest element (traps on an empty buffer).
double rt_f64buf_min(void *buf);

/// @brief Largest element (traps on an empty buffer).
double rt_f64buf_max(void *buf);

/// @brief Copy the elements into a fresh List of boxed floats.
void *rt_f64buf_to_list(void *buf);

/// @brief Copy the elements into a fresh owning Seq of boxed floats.
void *rt_f64buf_to_seq(void *buf);

/// @brief Allocate a zero-filled I64Buffer of @p len elements (traps when len < 0).
void *rt_i64buf_new(int64_t len);

/// @brief Build an I64Buffer from a Seq of boxed integers (traps on non-integer elements).
void *rt_i64buf_from_seq(void *seq);

/// @brief Number of elements in the buffer (0 for NULL).
int64_t rt_i64buf_len(void *buf);

/// @brief Read the element at @p index (traps when out of range).
int64_t rt_i64buf_get(void *buf, int64_t index);

/// @brief Write @p value at @p index (traps when out of range).
void rt_i64buf_set(void *buf, int64_t index, int64_t value);

/// @brief Set every element to @p value.
void rt_i64buf_fill(void *buf, int64_t value);

/// @brief Copy all elements of @p src into @p dst (traps on length mismatch).
void rt_i64buf_copy_from(void *dst, void *src);

/// @brief Return a fresh buffer holding elements [start, end) (bounds clamped).
void *rt_i64buf_slice(void *buf, int64_t start, int64_t end);

/// @brief Add @p value to every element in place (traps on signed overflow).
void rt_i64buf_add_scalar(void *buf, int64_t value);

/// @brief Multiply every element by @p value in place (traps on signed overflow).
void rt_i64buf_mul_scalar(void *buf, int64_t value);

/// @brief Element-wise add @p other into @p buf (traps on length mismatch or overflow).
void rt_i64buf_add_buffer(void *buf, void *other);

/// @brief Sum of all elements (traps on accumulation overflow; 0 when empty).
int64_t rt_i64buf_sum(void *buf);

/// @brief Dot product with @p other (traps on length mismatch or overflow).
int64_t rt_i64buf_dot(void *buf, void *other);

/// @brief Smallest element (traps on an empty buffer).
int64_t rt_i64buf_min(void *buf);

/// @brief Largest element (traps on an empty buffer).
int64_t rt_i64buf_max(void *buf);

/// @brief Copy the elements into a fresh List of boxed integers.
void *rt_i64buf_to_list(void *buf);

/// @brief Copy the elements into a fresh owning Seq of boxed integers.
void *rt_i64buf_to_seq(void *buf);

#ifdef __cplusplus
}
#endif
