il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_mid2(str, i64) -> str
extern @rt_mid3(str, i64, i64) -> str
extern @rt_str_empty() -> str
extern @rt_str_retain_maybe(str) -> void
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "HELLO"
global const str @.L1 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  %t1 = call @rt_str_empty()
  store str, %t0, %t1
  br L10
L10:
  .loc 1 1 13
  %t2 = const_str @.L0
  .loc 1 1 4
  %t3 = load str, %t0
  .loc 1 1 4
  call @rt_str_release_maybe(%t3)
  .loc 1 1 4
  call @rt_str_retain_maybe(%t2)
  .loc 1 1 4
  store str, %t0, %t2
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 14
  %t4 = load str, %t0
  .loc 1 2 10
  %t5 = call @rt_len(%t4)
  .loc 1 2 4
  call @rt_print_i64(%t5)
  .loc 1 2 4
  %t6 = const_str @.L1
  .loc 1 2 4
  call @rt_print_str(%t6)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 15
  %t7 = load str, %t0
  .loc 1 3 19
  %t8 = iadd.ovf 2, -1
  .loc 1 3 22
  %t9 = call @rt_mid3(%t7, %t8, 3)
  .loc 1 3 4
  call @rt_print_str(%t9)
  .loc 1 3 4
  %t10 = const_str @.L1
  .loc 1 3 4
  call @rt_print_str(%t10)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 15
  %t11 = load str, %t0
  .loc 1 4 19
  %t12 = iadd.ovf 3, -1
  .loc 1 4 10
  %t13 = call @rt_mid2(%t11, %t12)
  .loc 1 4 4
  call @rt_print_str(%t13)
  .loc 1 4 4
  %t14 = const_str @.L1
  .loc 1 4 4
  call @rt_print_str(%t14)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 4
  ret 0
exit:
  ret 0
}
