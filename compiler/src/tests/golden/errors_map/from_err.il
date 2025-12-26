il 0.1.3
extern @rt_println_i32(i32) -> void

func @main() -> void {
entry:
  eh.push ^H
  trap.from_err i32 1
exit:
  ret
handler ^H(%err:Error, %tok:ResumeTok):
  eh.entry
  %k = err.get_kind %err
  call @rt_println_i32(%k)
  resume.label %tok, ^exit
}
