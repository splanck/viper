il 0.1.3
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
extern @rt_trap(str) -> void
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @F() -> i64 {
entry_F:
  br L-1000000000_F
L-1000000000_F:
  .loc 1 2 1
  br if_test_0_F
ret_F:
  ret 0
if_test_0_F:
  .loc 1 2 4
  %t0 = trunc1 1
  .loc 1 2 4
  cbr %t0, if_then_0_F, if_else_0_F
if_then_0_F:
  .loc 1 2 21
  ret 1
if_else_0_F:
  .loc 1 2 35
  ret 2
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br UL1000000000
UL1000000000:
  .loc 1 4 7
  %t0 = call @F()
  .loc 1 4 7
  call @rt_print_i64(%t0)
  .loc 1 4 7
  %t1 = const_str @.L0
  .loc 1 4 7
  call @rt_print_str(%t1)
  .loc 1 4 1
  br exit
exit:
  ret 0
}
