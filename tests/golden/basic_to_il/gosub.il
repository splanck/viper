il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
global const str @.L0 = "Back"
global const str @.L1 = "\n"
global const str @.L2 = "Done"
global const str @.L3 = "First"
global const str @.L4 = "After Inner"
global const str @.L5 = "Second"
global const str @.L6 = "Inner"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  %t2 = alloca 8
  %t3 = alloca 512
  store i64, %t2, 0
  br L10
L10:
  .loc 1 1 4
  store i64, %t1, 1
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  store i64, %t0, 2
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  %t4 = load i64, %t2
  .loc 1 3 4
  %t5 = scmp_ge %t4, 128
  .loc 1 3 4
  cbr %t5, gosub_overflow, gosub_push
L40:
  .loc 1 4 10
  %t9 = const_str @.L0
  .loc 1 4 4
  call @rt_print_str(%t9)
  .loc 1 4 4
  %t10 = const_str @.L1
  .loc 1 4 4
  call @rt_print_str(%t10)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 4
  %t11 = load i64, %t2
  .loc 1 5 4
  %t12 = scmp_ge %t11, 128
  .loc 1 5 4
  cbr %t12, gosub_overflow1, gosub_push1
L60:
  .loc 1 6 10
  %t16 = const_str @.L2
  .loc 1 6 4
  call @rt_print_str(%t16)
  .loc 1 6 4
  %t17 = const_str @.L1
  .loc 1 6 4
  call @rt_print_str(%t17)
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 4
  ret 0
L200:
  .loc 1 8 11
  %t18 = const_str @.L3
  .loc 1 8 5
  call @rt_print_str(%t18)
  .loc 1 8 5
  %t19 = const_str @.L1
  .loc 1 8 5
  call @rt_print_str(%t19)
  .loc 1 8 5
  br L210
L210:
  .loc 1 9 5
  %t20 = load i64, %t2
  .loc 1 9 5
  %t21 = scmp_ge %t20, 128
  .loc 1 9 5
  cbr %t21, gosub_overflow2, gosub_push2
L220:
  .loc 1 10 11
  %t25 = const_str @.L4
  .loc 1 10 5
  call @rt_print_str(%t25)
  .loc 1 10 5
  %t26 = const_str @.L1
  .loc 1 10 5
  call @rt_print_str(%t26)
  .loc 1 10 5
  br L230
L230:
  .loc 1 11 5
  %t27 = load i64, %t2
  .loc 1 11 5
  %t28 = icmp_eq %t27, 0
  .loc 1 11 5
  cbr %t28, gosub_ret_empty, gosub_ret_cont
L300:
  .loc 1 12 11
  %t33 = const_str @.L5
  .loc 1 12 5
  call @rt_print_str(%t33)
  .loc 1 12 5
  %t34 = const_str @.L1
  .loc 1 12 5
  call @rt_print_str(%t34)
  .loc 1 12 5
  br L310
L310:
  .loc 1 13 5
  %t35 = load i64, %t2
  .loc 1 13 5
  %t36 = icmp_eq %t35, 0
  .loc 1 13 5
  cbr %t36, gosub_ret_empty1, gosub_ret_cont1
L400:
  .loc 1 14 11
  %t41 = const_str @.L6
  .loc 1 14 5
  call @rt_print_str(%t41)
  .loc 1 14 5
  %t42 = const_str @.L1
  .loc 1 14 5
  call @rt_print_str(%t42)
  .loc 1 14 5
  br L410
L410:
  .loc 1 15 5
  %t43 = load i64, %t2
  .loc 1 15 5
  %t44 = icmp_eq %t43, 0
  .loc 1 15 5
  cbr %t44, gosub_ret_empty2, gosub_ret_cont2
exit:
  ret 0
gosub_overflow:
  .loc 1 3 4
  trap
gosub_push:
  .loc 1 3 4
  %t6 = imul.ovf %t4, 4
  .loc 1 3 4
  %t7 = gep %t3, %t6
  .loc 1 3 4
  store i32, %t7, 0
  .loc 1 3 4
  %t8 = iadd.ovf %t4, 1
  .loc 1 3 4
  store i64, %t2, %t8
  .loc 1 3 4
  br L200
gosub_overflow1:
  .loc 1 5 4
  trap
gosub_push1:
  .loc 1 5 4
  %t13 = imul.ovf %t11, 4
  .loc 1 5 4
  %t14 = gep %t3, %t13
  .loc 1 5 4
  store i32, %t14, 1
  .loc 1 5 4
  %t15 = iadd.ovf %t11, 1
  .loc 1 5 4
  store i64, %t2, %t15
  .loc 1 5 4
  br L300
gosub_overflow2:
  .loc 1 9 5
  trap
gosub_push2:
  .loc 1 9 5
  %t22 = imul.ovf %t20, 4
  .loc 1 9 5
  %t23 = gep %t3, %t22
  .loc 1 9 5
  store i32, %t23, 2
  .loc 1 9 5
  %t24 = iadd.ovf %t20, 1
  .loc 1 9 5
  store i64, %t2, %t24
  .loc 1 9 5
  br L400
gosub_ret_empty:
  .loc 1 11 5
  trap
gosub_ret_cont:
  .loc 1 11 5
  %t29 = isub.ovf %t27, 1
  .loc 1 11 5
  store i64, %t2, %t29
  .loc 1 11 5
  %t30 = imul.ovf %t29, 4
  .loc 1 11 5
  %t31 = gep %t3, %t30
  .loc 1 11 5
  %t32 = load i32, %t31
  .loc 1 11 5
  switch.i32 %t32, ^gosub_ret_invalid, 0 -> ^L40, 1 -> ^L60, 2 -> ^L220
gosub_ret_invalid:
  .loc 1 11 5
  trap
gosub_ret_empty1:
  .loc 1 13 5
  trap
gosub_ret_cont1:
  .loc 1 13 5
  %t37 = isub.ovf %t35, 1
  .loc 1 13 5
  store i64, %t2, %t37
  .loc 1 13 5
  %t38 = imul.ovf %t37, 4
  .loc 1 13 5
  %t39 = gep %t3, %t38
  .loc 1 13 5
  %t40 = load i32, %t39
  .loc 1 13 5
  switch.i32 %t40, ^gosub_ret_invalid1, 0 -> ^L40, 1 -> ^L60, 2 -> ^L220
gosub_ret_invalid1:
  .loc 1 13 5
  trap
gosub_ret_empty2:
  .loc 1 15 5
  trap
gosub_ret_cont2:
  .loc 1 15 5
  %t45 = isub.ovf %t43, 1
  .loc 1 15 5
  store i64, %t2, %t45
  .loc 1 15 5
  %t46 = imul.ovf %t45, 4
  .loc 1 15 5
  %t47 = gep %t3, %t46
  .loc 1 15 5
  %t48 = load i32, %t47
  .loc 1 15 5
  switch.i32 %t48, ^gosub_ret_invalid2, 0 -> ^L40, 1 -> ^L60, 2 -> ^L220
gosub_ret_invalid2:
  .loc 1 15 5
  trap
}
