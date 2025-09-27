il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_int_floor(f64) -> f64
extern @rt_val(str) -> f64
extern @rt_val_to_double(ptr, ptr) -> f64
extern @rt_string_cstr(str) -> ptr
extern @rt_str(f64) -> str
extern @rt_int_floor(f64) -> f64
global const str @.L0 = " "
global const str @.L1 = "
"
global const str @.L2 = " 42 "
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  %t2 = alloca 8
  %t3 = alloca 8
  %t4 = alloca 8
  br L10
L10:
  .loc 1 1 4
  store i64, %t4, 1
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  store f64, %t3, 2.5
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 15
  %t5 = load i64, %t4
  .loc 1 3 15
  %t6 = sitofp %t5
  .loc 1 3 10
  %t7 = call @rt_str(%t6)
  .loc 1 3 4
  call @rt_print_str(%t7)
  .loc 1 3 4
  %t8 = const_str @.L0
  .loc 1 3 4
  call @rt_print_str(%t8)
  .loc 1 3 24
  %t9 = load f64, %t3
  .loc 1 3 19
  %t10 = call @rt_str(%t9)
  .loc 1 3 4
  call @rt_print_str(%t10)
  .loc 1 3 4
  %t11 = const_str @.L1
  .loc 1 3 4
  call @rt_print_str(%t11)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 13
  %t12 = const_str @.L2
  .loc 1 4 4
  store str, %t2, %t12
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 14
  %t13 = load str, %t2
  .loc 1 5 14
  %t14 = call @rt_string_cstr(%t13)
  .loc 1 5 14
  %t15 = alloca 1
  .loc 1 5 14
  %t16 = call @rt_val_to_double(%t14, %t15)
  .loc 1 5 14
  %t17 = load i1, %t15
  .loc 1 5 14
  cbr %t17, val_ok, val_fail
L60:
  .loc 1 6 4
  store f64, %t1, 1.9
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 4
  store f64, %t0, -1.9
  .loc 1 7 4
  br L80
L80:
  .loc 1 8 14
  %t22 = load f64, %t1
  .loc 1 8 10
  %t23 = call @rt_int_floor(%t22)
  .loc 1 8 4
  call @rt_print_f64(%t23)
  .loc 1 8 4
  %t24 = const_str @.L0
  .loc 1 8 4
  call @rt_print_str(%t24)
  .loc 1 8 23
  %t25 = load f64, %t0
  .loc 1 8 19
  %t26 = call @rt_int_floor(%t25)
  .loc 1 8 4
  call @rt_print_f64(%t26)
  .loc 1 8 4
  %t27 = const_str @.L1
  .loc 1 8 4
  call @rt_print_str(%t27)
  .loc 1 8 4
  br L90
L90:
  .loc 1 9 4
  br exit
exit:
  ret 0
val_ok:
  .loc 1 5 4
  call @rt_print_f64(%t16)
  .loc 1 5 4
  %t21 = const_str @.L1
  .loc 1 5 4
  call @rt_print_str(%t21)
  .loc 1 5 4
  br L60
val_fail:
  .loc 1 5 14
  %t18 = fcmp_ne %t16, %t16
  .loc 1 5 14
  cbr %t18, val_nan, val_over
val_nan:
  .loc 1 5 14
  %t19 = cast.fp_to_si.rte.chk nan
  .loc 1 5 14
  trap
val_over:
  .loc 1 5 14
  %t20 = cast.fp_to_si.rte.chk 1.797693134862316e+308
  .loc 1 5 14
  trap
}
