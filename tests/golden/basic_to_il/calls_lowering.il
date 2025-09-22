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
  %t1 = alloca 8
  store str, %t1, %t0
  br L0_ID$
L0_ID$:
  .loc 1 5 8
  %t2 = load str, %t1
  .loc 1 5 8
  ret %t2
ret_ID$:
  %t3 = const_str @.L0
  ret %t3
}
func @FIRST#(i64 %N) -> f64 {
entry_FIRST#:
  %t1 = alloca 8
  store i64, %t1, %t0
  br L0_FIRST#
L0_FIRST#:
L0_FIRST#:
  .loc 1 8 1
  br if_test_0_FIRST#
  .loc 1 9 16
  %t4 = load i64, %t1
  .loc 1 9 8
  %t5 = sitofp %t4
  .loc 1 9 8
  %t6 = call @SECOND#(%t5)
  .loc 1 9 8
  ret %t6
ret_FIRST#:
  ret 0.0
if_test_0_FIRST#:
  .loc 1 8 4
  %t2 = load i64, %t1
  .loc 1 8 6
  %t3 = scmp_le %t2, 0
  .loc 1 8 6
  cbr %t3, if_then_0_FIRST#, if_else_0_FIRST#
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
  %t1 = alloca 8
  store f64, %t1, %t0
  br L0_SECOND#
L0_SECOND#:
L0_SECOND#:
  .loc 1 12 1
  br if_test_0_SECOND#
  .loc 1 13 15
  %t5 = load f64, %t1
  .loc 1 13 18
  %t6 = sitofp 1
  .loc 1 13 18
  %t7 = fsub %t5, %t6
  .loc 1 13 8
  %t8 = fptosi %t7
  .loc 1 13 8
  %t9 = call @FIRST#(%t8)
  .loc 1 13 8
  ret %t9
ret_SECOND#:
  ret 0.0
if_test_0_SECOND#:
  .loc 1 12 4
  %t2 = load f64, %t1
  .loc 1 12 7
  %t3 = sitofp 0
  .loc 1 12 7
  %t4 = fcmp_le %t2, %t3
  .loc 1 12 7
  cbr %t4, if_then_0_SECOND#, if_else_0_SECOND#
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
  %t0 = sitofp 1
  .loc 1 15 10
  %t1 = call @F#(%t0)
  .loc 1 15 4
  call @rt_print_f64(%t1)
  .loc 1 15 4
  %t2 = const_str @.L1
  .loc 1 15 4
  call @rt_print_str(%t2)
  .loc 1 15 4
  br L20
L20:
  .loc 1 16 14
  %t3 = const_str @.L2
  .loc 1 16 10
  %t4 = call @ID$(%t3)
  .loc 1 16 4
  call @rt_print_str(%t4)
  .loc 1 16 4
  %t5 = const_str @.L1
  .loc 1 16 4
  call @rt_print_str(%t5)
  .loc 1 16 4
  br L30
L30:
  .loc 1 17 10
  %t6 = call @FIRST#(2)
  .loc 1 17 4
  call @rt_print_f64(%t6)
  .loc 1 17 4
  %t7 = const_str @.L1
  .loc 1 17 4
  call @rt_print_str(%t7)
  .loc 1 17 4
  br L40
L40:
  .loc 1 18 4
  br exit
exit:
  ret 0
}
