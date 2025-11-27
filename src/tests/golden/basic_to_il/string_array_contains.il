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
extern @rt_arr_str_alloc(i64) -> ptr
extern @rt_arr_str_release(ptr, i64) -> void
extern @rt_arr_str_get(ptr, i64) -> str
extern @rt_arr_str_put(ptr, i64, ptr) -> void
extern @rt_arr_str_len(ptr) -> i64
extern @rt_arr_oob_panic(i64, i64) -> void
extern @rt_str_retain_maybe(str) -> void
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "Ada"
global const str @.L1 = "Bob"
global const str @.L2 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  store ptr, %t0, null
  br UL1000000000
UL1000000000:
  .loc 1 2 1
  %t1 = iadd.ovf 3, 1
  .loc 1 2 1
  %t2 = scmp_lt %t1, 0
  .loc 1 2 1
  cbr %t2, dim_len_fail, dim_len_cont
UL999999999:
  .loc 1 3 13
  %t5 = const_str @.L0
  .loc 1 3 1
  %t6 = load ptr, %t0
  .loc 1 3 1
  %t7 = call @rt_arr_str_len(%t6)
  .loc 1 3 1
  %t8 = scmp_lt 0, 0
  .loc 1 3 1
  %t9 = scmp_ge 0, %t7
  .loc 1 3 1
  %t10 = zext1 %t8
  .loc 1 3 1
  %t11 = zext1 %t9
  .loc 1 3 1
  %t12 = or %t10, %t11
  .loc 1 3 1
  %t13 = icmp_ne %t12, 0
  .loc 1 3 1
  cbr %t13, bc_oob0, bc_ok0
UL999999998:
  .loc 1 4 13
  %t16 = const_str @.L1
  .loc 1 4 1
  %t17 = load ptr, %t0
  .loc 1 4 1
  %t18 = call @rt_arr_str_len(%t17)
  .loc 1 4 1
  %t19 = scmp_lt 1, 0
  .loc 1 4 1
  %t20 = scmp_ge 1, %t18
  .loc 1 4 1
  %t21 = zext1 %t19
  .loc 1 4 1
  %t22 = zext1 %t20
  .loc 1 4 1
  %t23 = or %t21, %t22
  .loc 1 4 1
  %t24 = icmp_ne %t23, 0
  .loc 1 4 1
  cbr %t24, bc_oob1, bc_ok1
UL999999997:
  .loc 1 5 7
  %t27 = load ptr, %t0
  .loc 1 5 7
  %t28 = call @rt_arr_str_len(%t27)
  .loc 1 5 7
  %t29 = scmp_lt 1, 0
  .loc 1 5 7
  %t30 = scmp_ge 1, %t28
  .loc 1 5 7
  %t31 = zext1 %t29
  .loc 1 5 7
  %t32 = zext1 %t30
  .loc 1 5 7
  %t33 = or %t31, %t32
  .loc 1 5 7
  %t34 = icmp_ne %t33, 0
  .loc 1 5 7
  cbr %t34, bc_oob2, bc_ok2
UL999999996:
  .loc 1 6 7
  %t38 = load ptr, %t0
  .loc 1 6 7
  %t39 = call @rt_arr_str_len(%t38)
  .loc 1 6 7
  %t40 = scmp_lt 0, 0
  .loc 1 6 7
  %t41 = scmp_ge 0, %t39
  .loc 1 6 7
  %t42 = zext1 %t40
  .loc 1 6 7
  %t43 = zext1 %t41
  .loc 1 6 7
  %t44 = or %t42, %t43
  .loc 1 6 7
  %t45 = icmp_ne %t44, 0
  .loc 1 6 7
  cbr %t45, bc_oob3, bc_ok3
UL999999995:
  .loc 1 7 1
  ret 0
exit:
  %t49 = load ptr, %t0
  call @rt_arr_str_release(%t49, 0)
  store ptr, %t0, null
  ret 0
dim_len_fail:
  .loc 1 2 1
  trap
dim_len_cont:
  .loc 1 2 12
  %t3 = call @rt_arr_str_alloc(%t1)
  .loc 1 2 12
  %t4 = load ptr, %t0
  .loc 1 2 12
  call @rt_arr_str_release(%t4, 0)
  .loc 1 2 12
  store ptr, %t0, %t3
  .loc 1 2 1
  br UL999999999
bc_ok0:
  .loc 1 3 1
  %t14 = load ptr, %t0
  .loc 1 3 8
  %t15 = alloca 8
  .loc 1 3 8
  store str, %t15, %t5
  .loc 1 3 8
  call @rt_arr_str_put(%t14, 0, %t15)
  .loc 1 3 1
  br UL999999998
bc_oob0:
  .loc 1 3 1
  call @rt_arr_oob_panic(0, %t7)
  .loc 1 3 1
  trap
bc_ok1:
  .loc 1 4 1
  %t25 = load ptr, %t0
  .loc 1 4 8
  %t26 = alloca 8
  .loc 1 4 8
  store str, %t26, %t16
  .loc 1 4 8
  call @rt_arr_str_put(%t25, 1, %t26)
  .loc 1 4 1
  br UL999999997
bc_oob1:
  .loc 1 4 1
  call @rt_arr_oob_panic(1, %t18)
  .loc 1 4 1
  trap
bc_ok2:
  .loc 1 5 7
  %t35 = load ptr, %t0
  .loc 1 5 7
  %t36 = call @rt_arr_str_get(%t35, 1)
  .loc 1 5 7
  call @rt_str_retain_maybe(%t36)
  .loc 1 5 7
  call @Viper.Console.PrintStr(%t36)
  .loc 1 5 7
  call @rt_str_release_maybe(%t36)
  .loc 1 5 7
  %t37 = const_str @.L2
  .loc 1 5 7
  call @Viper.Console.PrintStr(%t37)
  .loc 1 5 1
  br UL999999996
bc_oob2:
  .loc 1 5 7
  call @rt_arr_oob_panic(1, %t28)
  .loc 1 5 7
  trap
bc_ok3:
  .loc 1 6 7
  %t46 = load ptr, %t0
  .loc 1 6 7
  %t47 = call @rt_arr_str_get(%t46, 0)
  .loc 1 6 7
  call @rt_str_retain_maybe(%t47)
  .loc 1 6 7
  call @Viper.Console.PrintStr(%t47)
  .loc 1 6 7
  call @rt_str_release_maybe(%t47)
  .loc 1 6 7
  %t48 = const_str @.L2
  .loc 1 6 7
  call @Viper.Console.PrintStr(%t48)
  .loc 1 6 1
  br UL999999995
bc_oob3:
  .loc 1 6 7
  call @rt_arr_oob_panic(0, %t39)
  .loc 1 6 7
  trap
}
