il 0.1 ; // File: examples/il/random_three.il // Purpose: Prints three random numbers using the runtime PRNG. // Links: docs/runtime-abi.md#random

extern @Zanna.Math.Randomize(i64) -> void
extern @Zanna.Math.Rnd() -> f64
extern @Zanna.Terminal.PrintF64(f64) -> void
extern @Zanna.Terminal.PrintStr(str) -> void

global const str @.L0 = "\n"

func @main() -> i64 {
entry:
  call @Zanna.Math.Randomize(42)
  %r0 = call @Zanna.Math.Rnd()
  call @Zanna.Terminal.PrintF64(%r0)
  %nl0 = const_str @.L0
  call @Zanna.Terminal.PrintStr(%nl0)
  %r1 = call @Zanna.Math.Rnd()
  call @Zanna.Terminal.PrintF64(%r1)
  %nl1 = const_str @.L0
  call @Zanna.Terminal.PrintStr(%nl1)
  %r2 = call @Zanna.Math.Rnd()
  call @Zanna.Terminal.PrintF64(%r2)
  %nl2 = const_str @.L0
  call @Zanna.Terminal.PrintStr(%nl2)
  ret 0
}
