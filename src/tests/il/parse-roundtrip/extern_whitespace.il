il 0.3.0

extern     @foo   (   i64 ,   i64   ) -> void

func @main() -> void {
entry:
  call    @foo(   42 ,   13 )
  ret
}
