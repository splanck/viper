il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_input_line() -> str
extern @rt_to_int(str) -> i64

global const str @.L0 = "FACTORIAL"
global const str @.L1 = "ENTER N:"

func @main() -> i32 {
entry:
  %s_slot = alloca 8
  %n_slot = alloca 8
  %r_slot = alloca 8

  %h0 = const_str @.L0
  call @rt_print_str(%h0)
  %h1 = const_str @.L1
  call @rt_print_str(%h1)

  %line = call @rt_input_line()
  store str, %s_slot, %line

  %sval = load str, %s_slot
  %n0 = call @rt_to_int(%sval)
  store i64, %n_slot, %n0

  store i64, %r_slot, 1
  br label loop_head

loop_head:
  %n1 = load i64, %n_slot
  %c = scmp_gt %n1, 1
  cbr %c, label loop_body, label done

loop_body:
  %r0 = load i64, %r_slot
  %r1 = mul %r0, %n1
  store i64, %r_slot, %r1
  %n2 = sub %n1, 1
  store i64, %n_slot, %n2
  br label loop_head

done:
  %r2 = load i64, %r_slot
  call @rt_print_i64(%r2)
  ret 0
}
