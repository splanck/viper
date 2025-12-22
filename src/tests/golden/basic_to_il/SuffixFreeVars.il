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
extern @Viper.String.IndexOfFrom(str, i64, str) -> i64
extern @Viper.String.Chr(i64) -> str
extern @Viper.String.Asc(str) -> i64
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
  %t4 = iadd.ovf 2, 1
  .loc 1 3 1
  %t6 = scmp_lt %t4, 0
  .loc 1 3 1
  cbr %t6, dim_len_fail, dim_len_cont(%t4)
UL999999997:
  .loc 1 4 15
  %t9 = const_str @.L0
  .loc 1 4 1
  %t10 = load str, %t2
  .loc 1 4 1
  call @rt_str_release_maybe(%t10)
  .loc 1 4 1
  call @rt_str_retain_maybe(%t9)
  .loc 1 4 1
  store str, %t2, %t9
  .loc 1 4 1
  br UL999999996
UL999999996:
  .loc 1 5 1
  store i64, %t1, 5
  .loc 1 5 1
  br UL999999995
UL999999995:
  .loc 1 6 17
  %t11 = load i64, %t1
  .loc 1 6 1
  %t12 = load ptr, %t0
  .loc 1 6 5
  %t13 = call @rt_arr_i64_len(%t12)
  .loc 1 6 5
  %t14 = scmp_lt 0, 0
  .loc 1 6 5
  %t15 = scmp_ge 0, %t13
  .loc 1 6 5
  %t16 = zext1 %t14
  .loc 1 6 5
  %t17 = zext1 %t15
  .loc 1 6 5
  %t18 = or %t16, %t17
  .loc 1 6 5
  %t19 = icmp_ne %t18, 0
  .loc 1 6 5
  cbr %t19, bc_oob0, bc_ok0
UL999999994:
  .loc 1 7 7
  %t20 = load str, %t2
  .loc 1 7 7
  call @rt_str_retain_maybe(%t20)
  .loc 1 7 7
  call @Viper.Terminal.PrintStr(%t20)
  .loc 1 7 7
  call @rt_str_release_maybe(%t20)
  .loc 1 7 7
  %t21 = const_str @.L1
  .loc 1 7 7
  call @Viper.Terminal.PrintStr(%t21)
  .loc 1 7 1
  br UL999999993
UL999999993:
  .loc 1 8 7
  %t22 = load i64, %t1
  .loc 1 8 7
  call @Viper.Terminal.PrintI64(%t22)
  .loc 1 8 7
  %t23 = const_str @.L1
  .loc 1 8 7
  call @Viper.Terminal.PrintStr(%t23)
  .loc 1 8 1
  br UL999999992
UL999999992:
  .loc 1 9 7
  %t24 = load ptr, %t0
  .loc 1 9 7
  %t25 = call @rt_arr_i64_len(%t24)
  .loc 1 9 7
  %t26 = scmp_lt 0, 0
  .loc 1 9 7
  %t27 = scmp_ge 0, %t25
  .loc 1 9 7
  %t28 = zext1 %t26
  .loc 1 9 7
  %t29 = zext1 %t27
  .loc 1 9 7
  %t30 = or %t28, %t29
  .loc 1 9 7
  %t31 = icmp_ne %t30, 0
  .loc 1 9 7
  cbr %t31, bc_oob1, bc_ok1
exit:
  %t34 = load ptr, %t0
  call @rt_arr_i64_release(%t34)
  store ptr, %t0, null
  ret 0
dim_len_fail:
  .loc 1 3 1
  trap
dim_len_cont(%len:i64):
  .loc 1 3 12
  %t7 = call @rt_arr_i64_new(%len)
  .loc 1 3 12
  call @rt_arr_i64_retain(%t7)
  .loc 1 3 12
  %t8 = load ptr, %t0
  .loc 1 3 12
  call @rt_arr_i64_release(%t8)
  .loc 1 3 12
  store ptr, %t0, %t7
  .loc 1 3 1
  br UL999999997
bc_ok0:
  .loc 1 6 5
  call @rt_arr_i64_set(%t12, 0, %t11)
  .loc 1 6 1
  br UL999999994
bc_oob0:
  .loc 1 6 5
  call @rt_arr_oob_panic(0, %t13)
  .loc 1 6 5
  trap
bc_ok1:
  .loc 1 9 7
  %t32 = call @rt_arr_i64_get(%t24, 0)
  .loc 1 9 7
  call @Viper.Terminal.PrintI64(%t32)
  .loc 1 9 7
  %t33 = const_str @.L1
  .loc 1 9 7
  call @Viper.Terminal.PrintStr(%t33)
  .loc 1 9 1
  br exit
bc_oob1:
  .loc 1 9 7
  call @rt_arr_oob_panic(0, %t25)
  .loc 1 9 7
  trap
}
