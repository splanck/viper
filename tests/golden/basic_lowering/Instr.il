il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_instr2(str, str) -> i64
extern @rt_instr3(i64, str, str) -> i64
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
  store str, %t0, %t1
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 16
  %t2 = load str, %t0
  .loc 1 2 20
  %t3 = const_str @.L1
  .loc 1 2 20
  %t4 = call @rt_instr2(%t2, %t3)
  .loc 1 2 4
  call @rt_print_i64(%t4)
  .loc 1 2 4
  %t5 = const_str @.L2
  .loc 1 2 4
  call @rt_print_str(%t5)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 16
  %t6 = iadd.ovf 3, -1
  .loc 1 3 19
  %t7 = load str, %t0
  .loc 1 3 23
  %t8 = const_str @.L3
  .loc 1 3 23
  %t9 = call @rt_instr3(%t6, %t7, %t8)
  .loc 1 3 4
  call @rt_print_i64(%t9)
  .loc 1 3 4
  %t10 = const_str @.L2
  .loc 1 3 4
  call @rt_print_str(%t10)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  br exit
exit:
  ret 0
}
