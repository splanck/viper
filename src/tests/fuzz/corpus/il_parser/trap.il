module @trap
func @main() -> i64 {
entry:
  trap "boom"
}
