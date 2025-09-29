il 0.1.2
extern @rt_print_i64(i64) -> void
extern @rt_print_str(str) -> void
extern @rt_chr(i64) -> str
func @main() -> i64 {
entry:
  %t0 = srem.chk0 -3, 2
  call @rt_print_i64(%t0)
  %nl0 = call @rt_chr(10)
  call @rt_print_str(%nl0)
  %t1 = srem.chk0 3, -2
  call @rt_print_i64(%t1)
  %nl1 = call @rt_chr(10)
  call @rt_print_str(%nl1)
  %t2 = srem.chk0 -3, -2
  call @rt_print_i64(%t2)
  %nl2 = call @rt_chr(10)
  call @rt_print_str(%nl2)
  ret 0
}
