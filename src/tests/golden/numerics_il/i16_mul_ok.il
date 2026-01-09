il 0.2.0
extern @rt_print_i64(i64) -> void
extern @rt_print_str(str) -> void
extern @rt_chr(i64) -> str
func @main() -> i64 {
entry:
  %t0:i16 = imul.ovf 200, 150
  call @rt_print_i64(%t0)
  %t1 = call @rt_chr(10)
  call @rt_print_str(%t1)
  ret 0
}
