il 0.2.0
extern @Viper.String.Asc(str) -> i64
extern @Viper.String.Chr(i64) -> str
extern @Viper.String.IndexOf(str, str) -> i64
extern @Viper.String.IndexOfFrom(str, i64, str) -> i64
extern @Viper.String.ToLower(str) -> str
extern @Viper.String.TrimStart(str) -> str
extern @Viper.String.Left(str, i64) -> str
extern @Viper.String.get_Length(str) -> i64
extern @Viper.String.Mid(str, i64) -> str
extern @Viper.String.MidLen(str, i64, i64) -> str
extern @Viper.String.TrimEnd(str) -> str
extern @Viper.String.Right(str, i64) -> str
extern @Viper.String.Cmp(str, str) -> i64
extern @Viper.String.CmpNoCase(str, str) -> i64
extern @Viper.String.Count(str, str) -> i64
extern @Viper.String.EndsWith(str, str) -> i1
extern @Viper.String.Flip(str) -> str
extern @Viper.String.Has(str, str) -> i1
extern @Viper.String.PadLeft(str, i64, str) -> str
extern @Viper.String.PadRight(str, i64, str) -> str
extern @Viper.String.Repeat(str, i64) -> str
extern @Viper.String.Replace(str, str, str) -> str
extern @Viper.String.Split(str, str) -> ptr
extern @Viper.String.StartsWith(str, str) -> i1
extern @Viper.String.Substring(str, i64, i64) -> str
extern @Viper.String.Trim(str) -> str
extern @Viper.String.ToUpper(str) -> str
extern @Viper.String.Join(str, ptr) -> str
extern @Viper.Terminal.PrintF64(f64) -> void
extern @Viper.Terminal.PrintI64(i64) -> void
extern @Viper.Terminal.PrintStr(str) -> void
extern @Viper.String.Length(str) -> i64
extern @rt_str_empty() -> str
extern @rt_abs_i64(i64) -> i64
extern @rt_abs_f64(f64) -> f64
extern @rt_floor(f64) -> f64
extern @rt_ceil(f64) -> f64
extern @rt_sqrt(f64) -> f64
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 10
  %t0 = call @rt_abs_i64(-5)
  .loc 1 1 10
  call @Viper.Terminal.PrintI64(%t0)
  .loc 1 1 10
  %t1 = const_str @.L0
  .loc 1 1 10
  call @Viper.Terminal.PrintStr(%t1)
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t2 = call @rt_abs_f64(-1.5)
  .loc 1 2 10
  call @Viper.Terminal.PrintF64(%t2)
  .loc 1 2 10
  %t3 = const_str @.L0
  .loc 1 2 10
  call @Viper.Terminal.PrintStr(%t3)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 10
  %t4 = call @rt_floor(1.8999999999999999)
  .loc 1 3 10
  call @Viper.Terminal.PrintF64(%t4)
  .loc 1 3 10
  %t5 = const_str @.L0
  .loc 1 3 10
  call @Viper.Terminal.PrintStr(%t5)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 10
  %t6 = call @rt_ceil(1.1000000000000001)
  .loc 1 4 10
  call @Viper.Terminal.PrintF64(%t6)
  .loc 1 4 10
  %t7 = const_str @.L0
  .loc 1 4 10
  call @Viper.Terminal.PrintStr(%t7)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 10
  %t8 = call @rt_sqrt(9)
  .loc 1 5 10
  call @Viper.Terminal.PrintF64(%t8)
  .loc 1 5 10
  %t9 = const_str @.L0
  .loc 1 5 10
  call @Viper.Terminal.PrintStr(%t9)
  .loc 1 5 4
  br exit
exit:
  ret 0
}
