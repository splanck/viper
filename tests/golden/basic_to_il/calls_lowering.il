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
func @main() -> i64 {
entry:
  br L10
L10:
  .loc 1 7 10
  %t7 = sitofp 1
  .loc 1 7 10
  %t8 = call @F#(%t7)
  .loc 1 7 4
  call @rt_print_f64(%t8)
  .loc 1 7 4
  %t9 = const_str @.L1
  .loc 1 7 4
  call @rt_print_str(%t9)
  .loc 1 7 4
  br L20
L20:
  .loc 1 8 14
  %t10 = const_str @.L2
  .loc 1 8 10
  %t11 = call @ID$(%t10)
  .loc 1 8 4
  call @rt_print_str(%t11)
  .loc 1 8 4
  %t12 = const_str @.L1
  .loc 1 8 4
  call @rt_print_str(%t12)
  .loc 1 8 4
  br L30
L30:
  .loc 1 9 4
  br exit
exit:
  ret 0
}
