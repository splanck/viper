il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
global const str @.L0 = "SUM"
global const str @.L1 = "DONE"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  br label L10
L10:
  .loc 1 1 10
  %t1 = const_str @.L0
  .loc 1 1 4
  call @rt_print_str(%t1)
  .loc 1 1 4
  br label L20
L20:
  .loc 1 2 4
  store i64, %t0, 45
  .loc 1 2 4
  br label L30
L30:
  .loc 1 3 10
  %t2 = load i64, %t0
  .loc 1 3 4
  call @rt_print_i64(%t2)
  .loc 1 3 4
  br label L40
L40:
  .loc 1 4 10
  %t3 = const_str @.L1
  .loc 1 4 4
  call @rt_print_str(%t3)
  .loc 1 4 4
  br label L50
L50:
  .loc 1 5 4
  br label exit
exit:
  ret 0
}
