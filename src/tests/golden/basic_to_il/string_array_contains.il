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
extern @rt_arr_str_alloc(i64) -> ptr
extern @rt_arr_str_release(ptr, i64) -> void
extern @rt_arr_str_get(ptr, i64) -> str
extern @rt_arr_str_put(ptr, i64, str) -> void
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
  %t3 = scmp_lt %t1, 0
  .loc 1 2 1
  cbr %t3, dim_len_fail, dim_len_cont(%t1)
UL999999999:
  .loc 1 3 13
  %t6 = const_str @.L0
  .loc 1 3 1
  %t7 = load ptr, %t0
  .loc 1 3 1
  %t8 = call @rt_arr_str_len(%t7)
  .loc 1 3 1
  %t9 = scmp_lt 0, 0
  .loc 1 3 1
  %t10 = scmp_ge 0, %t8
  .loc 1 3 1
  %t11 = zext1 %t9
  .loc 1 3 1
  %t12 = zext1 %t10
  .loc 1 3 1
  %t13 = or %t11, %t12
  .loc 1 3 1
  %t14 = icmp_ne %t13, 0
  .loc 1 3 1
  cbr %t14, bc_oob0, bc_ok0
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
  %t26 = load ptr, %t0
  .loc 1 5 7
  %t27 = call @rt_arr_str_len(%t26)
  .loc 1 5 7
  %t28 = scmp_lt 1, 0
  .loc 1 5 7
  %t29 = scmp_ge 1, %t27
  .loc 1 5 7
  %t30 = zext1 %t28
  .loc 1 5 7
  %t31 = zext1 %t29
  .loc 1 5 7
  %t32 = or %t30, %t31
  .loc 1 5 7
  %t33 = icmp_ne %t32, 0
  .loc 1 5 7
  cbr %t33, bc_oob2, bc_ok2
UL999999996:
  .loc 1 6 7
  %t37 = load ptr, %t0
  .loc 1 6 7
  %t38 = call @rt_arr_str_len(%t37)
  .loc 1 6 7
  %t39 = scmp_lt 0, 0
  .loc 1 6 7
  %t40 = scmp_ge 0, %t38
  .loc 1 6 7
  %t41 = zext1 %t39
  .loc 1 6 7
  %t42 = zext1 %t40
  .loc 1 6 7
  %t43 = or %t41, %t42
  .loc 1 6 7
  %t44 = icmp_ne %t43, 0
  .loc 1 6 7
  cbr %t44, bc_oob3, bc_ok3
UL999999995:
  .loc 1 7 1
  ret 0
exit:
  %t48 = load ptr, %t0
  call @rt_arr_str_release(%t48, 0)
  store ptr, %t0, null
  ret 0
dim_len_fail:
  .loc 1 2 1
  trap
dim_len_cont(%len:i64):
  .loc 1 2 12
  %t4 = call @rt_arr_str_alloc(%len)
  .loc 1 2 12
  %t5 = load ptr, %t0
  .loc 1 2 12
  call @rt_arr_str_release(%t5, 0)
  .loc 1 2 12
  store ptr, %t0, %t4
  .loc 1 2 1
  br UL999999999
bc_ok0:
  .loc 1 3 1
  %t15 = load ptr, %t0
  .loc 1 3 8
  call @rt_arr_str_put(%t15, 0, %t6)
  .loc 1 3 1
  br UL999999998
bc_oob0:
  .loc 1 3 1
  call @rt_arr_oob_panic(0, %t8)
  .loc 1 3 1
  trap
bc_ok1:
  .loc 1 4 1
  %t25 = load ptr, %t0
  .loc 1 4 8
  call @rt_arr_str_put(%t25, 1, %t16)
  .loc 1 4 1
  br UL999999997
bc_oob1:
  .loc 1 4 1
  call @rt_arr_oob_panic(1, %t18)
  .loc 1 4 1
  trap
bc_ok2:
  .loc 1 5 7
  %t34 = load ptr, %t0
  .loc 1 5 7
  %t35 = call @rt_arr_str_get(%t34, 1)
  .loc 1 5 7
  call @rt_str_retain_maybe(%t35)
  .loc 1 5 7
  call @Viper.Terminal.PrintStr(%t35)
  .loc 1 5 7
  call @rt_str_release_maybe(%t35)
  .loc 1 5 7
  %t36 = const_str @.L2
  .loc 1 5 7
  call @Viper.Terminal.PrintStr(%t36)
  .loc 1 5 1
  br UL999999996
bc_oob2:
  .loc 1 5 7
  call @rt_arr_oob_panic(1, %t27)
  .loc 1 5 7
  trap
bc_ok3:
  .loc 1 6 7
  %t45 = load ptr, %t0
  .loc 1 6 7
  %t46 = call @rt_arr_str_get(%t45, 0)
  .loc 1 6 7
  call @rt_str_retain_maybe(%t46)
  .loc 1 6 7
  call @Viper.Terminal.PrintStr(%t46)
  .loc 1 6 7
  call @rt_str_release_maybe(%t46)
  .loc 1 6 7
  %t47 = const_str @.L2
  .loc 1 6 7
  call @Viper.Terminal.PrintStr(%t47)
  .loc 1 6 1
  br UL999999995
bc_oob3:
  .loc 1 6 7
  call @rt_arr_oob_panic(0, %t38)
  .loc 1 6 7
  trap
}
