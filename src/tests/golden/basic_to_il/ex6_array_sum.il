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
extern @Viper.Console.ReadLine() -> str
extern @Viper.Convert.ToInt(str) -> i64
extern @rt_str_empty() -> str
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_len(ptr) -> i64
extern @rt_arr_i32_get(ptr, i64) -> i64
extern @rt_arr_i32_set(ptr, i64, i64) -> void
extern @rt_arr_i32_retain(ptr) -> void
extern @rt_arr_i32_release(ptr) -> void
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
  %t4 = call @Viper.Console.ReadLine()
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
  %t8 = scmp_lt %t7, 0
  .loc 1 2 4
  cbr %t8, dim_len_fail, dim_len_cont
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
  %t43 = load i64, %t0
  .loc 1 10 11
  call @Viper.Console.PrintI64(%t43)
  .loc 1 10 11
  %t44 = const_str @.L0
  .loc 1 10 11
  call @Viper.Console.PrintStr(%t44)
  .loc 1 10 5
  br L110
L110:
  .loc 1 11 5
  ret 0
exit:
  %t45 = load ptr, %t2
  call @rt_arr_i32_release(%t45)
  store ptr, %t2, null
  ret 0
dim_len_fail:
  .loc 1 2 4
  trap
dim_len_cont:
  .loc 1 2 10
  %t9 = call @rt_arr_i32_new(%t7)
  .loc 1 2 10
  call @rt_arr_i32_retain(%t9)
  .loc 1 2 10
  %t10 = load ptr, %t2
  .loc 1 2 10
  call @rt_arr_i32_release(%t10)
  .loc 1 2 10
  store ptr, %t2, %t9
  .loc 1 2 4
  br L30
loop_head:
  .loc 1 5 10
  %t11 = load i64, %t1
  .loc 1 5 14
  %t12 = load i64, %t3
  .loc 1 5 12
  %t13 = scmp_lt %t11, %t12
  .loc 1 5 12
  %t14 = zext1 %t13
  .loc 1 5 12
  %t15 = isub.ovf 0, %t14
  .loc 1 5 4
  %t16 = trunc1 %t15
  .loc 1 5 12
  cbr %t16, loop_body, done
loop_body:
  .loc 1 6 17
  %t17 = load i64, %t1
  .loc 1 6 21
  %t18 = load i64, %t1
  .loc 1 6 19
  %t19 = imul.ovf %t17, %t18
  .loc 1 6 6
  %t20 = load ptr, %t2
  .loc 1 6 12
  %t21 = load i64, %t1
  .loc 1 6 10
  %t22 = call @rt_arr_i32_len(%t20)
  .loc 1 6 10
  %t23 = scmp_lt %t21, 0
  .loc 1 6 10
  %t24 = scmp_ge %t21, %t22
  .loc 1 6 10
  %t25 = zext1 %t23
  .loc 1 6 10
  %t26 = zext1 %t24
  .loc 1 6 10
  %t27 = or %t25, %t26
  .loc 1 6 10
  %t28 = icmp_ne %t27, 0
  .loc 1 6 10
  cbr %t28, bc_oob0, bc_ok0
done:
  .loc 1 5 4
  br L100
bc_ok0:
  .loc 1 6 10
  call @rt_arr_i32_set(%t20, %t21, %t19)
  .loc 1 7 14
  %t29 = load i64, %t0
  .loc 1 7 18
  %t30 = load ptr, %t2
  .loc 1 7 20
  %t31 = load i64, %t1
  .loc 1 7 18
  %t32 = call @rt_arr_i32_len(%t30)
  .loc 1 7 18
  %t33 = scmp_lt %t31, 0
  .loc 1 7 18
  %t34 = scmp_ge %t31, %t32
  .loc 1 7 18
  %t35 = zext1 %t33
  .loc 1 7 18
  %t36 = zext1 %t34
  .loc 1 7 18
  %t37 = or %t35, %t36
  .loc 1 7 18
  %t38 = icmp_ne %t37, 0
  .loc 1 7 18
  cbr %t38, bc_oob1, bc_ok1
bc_oob0:
  .loc 1 6 10
  call @rt_arr_oob_panic(%t21, %t22)
  .loc 1 6 10
  trap
bc_ok1:
  .loc 1 7 18
  %t39 = call @rt_arr_i32_get(%t30, %t31)
  .loc 1 7 16
  %t40 = iadd.ovf %t29, %t39
  .loc 1 7 6
  store i64, %t0, %t40
  .loc 1 8 14
  %t41 = load i64, %t1
  .loc 1 8 16
  %t42 = iadd.ovf %t41, 1
  .loc 1 8 6
  store i64, %t1, %t42
  .loc 1 8 6
  br loop_head
bc_oob1:
  .loc 1 7 18
  call @rt_arr_oob_panic(%t31, %t32)
  .loc 1 7 18
  trap
}
