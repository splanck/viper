#include "rt.h"
#include <cassert>
int main() {
  rt_str empty = rt_const_cstr("");
  assert(rt_len(empty) == 0);

  rt_str hello = rt_const_cstr("hello");
  rt_str world = rt_const_cstr("world");
  rt_str hw = rt_concat(hello, world);
  assert(rt_len(hw) == 10);
  rt_str helloworld = rt_const_cstr("helloworld");
  assert(rt_str_eq(hw, helloworld));

  rt_str sub0 = rt_substr(hw, 0, 5);
  assert(rt_str_eq(sub0, hello));
  rt_str sub1 = rt_substr(hw, 5, 5);
  assert(rt_str_eq(sub1, world));
  rt_str subempty = rt_substr(hw, 10, 0);
  assert(rt_len(subempty) == 0);

  assert(!rt_str_eq(hello, world));

  rt_str num = rt_const_cstr("  -42 ");
  assert(rt_to_int(num) == -42);

  return 0;
}
