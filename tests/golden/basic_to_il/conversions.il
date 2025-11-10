il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_i32_alloc(i32) -> str
extern @rt_str_d_alloc(f64) -> str
extern @rt_int_floor(f64) -> f64
extern @rt_val(str) -> f64
extern @rt_val_to_double(ptr, ptr) -> f64
extern @rt_string_cstr(str) -> ptr
extern @rt_str_empty() -> str
extern @rt_str_retain_maybe(str) -> void
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "              "
global const str @.L1 = "\n"
global const str @.L2 = " 42 "
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  %t1 = alloca 8
  %t2 = alloca 8
  %t3 = call @rt_str_empty()
  store str, %t2, %t3
  %t4 = alloca 8
  %t5 = alloca 8
  br L10
L10:
  .loc 1 1 4
  store i64, %t5, 1
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  store f64, %t4, 2.5
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 15
  %t6 = load i64, %t5
  .loc 1 3 15
  %t7:i32 = cast.si_narrow.chk %t6
  .loc 1 3 10
  %t8 = call @rt_str_i32_alloc(%t7)
  .loc 1 3 4
  call @rt_print_str(%t8)
  .loc 1 3 4
  %t9 = const_str @.L0
  .loc 1 3 4
  call @rt_print_str(%t9)
  .loc 1 3 24
  %t10 = load f64, %t4
  .loc 1 3 19
  %t11 = call @rt_str_d_alloc(%t10)
  .loc 1 3 4
  call @rt_print_str(%t11)
  .loc 1 3 4
  %t12 = const_str @.L1
  .loc 1 3 4
  call @rt_print_str(%t12)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 13
  %t13 = const_str @.L2
  .loc 1 4 4
  %t14 = load str, %t2
  .loc 1 4 4
  call @rt_str_release_maybe(%t14)
  .loc 1 4 4
  call @rt_str_retain_maybe(%t13)
  .loc 1 4 4
  store str, %t2, %t13
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 14
  %t15 = load str, %t2
  .loc 1 5 14
  %t16 = call @rt_string_cstr(%t15)
  .loc 1 5 14
  %t17 = alloca 1
  .loc 1 5 14
  %t18 = call @rt_val_to_double(%t16, %t17)
  .loc 1 5 14
  %t19 = load i1, %t17
  .loc 1 5 14
  cbr %t19, val_ok, val_fail
L60:
  .loc 1 6 4
  store f64, %t1, 1.8999999999999999
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 4
  store f64, %t0, -1.8999999999999999
  .loc 1 7 4
  br L80
L80:
  .loc 1 8 14
  %t24 = load f64, %t1
  .loc 1 8 10
  %t25 = call @rt_int_floor(%t24)
  .loc 1 8 4
  call @rt_print_f64(%t25)
  .loc 1 8 4
  %t26 = const_str @.L0
  .loc 1 8 4
  call @rt_print_str(%t26)
  .loc 1 8 23
  %t27 = load f64, %t0
  .loc 1 8 19
  %t28 = call @rt_int_floor(%t27)
  .loc 1 8 4
  call @rt_print_f64(%t28)
  .loc 1 8 4
  %t29 = const_str @.L1
  .loc 1 8 4
  call @rt_print_str(%t29)
  .loc 1 8 4
  br L90
L90:
  .loc 1 9 4
  ret 0
exit:
  ret 0
val_ok:
  .loc 1 5 4
  call @rt_print_f64(%t18)
  .loc 1 5 4
  %t23 = const_str @.L1
  .loc 1 5 4
  call @rt_print_str(%t23)
  .loc 1 5 4
  br L60
val_fail:
  .loc 1 5 14
  %t20 = fcmp_ne %t18, %t18
  .loc 1 5 14
  cbr %t20, val_nan, val_over
val_nan:
  .loc 1 5 14
  %t21 = cast.fp_to_si.rte.chk nan
  .loc 1 5 14
  trap
val_over:
  .loc 1 5 14
  %t22 = cast.fp_to_si.rte.chk 1.7976931348623157e+308
  .loc 1 5 14
  trap
}
