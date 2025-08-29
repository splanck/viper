il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void

global const str @.L0 = "HELLO"
global const str @.L1 = "READY"

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

  %s1 = const_str @.L1
  call @rt_print_str(%s1)

  %yv0 = load i64, %y_slot
  call @rt_print_i64(%yv0)

  %yv1 = load i64, %y_slot
  %cond = scmp_gt %yv1, 8
  cbr %cond, label then80, label else60

else60:
  call @rt_print_i64(4)
  br label after

then80:
  %yv2 = load i64, %y_slot
  call @rt_print_i64(%yv2)
  br label after

after:
  ret 0
}
