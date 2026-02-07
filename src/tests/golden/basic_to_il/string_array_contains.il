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
  %t1 = call @rt_arr_str_alloc(4)
  .loc 1 2 1
  %t2 = load ptr, %t0
  .loc 1 2 1
  call @rt_arr_str_release(%t2, 0)
  .loc 1 2 1
  store ptr, %t0, %t1
  .loc 1 2 1
  br UL999999999
UL999999999:
  .loc 1 3 13
  %t3 = const_str @.L0
  .loc 1 3 1
  %t4 = load ptr, %t0
  .loc 1 3 1
  %t5 = call @rt_arr_str_len(%t4)
  .loc 1 3 1
  %t6 = scmp_lt 0, 0
  .loc 1 3 1
  %t7 = scmp_ge 0, %t5
  .loc 1 3 1
  %t8 = zext1 %t6
  .loc 1 3 1
  %t9 = zext1 %t7
  .loc 1 3 1
  %t10 = or %t8, %t9
  .loc 1 3 1
  %t11 = icmp_ne %t10, 0
  .loc 1 3 1
  cbr %t11, bc_oob0, bc_ok0
UL999999998:
  .loc 1 4 13
  %t13 = const_str @.L1
  .loc 1 4 1
  %t14 = load ptr, %t0
  .loc 1 4 1
  %t15 = call @rt_arr_str_len(%t14)
  .loc 1 4 1
  %t16 = scmp_lt 1, 0
  .loc 1 4 1
  %t17 = scmp_ge 1, %t15
  .loc 1 4 1
  %t18 = zext1 %t16
  .loc 1 4 1
  %t19 = zext1 %t17
  .loc 1 4 1
  %t20 = or %t18, %t19
  .loc 1 4 1
  %t21 = icmp_ne %t20, 0
  .loc 1 4 1
  cbr %t21, bc_oob1, bc_ok1
UL999999997:
  .loc 1 5 7
  %t23 = load ptr, %t0
  .loc 1 5 7
  %t24 = call @rt_arr_str_len(%t23)
  .loc 1 5 7
  %t25 = scmp_lt 1, 0
  .loc 1 5 7
  %t26 = scmp_ge 1, %t24
  .loc 1 5 7
  %t27 = zext1 %t25
  .loc 1 5 7
  %t28 = zext1 %t26
  .loc 1 5 7
  %t29 = or %t27, %t28
  .loc 1 5 7
  %t30 = icmp_ne %t29, 0
  .loc 1 5 7
  cbr %t30, bc_oob2, bc_ok2
UL999999996:
  .loc 1 6 7
  %t34 = load ptr, %t0
  .loc 1 6 7
  %t35 = call @rt_arr_str_len(%t34)
  .loc 1 6 7
  %t36 = scmp_lt 0, 0
  .loc 1 6 7
  %t37 = scmp_ge 0, %t35
  .loc 1 6 7
  %t38 = zext1 %t36
  .loc 1 6 7
  %t39 = zext1 %t37
  .loc 1 6 7
  %t40 = or %t38, %t39
  .loc 1 6 7
  %t41 = icmp_ne %t40, 0
  .loc 1 6 7
  cbr %t41, bc_oob3, bc_ok3
UL999999995:
  .loc 1 7 1
  ret 0
exit:
  %t45 = load ptr, %t0
  call @rt_arr_str_release(%t45, 0)
  store ptr, %t0, null
  ret 0
bc_ok0:
  .loc 1 3 1
  %t12 = load ptr, %t0
  .loc 1 3 8
  call @rt_arr_str_put(%t12, 0, %t3)
  .loc 1 3 1
  br UL999999998
bc_oob0:
  .loc 1 3 1
  call @rt_arr_oob_panic(0, %t5)
  .loc 1 3 1
  trap
bc_ok1:
  .loc 1 4 1
  %t22 = load ptr, %t0
  .loc 1 4 8
  call @rt_arr_str_put(%t22, 1, %t13)
  .loc 1 4 1
  br UL999999997
bc_oob1:
  .loc 1 4 1
  call @rt_arr_oob_panic(1, %t15)
  .loc 1 4 1
  trap
bc_ok2:
  .loc 1 5 7
  %t31 = load ptr, %t0
  .loc 1 5 7
  %t32 = call @rt_arr_str_get(%t31, 1)
  .loc 1 5 7
  call @rt_str_retain_maybe(%t32)
  .loc 1 5 7
  call @Viper.Terminal.PrintStr(%t32)
  .loc 1 5 7
  call @rt_str_release_maybe(%t32)
  .loc 1 5 7
  %t33 = const_str @.L2
  .loc 1 5 7
  call @Viper.Terminal.PrintStr(%t33)
  .loc 1 5 1
  br UL999999996
bc_oob2:
  .loc 1 5 7
  call @rt_arr_oob_panic(1, %t24)
  .loc 1 5 7
  trap
bc_ok3:
  .loc 1 6 7
  %t42 = load ptr, %t0
  .loc 1 6 7
  %t43 = call @rt_arr_str_get(%t42, 0)
  .loc 1 6 7
  call @rt_str_retain_maybe(%t43)
  .loc 1 6 7
  call @Viper.Terminal.PrintStr(%t43)
  .loc 1 6 7
  call @rt_str_release_maybe(%t43)
  .loc 1 6 7
  %t44 = const_str @.L2
  .loc 1 6 7
  call @Viper.Terminal.PrintStr(%t44)
  .loc 1 6 1
  br UL999999995
bc_oob3:
  .loc 1 6 7
  call @rt_arr_oob_panic(0, %t35)
  .loc 1 6 7
  trap
}
