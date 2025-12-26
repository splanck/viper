il 0.1.3
extern @rt_print_str(str) -> void

global const str @.L0 = "continued"

func @main() -> i64 {
entry:
  eh.push ^H
  %i:i32 = idx.chk 5, 0, 3
  eh.pop
  %msg = const_str @.L0
  call @rt_print_str(%msg)
  ret 0

handler ^H(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.next %tok
}
