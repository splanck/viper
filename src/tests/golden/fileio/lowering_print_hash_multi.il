il 0.1.3
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
extern @Viper.Strings.FromI16(i16) -> str
extern @rt_str_empty() -> str
extern @rt_write_ch_err(i32, str) -> i32
extern @rt_println_ch_err(i32, str) -> i32
global const str @.L0 = "A"
global const str @.L1 = "B"
global const str @.L2 = "C"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 4
  %t0:i32 = cast.si_narrow.chk 2
  .loc 1 1 14
  %t1 = const_str @.L0
  .loc 1 1 14
  %t2 = call @rt_write_ch_err(%t0, %t1)
  .loc 1 1 14
  %t3 = alloca 8
  .loc 1 1 14
  store i64, %t3, 0
  .loc 1 1 14
  store i32, %t3, %t2
  .loc 1 1 14
  %t4 = load i64, %t3
  .loc 1 1 14
  %t5 = icmp_ne %t4, 0
  .loc 1 1 14
  cbr %t5, printch_fail, printch_cont
L20:
  .loc 1 2 4
  %t12:i32 = cast.si_narrow.chk 2
  .loc 1 2 14
  %t13 = const_str @.L1
  .loc 1 2 14
  %t14 = call @rt_write_ch_err(%t12, %t13)
  .loc 1 2 14
  %t15 = alloca 8
  .loc 1 2 14
  store i64, %t15, 0
  .loc 1 2 14
  store i32, %t15, %t14
  .loc 1 2 14
  %t16 = load i64, %t15
  .loc 1 2 14
  %t17 = icmp_ne %t16, 0
  .loc 1 2 14
  cbr %t17, printch_fail2, printch_cont2
L30:
  .loc 1 3 4
  %t24:i32 = cast.si_narrow.chk 2
  .loc 1 3 14
  %t25 = const_str @.L2
  .loc 1 3 14
  %t26 = call @rt_println_ch_err(%t24, %t25)
  .loc 1 3 14
  %t27 = alloca 8
  .loc 1 3 14
  store i64, %t27, 0
  .loc 1 3 14
  store i32, %t27, %t26
  .loc 1 3 14
  %t28 = load i64, %t27
  .loc 1 3 14
  %t29 = icmp_ne %t28, 0
  .loc 1 3 14
  cbr %t29, printch_fail4, printch_cont4
L40:
  .loc 1 4 4
  ret 0
exit:
  ret 0
printch_fail:
  .loc 1 1 14
  trap.from_err i32 %t2
printch_cont:
  .loc 1 1 19
  %t6:i16 = cast.si_narrow.chk 42
  .loc 1 1 19
  %t7 = call @Viper.Strings.FromI16(%t6)
  .loc 1 1 19
  %t8 = call @rt_println_ch_err(%t0, %t7)
  .loc 1 1 19
  %t9 = alloca 8
  .loc 1 1 19
  store i64, %t9, 0
  .loc 1 1 19
  store i32, %t9, %t8
  .loc 1 1 19
  %t10 = load i64, %t9
  .loc 1 1 19
  %t11 = icmp_ne %t10, 0
  .loc 1 1 19
  cbr %t11, printch_fail1, printch_cont1
printch_fail1:
  .loc 1 1 19
  trap.from_err i32 %t8
printch_cont1:
  .loc 1 1 4
  br L20
printch_fail2:
  .loc 1 2 14
  trap.from_err i32 %t14
printch_cont2:
  .loc 1 2 19
  %t18:i16 = cast.si_narrow.chk 7
  .loc 1 2 19
  %t19 = call @Viper.Strings.FromI16(%t18)
  .loc 1 2 19
  %t20 = call @rt_println_ch_err(%t12, %t19)
  .loc 1 2 19
  %t21 = alloca 8
  .loc 1 2 19
  store i64, %t21, 0
  .loc 1 2 19
  store i32, %t21, %t20
  .loc 1 2 19
  %t22 = load i64, %t21
  .loc 1 2 19
  %t23 = icmp_ne %t22, 0
  .loc 1 2 19
  cbr %t23, printch_fail3, printch_cont3
printch_fail3:
  .loc 1 2 19
  trap.from_err i32 %t20
printch_cont3:
  .loc 1 2 4
  br L30
printch_fail4:
  .loc 1 3 14
  trap.from_err i32 %t26
printch_cont4:
  .loc 1 3 4
  br L40
}
