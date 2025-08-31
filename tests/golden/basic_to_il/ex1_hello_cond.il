il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
global const str @.L0 = "HELLO"
global const str @.L1 = "READY"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  br label L10
L10:
  .loc 1 1 14
  %t2 = add 2, 3
  .loc 1 1 4
  store i64, %t1, %t2
  .loc 1 1 4
  br label L20
L20:
  .loc 1 2 12
  %t3 = load i64, %t1
  .loc 1 2 14
  %t4 = mul %t3, 2
  .loc 1 2 4
  store i64, %t0, %t4
  .loc 1 2 4
  br label L30
L30:
  .loc 1 3 10
  %t5 = const_str @.L0
  .loc 1 3 4
  call @rt_print_str(%t5)
  .loc 1 3 4
  br label L40
L40:
  .loc 1 4 10
  %t6 = const_str @.L1
  .loc 1 4 4
  call @rt_print_str(%t6)
  .loc 1 4 4
  br label L50
L50:
  .loc 1 5 10
  %t7 = load i64, %t0
  .loc 1 5 4
  call @rt_print_i64(%t7)
  .loc 1 5 4
  br label L60
L60:
  .loc 1 6 7
  %t8 = load i64, %t0
  .loc 1 6 9
  %t9 = scmp_gt %t8, 8
  .loc 1 6 4
  cbr %t9, label then, label else
L70:
  .loc 1 7 4
  br label exit
exit:
  ret 0
then:
  .loc 1 6 24
  %t10 = load i64, %t0
  .loc 1 6 18
  call @rt_print_i64(%t10)
  .loc 1 6 4
  br label exit1
exit1:
  .loc 1 6 4
  br label L70
else:
  .loc 1 6 31
  call @rt_print_i64(4)
  .loc 1 6 4
  br label exit1
}
