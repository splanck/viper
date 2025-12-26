il 0.1
extern @Viper.Console.PrintStr(str) -> void
extern @Viper.Console.PrintI64(i64) -> void

global const str @.L0 = "SUM 1..10"
global const str @.L1 = "DONE"
global const str @.L2 = "\n"

func @main() -> i64 {
entry:
  %i_slot = alloca 8
  %s_slot = alloca 8

  %h = const_str @.L0
  call @Viper.Console.PrintStr(%h)
  %nl0 = const_str @.L2
  call @Viper.Console.PrintStr(%nl0)

  store i64, %i_slot, 1
  store i64, %s_slot, 0
  br label loop_head

loop_head:
  %i0 = load i64, %i_slot
  %c = scmp_le %i0, 10
  cbr %c, label loop_body, label done

loop_body:
  %s0 = load i64, %s_slot
  %s1 = iadd.ovf %s0, %i0
  store i64, %s_slot, %s1

  %i1 = iadd.ovf %i0, 1
  store i64, %i_slot, %i1

  br label loop_head

done:
  %s2 = load i64, %s_slot
  call @Viper.Console.PrintI64(%s2)
  %nl1 = const_str @.L2
  call @Viper.Console.PrintStr(%nl1)

  %d = const_str @.L1
  call @Viper.Console.PrintStr(%d)
  %nl2 = const_str @.L2
  call @Viper.Console.PrintStr(%nl2)

  ret 0
}
