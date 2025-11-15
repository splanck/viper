il 0.1.2

func @trap_newline() -> void {
entry:
  %msg = const_str "\n"
  %err = trap.err 42, %msg
  trap
}
