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
  %t1 = alloca 8
  %t2 = alloca 8
  store ptr, %t2, null
  br L10
L10:
  .loc 1 1 4
  %t3 = iadd.ovf 1, 1
  .loc 1 1 4
  %t5 = scmp_lt %t3, 0
  .loc 1 1 4
  cbr %t5, dim_len_fail, dim_len_cont(%t3)
L20:
  .loc 1 2 13
  %t8 = load ptr, %t2
  .loc 1 2 13
  %t9 = call @rt_arr_i64_len(%t8)
  .loc 1 2 13
  %t10 = isub.ovf %t9, 1
  .loc 1 2 4
  store i64, %t1, %t10
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 12
  %t11 = load i64, %t1
  .loc 1 3 15
  %t12 = iadd.ovf %t11, 2
  .loc 1 3 4
  %t13 = iadd.ovf %t12, 1
  .loc 1 3 4
  %t15 = scmp_lt %t13, 0
  .loc 1 3 4
  cbr %t15, redim_len_fail, redim_len_cont(%t13)
L40:
  .loc 1 4 4
  %t19 = load ptr, %t2
  .loc 1 4 10
  %t20 = load i64, %t1
  .loc 1 4 8
  %t21 = call @rt_arr_i64_len(%t19)
  .loc 1 4 8
  %t22 = scmp_lt %t20, 0
  .loc 1 4 8
  %t23 = scmp_ge %t20, %t21
  .loc 1 4 8
  %t24 = zext1 %t22
  .loc 1 4 8
  %t25 = zext1 %t23
  .loc 1 4 8
  %t26 = or %t24, %t25
  .loc 1 4 8
  %t27 = icmp_ne %t26, 0
  .loc 1 4 8
  cbr %t27, bc_oob0, bc_ok0
L50:
  .loc 1 5 12
  %t28 = load ptr, %t2
  .loc 1 5 14
  %t29 = load i64, %t1
  .loc 1 5 12
  %t30 = call @rt_arr_i64_len(%t28)
  .loc 1 5 12
  %t31 = scmp_lt %t29, 0
  .loc 1 5 12
  %t32 = scmp_ge %t29, %t30
  .loc 1 5 12
  %t33 = zext1 %t31
  .loc 1 5 12
  %t34 = zext1 %t32
  .loc 1 5 12
  %t35 = or %t33, %t34
  .loc 1 5 12
  %t36 = icmp_ne %t35, 0
  .loc 1 5 12
  cbr %t36, bc_oob1, bc_ok1
L60:
  .loc 1 6 10
  %t38 = load i64, %t1
  .loc 1 6 10
  call @Viper.Terminal.PrintI64(%t38)
  .loc 1 6 10
  %t39 = const_str @.L0
  .loc 1 6 10
  call @Viper.Terminal.PrintStr(%t39)
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 10
  %t40 = load i64, %t0
  .loc 1 7 10
  call @Viper.Terminal.PrintI64(%t40)
  .loc 1 7 10
  %t41 = const_str @.L0
  .loc 1 7 10
  call @Viper.Terminal.PrintStr(%t41)
  .loc 1 7 4
  br L80
L80:
  .loc 1 8 4
  ret 0
exit:
  %t42 = load ptr, %t2
  call @rt_arr_i64_release(%t42)
  store ptr, %t2, null
  ret 0
dim_len_fail:
  .loc 1 1 4
  trap
dim_len_cont(%len:i64):
  .loc 1 1 10
  %t6 = call @rt_arr_i64_new(%len)
  .loc 1 1 10
  call @rt_arr_i64_retain(%t6)
  .loc 1 1 10
  %t7 = load ptr, %t2
  .loc 1 1 10
  call @rt_arr_i64_release(%t7)
  .loc 1 1 10
  store ptr, %t2, %t6
  .loc 1 1 4
  br L20
redim_len_fail:
  .loc 1 3 4
  trap
redim_len_cont(%len:i64):
  .loc 1 3 15
  %t16 = load ptr, %t2
  .loc 1 3 15
  %t17 = call @rt_arr_i64_resize(%t16, %len)
  .loc 1 3 15
  call @rt_arr_i64_retain(%t17)
  .loc 1 3 15
  %t18 = load ptr, %t2
  .loc 1 3 15
  call @rt_arr_i64_release(%t18)
  .loc 1 3 15
  store ptr, %t2, %t17
  .loc 1 3 4
  br L40
bc_ok0:
  .loc 1 4 8
  call @rt_arr_i64_set(%t19, %t20, 7)
  .loc 1 4 4
  br L50
bc_oob0:
  .loc 1 4 8
  call @rt_arr_oob_panic(%t20, %t21)
  .loc 1 4 8
  trap
bc_ok1:
  .loc 1 5 12
  %t37 = call @rt_arr_i64_get(%t28, %t29)
  .loc 1 5 4
  store i64, %t0, %t37
  .loc 1 5 4
  br L60
bc_oob1:
  .loc 1 5 12
  call @rt_arr_oob_panic(%t29, %t30)
  .loc 1 5 12
  trap
}
