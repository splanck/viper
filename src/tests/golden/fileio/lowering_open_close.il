il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
extern @rt_open_err_vstr(str, i32, i32) -> i32
extern @rt_close_err(i32) -> i32
global const str @.L0 = "foo.txt"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 9
  %t0 = const_str @.L0
  .loc 1 1 4
  %t1:i32 = cast.si_narrow.chk 1
  .loc 1 1 4
  %t2:i32 = cast.si_narrow.chk 1
  .loc 1 1 4
  %t3 = call @rt_open_err_vstr(%t0, %t2, %t1)
  .loc 1 1 4
  %t4 = alloca 8
  .loc 1 1 4
  store i64, %t4, 0
  .loc 1 1 4
  store i32, %t4, %t3
  .loc 1 1 4
  %t5 = load i64, %t4
  .loc 1 1 4
  %t6 = icmp_ne %t5, 0
  .loc 1 1 4
  cbr %t6, open_fail, open_cont
L20:
  .loc 1 2 4
  %t7:i32 = cast.si_narrow.chk 1
  .loc 1 2 4
  %t8 = call @rt_close_err(%t7)
  .loc 1 2 4
  %t9 = alloca 8
  .loc 1 2 4
  store i64, %t9, 0
  .loc 1 2 4
  store i32, %t9, %t8
  .loc 1 2 4
  %t10 = load i64, %t9
  .loc 1 2 4
  %t11 = icmp_ne %t10, 0
  .loc 1 2 4
  cbr %t11, close_fail, close_cont
exit:
  ret 0
open_fail:
  .loc 1 1 4
  trap.from_err i32 %t3
open_cont:
  .loc 1 1 4
  br L20
close_fail:
  .loc 1 2 4
  trap.from_err i32 %t8
close_cont:
  .loc 1 2 4
  br exit
}
