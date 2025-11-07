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
  %t0 = alloca 1
  %t1 = trunc1 0
  store i1, %t0, %t1
  br L10
L10:
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 12
  %t2 = trunc1 1
  .loc 1 2 4
  store i1, %t0, %t2
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 10
  %t3 = load i1, %t0
  .loc 1 3 15
  %t4 = trunc1 0
  .loc 1 3 12
  %t5 = zext1 %t3
  .loc 1 3 12
  %t6 = isub.ovf 0, %t5
  .loc 1 3 12
  %t7 = zext1 %t4
  .loc 1 3 12
  %t8 = isub.ovf 0, %t7
  .loc 1 3 12
  %t9 = or %t6, %t8
  .loc 1 3 4
  call @rt_print_i64(%t9)
  .loc 1 3 4
  %t10 = const_str @.L0
  .loc 1 3 4
  call @rt_print_str(%t10)
  .loc 1 3 4
  br exit
exit:
  ret 0
}
