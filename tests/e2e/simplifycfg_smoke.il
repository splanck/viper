il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void

global const str @.Lnewline = "\n"

func @main() -> i64 {
entry:
  %always0 = scmp_ge 1, 0
  cbr %always0, label split_true, label split_false

split_true:
  br label split_join

split_false:
  br label split_join

split_join:
  %always1 = scmp_gt 5, 3
  cbr %always1, label nested_true, label nested_false

nested_true:
  br label nested_join

nested_false:
  br label nested_join

nested_join:
  call @rt_print_i64(42)
  %nl = const_str @.Lnewline
  call @rt_print_str(%nl)
  ret 0
}
