il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
global const str @.L0 = ""
global const str @.L1 = "
"
global const str @.L2 = "HI"
func @F#(f64 %X#) -> f64 {
entry_F#:
  %t1 = alloca 8
  store f64, %t1, %t0
  br L0_F#
L0_F#:
  .loc 1 2 8
  %t2 = load f64, %t1
  .loc 1 2 8
  ret %t2
ret_F#:
  ret 0.0
}
func @ID$(str %S$) -> str {
entry_ID$:
  %t4 = alloca 8
  store str, %t4, %t0
  br L0_ID$
L0_ID$:
  .loc 1 5 8
  %t5 = load str, %t4
  .loc 1 5 8
  ret %t5
ret_ID$:
  %t6 = const_str @.L0
  ret %t6
}
func @FIRST#(i64 %N) -> f64 {
entry_FIRST#:
  %t8 = alloca 8
  store i64, %t8, %t0
  br L0_FIRST#
L0_FIRST#:
L0_FIRST#:
  .loc 1 8 1
  br if_test_0_FIRST#
  .loc 1 9 16
  %t11 = load i64, %t8
  .loc 1 9 8
  %t12 = sitofp %t11
  .loc 1 9 8
  %t13 = call @SECOND#(%t12)
  .loc 1 9 8
  ret %t13
ret_FIRST#:
  ret 0.0
if_test_0_FIRST#:
  .loc 1 8 4
  %t9 = load i64, %t8
  .loc 1 8 6
  %t10 = scmp_le %t9, 0
  .loc 1 8 6
  cbr %t10, if_then_0_FIRST#, if_else_0_FIRST#
if_then_0_FIRST#:
  .loc 1 8 23
  ret 0.0
if_else_0_FIRST#:
  .loc 1 8 1
  br if_end_0_FIRST#
if_end_0_FIRST#:
  .loc 1 8 1
  br L0_FIRST#
}
func @SECOND#(f64 %N#) -> f64 {
entry_SECOND#:
  %t15 = alloca 8
  store f64, %t15, %t0
  br L0_SECOND#
L0_SECOND#:
L0_SECOND#:
  .loc 1 12 1
  br if_test_0_SECOND#
  .loc 1 13 15
  %t19 = load f64, %t15
  .loc 1 13 18
  %t20 = sitofp 1
  .loc 1 13 18
  %t21 = fsub %t19, %t20
  .loc 1 13 8
  %t22 = fptosi %t21
  .loc 1 13 8
  %t23 = call @FIRST#(%t22)
  .loc 1 13 8
  ret %t23
ret_SECOND#:
  ret 0.0
if_test_0_SECOND#:
  .loc 1 12 4
  %t16 = load f64, %t15
  .loc 1 12 7
  %t17 = sitofp 0
  .loc 1 12 7
  %t18 = fcmp_le %t16, %t17
  .loc 1 12 7
  cbr %t18, if_then_0_SECOND#, if_else_0_SECOND#
if_then_0_SECOND#:
  .loc 1 12 24
  ret 0.0
if_else_0_SECOND#:
  .loc 1 12 1
  br if_end_0_SECOND#
if_end_0_SECOND#:
  .loc 1 12 1
  br L0_SECOND#
}
func @main() -> i64 {
entry:
  br L10
L10:
  .loc 1 15 10
  %t24 = sitofp 1
  .loc 1 15 10
  %t25 = call @F#(%t24)
  .loc 1 15 4
  call @rt_print_f64(%t25)
  .loc 1 15 4
  %t26 = const_str @.L1
  .loc 1 15 4
  call @rt_print_str(%t26)
  .loc 1 15 4
  br L20
L20:
  .loc 1 16 14
  %t27 = const_str @.L2
  .loc 1 16 10
  %t28 = call @ID$(%t27)
  .loc 1 16 4
  call @rt_print_str(%t28)
  .loc 1 16 4
  %t29 = const_str @.L1
  .loc 1 16 4
  call @rt_print_str(%t29)
  .loc 1 16 4
  br L30
L30:
  .loc 1 17 10
  %t30 = call @FIRST#(2)
  .loc 1 17 4
  call @rt_print_f64(%t30)
  .loc 1 17 4
  %t31 = const_str @.L1
  .loc 1 17 4
  call @rt_print_str(%t31)
  .loc 1 17 4
  br L40
L40:
  .loc 1 18 4
  br exit
exit:
  ret 0
}
