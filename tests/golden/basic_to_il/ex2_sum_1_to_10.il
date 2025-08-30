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
  %t1 = const_str @.L0
  call @rt_print_str(%t1)
  br label L20
L20:
  %t2 = add 1, 2
  %t3 = add %t2, 3
  %t4 = add %t3, 4
  %t5 = add %t4, 5
  %t6 = add %t5, 6
  %t7 = add %t6, 7
  %t8 = add %t7, 8
  %t9 = add %t8, 9
  store i64, %t0, %t9
  br label L30
L30:
  %t10 = load i64, %t0
  call @rt_print_i64(%t10)
  br label L40
L40:
  %t11 = const_str @.L1
  call @rt_print_str(%t11)
  br label L50
L50:
  br label exit
exit:
  ret 0
}
