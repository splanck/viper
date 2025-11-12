il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 4
  eh.push ^handler_L100
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  eh.pop
  .loc 1 2 4
  eh.push ^handler_L200
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  eh.pop
  .loc 1 3 4
  eh.push ^handler_L300
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  eh.pop
  .loc 1 4 4
  ret 0
L100:
  .loc 1 5 5
  trap
L110:
  .loc 1 6 5
  eh.pop
  .loc 1 6 5
  ret 0
L200:
  .loc 1 7 5
  trap
L210:
  .loc 1 8 5
  eh.pop
  .loc 1 8 5
  ret 0
L300:
  .loc 1 9 5
  trap
L310:
  .loc 1 10 5
  eh.pop
  .loc 1 10 5
  ret 0
L400:
  .loc 1 11 5
  eh.pop
  .loc 1 11 5
  ret 0
exit:
  eh.pop
  ret 0
handler ^handler_L100(%err:Error, %tok:ResumeTok):
  eh.entry
handler ^handler_L200(%err:Error, %tok:ResumeTok):
  eh.entry
handler ^handler_L300(%err:Error, %tok:ResumeTok):
  eh.entry
  .loc 1 5 5
  resume.same %t5
}
