il 0.1.2
extern @Viper.Console.PrintStr(str) -> void
extern @Viper.Console.PrintI64(i64) -> void
extern @Viper.Console.PrintF64(f64) -> void
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
extern @rt_str_empty() -> str
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_len(ptr) -> i64
extern @rt_arr_i32_get(ptr, i64) -> i64
extern @rt_arr_i32_set(ptr, i64, i64) -> void
extern @rt_arr_i32_retain(ptr) -> void
extern @rt_arr_i32_release(ptr) -> void
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
  %t5 = scmp_lt %t4, 0
  .loc 1 3 1
  cbr %t5, dim_len_fail, dim_len_cont
UL999999997:
  .loc 1 4 15
  %t8 = const_str @.L0
  .loc 1 4 1
  %t9 = load str, %t2
  .loc 1 4 1
  call @rt_str_release_maybe(%t9)
  .loc 1 4 1
  call @rt_str_retain_maybe(%t8)
  .loc 1 4 1
  store str, %t2, %t8
  .loc 1 4 1
  br UL999999996
UL999999996:
  .loc 1 5 1
  store i64, %t1, 5
  .loc 1 5 1
  br UL999999995
UL999999995:
  .loc 1 6 17
  %t10 = load i64, %t1
  .loc 1 6 1
  %t11 = load ptr, %t0
  .loc 1 6 5
  %t12 = call @rt_arr_i32_len(%t11)
  .loc 1 6 5
  %t13 = scmp_lt 0, 0
  .loc 1 6 5
  %t14 = scmp_ge 0, %t12
  .loc 1 6 5
  %t15 = zext1 %t13
  .loc 1 6 5
  %t16 = zext1 %t14
  .loc 1 6 5
  %t17 = or %t15, %t16
  .loc 1 6 5
  %t18 = icmp_ne %t17, 0
  .loc 1 6 5
  cbr %t18, bc_oob0, bc_ok0
UL999999994:
  .loc 1 7 7
  %t19 = load str, %t2
  .loc 1 7 7
  call @rt_str_retain_maybe(%t19)
  .loc 1 7 7
  call @Viper.Console.PrintStr(%t19)
  .loc 1 7 7
  call @rt_str_release_maybe(%t19)
  .loc 1 7 7
  %t20 = const_str @.L1
  .loc 1 7 7
  call @Viper.Console.PrintStr(%t20)
  .loc 1 7 1
  br UL999999993
UL999999993:
  .loc 1 8 7
  %t21 = load i64, %t1
  .loc 1 8 7
  call @Viper.Console.PrintI64(%t21)
  .loc 1 8 7
  %t22 = const_str @.L1
  .loc 1 8 7
  call @Viper.Console.PrintStr(%t22)
  .loc 1 8 1
  br UL999999992
UL999999992:
  .loc 1 9 7
  %t23 = load ptr, %t0
  .loc 1 9 7
  %t24 = call @rt_arr_i32_len(%t23)
  .loc 1 9 7
  %t25 = scmp_lt 0, 0
  .loc 1 9 7
  %t26 = scmp_ge 0, %t24
  .loc 1 9 7
  %t27 = zext1 %t25
  .loc 1 9 7
  %t28 = zext1 %t26
  .loc 1 9 7
  %t29 = or %t27, %t28
  .loc 1 9 7
  %t30 = icmp_ne %t29, 0
  .loc 1 9 7
  cbr %t30, bc_oob1, bc_ok1
exit:
  %t33 = load ptr, %t0
  call @rt_arr_i32_release(%t33)
  store ptr, %t0, null
  ret 0
dim_len_fail:
  .loc 1 3 1
  trap
dim_len_cont:
  .loc 1 3 12
  %t6 = call @rt_arr_i32_new(%t4)
  .loc 1 3 12
  call @rt_arr_i32_retain(%t6)
  .loc 1 3 12
  %t7 = load ptr, %t0
  .loc 1 3 12
  call @rt_arr_i32_release(%t7)
  .loc 1 3 12
  store ptr, %t0, %t6
  .loc 1 3 1
  br UL999999997
bc_ok0:
  .loc 1 6 5
  call @rt_arr_i32_set(%t11, 0, %t10)
  .loc 1 6 1
  br UL999999994
bc_oob0:
  .loc 1 6 5
  call @rt_arr_oob_panic(0, %t12)
  .loc 1 6 5
  trap
bc_ok1:
  .loc 1 9 7
  %t31 = call @rt_arr_i32_get(%t23, 0)
  .loc 1 9 7
  call @Viper.Console.PrintI64(%t31)
  .loc 1 9 7
  %t32 = const_str @.L1
  .loc 1 9 7
  call @Viper.Console.PrintStr(%t32)
  .loc 1 9 1
  br exit
bc_oob1:
  .loc 1 9 7
  call @rt_arr_oob_panic(0, %t24)
  .loc 1 9 7
  trap
}
