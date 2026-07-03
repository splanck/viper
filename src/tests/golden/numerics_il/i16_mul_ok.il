il 0.3.0
extern @rt_print_i64(i64) -> void
extern @rt_print_str(str) -> void
extern @rt_str_chr(i64) -> str
func @main() -> i64 {
entry:
  %t0:i16 = imul.ovf 200, 150
  %t1 = and %t0, 65535
  %t2 = shl %t1, 48
  %t3 = ashr %t2, 48
  call @rt_print_i64(%t3)
  %t4:str = call @rt_str_chr(10)
  call @rt_print_str(%t4)
  ret 0
}
