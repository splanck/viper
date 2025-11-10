il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
global const str @.L0 = "A"
global const str @.L1 = "B"
global const str @.L2 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 10
  %t0 = const_str @.L0
  .loc 1 1 4
  call @rt_print_str(%t0)
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t1 = const_str @.L1
  .loc 1 2 4
  call @rt_print_str(%t1)
  .loc 1 2 4
  %t2 = const_str @.L2
  .loc 1 2 4
  call @rt_print_str(%t2)
  .loc 1 2 4
  br exit
exit:
  ret 0
}
