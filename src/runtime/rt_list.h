//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_list.h
// Purpose: Runtime-backed list of object references for Viper.Collections.List.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Allocate a new List instance (opaque object pointer with vptr at offset 0).
    void *rt_ns_list_new(void);
    // Property getter: Count
    int64_t rt_list_get_count(void *list);
    // Methods
    void rt_list_add(void *list, void *elem);
    void rt_list_clear(void *list);
    void rt_list_remove_at(void *list, int64_t index);
    void *rt_list_get_item(void *list, int64_t index);
    void rt_list_set_item(void *list, int64_t index, void *elem);

#ifdef __cplusplus
}
#endif
