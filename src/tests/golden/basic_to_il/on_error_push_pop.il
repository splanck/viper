il 0.2.0
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 4
  eh.push ^handler_L100
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  call @rt_print_i64(1)
  .loc 1 2 10
  %t2 = const_str @.L0
  .loc 1 2 10
  call @rt_print_str(%t2)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  eh.pop
  .loc 1 3 4
  ret 0
L100:
  .loc 1 4 5
  trap
L110:
  .loc 1 5 5
  eh.pop
  .loc 1 5 5
  ret 0
exit:
  eh.pop
  ret 0
handler ^handler_L100(%err:Error, %tok:ResumeTok):
  eh.entry
  .loc 1 4 11
  call @rt_print_i64(2)
  .loc 1 4 11
  %t3 = const_str @.L0
  .loc 1 4 11
  call @rt_print_str(%t3)
}
