//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_numbuf.c
// Purpose: Implements packed F64Buffer and I64Buffer collections over existing
//          refcounted primitive array payloads.
//
//===----------------------------------------------------------------------===//

#include "rt_numbuf.h"

#include "rt_array_f64.h"
#include "rt_array_i64.h"
#include "rt_box.h"
#include "rt_collection_ids.h"
#include "rt_internal.h"
#include "rt_list.h"
#include "rt_object.h"
#include "rt_seq.h"

#include <stddef.h>
#include <stdint.h>

typedef struct rt_f64buf_impl {
    void **vptr;
    double *arr;
} rt_f64buf_impl;

typedef struct rt_i64buf_impl {
    void **vptr;
    int64_t *arr;
} rt_i64buf_impl;

static size_t checked_len_from_i64(int64_t len, const char *what) {
    if (len < 0) {
        rt_trap(what);
        return 0;
    }
    return (size_t)len;
}

static size_t checked_index_from_i64(int64_t index, const char *what) {
    if (index < 0) {
        rt_trap(what);
        return 0;
    }
    return (size_t)index;
}

static void release_temp_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void rt_f64buf_finalize(void *obj) {
    if (!obj)
        return;
    rt_f64buf_impl *buf = (rt_f64buf_impl *)obj;
    rt_arr_f64_release(buf->arr);
    buf->arr = NULL;
}

static void rt_i64buf_finalize(void *obj) {
    if (!obj)
        return;
    rt_i64buf_impl *buf = (rt_i64buf_impl *)obj;
    rt_arr_i64_release(buf->arr);
    buf->arr = NULL;
}

static rt_f64buf_impl *as_f64buf(void *obj, const char *what) {
    if (!rt_obj_is_instance(obj, RT_F64BUFFER_CLASS_ID, sizeof(rt_f64buf_impl))) {
        rt_trap(what);
        return NULL;
    }
    return (rt_f64buf_impl *)obj;
}

static rt_i64buf_impl *as_i64buf(void *obj, const char *what) {
    if (!rt_obj_is_instance(obj, RT_I64BUFFER_CLASS_ID, sizeof(rt_i64buf_impl))) {
        rt_trap(what);
        return NULL;
    }
    return (rt_i64buf_impl *)obj;
}

static void *alloc_f64buf_with_len(size_t len) {
    rt_f64buf_impl *buf =
        (rt_f64buf_impl *)rt_obj_new_i64(RT_F64BUFFER_CLASS_ID, (int64_t)sizeof(rt_f64buf_impl));
    if (!buf) {
        rt_trap("F64Buffer.New: memory allocation failed");
        return NULL;
    }
    buf->vptr = NULL;
    buf->arr = rt_arr_f64_new(len);
    rt_obj_set_finalizer(buf, rt_f64buf_finalize);
    if (!buf->arr) {
        release_temp_obj(buf);
        rt_trap("F64Buffer.New: memory allocation failed");
        return NULL;
    }
    return buf;
}

static void *alloc_i64buf_with_len(size_t len) {
    rt_i64buf_impl *buf =
        (rt_i64buf_impl *)rt_obj_new_i64(RT_I64BUFFER_CLASS_ID, (int64_t)sizeof(rt_i64buf_impl));
    if (!buf) {
        rt_trap("I64Buffer.New: memory allocation failed");
        return NULL;
    }
    buf->vptr = NULL;
    buf->arr = rt_arr_i64_new(len);
    rt_obj_set_finalizer(buf, rt_i64buf_finalize);
    if (!buf->arr) {
        release_temp_obj(buf);
        rt_trap("I64Buffer.New: memory allocation failed");
        return NULL;
    }
    return buf;
}

static void clamp_slice(
    size_t len, int64_t start, int64_t end, size_t *out_start, size_t *out_end) {
    int64_t clamped_start = start < 0 ? 0 : start;
    int64_t clamped_end = end < 0 ? 0 : end;
    if ((uint64_t)clamped_start > len)
        clamped_start = (int64_t)len;
    if ((uint64_t)clamped_end > len)
        clamped_end = (int64_t)len;
    if (clamped_end < clamped_start)
        clamped_end = clamped_start;
    *out_start = (size_t)clamped_start;
    *out_end = (size_t)clamped_end;
}

void *rt_f64buf_new(int64_t len) {
    return alloc_f64buf_with_len(checked_len_from_i64(len, "F64Buffer.New: negative length"));
}

void *rt_i64buf_new(int64_t len) {
    return alloc_i64buf_with_len(checked_len_from_i64(len, "I64Buffer.New: negative length"));
}

void *rt_f64buf_from_seq(void *seq) {
    int64_t len_i64 = rt_seq_len(seq);
    void *obj = rt_f64buf_new(len_i64);
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.FromSeq: invalid buffer object");
    size_t len = rt_arr_f64_len(buf->arr);
    for (size_t i = 0; i < len; i++) {
        void *item = rt_seq_get(seq, (int64_t)i);
        double value = 0.0;
        int64_t i64_value = 0;
        if (!rt_box_try_to_f64(item, &value)) {
            if (rt_box_try_to_i64(item, &i64_value))
                value = (double)i64_value;
            else {
                release_temp_obj(obj);
                rt_trap("F64Buffer.FromSeq: value is not numeric");
                return NULL;
            }
        }
        rt_arr_f64_set_fast(buf->arr, i, value);
    }
    return obj;
}

void *rt_i64buf_from_seq(void *seq) {
    int64_t len_i64 = rt_seq_len(seq);
    void *obj = rt_i64buf_new(len_i64);
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.FromSeq: invalid buffer object");
    size_t len = rt_arr_i64_len(buf->arr);
    for (size_t i = 0; i < len; i++) {
        void *item = rt_seq_get(seq, (int64_t)i);
        int64_t value = 0;
        if (!rt_box_try_to_i64(item, &value)) {
            release_temp_obj(obj);
            rt_trap("I64Buffer.FromSeq: value is not i64");
            return NULL;
        }
        rt_arr_i64_set_fast(buf->arr, i, value);
    }
    return obj;
}

int64_t rt_f64buf_len(void *obj) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.Length: invalid buffer object");
    return (int64_t)rt_arr_f64_len(buf->arr);
}

int64_t rt_i64buf_len(void *obj) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.Length: invalid buffer object");
    return (int64_t)rt_arr_i64_len(buf->arr);
}

double rt_f64buf_get(void *obj, int64_t index) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.Get: invalid buffer object");
    return rt_arr_f64_get(buf->arr, checked_index_from_i64(index, "F64Buffer.Get: negative index"));
}

int64_t rt_i64buf_get(void *obj, int64_t index) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.Get: invalid buffer object");
    return rt_arr_i64_get(buf->arr, checked_index_from_i64(index, "I64Buffer.Get: negative index"));
}

void rt_f64buf_set(void *obj, int64_t index, double value) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.Set: invalid buffer object");
    rt_arr_f64_set(buf->arr, checked_index_from_i64(index, "F64Buffer.Set: negative index"), value);
}

void rt_i64buf_set(void *obj, int64_t index, int64_t value) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.Set: invalid buffer object");
    rt_arr_i64_set(buf->arr, checked_index_from_i64(index, "I64Buffer.Set: negative index"), value);
}

void rt_f64buf_fill(void *obj, double value) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.Fill: invalid buffer object");
    size_t len = rt_arr_f64_len(buf->arr);
    for (size_t i = 0; i < len; i++)
        rt_arr_f64_set_fast(buf->arr, i, value);
}

void rt_i64buf_fill(void *obj, int64_t value) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.Fill: invalid buffer object");
    size_t len = rt_arr_i64_len(buf->arr);
    for (size_t i = 0; i < len; i++)
        rt_arr_i64_set_fast(buf->arr, i, value);
}

void rt_f64buf_copy_from(void *dst_obj, void *src_obj) {
    rt_f64buf_impl *dst = as_f64buf(dst_obj, "F64Buffer.CopyFrom: invalid destination buffer");
    rt_f64buf_impl *src = as_f64buf(src_obj, "F64Buffer.CopyFrom: invalid source buffer");
    size_t len = rt_arr_f64_len(src->arr);
    if (rt_arr_f64_resize(&dst->arr, len) != 0) {
        rt_trap("F64Buffer.CopyFrom: memory allocation failed");
        return;
    }
    rt_arr_f64_copy_payload(dst->arr, src->arr, len);
}

void rt_i64buf_copy_from(void *dst_obj, void *src_obj) {
    rt_i64buf_impl *dst = as_i64buf(dst_obj, "I64Buffer.CopyFrom: invalid destination buffer");
    rt_i64buf_impl *src = as_i64buf(src_obj, "I64Buffer.CopyFrom: invalid source buffer");
    size_t len = rt_arr_i64_len(src->arr);
    if (rt_arr_i64_resize(&dst->arr, len) != 0) {
        rt_trap("I64Buffer.CopyFrom: memory allocation failed");
        return;
    }
    rt_arr_i64_copy_payload(dst->arr, src->arr, len);
}

void *rt_f64buf_slice(void *obj, int64_t start, int64_t end) {
    rt_f64buf_impl *src = as_f64buf(obj, "F64Buffer.Slice: invalid buffer object");
    size_t from = 0;
    size_t to = 0;
    clamp_slice(rt_arr_f64_len(src->arr), start, end, &from, &to);
    void *slice_obj = rt_f64buf_new((int64_t)(to - from));
    rt_f64buf_impl *slice = as_f64buf(slice_obj, "F64Buffer.Slice: invalid slice object");
    rt_arr_f64_copy_payload(slice->arr, src->arr + from, to - from);
    return slice_obj;
}

void *rt_i64buf_slice(void *obj, int64_t start, int64_t end) {
    rt_i64buf_impl *src = as_i64buf(obj, "I64Buffer.Slice: invalid buffer object");
    size_t from = 0;
    size_t to = 0;
    clamp_slice(rt_arr_i64_len(src->arr), start, end, &from, &to);
    void *slice_obj = rt_i64buf_new((int64_t)(to - from));
    rt_i64buf_impl *slice = as_i64buf(slice_obj, "I64Buffer.Slice: invalid slice object");
    rt_arr_i64_copy_payload(slice->arr, src->arr + from, to - from);
    return slice_obj;
}

void rt_f64buf_add_scalar(void *obj, double value) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.AddScalar: invalid buffer object");
    size_t len = rt_arr_f64_len(buf->arr);
    for (size_t i = 0; i < len; i++)
        rt_arr_f64_set_fast(buf->arr, i, rt_arr_f64_get_fast(buf->arr, i) + value);
}

/// @brief Overflow-checked signed 64-bit addition (VDOC-101). Returns 1 on overflow.
static int numbuf_checked_add_i64(int64_t a, int64_t b, int64_t *out) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_add_overflow(a, b, out);
#else
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return 1;
    *out = a + b;
    return 0;
#endif
}

/// @brief Overflow-checked signed 64-bit multiplication (VDOC-101). Returns 1 on overflow.
static int numbuf_checked_mul_i64(int64_t a, int64_t b, int64_t *out) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_mul_overflow(a, b, out);
#else
    if (a == 0 || b == 0) {
        *out = 0;
        return 0;
    }
    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b)
                return 1;
        } else if (b < INT64_MIN / a) {
            return 1;
        }
    } else {
        if (b > 0) {
            if (a < INT64_MIN / b)
                return 1;
        } else if (a < INT64_MAX / b) {
            return 1;
        }
    }
    *out = a * b;
    return 0;
#endif
}

void rt_i64buf_add_scalar(void *obj, int64_t value) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.AddScalar: invalid buffer object");
    size_t len = rt_arr_i64_len(buf->arr);
    for (size_t i = 0; i < len; i++) {
        int64_t out;
        if (numbuf_checked_add_i64(rt_arr_i64_get_fast(buf->arr, i), value, &out)) {
            rt_trap("I64Buffer.AddScalar: integer overflow");
            return;
        }
        rt_arr_i64_set_fast(buf->arr, i, out);
    }
}

void rt_f64buf_mul_scalar(void *obj, double value) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.MulScalar: invalid buffer object");
    size_t len = rt_arr_f64_len(buf->arr);
    for (size_t i = 0; i < len; i++)
        rt_arr_f64_set_fast(buf->arr, i, rt_arr_f64_get_fast(buf->arr, i) * value);
}

void rt_i64buf_mul_scalar(void *obj, int64_t value) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.MulScalar: invalid buffer object");
    size_t len = rt_arr_i64_len(buf->arr);
    for (size_t i = 0; i < len; i++) {
        int64_t out;
        if (numbuf_checked_mul_i64(rt_arr_i64_get_fast(buf->arr, i), value, &out)) {
            rt_trap("I64Buffer.MulScalar: integer overflow");
            return;
        }
        rt_arr_i64_set_fast(buf->arr, i, out);
    }
}

void rt_f64buf_add_buffer(void *obj, void *other_obj) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.AddBuffer: invalid buffer object");
    rt_f64buf_impl *other = as_f64buf(other_obj, "F64Buffer.AddBuffer: invalid source buffer");
    size_t len = rt_arr_f64_len(buf->arr);
    if (rt_arr_f64_len(other->arr) != len) {
        rt_trap("F64Buffer.AddBuffer: length mismatch");
        return;
    }
    for (size_t i = 0; i < len; i++) {
        rt_arr_f64_set_fast(
            buf->arr, i, rt_arr_f64_get_fast(buf->arr, i) + rt_arr_f64_get_fast(other->arr, i));
    }
}

void rt_i64buf_add_buffer(void *obj, void *other_obj) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.AddBuffer: invalid buffer object");
    rt_i64buf_impl *other = as_i64buf(other_obj, "I64Buffer.AddBuffer: invalid source buffer");
    size_t len = rt_arr_i64_len(buf->arr);
    if (rt_arr_i64_len(other->arr) != len) {
        rt_trap("I64Buffer.AddBuffer: length mismatch");
        return;
    }
    for (size_t i = 0; i < len; i++) {
        int64_t out;
        if (numbuf_checked_add_i64(
                rt_arr_i64_get_fast(buf->arr, i), rt_arr_i64_get_fast(other->arr, i), &out)) {
            rt_trap("I64Buffer.AddBuffer: integer overflow");
            return;
        }
        rt_arr_i64_set_fast(buf->arr, i, out);
    }
}

double rt_f64buf_sum(void *obj) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.Sum: invalid buffer object");
    double sum = 0.0;
    size_t len = rt_arr_f64_len(buf->arr);
    for (size_t i = 0; i < len; i++)
        sum += rt_arr_f64_get_fast(buf->arr, i);
    return sum;
}

int64_t rt_i64buf_sum(void *obj) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.Sum: invalid buffer object");
    int64_t sum = 0;
    size_t len = rt_arr_i64_len(buf->arr);
    for (size_t i = 0; i < len; i++) {
        if (numbuf_checked_add_i64(sum, rt_arr_i64_get_fast(buf->arr, i), &sum)) {
            rt_trap("I64Buffer.Sum: integer overflow");
            return 0;
        }
    }
    return sum;
}

double rt_f64buf_dot(void *obj, void *other_obj) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.Dot: invalid buffer object");
    rt_f64buf_impl *other = as_f64buf(other_obj, "F64Buffer.Dot: invalid source buffer");
    size_t len = rt_arr_f64_len(buf->arr);
    if (rt_arr_f64_len(other->arr) != len) {
        rt_trap("F64Buffer.Dot: length mismatch");
        return 0.0;
    }
    double sum = 0.0;
    for (size_t i = 0; i < len; i++)
        sum += rt_arr_f64_get_fast(buf->arr, i) * rt_arr_f64_get_fast(other->arr, i);
    return sum;
}

int64_t rt_i64buf_dot(void *obj, void *other_obj) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.Dot: invalid buffer object");
    rt_i64buf_impl *other = as_i64buf(other_obj, "I64Buffer.Dot: invalid source buffer");
    size_t len = rt_arr_i64_len(buf->arr);
    if (rt_arr_i64_len(other->arr) != len) {
        rt_trap("I64Buffer.Dot: length mismatch");
        return 0;
    }
    int64_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        int64_t prod;
        if (numbuf_checked_mul_i64(
                rt_arr_i64_get_fast(buf->arr, i), rt_arr_i64_get_fast(other->arr, i), &prod) ||
            numbuf_checked_add_i64(sum, prod, &sum)) {
            rt_trap("I64Buffer.Dot: integer overflow");
            return 0;
        }
    }
    return sum;
}

double rt_f64buf_min(void *obj) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.Min: invalid buffer object");
    size_t len = rt_arr_f64_len(buf->arr);
    if (len == 0) {
        rt_trap("F64Buffer.Min: empty buffer");
        return 0.0;
    }
    double value = rt_arr_f64_get_fast(buf->arr, 0);
    for (size_t i = 1; i < len; i++) {
        double next = rt_arr_f64_get_fast(buf->arr, i);
        if (next < value)
            value = next;
    }
    return value;
}

int64_t rt_i64buf_min(void *obj) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.Min: invalid buffer object");
    size_t len = rt_arr_i64_len(buf->arr);
    if (len == 0) {
        rt_trap("I64Buffer.Min: empty buffer");
        return 0;
    }
    int64_t value = rt_arr_i64_get_fast(buf->arr, 0);
    for (size_t i = 1; i < len; i++) {
        int64_t next = rt_arr_i64_get_fast(buf->arr, i);
        if (next < value)
            value = next;
    }
    return value;
}

double rt_f64buf_max(void *obj) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.Max: invalid buffer object");
    size_t len = rt_arr_f64_len(buf->arr);
    if (len == 0) {
        rt_trap("F64Buffer.Max: empty buffer");
        return 0.0;
    }
    double value = rt_arr_f64_get_fast(buf->arr, 0);
    for (size_t i = 1; i < len; i++) {
        double next = rt_arr_f64_get_fast(buf->arr, i);
        if (next > value)
            value = next;
    }
    return value;
}

int64_t rt_i64buf_max(void *obj) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.Max: invalid buffer object");
    size_t len = rt_arr_i64_len(buf->arr);
    if (len == 0) {
        rt_trap("I64Buffer.Max: empty buffer");
        return 0;
    }
    int64_t value = rt_arr_i64_get_fast(buf->arr, 0);
    for (size_t i = 1; i < len; i++) {
        int64_t next = rt_arr_i64_get_fast(buf->arr, i);
        if (next > value)
            value = next;
    }
    return value;
}

void *rt_f64buf_to_list(void *obj) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.ToList: invalid buffer object");
    void *list = rt_list_new();
    size_t len = rt_arr_f64_len(buf->arr);
    for (size_t i = 0; i < len; i++) {
        void *boxed = rt_box_f64(rt_arr_f64_get_fast(buf->arr, i));
        rt_list_push(list, boxed);
        release_temp_obj(boxed);
    }
    return list;
}

void *rt_i64buf_to_list(void *obj) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.ToList: invalid buffer object");
    void *list = rt_list_new();
    size_t len = rt_arr_i64_len(buf->arr);
    for (size_t i = 0; i < len; i++) {
        void *boxed = rt_box_i64(rt_arr_i64_get_fast(buf->arr, i));
        rt_list_push(list, boxed);
        release_temp_obj(boxed);
    }
    return list;
}

void *rt_f64buf_to_seq(void *obj) {
    rt_f64buf_impl *buf = as_f64buf(obj, "F64Buffer.ToSeq: invalid buffer object");
    void *seq = rt_seq_with_capacity_owned(rt_f64buf_len(obj) > 0 ? rt_f64buf_len(obj) : 1);
    size_t len = rt_arr_f64_len(buf->arr);
    for (size_t i = 0; i < len; i++) {
        void *boxed = rt_box_f64(rt_arr_f64_get_fast(buf->arr, i));
        rt_seq_push(seq, boxed);
        release_temp_obj(boxed);
    }
    return seq;
}

void *rt_i64buf_to_seq(void *obj) {
    rt_i64buf_impl *buf = as_i64buf(obj, "I64Buffer.ToSeq: invalid buffer object");
    void *seq = rt_seq_with_capacity_owned(rt_i64buf_len(obj) > 0 ? rt_i64buf_len(obj) : 1);
    size_t len = rt_arr_i64_len(buf->arr);
    for (size_t i = 0; i < len; i++) {
        void *boxed = rt_box_i64(rt_arr_i64_get_fast(buf->arr, i));
        rt_seq_push(seq, boxed);
        release_temp_obj(boxed);
    }
    return seq;
}
