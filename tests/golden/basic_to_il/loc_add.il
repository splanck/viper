il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
func @main() -> i64 {
entry:
  br label L0
L0:
  .loc 1 1 8
  %t0 = add 1, 2
  .loc 1 1 1
  call @rt_print_i64(%t0)
  .loc 1 1 1
  br label exit
exit:
  ret 0
}
