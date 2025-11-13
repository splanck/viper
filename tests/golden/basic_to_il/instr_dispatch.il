il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_instr2(str, str) -> i64
extern @rt_instr3(i64, str, str) -> i64
extern @rt_str_empty() -> str
extern @rt_str_retain_maybe(str) -> void
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "HELLO"
global const str @.L1 = "LL"
global const str @.L2 = "\n"
global const str @.L3 = "L"
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
  .loc 1 2 16
  %t4 = load str, %t0
  .loc 1 2 20
  %t5 = const_str @.L1
  .loc 1 2 20
  %t6 = call @rt_instr2(%t4, %t5)
  .loc 1 2 20
  call @rt_print_i64(%t6)
  .loc 1 2 20
  %t7 = const_str @.L2
  .loc 1 2 20
  call @rt_print_str(%t7)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 16
  %t8 = iadd.ovf 3, -1
  .loc 1 3 19
  %t9 = load str, %t0
  .loc 1 3 23
  %t10 = const_str @.L3
  .loc 1 3 23
  %t11 = call @rt_instr3(%t8, %t9, %t10)
  .loc 1 3 23
  call @rt_print_i64(%t11)
  .loc 1 3 23
  %t12 = const_str @.L2
  .loc 1 3 23
  call @rt_print_str(%t12)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  ret 0
exit:
  ret 0
}
