il 0.2.0
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
extern @Viper.String.IndexOfFrom(str, i64, str) -> i64
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
extern @rt_arr_i64_new(i64) -> ptr
extern @rt_arr_i64_len(ptr) -> i64
extern @rt_arr_i64_get(ptr, i64) -> i64
extern @rt_arr_i64_set(ptr, i64, i64) -> void
extern @rt_arr_i64_retain(ptr) -> void
extern @rt_arr_i64_release(ptr) -> void
extern @rt_arr_oob_panic(i64, i64) -> void
extern @rt_str_retain_maybe(str) -> void
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "ok"
global const str @.L1 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  store ptr, %t0, null
  %t1 = alloca 8
  %t2 = alloca 8
  %t3 = call @rt_str_empty()
  store str, %t2, %t3
  br UL1000000000
UL1000000000:
  .loc 1 1 1
  br UL999999999
UL999999999:
  .loc 1 2 1
  br UL999999998
UL999999998:
  .loc 1 3 1
  %t4 = call @rt_arr_i64_new(3)
  .loc 1 3 1
  call @rt_arr_i64_retain(%t4)
  .loc 1 3 1
  %t5 = load ptr, %t0
  .loc 1 3 1
  call @rt_arr_i64_release(%t5)
  .loc 1 3 1
  store ptr, %t0, %t4
  .loc 1 3 1
  br UL999999997
UL999999997:
  .loc 1 4 15
  %t6 = const_str @.L0
  .loc 1 4 1
  %t7 = load str, %t2
  .loc 1 4 1
  call @rt_str_release_maybe(%t7)
  .loc 1 4 1
  call @rt_str_retain_maybe(%t6)
  .loc 1 4 1
  store str, %t2, %t6
  .loc 1 4 1
  br UL999999996
UL999999996:
  .loc 1 5 1
  store i64, %t1, 5
  .loc 1 5 1
  br UL999999995
UL999999995:
  .loc 1 6 17
  %t8 = load i64, %t1
  .loc 1 6 1
  %t9 = load ptr, %t0
  .loc 1 6 5
  %t10 = call @rt_arr_i64_len(%t9)
  .loc 1 6 5
  %t11 = scmp_lt 0, 0
  .loc 1 6 5
  %t12 = scmp_ge 0, %t10
  .loc 1 6 5
  %t13 = zext1 %t11
  .loc 1 6 5
  %t14 = zext1 %t12
  .loc 1 6 5
  %t15 = or %t13, %t14
  .loc 1 6 5
  %t16 = icmp_ne %t15, 0
  .loc 1 6 5
  cbr %t16, bc_oob0, bc_ok0
UL999999994:
  .loc 1 7 7
  %t17 = load str, %t2
  .loc 1 7 7
  call @rt_str_retain_maybe(%t17)
  .loc 1 7 7
  call @Viper.Terminal.PrintStr(%t17)
  .loc 1 7 7
  call @rt_str_release_maybe(%t17)
  .loc 1 7 7
  %t18 = const_str @.L1
  .loc 1 7 7
  call @Viper.Terminal.PrintStr(%t18)
  .loc 1 7 1
  br UL999999993
UL999999993:
  .loc 1 8 7
  %t19 = load i64, %t1
  .loc 1 8 7
  call @Viper.Terminal.PrintI64(%t19)
  .loc 1 8 7
  %t20 = const_str @.L1
  .loc 1 8 7
  call @Viper.Terminal.PrintStr(%t20)
  .loc 1 8 1
  br UL999999992
UL999999992:
  .loc 1 9 7
  %t21 = load ptr, %t0
  .loc 1 9 7
  %t22 = call @rt_arr_i64_len(%t21)
  .loc 1 9 7
  %t23 = scmp_lt 0, 0
  .loc 1 9 7
  %t24 = scmp_ge 0, %t22
  .loc 1 9 7
  %t25 = zext1 %t23
  .loc 1 9 7
  %t26 = zext1 %t24
  .loc 1 9 7
  %t27 = or %t25, %t26
  .loc 1 9 7
  %t28 = icmp_ne %t27, 0
  .loc 1 9 7
  cbr %t28, bc_oob1, bc_ok1
exit:
  %t31 = load ptr, %t0
  call @rt_arr_i64_release(%t31)
  store ptr, %t0, null
  ret 0
bc_ok0:
  .loc 1 6 5
  call @rt_arr_i64_set(%t9, 0, %t8)
  .loc 1 6 1
  br UL999999994
bc_oob0:
  .loc 1 6 5
  call @rt_arr_oob_panic(0, %t10)
  .loc 1 6 5
  trap
bc_ok1:
  .loc 1 9 7
  %t29 = call @rt_arr_i64_get(%t21, 0)
  .loc 1 9 7
  call @Viper.Terminal.PrintI64(%t29)
  .loc 1 9 7
  %t30 = const_str @.L1
  .loc 1 9 7
  call @Viper.Terminal.PrintStr(%t30)
  .loc 1 9 1
  br exit
bc_oob1:
  .loc 1 9 7
  call @rt_arr_oob_panic(0, %t22)
  .loc 1 9 7
  trap
}
