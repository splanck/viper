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
  %t2 = add 2, 3
  store i64, %t1, %t2
  br label L20
L20:
  %t3 = load i64, %t1
  %t4 = mul %t3, 2
  store i64, %t0, %t4
  br label L30
L30:
  %t5 = const_str @.L0
  call @rt_print_str(%t5)
  br label L40
L40:
  %t6 = const_str @.L1
  call @rt_print_str(%t6)
  br label L50
L50:
  %t7 = load i64, %t0
  call @rt_print_i64(%t7)
  br label L60
L60:
  %t8 = load i64, %t0
  %t9 = scmp_gt %t8, 8
  cbr %t9, label then, label else
L70:
  br label exit
exit:
  ret 0
then:
  %t10 = load i64, %t0
  call @rt_print_i64(%t10)
  br label exit1
exit1:
  br label L70
else:
  call @rt_print_i64(4)
  br label exit1
}
