il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
func @main() -> i64 {
entry:
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
  br exit
L100:
  .loc 1 5 5
  br L110
L110:
  .loc 1 6 5
  br exit
L200:
  .loc 1 7 5
  br L210
L210:
  .loc 1 8 5
  br exit
L300:
  .loc 1 9 5
  br L310
L310:
  .loc 1 10 5
  br exit
L400:
  .loc 1 11 5
  br exit
exit:
  eh.pop
  ret 0
handler ^handler_L100(%err:Error, %tok:ResumeTok):
  eh.entry
  .loc 1 5 5
  resume.same %t1
handler ^handler_L200(%err:Error, %tok:ResumeTok):
  eh.entry
  .loc 1 7 5
  resume.next %t3
handler ^handler_L300(%err:Error, %tok:ResumeTok):
  eh.entry
  .loc 1 9 5
  resume.label %t5, ^L400
}
