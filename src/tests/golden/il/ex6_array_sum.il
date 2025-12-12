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
extern @Viper.Convert.ToInt(str) -> i64
extern @rt_str_empty() -> str
extern @Viper.Terminal.ReadLine() -> str
extern @rt_arr_i64_new(i64) -> ptr
extern @rt_arr_i64_len(ptr) -> i64
extern @rt_arr_i64_get(ptr, i64) -> i64
extern @rt_arr_i64_set(ptr, i64, i64) -> void
extern @rt_arr_i64_retain(ptr) -> void
extern @rt_arr_i64_release(ptr) -> void
extern @rt_arr_oob_panic(i64, i64) -> void
extern @rt_str_release_maybe(str) -> void
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
  %t3 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t4 = call @Viper.Terminal.ReadLine()
  .loc 1 1 4
  %t5 = call @Viper.Convert.ToInt(%t4)
  .loc 1 1 4
  store i64, %t3, %t5
  .loc 1 1 4
  call @rt_str_release_maybe(%t4)
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t6 = load i64, %t3
  .loc 1 2 4
  %t7 = iadd.ovf %t6, 1
  .loc 1 2 4
  %t9 = scmp_lt %t7, 0
  .loc 1 2 4
  cbr %t9, dim_len_fail, dim_len_cont(%t7)
L30:
  .loc 1 3 4
  store i64, %t1, 0
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  store i64, %t0, 0
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 4
  br loop_head
L100:
  .loc 1 10 11
  %t44 = load i64, %t0
  .loc 1 10 11
  call @Viper.Terminal.PrintI64(%t44)
  .loc 1 10 11
  %t45 = const_str @.L0
  .loc 1 10 11
  call @Viper.Terminal.PrintStr(%t45)
  .loc 1 10 5
  br L110
L110:
  .loc 1 11 5
  ret 0
exit:
  %t46 = load ptr, %t2
  call @rt_arr_i64_release(%t46)
  store ptr, %t2, null
  ret 0
dim_len_fail:
  .loc 1 2 4
  trap
dim_len_cont(%len:i64):
  .loc 1 2 10
  %t10 = call @rt_arr_i64_new(%len)
  .loc 1 2 10
  call @rt_arr_i64_retain(%t10)
  .loc 1 2 10
  %t11 = load ptr, %t2
  .loc 1 2 10
  call @rt_arr_i64_release(%t11)
  .loc 1 2 10
  store ptr, %t2, %t10
  .loc 1 2 4
  br L30
loop_head:
  .loc 1 5 10
  %t12 = load i64, %t1
  .loc 1 5 14
  %t13 = load i64, %t3
  .loc 1 5 12
  %t14 = scmp_lt %t12, %t13
  .loc 1 5 12
  %t15 = zext1 %t14
  .loc 1 5 12
  %t16 = isub.ovf 0, %t15
  .loc 1 5 4
  %t17 = trunc1 %t16
  .loc 1 5 12
  cbr %t17, loop_body, done
loop_body:
  .loc 1 6 17
  %t18 = load i64, %t1
  .loc 1 6 21
  %t19 = load i64, %t1
  .loc 1 6 19
  %t20 = imul.ovf %t18, %t19
  .loc 1 6 6
  %t21 = load ptr, %t2
  .loc 1 6 12
  %t22 = load i64, %t1
  .loc 1 6 10
  %t23 = call @rt_arr_i64_len(%t21)
  .loc 1 6 10
  %t24 = scmp_lt %t22, 0
  .loc 1 6 10
  %t25 = scmp_ge %t22, %t23
  .loc 1 6 10
  %t26 = zext1 %t24
  .loc 1 6 10
  %t27 = zext1 %t25
  .loc 1 6 10
  %t28 = or %t26, %t27
  .loc 1 6 10
  %t29 = icmp_ne %t28, 0
  .loc 1 6 10
  cbr %t29, bc_oob0, bc_ok0
done:
  .loc 1 5 4
  br L100
bc_ok0:
  .loc 1 6 10
  call @rt_arr_i64_set(%t21, %t22, %t20)
  .loc 1 7 14
  %t30 = load i64, %t0
  .loc 1 7 18
  %t31 = load ptr, %t2
  .loc 1 7 20
  %t32 = load i64, %t1
  .loc 1 7 18
  %t33 = call @rt_arr_i64_len(%t31)
  .loc 1 7 18
  %t34 = scmp_lt %t32, 0
  .loc 1 7 18
  %t35 = scmp_ge %t32, %t33
  .loc 1 7 18
  %t36 = zext1 %t34
  .loc 1 7 18
  %t37 = zext1 %t35
  .loc 1 7 18
  %t38 = or %t36, %t37
  .loc 1 7 18
  %t39 = icmp_ne %t38, 0
  .loc 1 7 18
  cbr %t39, bc_oob1, bc_ok1
bc_oob0:
  .loc 1 6 10
  call @rt_arr_oob_panic(%t22, %t23)
  .loc 1 6 10
  trap
bc_ok1:
  .loc 1 7 18
  %t40 = call @rt_arr_i64_get(%t31, %t32)
  .loc 1 7 16
  %t41 = iadd.ovf %t30, %t40
  .loc 1 7 6
  store i64, %t0, %t41
  .loc 1 8 14
  %t42 = load i64, %t1
  .loc 1 8 16
  %t43 = iadd.ovf %t42, 1
  .loc 1 8 6
  store i64, %t1, %t43
  .loc 1 8 6
  br loop_head
bc_oob1:
  .loc 1 7 18
  call @rt_arr_oob_panic(%t32, %t33)
  .loc 1 7 18
  trap
}
