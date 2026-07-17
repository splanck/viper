il 0.1
extern @Zanna.Terminal.PrintStr(str) -> void
extern @Zanna.Terminal.PrintI64(i64) -> void
extern @Zanna.Terminal.ReadLine() -> str
extern @Zanna.Core.Convert.ToInt64(str) -> i64

global const str @.L0 = "FACTORIAL"
global const str @.L1 = "ENTER N:"

func @main() -> i64 {
entry:
  %s_slot = alloca 8
  %n_slot = alloca 8
  %r_slot = alloca 8

  %h0 = const_str @.L0
  call @Zanna.Terminal.PrintStr(%h0)
  %h1 = const_str @.L1
  call @Zanna.Terminal.PrintStr(%h1)

  %line = call @Zanna.Terminal.ReadLine()
  store str, %s_slot, %line

  %sval = load str, %s_slot
  %n0 = call @Zanna.Core.Convert.ToInt64(%sval)
  store i64, %n_slot, %n0

  store i64, %r_slot, 1
  br label loop_head

loop_head:
  %n1 = load i64, %n_slot
  %c = scmp_gt %n1, 1
  cbr %c, label loop_body, label done

loop_body:
  %r0 = load i64, %r_slot
  %r1 = imul.ovf %r0, %n1
  store i64, %r_slot, %r1
  %n2 = isub.ovf %n1, 1
  store i64, %n_slot, %n2
  br label loop_head

done:
  %r2 = load i64, %r_slot
  call @Zanna.Terminal.PrintI64(%r2)
  ret 0
}
