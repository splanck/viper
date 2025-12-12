il 0.1.3
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
global const str @.L0 = "SUM"
global const str @.L1 = "\n"
global const str @.L2 = "DONE"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  br L10
L10:
  .loc 1 1 10
  %t1 = const_str @.L0
  .loc 1 1 10
  call @rt_print_str(%t1)
  .loc 1 1 10
  %t2 = const_str @.L1
  .loc 1 1 10
  call @rt_print_str(%t2)
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  store i64, %t0, 45
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 10
  %t3 = load i64, %t0
  .loc 1 3 10
  call @rt_print_i64(%t3)
  .loc 1 3 10
  %t4 = const_str @.L1
  .loc 1 3 10
  call @rt_print_str(%t4)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 10
  %t5 = const_str @.L2
  .loc 1 4 10
  call @rt_print_str(%t5)
  .loc 1 4 10
  %t6 = const_str @.L1
  .loc 1 4 10
  call @rt_print_str(%t6)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 4
  ret 0
exit:
  ret 0
}
