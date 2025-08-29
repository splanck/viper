#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_str_impl *rt_str; // opaque handle
void rt_print_str(rt_str s);
void rt_print_i64(int64_t v);
rt_str rt_input_line(void);           // stub OK (not used in tests)
int64_t rt_len(rt_str s);             // stub OK for now
rt_str rt_concat(rt_str a, rt_str b); // stub OK for now
void *rt_alloc(int64_t bytes);        // wraps malloc
rt_str rt_const_cstr(const char *);   // create immutable rt_str from UTF-8 literal

#ifdef __cplusplus
}
#endif
