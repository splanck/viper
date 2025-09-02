il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void

global const str @.L0 = "HELLO"
global const str @.L1 = "READY"
global const str @.L2 = "\n"

func @main() -> i32 {
entry:
  %x_slot = alloca 8
  %y_slot = alloca 8

  %t0 = add 2, 3
  store i64, %x_slot, %t0

  %xv = load i64, %x_slot
  %t1 = mul %xv, 2
  store i64, %y_slot, %t1

  %s0 = const_str @.L0
  call @rt_print_str(%s0)
  %nl0 = const_str @.L2
  call @rt_print_str(%nl0)

  %s1 = const_str @.L1
  call @rt_print_str(%s1)
  %nl1 = const_str @.L2
  call @rt_print_str(%nl1)

  %yv0 = load i64, %y_slot
  call @rt_print_i64(%yv0)
  %nl2 = const_str @.L2
  call @rt_print_str(%nl2)

  %yv1 = load i64, %y_slot
  %cond = scmp_gt %yv1, 8
  cbr %cond, label then80, label else60

else60:
  call @rt_print_i64(4)
  %nl3 = const_str @.L2
  call @rt_print_str(%nl3)
  br label after

then80:
  %yv2 = load i64, %y_slot
  call @rt_print_i64(%yv2)
  %nl4 = const_str @.L2
  call @rt_print_str(%nl4)
  br label after

after:
  ret 0
}
