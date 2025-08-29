#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rt_print_str(rt_str s) {
  if (s && s->data) {
    fputs(s->data, stdout);
    fputc('\n', stdout);
  }
}
void rt_print_i64(int64_t v) { printf("%lld\n", (long long)v); }
rt_str rt_input_line(void) { return NULL; }
int64_t rt_len(rt_str s) { return s && s->data ? (int64_t)strlen(s->data) : 0; }
rt_str rt_concat(rt_str a, rt_str b) {
  (void)a;
  (void)b;
  return NULL;
}
void *rt_alloc(int64_t bytes) { return malloc((size_t)bytes); }
rt_str rt_const_cstr(const char *c) {
  rt_str s = (rt_str)rt_alloc(sizeof(*s));
  if (!s)
    return NULL;
  s->data = c;
  return s;
}
