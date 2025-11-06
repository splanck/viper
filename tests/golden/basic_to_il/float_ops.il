il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
global const str @.L0 = "\n"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  br L10
L10:
  .loc 1 1 4
  store f64, %t0, 3.5
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  call @rt_print_i64(-1)
  .loc 1 2 4
  %t1 = const_str @.L0
  .loc 1 2 4
  call @rt_print_str(%t1)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 10
  %t2 = load f64, %t0
  .loc 1 3 4
  call @rt_print_f64(%t2)
  .loc 1 3 4
  %t3 = const_str @.L0
  .loc 1 3 4
  call @rt_print_str(%t3)
  .loc 1 3 4
  br exit
exit:
  ret 0
}
