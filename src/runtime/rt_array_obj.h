#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

// Arrays of object references (void*). Payload pointer is the first element.
// Semantics:
// - new: allocate array of length len, initialized to NULLs
// - len: return logical length
// - get: returns retained reference (caller must later release)
// - put: retain new value, release old value
// - resize: adjust length, zero-initialize new tail; may move payload
// - release: release all elements and the array itself

void **rt_arr_obj_new(size_t len);
size_t rt_arr_obj_len(void **arr);
void  *rt_arr_obj_get(void **arr, size_t idx);            // returns retained
void   rt_arr_obj_put(void **arr, size_t idx, void *obj); // retain new, release old
void **rt_arr_obj_resize(void **arr, size_t len);
void   rt_arr_obj_release(void **arr);

#ifdef __cplusplus
}
#endif

