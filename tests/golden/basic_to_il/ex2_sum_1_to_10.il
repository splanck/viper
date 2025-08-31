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
  .loc 1 2 14
  %t2 = add 1, 2
  .loc 1 2 18
  %t3 = add %t2, 3
  .loc 1 2 22
  %t4 = add %t3, 4
  .loc 1 2 26
  %t5 = add %t4, 5
  .loc 1 2 30
  %t6 = add %t5, 6
  .loc 1 2 34
  %t7 = add %t6, 7
  .loc 1 2 38
  %t8 = add %t7, 8
  .loc 1 2 42
  %t9 = add %t8, 9
  .loc 1 2 4
  store i64, %t0, %t9
  .loc 1 2 4
  br label L30
L30:
  .loc 1 3 10
  %t10 = load i64, %t0
  .loc 1 3 4
  call @rt_print_i64(%t10)
  .loc 1 3 4
  br label L40
L40:
  .loc 1 4 10
  %t11 = const_str @.L1
  .loc 1 4 4
  call @rt_print_str(%t11)
  .loc 1 4 4
  br label L50
L50:
  .loc 1 5 4
  br label exit
exit:
  ret 0
}
