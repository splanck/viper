il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  br label L10
L10:
  .loc 1 1 15
  %t1 = sitofp 1
  .loc 1 1 15
  %t2 = fadd %t1, 2.5
  .loc 1 1 4
  store f64, %t0, %t2
  .loc 1 1 4
  br label L20
L20:
  .loc 1 2 12
  %t3 = sitofp 1
  .loc 1 2 12
  %t4 = fcmp_lt %t3, 2.5
  .loc 1 2 4
  %t5 = zext1 %t4
  .loc 1 2 4
  call @rt_print_i64(%t5)
  .loc 1 2 4
  %t6 = const_str @.L0
  .loc 1 2 4
  call @rt_print_str(%t6)
  .loc 1 2 4
  br label L30
L30:
  .loc 1 3 10
  %t7 = load f64, %t0
  .loc 1 3 4
  call @rt_print_f64(%t7)
  .loc 1 3 4
  %t8 = const_str @.L0
  .loc 1 3 4
  call @rt_print_str(%t8)
  .loc 1 3 4
  br label exit
exit:
  ret 0
}
