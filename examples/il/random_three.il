il 0.1 ; // File: examples/il/random_three.il // Purpose: Prints three random numbers using the runtime PRNG. // Links: docs/runtime-abi.md#random

extern @Viper.Math.Randomize(i64) -> void
extern @Viper.Math.Rnd() -> f64
extern @Viper.Terminal.PrintF64(f64) -> void
extern @Viper.Terminal.PrintStr(str) -> void

global const str @.L0 = "\n"

func @main() -> i64 {
entry:
  call @Viper.Math.Randomize(42)
  %r0 = call @Viper.Math.Rnd()
  call @Viper.Terminal.PrintF64(%r0)
  %nl0 = const_str @.L0
  call @Viper.Terminal.PrintStr(%nl0)
  %r1 = call @Viper.Math.Rnd()
  call @Viper.Terminal.PrintF64(%r1)
  %nl1 = const_str @.L0
  call @Viper.Terminal.PrintStr(%nl1)
  %r2 = call @Viper.Math.Rnd()
  call @Viper.Terminal.PrintF64(%r2)
  %nl2 = const_str @.L0
  call @Viper.Terminal.PrintStr(%nl2)
  ret 0
}
