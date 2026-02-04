il 0.2.0
extern @Viper.Terminal.PrintStr(str) -> void
extern @Viper.Terminal.PrintI64(i64) -> void
extern @Viper.Terminal.PrintF64(f64) -> void
extern @Viper.String.get_Length(str) -> i64
extern @Viper.String.Substring(str, i64, i64) -> str
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
extern @Viper.String.Join(str, ptr) -> str
extern @rt_str_empty() -> str
extern @rt_arr_i64_new(i64) -> ptr
extern @rt_arr_i64_resize(ptr, i64) -> ptr
extern @rt_arr_i64_len(ptr) -> i64
extern @rt_arr_i64_get(ptr, i64) -> i64
extern @rt_arr_i64_set(ptr, i64, i64) -> void
extern @rt_arr_i64_retain(ptr) -> void
extern @rt_arr_i64_release(ptr) -> void
extern @rt_arr_oob_panic(i64, i64) -> void
global const str @.L0 = "\n"
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
  br L10
L10:
  .loc 1 1 4
  %t3 = call @rt_arr_i64_new(2)
  .loc 1 1 4
  call @rt_arr_i64_retain(%t3)
  .loc 1 1 4
  %t4 = load ptr, %t0
  .loc 1 1 4
  call @rt_arr_i64_release(%t4)
  .loc 1 1 4
  store ptr, %t0, %t3
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 13
  %t5 = load ptr, %t0
  .loc 1 2 13
  %t6 = call @rt_arr_i64_len(%t5)
  .loc 1 2 13
  %t7 = isub.ovf %t6, 1
  .loc 1 2 4
  store i64, %t1, %t7
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 12
  %t8 = load i64, %t1
  .loc 1 3 15
  %t9 = iadd.ovf %t8, 2
  .loc 1 3 4
  %t10 = iadd.ovf %t9, 1
  .loc 1 3 4
  %t12 = scmp_lt %t10, 0
  .loc 1 3 4
  cbr %t12, redim_len_fail, redim_len_cont(%t10)
L40:
  .loc 1 4 4
  %t16 = load ptr, %t0
  .loc 1 4 10
  %t17 = load i64, %t1
  .loc 1 4 8
  %t18 = call @rt_arr_i64_len(%t16)
  .loc 1 4 8
  %t19 = scmp_lt %t17, 0
  .loc 1 4 8
  %t20 = scmp_ge %t17, %t18
  .loc 1 4 8
  %t21 = zext1 %t19
  .loc 1 4 8
  %t22 = zext1 %t20
  .loc 1 4 8
  %t23 = or %t21, %t22
  .loc 1 4 8
  %t24 = icmp_ne %t23, 0
  .loc 1 4 8
  cbr %t24, bc_oob0, bc_ok0
L50:
  .loc 1 5 12
  %t25 = load ptr, %t0
  .loc 1 5 14
  %t26 = load i64, %t1
  .loc 1 5 12
  %t27 = call @rt_arr_i64_len(%t25)
  .loc 1 5 12
  %t28 = scmp_lt %t26, 0
  .loc 1 5 12
  %t29 = scmp_ge %t26, %t27
  .loc 1 5 12
  %t30 = zext1 %t28
  .loc 1 5 12
  %t31 = zext1 %t29
  .loc 1 5 12
  %t32 = or %t30, %t31
  .loc 1 5 12
  %t33 = icmp_ne %t32, 0
  .loc 1 5 12
  cbr %t33, bc_oob1, bc_ok1
L60:
  .loc 1 6 10
  %t35 = load i64, %t1
  .loc 1 6 10
  call @Viper.Terminal.PrintI64(%t35)
  .loc 1 6 10
  %t36 = const_str @.L0
  .loc 1 6 10
  call @Viper.Terminal.PrintStr(%t36)
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 10
  %t37 = load i64, %t2
  .loc 1 7 10
  call @Viper.Terminal.PrintI64(%t37)
  .loc 1 7 10
  %t38 = const_str @.L0
  .loc 1 7 10
  call @Viper.Terminal.PrintStr(%t38)
  .loc 1 7 4
  br L80
L80:
  .loc 1 8 4
  ret 0
exit:
  %t39 = load ptr, %t0
  call @rt_arr_i64_release(%t39)
  store ptr, %t0, null
  ret 0
redim_len_fail:
  .loc 1 3 4
  trap
redim_len_cont(%len:i64):
  .loc 1 3 15
  %t13 = load ptr, %t0
  .loc 1 3 15
  %t14 = call @rt_arr_i64_resize(%t13, %len)
  .loc 1 3 15
  call @rt_arr_i64_retain(%t14)
  .loc 1 3 15
  %t15 = load ptr, %t0
  .loc 1 3 15
  call @rt_arr_i64_release(%t15)
  .loc 1 3 15
  store ptr, %t0, %t14
  .loc 1 3 4
  br L40
bc_ok0:
  .loc 1 4 8
  call @rt_arr_i64_set(%t16, %t17, 7)
  .loc 1 4 4
  br L50
bc_oob0:
  .loc 1 4 8
  call @rt_arr_oob_panic(%t17, %t18)
  .loc 1 4 8
  trap
bc_ok1:
  .loc 1 5 12
  %t34 = call @rt_arr_i64_get(%t25, %t26)
  .loc 1 5 4
  store i64, %t2, %t34
  .loc 1 5 4
  br L60
bc_oob1:
  .loc 1 5 12
  call @rt_arr_oob_panic(%t26, %t27)
  .loc 1 5 12
  trap
}
