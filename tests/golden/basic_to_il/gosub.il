il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_input_line() -> str
extern @rt_to_int(str) -> i64
extern @rt_str_empty() -> str
global const str @.L0 = "Back"
global const str @.L1 = "\n"
global const str @.L2 = "RETURN without GOSUB"
global const str @.L3 = "Nested"
global const str @.L4 = "Deep"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  %t2 = alloca 8
  %t3 = alloca 8
  %t4 = alloca 512
  store i64, %t3, 0
  br L10
L10:
  .loc 1 1 4
  %t5 = call @rt_input_line()
  .loc 1 1 4
  %t6 = call @rt_to_int(%t5)
  .loc 1 1 4
  store i64, %t2, %t6
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  %t7 = call @rt_input_line()
  .loc 1 2 4
  %t8 = call @rt_to_int(%t7)
  .loc 1 2 4
  store i64, %t1, %t8
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  %t9 = load i64, %t3
  .loc 1 3 4
  %t10 = scmp_ge %t9, 128
  .loc 1 3 4
  cbr %t10, gosub_overflow, gosub_push
L40:
  .loc 1 4 10
  %t15 = const_str @.L0
  .loc 1 4 4
  call @rt_print_str(%t15)
  .loc 1 4 4
  %t16 = const_str @.L1
  .loc 1 4 4
  call @rt_print_str(%t16)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 4
  %t17 = load i64, %t3
  .loc 1 5 4
  %t18 = scmp_ge %t17, 128
  .loc 1 5 4
  cbr %t18, gosub_overflow1, gosub_push1
L60:
  .loc 1 6 4
  ret 0
L200:
  .loc 1 7 5
  br if_test_0
L230:
  .loc 1 8 11
  %t32 = load i64, %t2
  .loc 1 8 5
  call @rt_print_i64(%t32)
  .loc 1 8 14
  %t33 = load i64, %t1
  .loc 1 8 5
  call @rt_print_i64(%t33)
  .loc 1 8 5
  %t34 = const_str @.L1
  .loc 1 8 5
  call @rt_print_str(%t34)
  .loc 1 8 5
  br L240
L240:
  .loc 1 9 5
  %t35 = load i64, %t3
  .loc 1 9 5
  %t36 = icmp_eq %t35, 0
  .loc 1 9 5
  cbr %t36, gosub_underflow, gosub_return
L300:
  .loc 1 10 5
  %t44 = load i64, %t3
  .loc 1 10 5
  %t45 = scmp_ge %t44, 128
  .loc 1 10 5
  cbr %t45, gosub_overflow2, gosub_push2
L310:
  .loc 1 11 11
  %t50 = const_str @.L3
  .loc 1 11 5
  call @rt_print_str(%t50)
  .loc 1 11 5
  %t51 = const_str @.L1
  .loc 1 11 5
  call @rt_print_str(%t51)
  .loc 1 11 5
  br L320
L320:
  .loc 1 12 5
  %t52 = load i64, %t3
  .loc 1 12 5
  %t53 = icmp_eq %t52, 0
  .loc 1 12 5
  cbr %t53, gosub_underflow1, gosub_return1
L400:
  .loc 1 13 11
  %t61 = const_str @.L4
  .loc 1 13 5
  call @rt_print_str(%t61)
  .loc 1 13 5
  %t62 = const_str @.L1
  .loc 1 13 5
  call @rt_print_str(%t62)
  .loc 1 13 5
  br L410
L410:
  .loc 1 14 5
  %t63 = load i64, %t3
  .loc 1 14 5
  %t64 = icmp_eq %t63, 0
  .loc 1 14 5
  cbr %t64, gosub_underflow2, gosub_return2
exit:
  ret 0
gosub_overflow:
  .loc 1 3 4
  trap
gosub_push:
  .loc 1 3 4
  %t11 = shl %t9, 2
  .loc 1 3 4
  %t12 = gep %t4, %t11
  .loc 1 3 4
  %t13:i32 = cast.si_narrow.chk 0
  .loc 1 3 4
  store i32, %t12, %t13
  .loc 1 3 4
  %t14 = iadd.ovf %t9, 1
  .loc 1 3 4
  store i64, %t3, %t14
  .loc 1 3 4
  br L200
gosub_overflow1:
  .loc 1 5 4
  trap
gosub_push1:
  .loc 1 5 4
  %t19 = shl %t17, 2
  .loc 1 5 4
  %t20 = gep %t4, %t19
  .loc 1 5 4
  %t21:i32 = cast.si_narrow.chk 1
  .loc 1 5 4
  store i32, %t20, %t21
  .loc 1 5 4
  %t22 = iadd.ovf %t17, 1
  .loc 1 5 4
  store i64, %t3, %t22
  .loc 1 5 4
  br L300
if_test_0:
  .loc 1 7 8
  %t23 = load i64, %t2
  .loc 1 7 12
  %t24 = load i64, %t1
  .loc 1 7 10
  %t25 = scmp_lt %t23, %t24
  .loc 1 7 10
  %t26 = zext1 %t25
  .loc 1 7 10
  %t27 = isub.ovf 0, %t26
  .loc 1 7 5
  %t28 = trunc1 %t27
  .loc 1 7 5
  cbr %t28, if_then_0, if_else
if_then_0:
  .loc 1 7 27
  %t29 = load i64, %t2
  .loc 1 7 19
  store i64, %t0, %t29
  .loc 1 7 5
  br if_exit
if_else:
  .loc 1 7 5
  br if_exit
if_exit:
  .loc 1 7 38
  %t30 = load i64, %t1
  .loc 1 7 30
  store i64, %t2, %t30
  .loc 1 7 49
  %t31 = load i64, %t0
  .loc 1 7 41
  store i64, %t1, %t31
  .loc 1 7 5
  br L230
gosub_underflow:
  .loc 1 9 5
  %t37 = const_str @.L2
  .loc 1 9 5
  %t38 = trap.err 8, %t37
  .loc 1 9 5
  %t39 = err.get_code %t38
  .loc 1 9 5
  trap.from_err i32 %t39
gosub_return:
  .loc 1 9 5
  %t40 = isub.ovf %t35, 1
  .loc 1 9 5
  store i64, %t3, %t40
  .loc 1 9 5
  %t41 = shl %t40, 2
  .loc 1 9 5
  %t42 = gep %t4, %t41
  .loc 1 9 5
  %t43 = load i32, %t42
  .loc 1 9 5
  switch.i32 %t43, ^gosub_bad_index, 0 -> ^L40, 1 -> ^L60, 2 -> ^L310
gosub_bad_index:
  .loc 1 9 5
  trap
gosub_overflow2:
  .loc 1 10 5
  trap
gosub_push2:
  .loc 1 10 5
  %t46 = shl %t44, 2
  .loc 1 10 5
  %t47 = gep %t4, %t46
  .loc 1 10 5
  %t48:i32 = cast.si_narrow.chk 2
  .loc 1 10 5
  store i32, %t47, %t48
  .loc 1 10 5
  %t49 = iadd.ovf %t44, 1
  .loc 1 10 5
  store i64, %t3, %t49
  .loc 1 10 5
  br L400
gosub_underflow1:
  .loc 1 12 5
  %t54 = const_str @.L2
  .loc 1 12 5
  %t55 = trap.err 8, %t54
  .loc 1 12 5
  %t56 = err.get_code %t55
  .loc 1 12 5
  trap.from_err i32 %t56
gosub_return1:
  .loc 1 12 5
  %t57 = isub.ovf %t52, 1
  .loc 1 12 5
  store i64, %t3, %t57
  .loc 1 12 5
  %t58 = shl %t57, 2
  .loc 1 12 5
  %t59 = gep %t4, %t58
  .loc 1 12 5
  %t60 = load i32, %t59
  .loc 1 12 5
  switch.i32 %t60, ^gosub_bad_index1, 0 -> ^L40, 1 -> ^L60, 2 -> ^L310
gosub_bad_index1:
  .loc 1 12 5
  trap
gosub_underflow2:
  .loc 1 14 5
  %t65 = const_str @.L2
  .loc 1 14 5
  %t66 = trap.err 8, %t65
  .loc 1 14 5
  %t67 = err.get_code %t66
  .loc 1 14 5
  trap.from_err i32 %t67
gosub_return2:
  .loc 1 14 5
  %t68 = isub.ovf %t63, 1
  .loc 1 14 5
  store i64, %t3, %t68
  .loc 1 14 5
  %t69 = shl %t68, 2
  .loc 1 14 5
  %t70 = gep %t4, %t69
  .loc 1 14 5
  %t71 = load i32, %t70
  .loc 1 14 5
  switch.i32 %t71, ^gosub_bad_index2, 0 -> ^L40, 1 -> ^L60, 2 -> ^L310
gosub_bad_index2:
  .loc 1 14 5
  trap
}
