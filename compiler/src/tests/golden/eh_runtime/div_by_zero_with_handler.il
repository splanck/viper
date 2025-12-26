il 0.1.3
extern @rt_print_str(str) -> void

global const str @.L0 = "ok"

func @main() -> i64 {
entry:
  eh.push ^H
  br ^div

div:
  %t0:i64 = sdiv.chk0 42, 0
  eh.pop
  ret 1

recover:
  eh.pop
  %msg = const_str @.L0
  call @rt_print_str(%msg)
  ret 0

handler ^H(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.label %tok, ^recover
}
