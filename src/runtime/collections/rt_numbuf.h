//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_numbuf.h
// Purpose: Packed numeric collection APIs for F64Buffer and I64Buffer.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_f64buf_new(int64_t len);
void *rt_f64buf_from_seq(void *seq);
int64_t rt_f64buf_len(void *buf);
double rt_f64buf_get(void *buf, int64_t index);
void rt_f64buf_set(void *buf, int64_t index, double value);
void rt_f64buf_fill(void *buf, double value);
void rt_f64buf_copy_from(void *dst, void *src);
void *rt_f64buf_slice(void *buf, int64_t start, int64_t end);
void rt_f64buf_add_scalar(void *buf, double value);
void rt_f64buf_mul_scalar(void *buf, double value);
void rt_f64buf_add_buffer(void *buf, void *other);
double rt_f64buf_sum(void *buf);
double rt_f64buf_dot(void *buf, void *other);
double rt_f64buf_min(void *buf);
double rt_f64buf_max(void *buf);
void *rt_f64buf_to_list(void *buf);
void *rt_f64buf_to_seq(void *buf);

void *rt_i64buf_new(int64_t len);
void *rt_i64buf_from_seq(void *seq);
int64_t rt_i64buf_len(void *buf);
int64_t rt_i64buf_get(void *buf, int64_t index);
void rt_i64buf_set(void *buf, int64_t index, int64_t value);
void rt_i64buf_fill(void *buf, int64_t value);
void rt_i64buf_copy_from(void *dst, void *src);
void *rt_i64buf_slice(void *buf, int64_t start, int64_t end);
void rt_i64buf_add_scalar(void *buf, int64_t value);
void rt_i64buf_mul_scalar(void *buf, int64_t value);
void rt_i64buf_add_buffer(void *buf, void *other);
int64_t rt_i64buf_sum(void *buf);
int64_t rt_i64buf_dot(void *buf, void *other);
int64_t rt_i64buf_min(void *buf);
int64_t rt_i64buf_max(void *buf);
void *rt_i64buf_to_list(void *buf);
void *rt_i64buf_to_seq(void *buf);

#ifdef __cplusplus
}
#endif
