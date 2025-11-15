il 0.1.2
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
  br UL1000000000
UL1000000000:
  .loc 1 1 8
  call @rt_print_i64(3)
  .loc 1 1 8
  %t0 = const_str @.L0
  .loc 1 1 8
  call @rt_print_str(%t0)
  .loc 1 1 1
  br exit
exit:
  ret 0
}
