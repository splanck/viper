#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct rt_str_impl {
  const char *data;
};

void rt_print_str(rt_str s) {
  if (s && s->data)
    printf("%s\n", s->data);
}

void rt_print_i64(int64_t v) { printf("%lld\n", (long long)v); }

rt_str rt_input_line(void) { return rt_const_cstr(""); }

int64_t rt_len(rt_str s) {
  (void)s;
  return 0;
}

rt_str rt_concat(rt_str a, rt_str b) {
  (void)a;
  (void)b;
  return rt_const_cstr("");
}

void *rt_alloc(int64_t bytes) { return malloc((size_t)bytes); }

rt_str rt_const_cstr(const char *c) {
  struct rt_str_impl *s = (struct rt_str_impl *)rt_alloc(sizeof(struct rt_str_impl));
  s->data = c;
  return s;
}
