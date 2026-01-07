il 0.1
extern @Viper.Console.PrintStr(str) -> void
extern @Viper.Console.PrintI64(i64) -> void

global const str @.L0 = "TABLE 5x5"

func @main() -> i64 {
entry:
  %n_slot = alloca 8
  %i_slot = alloca 8
  %j_slot = alloca 8

  %h = const_str @.L0
  call @Viper.Console.PrintStr(%h)

  store i64, %n_slot, 5
  store i64, %i_slot, 1
  br label outer_head

outer_head:
  %i0 = load i64, %i_slot
  %n0 = load i64, %n_slot
  %oc = scmp_le %i0, %n0
  cbr %oc, label outer_body, label outer_done

outer_body:
  store i64, %j_slot, 1
  br label inner_head

inner_head:
  %j0 = load i64, %j_slot
  %n1 = load i64, %n_slot
  %ic = scmp_le %j0, %n1
  cbr %ic, label inner_body, label inner_done

inner_body:
  %i1 = load i64, %i_slot
  %prod = imul.ovf %i1, %j0
  call @Viper.Console.PrintI64(%prod)
  %j1 = iadd.ovf %j0, 1
  store i64, %j_slot, %j1
  br label inner_head

inner_done:
  %i2 = iadd.ovf %i0, 1
  store i64, %i_slot, %i2
  br label outer_head

outer_done:
  ret 0
}
