il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_instr2(str, str) -> i64
extern @rt_instr3(i64, str, str) -> i64
extern @rt_str_retain_maybe(str) -> void
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "HELLO"
global const str @.L1 = "LL"
global const str @.L2 = "
"
global const str @.L3 = "L"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  br L10
L10:
  .loc 1 1 13
  %t1 = const_str @.L0
  .loc 1 1 4
  %t2 = load str, %t0
  .loc 1 1 4
  call @rt_str_release_maybe(%t2)
  .loc 1 1 4
  call @rt_str_retain_maybe(%t1)
  .loc 1 1 4
  store str, %t0, %t1
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 16
  %t3 = load str, %t0
  .loc 1 2 20
  %t4 = const_str @.L1
  .loc 1 2 20
  %t5 = call @rt_instr2(%t3, %t4)
  .loc 1 2 4
  call @rt_print_i64(%t5)
  .loc 1 2 4
  %t6 = const_str @.L2
  .loc 1 2 4
  call @rt_print_str(%t6)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 16
  %t7 = iadd.ovf 3, -1
  .loc 1 3 19
  %t8 = load str, %t0
  .loc 1 3 23
  %t9 = const_str @.L3
  .loc 1 3 23
  %t10 = call @rt_instr3(%t7, %t8, %t9)
  .loc 1 3 4
  call @rt_print_i64(%t10)
  .loc 1 3 4
  %t11 = const_str @.L2
  .loc 1 3 4
  call @rt_print_str(%t11)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  ret 0
exit:
  ret 0
}
