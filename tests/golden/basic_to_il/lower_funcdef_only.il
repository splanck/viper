il 0.1.2
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
  .loc 1 2 8
  ret 5
ret_F:
  ret 0
}
func @S() -> void {
entry_S:
  br L-1000000000_S
L-1000000000_S:
  .loc 1 5 1
  call @rt_print_i64(1)
  .loc 1 5 1
  %t0 = const_str @.L0
  .loc 1 5 1
  call @rt_print_str(%t0)
  .loc 1 5 1
  br ret_S
ret_S:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  ret 0
exit:
  ret 0
}
