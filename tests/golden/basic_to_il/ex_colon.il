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
  %t0 = alloca 8
  br L10
L10:
  .loc 1 1 4
  store i64, %t0, 1
  .loc 1 1 22
  %t1 = load i64, %t0
  .loc 1 1 16
  call @rt_print_i64(%t1)
  .loc 1 1 16
  %t2 = const_str @.L0
  .loc 1 1 16
  call @rt_print_str(%t2)
  .loc 1 1 34
  %t3 = load i64, %t0
  .loc 1 1 36
  %t4 = iadd.ovf %t3, 1
  .loc 1 1 26
  store i64, %t0, %t4
  .loc 1 1 48
  %t5 = load i64, %t0
  .loc 1 1 42
  call @rt_print_i64(%t5)
  .loc 1 1 42
  %t6 = const_str @.L0
  .loc 1 1 42
  call @rt_print_str(%t6)
  .loc 1 1 4
  br exit
exit:
  ret 0
}
