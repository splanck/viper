il 0.1.3
extern @rt_println_i32(i32) -> void

func @main() -> void {
entry:
  eh.push ^H
  %zero = sitofp 0
  %nan = fdiv %zero, %zero
  %v:i16 = cast.fp_to_si.rte.chk %nan
  eh.pop
  ret
handler ^H(%err:Error, %tok:ResumeTok):
  eh.entry
  %kind = err.get_kind %err
  call @rt_println_i32(%kind)
  resume.next %tok
}
