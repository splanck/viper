il 0.1.2
extern @Viper.Terminal.PrintStr(str) -> void
extern @Viper.Terminal.PrintI64(i64) -> void
extern @Viper.Terminal.PrintF64(f64) -> void
extern @Viper.Strings.Len(str) -> i64
extern @Viper.Strings.Mid(str, i64, i64) -> str
extern @Viper.String.Left(str, i64) -> str
extern @Viper.String.Right(str, i64) -> str
extern @Viper.String.Mid(str, i64) -> str
extern @Viper.String.MidLen(str, i64, i64) -> str
extern @Viper.String.Trim(str) -> str
extern @Viper.String.TrimStart(str) -> str
extern @Viper.String.TrimEnd(str) -> str
extern @Viper.String.ToUpper(str) -> str
extern @Viper.String.ToLower(str) -> str
extern @Viper.String.IndexOf(str, str) -> i64
extern @Viper.String.IndexOfFrom(i64, str, str) -> i64
extern @Viper.String.Chr(i64) -> str
extern @Viper.String.Asc(str) -> i64
extern @Viper.String.Replace(str, str, str) -> str
extern @Viper.String.StartsWith(str, str) -> i1
extern @Viper.String.EndsWith(str, str) -> i1
extern @Viper.String.Has(str, str) -> i1
extern @Viper.String.Count(str, str) -> i64
extern @Viper.String.PadLeft(str, i64, str) -> str
extern @Viper.String.PadRight(str, i64, str) -> str
extern @Viper.String.Split(str, str) -> ptr
extern @Viper.String.Repeat(str, i64) -> str
extern @Viper.String.Flip(str) -> str
extern @Viper.String.Cmp(str, str) -> i64
extern @Viper.String.CmpNoCase(str, str) -> i64
extern @Viper.Strings.Join(str, ptr) -> str
extern @rt_str_empty() -> str
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  %t1 = alloca 8
  br L10
L10:
  .loc 1 1 17
  store i64, %t1, 1
  .loc 1 1 4
  %t2 = scmp_ge 1, 0
  .loc 1 1 4
  cbr %t2, for_head_pos, for_head_neg
exit:
  ret 0
for_head_pos:
  .loc 1 1 4
  %t3 = load i64, %t1
  .loc 1 1 4
  %t4 = scmp_le %t3, 5
  .loc 1 1 4
  cbr %t4, for_body, for_done
for_head_neg:
  .loc 1 1 4
  %t5 = load i64, %t1
  .loc 1 1 4
  %t6 = scmp_ge %t5, 5
  .loc 1 1 4
  cbr %t6, for_body, for_done
for_body:
  .loc 1 2 17
  store i64, %t0, 1
  .loc 1 2 4
  %t7 = scmp_ge 1, 0
  .loc 1 2 4
  cbr %t7, for_head_pos1, for_head_neg1
for_inc:
  .loc 1 2 4
  %t18 = load i64, %t1
  .loc 1 2 4
  %t19 = iadd.ovf %t18, 1
  .loc 1 2 4
  store i64, %t1, %t19
  .loc 1 2 4
  cbr %t2, for_head_pos, for_head_neg
for_done:
  .loc 1 1 4
  br exit
for_head_pos1:
  .loc 1 2 4
  %t8 = load i64, %t0
  .loc 1 2 4
  %t9 = scmp_le %t8, 5
  .loc 1 2 4
  cbr %t9, for_body1, for_done1
for_head_neg1:
  .loc 1 2 4
  %t10 = load i64, %t0
  .loc 1 2 4
  %t11 = scmp_ge %t10, 5
  .loc 1 2 4
  cbr %t11, for_body1, for_done1
for_body1:
  .loc 1 3 10
  %t12 = load i64, %t1
  .loc 1 3 14
  %t13 = load i64, %t0
  .loc 1 3 12
  %t14 = imul.ovf %t12, %t13
  .loc 1 3 12
  call @Viper.Terminal.PrintI64(%t14)
  .loc 1 3 12
  %t15 = const_str @.L0
  .loc 1 3 12
  call @Viper.Terminal.PrintStr(%t15)
  .loc 1 3 4
  br for_inc1
for_inc1:
  .loc 1 3 4
  %t16 = load i64, %t0
  .loc 1 3 4
  %t17 = iadd.ovf %t16, 1
  .loc 1 3 4
  store i64, %t0, %t17
  .loc 1 3 4
  cbr %t7, for_head_pos1, for_head_neg1
for_done1:
  .loc 1 2 4
  br for_inc
}
