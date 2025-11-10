il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
global const str @.L0 = "A"
global const str @.L1 = "             "
global const str @.L2 = "B"
global const str @.L3 = "\n"
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
  %t1 = const_str @.L1
  .loc 1 1 4
  call @rt_print_str(%t1)
  .loc 1 1 4
  call @rt_print_i64(1)
  .loc 1 1 4
  %t2 = const_str @.L1
  .loc 1 1 4
  call @rt_print_str(%t2)
  .loc 1 1 18
  %t3 = const_str @.L2
  .loc 1 1 4
  call @rt_print_str(%t3)
  .loc 1 1 4
  %t4 = const_str @.L3
  .loc 1 1 4
  call @rt_print_str(%t4)
  .loc 1 1 4
  br exit
exit:
  ret 0
}
