il 0.1.2
extern @rt_println_i32(i32) -> void

func @main() -> void {
entry:
  eh.push ^H
  %i:i32 = idx.chk 5, 0, 3
  eh.pop
  ret
handler ^H(%err:Error, %tok:ResumeTok):
  eh.entry
  %kind = err.get_kind %err
  call @rt_println_i32(%kind)
  resume.next %tok
}
