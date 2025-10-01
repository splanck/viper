il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_open_err_vstr(str, i32, i32) -> i32
extern @rt_close_err(i32) -> i32
global const str @.L0 = "foo.txt"
func @main() -> i64 {
entry:
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
  %t4 = icmp_ne %t3, 0
  .loc 1 1 4
  cbr %t4, open_fail, open_cont
L20:
  .loc 1 2 4
  %t5:i32 = cast.si_narrow.chk 1
  .loc 1 2 4
  %t6 = call @rt_close_err(%t5)
  .loc 1 2 4
  %t7 = icmp_ne %t6, 0
  .loc 1 2 4
  cbr %t7, close_fail, close_cont
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
  trap.from_err i32 %t6
close_cont:
  .loc 1 2 4
  br exit
}
