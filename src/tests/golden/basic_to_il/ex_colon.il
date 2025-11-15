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
  %t1 = alloca 8
  %t2 = alloca 512
  store i64, %t1, 0
  br L10
L10:
  .loc 1 1 4
  store i64, %t0, 1
  .loc 1 1 22
  %t3 = load i64, %t0
  .loc 1 1 22
  call @rt_print_i64(%t3)
  .loc 1 1 22
  %t4 = const_str @.L0
  .loc 1 1 22
  call @rt_print_str(%t4)
  .loc 1 1 34
  %t5 = load i64, %t0
  .loc 1 1 36
  %t6 = iadd.ovf %t5, 1
  .loc 1 1 26
  store i64, %t0, %t6
  .loc 1 1 48
  %t7 = load i64, %t0
  .loc 1 1 48
  call @rt_print_i64(%t7)
  .loc 1 1 48
  %t8 = const_str @.L0
  .loc 1 1 48
  call @rt_print_str(%t8)
  .loc 1 1 4
  br exit
exit:
  ret 0
}
