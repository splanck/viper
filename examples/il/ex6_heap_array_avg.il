il 0.1
extern @Viper.Memory.Alloc(i64) -> ptr
extern @Viper.Console.PrintF64(f64) -> void
extern @Viper.Console.PrintStr(str) -> void

global const str @.L0 = "DONE"

func @main() -> i64 {
entry:
  %n_slot = alloca 8
  %i_slot = alloca 8
  %sum_slot = alloca 8
  %a_slot = alloca 8

  store i64, %n_slot, 5
  store i64, %i_slot, 0
  store i64, %sum_slot, 0

  %n0 = load i64, %n_slot
  %bytes = imul.ovf %n0, 8
  %abase = call @Viper.Memory.Alloc(%bytes)
  store ptr, %a_slot, %abase
  br label loop_head

loop_head:
  %i0 = load i64, %i_slot
  %n1 = load i64, %n_slot
  %c = scmp_lt %i0, %n1
  cbr %c, label loop_body, label done

loop_body:
  %a0 = load ptr, %a_slot
  %off = shl %i0, 3
  %elem_ptr = gep %a0, %off

  %sq = imul.ovf %i0, %i0
  store i64, %elem_ptr, %sq

  %val = load i64, %elem_ptr
  %sum0 = load i64, %sum_slot
  %sum1 = iadd.ovf %sum0, %val
  store i64, %sum_slot, %sum1

  %i1 = iadd.ovf %i0, 1
  store i64, %i_slot, %i1
  br label loop_head

done:
  %sum2 = load i64, %sum_slot
  %n2 = load i64, %n_slot
  %fsum = sitofp %sum2
  %fn = sitofp %n2
  %avg = fdiv %fsum, %fn
  call @Viper.Console.PrintF64(%avg)

  %d = const_str @.L0
  call @Viper.Console.PrintStr(%d)
  ret 0
}
