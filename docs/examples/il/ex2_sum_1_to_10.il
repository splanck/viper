il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void

global const str @.L0 = "SUM 1..10"
global const str @.L1 = "DONE"

func @main() -> i32 {
entry:
  %i_slot = alloca 8
  %s_slot = alloca 8

  %h = const_str @.L0
  call @rt_print_str(%h)

  store i64, %i_slot, 1
  store i64, %s_slot, 0
  br label loop_head

loop_head:
  %i0 = load i64, %i_slot
  %c = scmp_le %i0, 10
  cbr %c, label loop_body, label done

loop_body:
  %s0 = load i64, %s_slot
  %s1 = add %s0, %i0
  store i64, %s_slot, %s1

  %i1 = add %i0, 1
  store i64, %i_slot, %i1

  br label loop_head

done:
  %s2 = load i64, %s_slot
  call @rt_print_i64(%s2)

  %d = const_str @.L1
  call @rt_print_str(%d)

  ret 0
}
