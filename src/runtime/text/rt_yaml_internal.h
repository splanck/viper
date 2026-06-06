//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_yaml_internal.h
// Purpose: Shared YAML value classifiers/release used by the parser (rt_yaml.c)
//   and the emitter (rt_yaml_format.c).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_box.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"

#define YAML_SEQ_MIN_PAYLOAD (sizeof(int64_t) * 2 + sizeof(void *) + sizeof(int8_t))
#define YAML_MAP_MIN_PAYLOAD (sizeof(void *) * 2 + sizeof(size_t) * 2)


/// @brief Drop a temporary reference to a parsed-out object (refcount-aware).
static inline void yaml_release(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief True if `obj` is a boxed primitive (Int/Bool/Float/String wrapper).
///
/// YAML scalars become boxed primitives; sequences/mappings stay
/// as plain GC objects. The class-id check distinguishes the two.
static inline bool yaml_is_boxed(void *obj) {
    return obj && !rt_string_is_handle(obj) && rt_box_type(obj) >= 0;
}

/// @brief True if `obj` is a Viper Sequence (`Seq`) container.
static inline bool yaml_is_sequence(void *obj) {
    if (!obj || rt_string_is_handle(obj) || yaml_is_boxed(obj))
        return false;
    return rt_obj_is_instance(obj, RT_SEQ_CLASS_ID, YAML_SEQ_MIN_PAYLOAD);
}

/// @brief True if `obj` is a Viper Mapping (`Map`) container.
static inline bool yaml_is_mapping(void *obj) {
    if (!obj || rt_string_is_handle(obj) || yaml_is_boxed(obj))
        return false;
    return rt_obj_is_instance(obj, RT_MAP_CLASS_ID, YAML_MAP_MIN_PAYLOAD);
}

void *parse_scalar(const char *str, size_t len);
