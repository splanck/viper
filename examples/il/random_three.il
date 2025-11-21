il 0.1 ; // File: examples/il/random_three.il // Purpose: Prints three random numbers using the runtime PRNG. // Links: docs/runtime-abi.md#random

extern @rt_randomize_i64(i64) -> void
extern @rt_rnd() -> f64
extern @Viper.Console.PrintF64(f64) -> void
extern @Viper.Console.PrintStr(str) -> void

global const str @.L0 = "\n"

func @main() -> i64 {
entry:
  call @rt_randomize_i64(42)
  %r0 = call @rt_rnd()
  call @Viper.Console.PrintF64(%r0)
  %nl0 = const_str @.L0
  call @Viper.Console.PrintStr(%nl0)
  %r1 = call @rt_rnd()
  call @Viper.Console.PrintF64(%r1)
  %nl1 = const_str @.L0
  call @Viper.Console.PrintStr(%nl1)
  %r2 = call @rt_rnd()
  call @Viper.Console.PrintF64(%r2)
  %nl2 = const_str @.L0
  call @Viper.Console.PrintStr(%nl2)
  ret 0
}
