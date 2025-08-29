#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_str_impl {
  int64_t refcnt;
  int64_t size;
  int64_t capacity;
  char *data;
} *rt_str;

void rt_trap(const char *msg);
void rt_print_str(rt_str s);
void rt_print_i64(int64_t v);
rt_str rt_input_line(void);
int64_t rt_len(rt_str s);
rt_str rt_concat(rt_str a, rt_str b);
rt_str rt_substr(rt_str s, int64_t start, int64_t len);
int64_t rt_str_eq(rt_str a, rt_str b);
int64_t rt_to_int(rt_str s);
void *rt_alloc(int64_t bytes);
rt_str rt_const_cstr(const char *);

#ifdef __cplusplus
}
#endif
