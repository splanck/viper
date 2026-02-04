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
extern @rt_left(str, i64) -> str
extern @rt_right(str, i64) -> str
extern @rt_mid2(str, i64) -> str
extern @rt_mid3(str, i64, i64) -> str
extern @rt_instr2(str, str) -> i64
extern @rt_ltrim(str) -> str
extern @rt_rtrim(str) -> str
extern @rt_trim(str) -> str
extern @rt_ucase(str) -> str
extern @rt_lcase(str) -> str
extern @rt_val(str) -> f64
extern @rt_val_to_double(ptr, ptr) -> f64
extern @rt_string_cstr(str) -> ptr
extern @rt_str_empty() -> str
extern @rt_obj_new_i64(i64, i64) -> ptr
extern @rt_obj_retain_maybe(ptr) -> void
extern @rt_obj_release_check0(ptr) -> i1
extern @rt_obj_free(ptr) -> void
extern @rt_sqrt(f64) -> f64
extern @rt_abs_i64(i64) -> i64
extern @rt_abs_f64(f64) -> f64
extern @rt_sin(f64) -> f64
extern @rt_cos(f64) -> f64
extern @rt_tan(f64) -> f64
extern @rt_atan(f64) -> f64
extern @rt_exp(f64) -> f64
extern @rt_log(f64) -> f64
extern @rt_pow_f64_chkdom(f64, f64) -> f64
extern @rt_sgn_i64(i64) -> i64
extern @rt_randomize_i64(i64) -> void
extern @rt_rnd() -> f64
extern @Viper.Collections.List.RemoveAt(ptr, i64) -> void
extern @Viper.Collections.List.Add(ptr, ptr) -> void
extern @Viper.Collections.List.get_Count(ptr) -> i64
extern @Viper.Collections.List.New() -> ptr
extern @Viper.Text.StringBuilder.Clear(ptr) -> void
extern @Viper.Text.StringBuilder.Append(ptr, str) -> ptr
extern @Viper.Text.StringBuilder.New() -> ptr
extern @Viper.Text.StringBuilder.ToString(ptr) -> str
extern @Viper.Text.StringBuilder.get_Length(ptr) -> i64
extern @Viper.Collections.List.Clear(ptr) -> void
extern @rt_str_retain_maybe(str) -> void
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "=== Viper.String ==="
global const str @.L1 = "\n"
global const str @.L2 = "  Hello World  "
global const str @.L3 = "Original: '"
global const str @.L4 = "'"
global const str @.L5 = "Length: "
global const str @.L6 = "Trim: '"
global const str @.L7 = "TrimStart: '"
global const str @.L8 = "TrimEnd: '"
global const str @.L9 = "ToUpper: "
global const str @.L10 = "ToLower: "
global const str @.L11 = "Left(5): "
global const str @.L12 = "Right(5): "
global const str @.L13 = "Mid(7): "
global const str @.L14 = "Mid(7,3): "
global const str @.L15 = "World"
global const str @.L16 = "IndexOf 'World': "
global const str @.L17 = "Substring(1,5): "
global const str @.L18 = "Empty string length is 0: PASS"
global const str @.L19 = ""
global const str @.L20 = "=== Viper.Text.StringBuilder ==="
global const str @.L21 = "Initial Length: "
global const str @.L22 = "Hello"
global const str @.L23 = "After 'Hello' Length: "
global const str @.L24 = " "
global const str @.L25 = "After ' World' Length: "
global const str @.L26 = "ToString: "
global const str @.L27 = "After Clear Length: "
global const str @.L28 = "=== Viper.Collections.List ==="
global const str @.L29 = "Initial Count: "
global const str @.L30 = "After 3 Add: "
global const str @.L31 = "After RemoveAt(1): "
global const str @.L32 = "After Clear: "
global const str @.L33 = "=== Viper.Math ==="
global const str @.L34 = "Sqrt(16): "
global const str @.L35 = "Abs(-5): "
global const str @.L36 = "Abs(-3.14): "
global const str @.L37 = "Floor(3.7): "
global const str @.L38 = "Floor(-3.7): "
global const str @.L39 = "Sin(0): "
global const str @.L40 = "Cos(0): "
global const str @.L41 = "Tan(0): "
global const str @.L42 = "Atan(0): "
global const str @.L43 = "Exp(0): "
global const str @.L44 = "Exp(1): "
global const str @.L45 = "Log(1): "
global const str @.L46 = "Pow(2,10): "
global const str @.L47 = "Sgn(-5): "
global const str @.L48 = "Sgn(0): "
global const str @.L49 = "Sgn(5): "
global const str @.L50 = "=== Viper.Random ==="
global const str @.L51 = "Seeded random (12345):"
global const str @.L52 = "  r1 in [0,1): "
global const str @.L53 = "  r2 in [0,1): "
global const str @.L54 = "  r3 in [0,1): "
global const str @.L55 = "Reproducible with same seed: PASS"
global const str @.L56 = "Reproducible with same seed: FAIL"
global const str @.L57 = "=== Viper.Console ==="
global const str @.L58 = "WriteLine test: PASS"
global const str @.L59 = "=== Viper.Convert ==="
global const str @.L60 = "42"
global const str @.L61 = "ToInt64('42'): "
global const str @.L62 = "3.14159"
global const str @.L63 = "ToDouble('3.14159'): "
global const str @.L64 = "123"
global const str @.L65 = "ToString_Int(123): '"
global const str @.L66 = "2.5"
global const str @.L67 = "ToString_Double(2.5): '"
global const str @.L68 = "=== All Tests Complete ==="
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  %t1 = call @rt_str_empty()
  store str, %t0, %t1
  %t2 = alloca 8
  %t3 = call @rt_str_empty()
  store str, %t2, %t3
  %t4 = alloca 8
  %t5 = call @rt_str_empty()
  store str, %t4, %t5
  %t6 = alloca 8
  %t7 = call @rt_str_empty()
  store str, %t6, %t7
  %t8 = alloca 8
  %t9 = alloca 8
  %t10 = alloca 8
  %t11 = alloca 8
  %t12 = alloca 8
  %t13 = alloca 8
  %t14 = alloca 8
  store ptr, %t14, null
  %t15 = alloca 8
  store ptr, %t15, null
  %t16 = alloca 8
  store ptr, %t16, null
  %t17 = alloca 8
  %t18 = call @rt_str_empty()
  store str, %t17, %t18
  %t19 = alloca 8
  %t20 = alloca 8
  %t21 = alloca 8
  %t22 = call @rt_str_empty()
  store str, %t21, %t22
  %t23 = alloca 8
  %t24 = call @rt_str_empty()
  store str, %t23, %t24
  %t25 = alloca 8
  %t26 = alloca 512
  store i64, %t25, 0
  br UL1000000000
UL1000000000:
  .loc 1 7 7
  %t27 = const_str @.L0
  .loc 1 7 7
  call @Viper.Terminal.PrintStr(%t27)
  .loc 1 7 7
  %t28 = const_str @.L1
  .loc 1 7 7
  call @Viper.Terminal.PrintStr(%t28)
  .loc 1 7 1
  br UL999999999
UL999999999:
  .loc 1 8 1
  br UL999999998
UL999999998:
  .loc 1 9 5
  %t29 = const_str @.L2
  .loc 1 9 1
  %t30 = load str, %t23
  .loc 1 9 1
  call @rt_str_release_maybe(%t30)
  .loc 1 9 1
  call @rt_str_retain_maybe(%t29)
  .loc 1 9 1
  store str, %t23, %t29
  .loc 1 9 1
  br UL999999997
UL999999997:
  .loc 1 10 7
  %t31 = const_str @.L3
  .loc 1 10 7
  call @Viper.Terminal.PrintStr(%t31)
  .loc 1 10 22
  %t32 = load str, %t23
  .loc 1 10 22
  call @rt_str_retain_maybe(%t32)
  .loc 1 10 22
  call @Viper.Terminal.PrintStr(%t32)
  .loc 1 10 22
  call @rt_str_release_maybe(%t32)
  .loc 1 10 25
  %t33 = const_str @.L4
  .loc 1 10 25
  call @Viper.Terminal.PrintStr(%t33)
  .loc 1 10 25
  %t34 = const_str @.L1
  .loc 1 10 25
  call @Viper.Terminal.PrintStr(%t34)
  .loc 1 10 1
  br UL999999996
UL999999996:
  .loc 1 11 7
  %t35 = const_str @.L5
  .loc 1 11 7
  call @Viper.Terminal.PrintStr(%t35)
  .loc 1 11 23
  %t36 = load str, %t23
  .loc 1 11 19
  %t37 = call @Viper.String.get_Length(%t36)
  .loc 1 11 19
  call @Viper.Terminal.PrintI64(%t37)
  .loc 1 11 19
  %t38 = const_str @.L1
  .loc 1 11 19
  call @Viper.Terminal.PrintStr(%t38)
  .loc 1 11 1
  br UL999999995
UL999999995:
  .loc 1 13 1
  br UL999999994
UL999999994:
  .loc 1 14 17
  %t39 = load str, %t23
  .loc 1 14 11
  %t40 = call @rt_trim(%t39)
  .loc 1 14 1
  %t41 = load str, %t21
  .loc 1 14 1
  call @rt_str_release_maybe(%t41)
  .loc 1 14 1
  call @rt_str_retain_maybe(%t40)
  .loc 1 14 1
  store str, %t21, %t40
  .loc 1 14 1
  br UL999999993
UL999999993:
  .loc 1 15 7
  %t42 = const_str @.L6
  .loc 1 15 7
  call @Viper.Terminal.PrintStr(%t42)
  .loc 1 15 18
  %t43 = load str, %t21
  .loc 1 15 18
  call @rt_str_retain_maybe(%t43)
  .loc 1 15 18
  call @Viper.Terminal.PrintStr(%t43)
  .loc 1 15 18
  call @rt_str_release_maybe(%t43)
  .loc 1 15 27
  %t44 = const_str @.L4
  .loc 1 15 27
  call @Viper.Terminal.PrintStr(%t44)
  .loc 1 15 27
  %t45 = const_str @.L1
  .loc 1 15 27
  call @Viper.Terminal.PrintStr(%t45)
  .loc 1 15 1
  br UL999999992
UL999999992:
  .loc 1 16 7
  %t46 = const_str @.L7
  .loc 1 16 7
  call @Viper.Terminal.PrintStr(%t46)
  .loc 1 16 30
  %t47 = load str, %t23
  .loc 1 16 23
  %t48 = call @rt_ltrim(%t47)
  .loc 1 16 23
  call @Viper.Terminal.PrintStr(%t48)
  .loc 1 16 34
  %t49 = const_str @.L4
  .loc 1 16 34
  call @Viper.Terminal.PrintStr(%t49)
  .loc 1 16 34
  %t50 = const_str @.L1
  .loc 1 16 34
  call @Viper.Terminal.PrintStr(%t50)
  .loc 1 16 1
  br UL999999991
UL999999991:
  .loc 1 17 7
  %t51 = const_str @.L8
  .loc 1 17 7
  call @Viper.Terminal.PrintStr(%t51)
  .loc 1 17 28
  %t52 = load str, %t23
  .loc 1 17 21
  %t53 = call @rt_rtrim(%t52)
  .loc 1 17 21
  call @Viper.Terminal.PrintStr(%t53)
  .loc 1 17 32
  %t54 = const_str @.L4
  .loc 1 17 32
  call @Viper.Terminal.PrintStr(%t54)
  .loc 1 17 32
  %t55 = const_str @.L1
  .loc 1 17 32
  call @Viper.Terminal.PrintStr(%t55)
  .loc 1 17 1
  br UL999999990
UL999999990:
  .loc 1 19 7
  %t56 = const_str @.L9
  .loc 1 19 7
  call @Viper.Terminal.PrintStr(%t56)
  .loc 1 19 27
  %t57 = load str, %t21
  .loc 1 19 20
  %t58 = call @rt_ucase(%t57)
  .loc 1 19 20
  call @Viper.Terminal.PrintStr(%t58)
  .loc 1 19 20
  %t59 = const_str @.L1
  .loc 1 19 20
  call @Viper.Terminal.PrintStr(%t59)
  .loc 1 19 1
  br UL999999989
UL999999989:
  .loc 1 20 7
  %t60 = const_str @.L10
  .loc 1 20 7
  call @Viper.Terminal.PrintStr(%t60)
  .loc 1 20 27
  %t61 = load str, %t21
  .loc 1 20 20
  %t62 = call @rt_lcase(%t61)
  .loc 1 20 20
  call @Viper.Terminal.PrintStr(%t62)
  .loc 1 20 20
  %t63 = const_str @.L1
  .loc 1 20 20
  call @Viper.Terminal.PrintStr(%t63)
  .loc 1 20 1
  br UL999999988
UL999999988:
  .loc 1 22 7
  %t64 = const_str @.L11
  .loc 1 22 7
  call @Viper.Terminal.PrintStr(%t64)
  .loc 1 22 26
  %t65 = load str, %t21
  .loc 1 22 20
  %t66 = call @rt_left(%t65, 5)
  .loc 1 22 20
  call @Viper.Terminal.PrintStr(%t66)
  .loc 1 22 20
  %t67 = const_str @.L1
  .loc 1 22 20
  call @Viper.Terminal.PrintStr(%t67)
  .loc 1 22 1
  br UL999999987
UL999999987:
  .loc 1 23 7
  %t68 = const_str @.L12
  .loc 1 23 7
  call @Viper.Terminal.PrintStr(%t68)
  .loc 1 23 28
  %t69 = load str, %t21
  .loc 1 23 21
  %t70 = call @rt_right(%t69, 5)
  .loc 1 23 21
  call @Viper.Terminal.PrintStr(%t70)
  .loc 1 23 21
  %t71 = const_str @.L1
  .loc 1 23 21
  call @Viper.Terminal.PrintStr(%t71)
  .loc 1 23 1
  br UL999999986
UL999999986:
  .loc 1 24 7
  %t72 = const_str @.L13
  .loc 1 24 7
  call @Viper.Terminal.PrintStr(%t72)
  .loc 1 24 24
  %t73 = load str, %t21
  .loc 1 24 19
  %t74 = call @rt_mid2(%t73, 7)
  .loc 1 24 19
  call @Viper.Terminal.PrintStr(%t74)
  .loc 1 24 19
  %t75 = const_str @.L1
  .loc 1 24 19
  call @Viper.Terminal.PrintStr(%t75)
  .loc 1 24 1
  br UL999999985
UL999999985:
  .loc 1 25 7
  %t76 = const_str @.L14
  .loc 1 25 7
  call @Viper.Terminal.PrintStr(%t76)
  .loc 1 25 26
  %t77 = load str, %t21
  .loc 1 25 38
  %t78 = call @rt_mid3(%t77, 7, 3)
  .loc 1 25 38
  call @Viper.Terminal.PrintStr(%t78)
  .loc 1 25 38
  %t79 = const_str @.L1
  .loc 1 25 38
  call @Viper.Terminal.PrintStr(%t79)
  .loc 1 25 1
  br UL999999984
UL999999984:
  .loc 1 27 1
  br UL999999983
UL999999983:
  .loc 1 28 13
  %t80 = load str, %t21
  .loc 1 28 22
  %t81 = const_str @.L15
  .loc 1 28 22
  %t82 = call @rt_instr2(%t80, %t81)
  .loc 1 28 1
  store i64, %t20, %t82
  .loc 1 28 1
  br UL999999982
UL999999982:
  .loc 1 29 7
  %t83 = const_str @.L16
  .loc 1 29 7
  call @Viper.Terminal.PrintStr(%t83)
  .loc 1 29 28
  %t84 = load i64, %t20
  .loc 1 29 28
  call @Viper.Terminal.PrintI64(%t84)
  .loc 1 29 28
  %t85 = const_str @.L1
  .loc 1 29 28
  call @Viper.Terminal.PrintStr(%t85)
  .loc 1 29 1
  br UL999999981
UL999999981:
  .loc 1 31 1
  br UL999999980
UL999999980:
  .loc 1 32 13
  %t86 = load str, %t21
  .loc 1 32 25
  %t87 = call @rt_mid3(%t86, 1, 5)
  .loc 1 32 1
  %t88 = load str, %t17
  .loc 1 32 1
  call @rt_str_release_maybe(%t88)
  .loc 1 32 1
  call @rt_str_retain_maybe(%t87)
  .loc 1 32 1
  store str, %t17, %t87
  .loc 1 32 1
  br UL999999979
UL999999979:
  .loc 1 33 7
  %t89 = const_str @.L17
  .loc 1 33 7
  call @Viper.Terminal.PrintStr(%t89)
  .loc 1 33 27
  %t90 = load str, %t17
  .loc 1 33 27
  call @rt_str_retain_maybe(%t90)
  .loc 1 33 27
  call @Viper.Terminal.PrintStr(%t90)
  .loc 1 33 27
  call @rt_str_release_maybe(%t90)
  .loc 1 33 27
  %t91 = const_str @.L1
  .loc 1 33 27
  call @Viper.Terminal.PrintStr(%t91)
  .loc 1 33 1
  br UL999999978
UL999999978:
  .loc 1 35 1
  br if_test_0
UL999999977:
  .loc 1 39 7
  %t95 = const_str @.L19
  .loc 1 39 7
  call @Viper.Terminal.PrintStr(%t95)
  .loc 1 39 7
  %t96 = const_str @.L1
  .loc 1 39 7
  call @Viper.Terminal.PrintStr(%t96)
  .loc 1 39 1
  br UL999999976
UL999999976:
  .loc 1 44 7
  %t97 = const_str @.L20
  .loc 1 44 7
  call @Viper.Terminal.PrintStr(%t97)
  .loc 1 44 7
  %t98 = const_str @.L1
  .loc 1 44 7
  call @Viper.Terminal.PrintStr(%t98)
  .loc 1 44 1
  br UL999999975
UL999999975:
  .loc 1 45 1
  br UL999999974
UL999999974:
  .loc 1 46 6
  %t99 = call @Viper.Text.StringBuilder.New()
  .loc 1 46 1
  %t100 = load ptr, %t15
  .loc 1 46 1
  %t101 = call @rt_obj_release_check0(%t100)
  .loc 1 46 1
  cbr %t101, obj_assign_dtor, obj_assign_cont
UL999999973:
  .loc 1 47 7
  %t102 = const_str @.L21
  .loc 1 47 7
  call @Viper.Terminal.PrintStr(%t102)
  .loc 1 47 27
  %t103 = load ptr, %t15
  .loc 1 47 27
  %t104 = load ptr, %t15
  .loc 1 47 30
  %t105 = call @Viper.Text.StringBuilder.get_Length(%t104)
  .loc 1 47 30
  call @Viper.Terminal.PrintI64(%t105)
  .loc 1 47 30
  %t106 = const_str @.L1
  .loc 1 47 30
  call @Viper.Terminal.PrintStr(%t106)
  .loc 1 47 1
  br UL999999972
UL999999972:
  .loc 1 48 1
  %t107 = load ptr, %t15
  .loc 1 48 11
  %t108 = const_str @.L22
  .loc 1 48 4
  %t109 = call @Viper.Text.StringBuilder.Append(%t107, %t108)
  .loc 1 48 1
  br UL999999971
UL999999971:
  .loc 1 49 7
  %t110 = const_str @.L23
  .loc 1 49 7
  call @Viper.Terminal.PrintStr(%t110)
  .loc 1 49 33
  %t111 = load ptr, %t15
  .loc 1 49 33
  %t112 = load ptr, %t15
  .loc 1 49 36
  %t113 = call @Viper.Text.StringBuilder.get_Length(%t112)
  .loc 1 49 36
  call @Viper.Terminal.PrintI64(%t113)
  .loc 1 49 36
  %t114 = const_str @.L1
  .loc 1 49 36
  call @Viper.Terminal.PrintStr(%t114)
  .loc 1 49 1
  br UL999999970
UL999999970:
  .loc 1 50 1
  %t115 = load ptr, %t15
  .loc 1 50 11
  %t116 = const_str @.L24
  .loc 1 50 4
  %t117 = call @Viper.Text.StringBuilder.Append(%t115, %t116)
  .loc 1 50 1
  br UL999999969
UL999999969:
  .loc 1 51 1
  %t118 = load ptr, %t15
  .loc 1 51 11
  %t119 = const_str @.L15
  .loc 1 51 4
  %t120 = call @Viper.Text.StringBuilder.Append(%t118, %t119)
  .loc 1 51 1
  br UL999999968
UL999999968:
  .loc 1 52 7
  %t121 = const_str @.L25
  .loc 1 52 7
  call @Viper.Terminal.PrintStr(%t121)
  .loc 1 52 34
  %t122 = load ptr, %t15
  .loc 1 52 34
  %t123 = load ptr, %t15
  .loc 1 52 37
  %t124 = call @Viper.Text.StringBuilder.get_Length(%t123)
  .loc 1 52 37
  call @Viper.Terminal.PrintI64(%t124)
  .loc 1 52 37
  %t125 = const_str @.L1
  .loc 1 52 37
  call @Viper.Terminal.PrintStr(%t125)
  .loc 1 52 1
  br UL999999967
UL999999967:
  .loc 1 53 7
  %t126 = const_str @.L26
  .loc 1 53 7
  call @Viper.Terminal.PrintStr(%t126)
  .loc 1 53 21
  %t127 = load ptr, %t15
  .loc 1 53 24
  %t128 = call @Viper.Text.StringBuilder.ToString(%t127)
  .loc 1 53 24
  call @Viper.Terminal.PrintStr(%t128)
  .loc 1 53 24
  %t129 = const_str @.L1
  .loc 1 53 24
  call @Viper.Terminal.PrintStr(%t129)
  .loc 1 53 1
  call @rt_str_release_maybe(%t128)
  .loc 1 53 1
  br UL999999966
UL999999966:
  .loc 1 54 1
  %t130 = load ptr, %t15
  .loc 1 54 4
  call @Viper.Text.StringBuilder.Clear(%t130)
  .loc 1 54 1
  br UL999999965
UL999999965:
  .loc 1 55 7
  %t131 = const_str @.L27
  .loc 1 55 7
  call @Viper.Terminal.PrintStr(%t131)
  .loc 1 55 31
  %t132 = load ptr, %t15
  .loc 1 55 31
  %t133 = load ptr, %t15
  .loc 1 55 34
  %t134 = call @Viper.Text.StringBuilder.get_Length(%t133)
  .loc 1 55 34
  call @Viper.Terminal.PrintI64(%t134)
  .loc 1 55 34
  %t135 = const_str @.L1
  .loc 1 55 34
  call @Viper.Terminal.PrintStr(%t135)
  .loc 1 55 1
  br UL999999964
UL999999964:
  .loc 1 56 7
  %t136 = const_str @.L19
  .loc 1 56 7
  call @Viper.Terminal.PrintStr(%t136)
  .loc 1 56 7
  %t137 = const_str @.L1
  .loc 1 56 7
  call @Viper.Terminal.PrintStr(%t137)
  .loc 1 56 1
  br UL999999963
UL999999963:
  .loc 1 61 7
  %t138 = const_str @.L28
  .loc 1 61 7
  call @Viper.Terminal.PrintStr(%t138)
  .loc 1 61 7
  %t139 = const_str @.L1
  .loc 1 61 7
  call @Viper.Terminal.PrintStr(%t139)
  .loc 1 61 1
  br UL999999962
UL999999962:
  .loc 1 62 1
  br UL999999961
UL999999961:
  .loc 1 63 8
  %t140 = call @Viper.Collections.List.New()
  .loc 1 63 1
  %t141 = load ptr, %t14
  .loc 1 63 1
  %t142 = call @rt_obj_release_check0(%t141)
  .loc 1 63 1
  cbr %t142, obj_assign_dtor1, obj_assign_cont1
UL999999960:
  .loc 1 64 7
  %t143 = const_str @.L29
  .loc 1 64 7
  call @Viper.Terminal.PrintStr(%t143)
  .loc 1 64 26
  %t144 = load ptr, %t14
  .loc 1 64 26
  %t145 = load ptr, %t14
  .loc 1 64 31
  %t146 = call @Viper.Collections.List.get_Count(%t145)
  .loc 1 64 31
  call @Viper.Terminal.PrintI64(%t146)
  .loc 1 64 31
  %t147 = const_str @.L1
  .loc 1 64 31
  call @Viper.Terminal.PrintStr(%t147)
  .loc 1 64 1
  br UL999999959
UL999999959:
  .loc 1 67 1
  br UL999999958
UL999999958:
  .loc 1 68 7
  %t148 = call @Viper.Text.StringBuilder.New()
  .loc 1 68 1
  %t149 = load ptr, %t16
  .loc 1 68 1
  %t150 = call @rt_obj_release_check0(%t149)
  .loc 1 68 1
  cbr %t150, obj_assign_dtor2, obj_assign_cont2
UL999999957:
  .loc 1 69 1
  %t151 = load ptr, %t14
  .loc 1 69 10
  %t152 = load ptr, %t16
  .loc 1 69 6
  call @Viper.Collections.List.Add(%t151, %t152)
  .loc 1 69 1
  br UL999999956
UL999999956:
  .loc 1 70 1
  %t153 = load ptr, %t14
  .loc 1 70 10
  %t154 = load ptr, %t16
  .loc 1 70 6
  call @Viper.Collections.List.Add(%t153, %t154)
  .loc 1 70 1
  br UL999999955
UL999999955:
  .loc 1 71 1
  %t155 = load ptr, %t14
  .loc 1 71 10
  %t156 = load ptr, %t16
  .loc 1 71 6
  call @Viper.Collections.List.Add(%t155, %t156)
  .loc 1 71 1
  br UL999999954
UL999999954:
  .loc 1 72 7
  %t157 = const_str @.L30
  .loc 1 72 7
  call @Viper.Terminal.PrintStr(%t157)
  .loc 1 72 24
  %t158 = load ptr, %t14
  .loc 1 72 24
  %t159 = load ptr, %t14
  .loc 1 72 29
  %t160 = call @Viper.Collections.List.get_Count(%t159)
  .loc 1 72 29
  call @Viper.Terminal.PrintI64(%t160)
  .loc 1 72 29
  %t161 = const_str @.L1
  .loc 1 72 29
  call @Viper.Terminal.PrintStr(%t161)
  .loc 1 72 1
  br UL999999953
UL999999953:
  .loc 1 74 1
  %t162 = load ptr, %t14
  .loc 1 74 6
  call @Viper.Collections.List.RemoveAt(%t162, 1)
  .loc 1 74 1
  br UL999999952
UL999999952:
  .loc 1 75 7
  %t163 = const_str @.L31
  .loc 1 75 7
  call @Viper.Terminal.PrintStr(%t163)
  .loc 1 75 30
  %t164 = load ptr, %t14
  .loc 1 75 30
  %t165 = load ptr, %t14
  .loc 1 75 35
  %t166 = call @Viper.Collections.List.get_Count(%t165)
  .loc 1 75 35
  call @Viper.Terminal.PrintI64(%t166)
  .loc 1 75 35
  %t167 = const_str @.L1
  .loc 1 75 35
  call @Viper.Terminal.PrintStr(%t167)
  .loc 1 75 1
  br UL999999951
UL999999951:
  .loc 1 77 1
  %t168 = load ptr, %t14
  .loc 1 77 6
  call @Viper.Collections.List.Clear(%t168)
  .loc 1 77 1
  br UL999999950
UL999999950:
  .loc 1 78 7
  %t169 = const_str @.L32
  .loc 1 78 7
  call @Viper.Terminal.PrintStr(%t169)
  .loc 1 78 24
  %t170 = load ptr, %t14
  .loc 1 78 24
  %t171 = load ptr, %t14
  .loc 1 78 29
  %t172 = call @Viper.Collections.List.get_Count(%t171)
  .loc 1 78 29
  call @Viper.Terminal.PrintI64(%t172)
  .loc 1 78 29
  %t173 = const_str @.L1
  .loc 1 78 29
  call @Viper.Terminal.PrintStr(%t173)
  .loc 1 78 1
  br UL999999949
UL999999949:
  .loc 1 79 7
  %t174 = const_str @.L19
  .loc 1 79 7
  call @Viper.Terminal.PrintStr(%t174)
  .loc 1 79 7
  %t175 = const_str @.L1
  .loc 1 79 7
  call @Viper.Terminal.PrintStr(%t175)
  .loc 1 79 1
  br UL999999948
UL999999948:
  .loc 1 84 7
  %t176 = const_str @.L33
  .loc 1 84 7
  call @Viper.Terminal.PrintStr(%t176)
  .loc 1 84 7
  %t177 = const_str @.L1
  .loc 1 84 7
  call @Viper.Terminal.PrintStr(%t177)
  .loc 1 84 1
  br UL999999947
UL999999947:
  .loc 1 85 7
  %t178 = const_str @.L34
  .loc 1 85 7
  call @Viper.Terminal.PrintStr(%t178)
  .loc 1 85 25
  %t179 = cast.si_to_fp 16
  .loc 1 85 21
  %t180 = call @rt_sqrt(%t179)
  .loc 1 85 21
  call @Viper.Terminal.PrintF64(%t180)
  .loc 1 85 21
  %t181 = const_str @.L1
  .loc 1 85 21
  call @Viper.Terminal.PrintStr(%t181)
  .loc 1 85 1
  br UL999999946
UL999999946:
  .loc 1 86 7
  %t182 = const_str @.L35
  .loc 1 86 7
  call @Viper.Terminal.PrintStr(%t182)
  .loc 1 86 20
  %t183 = call @rt_abs_i64(-5)
  .loc 1 86 20
  call @Viper.Terminal.PrintI64(%t183)
  .loc 1 86 20
  %t184 = const_str @.L1
  .loc 1 86 20
  call @Viper.Terminal.PrintStr(%t184)
  .loc 1 86 1
  br UL999999945
UL999999945:
  .loc 1 87 7
  %t185 = const_str @.L36
  .loc 1 87 7
  call @Viper.Terminal.PrintStr(%t185)
  .loc 1 87 23
  %t186 = call @rt_abs_f64(-3.1400000000000001)
  .loc 1 87 23
  call @Viper.Terminal.PrintF64(%t186)
  .loc 1 87 23
  %t187 = const_str @.L1
  .loc 1 87 23
  call @Viper.Terminal.PrintStr(%t187)
  .loc 1 87 1
  br UL999999944
UL999999944:
  .loc 1 89 1
  br UL999999943
UL999999943:
  .loc 1 90 1
  store f64, %t13, 3.14159265358979
  .loc 1 90 1
  br UL999999942
UL999999942:
  .loc 1 92 7
  %t188 = const_str @.L37
  .loc 1 92 7
  call @Viper.Terminal.PrintStr(%t188)
  .loc 1 92 23
  call @Viper.Terminal.PrintF64(3)
  .loc 1 92 23
  %t189 = const_str @.L1
  .loc 1 92 23
  call @Viper.Terminal.PrintStr(%t189)
  .loc 1 92 1
  br UL999999941
UL999999941:
  .loc 1 93 7
  %t190 = const_str @.L38
  .loc 1 93 7
  call @Viper.Terminal.PrintStr(%t190)
  .loc 1 93 24
  call @Viper.Terminal.PrintF64(-4)
  .loc 1 93 24
  %t191 = const_str @.L1
  .loc 1 93 24
  call @Viper.Terminal.PrintStr(%t191)
  .loc 1 93 1
  br UL999999940
UL999999940:
  .loc 1 95 7
  %t192 = const_str @.L39
  .loc 1 95 7
  call @Viper.Terminal.PrintStr(%t192)
  .loc 1 95 23
  %t193 = cast.si_to_fp 0
  .loc 1 95 19
  %t194 = call @rt_sin(%t193)
  .loc 1 95 19
  call @Viper.Terminal.PrintF64(%t194)
  .loc 1 95 19
  %t195 = const_str @.L1
  .loc 1 95 19
  call @Viper.Terminal.PrintStr(%t195)
  .loc 1 95 1
  br UL999999939
UL999999939:
  .loc 1 96 7
  %t196 = const_str @.L40
  .loc 1 96 7
  call @Viper.Terminal.PrintStr(%t196)
  .loc 1 96 23
  %t197 = cast.si_to_fp 0
  .loc 1 96 19
  %t198 = call @rt_cos(%t197)
  .loc 1 96 19
  call @Viper.Terminal.PrintF64(%t198)
  .loc 1 96 19
  %t199 = const_str @.L1
  .loc 1 96 19
  call @Viper.Terminal.PrintStr(%t199)
  .loc 1 96 1
  br UL999999938
UL999999938:
  .loc 1 97 7
  %t200 = const_str @.L41
  .loc 1 97 7
  call @Viper.Terminal.PrintStr(%t200)
  .loc 1 97 23
  %t201 = cast.si_to_fp 0
  .loc 1 97 19
  %t202 = call @rt_tan(%t201)
  .loc 1 97 19
  call @Viper.Terminal.PrintF64(%t202)
  .loc 1 97 19
  %t203 = const_str @.L1
  .loc 1 97 19
  call @Viper.Terminal.PrintStr(%t203)
  .loc 1 97 1
  br UL999999937
UL999999937:
  .loc 1 98 7
  %t204 = const_str @.L42
  .loc 1 98 7
  call @Viper.Terminal.PrintStr(%t204)
  .loc 1 98 24
  %t205 = cast.si_to_fp 0
  .loc 1 98 20
  %t206 = call @rt_atan(%t205)
  .loc 1 98 20
  call @Viper.Terminal.PrintF64(%t206)
  .loc 1 98 20
  %t207 = const_str @.L1
  .loc 1 98 20
  call @Viper.Terminal.PrintStr(%t207)
  .loc 1 98 1
  br UL999999936
UL999999936:
  .loc 1 100 7
  %t208 = const_str @.L43
  .loc 1 100 7
  call @Viper.Terminal.PrintStr(%t208)
  .loc 1 100 23
  %t209 = cast.si_to_fp 0
  .loc 1 100 19
  %t210 = call @rt_exp(%t209)
  .loc 1 100 19
  call @Viper.Terminal.PrintF64(%t210)
  .loc 1 100 19
  %t211 = const_str @.L1
  .loc 1 100 19
  call @Viper.Terminal.PrintStr(%t211)
  .loc 1 100 1
  br UL999999935
UL999999935:
  .loc 1 101 7
  %t212 = const_str @.L44
  .loc 1 101 7
  call @Viper.Terminal.PrintStr(%t212)
  .loc 1 101 23
  %t213 = cast.si_to_fp 1
  .loc 1 101 19
  %t214 = call @rt_exp(%t213)
  .loc 1 101 19
  call @Viper.Terminal.PrintF64(%t214)
  .loc 1 101 19
  %t215 = const_str @.L1
  .loc 1 101 19
  call @Viper.Terminal.PrintStr(%t215)
  .loc 1 101 1
  br UL999999934
UL999999934:
  .loc 1 102 7
  %t216 = const_str @.L45
  .loc 1 102 7
  call @Viper.Terminal.PrintStr(%t216)
  .loc 1 102 23
  %t217 = cast.si_to_fp 1
  .loc 1 102 19
  %t218 = call @rt_log(%t217)
  .loc 1 102 19
  call @Viper.Terminal.PrintF64(%t218)
  .loc 1 102 19
  %t219 = const_str @.L1
  .loc 1 102 19
  call @Viper.Terminal.PrintStr(%t219)
  .loc 1 102 1
  br UL999999933
UL999999933:
  .loc 1 104 7
  %t220 = const_str @.L46
  .loc 1 104 7
  call @Viper.Terminal.PrintStr(%t220)
  .loc 1 104 22
  %t221 = sitofp 2
  .loc 1 104 24
  %t222 = sitofp 10
  .loc 1 104 23
  %t223 = call @rt_pow_f64_chkdom(%t221, %t222)
  .loc 1 104 23
  call @Viper.Terminal.PrintF64(%t223)
  .loc 1 104 23
  %t224 = const_str @.L1
  .loc 1 104 23
  call @Viper.Terminal.PrintStr(%t224)
  .loc 1 104 1
  br UL999999932
UL999999932:
  .loc 1 106 7
  %t225 = const_str @.L47
  .loc 1 106 7
  call @Viper.Terminal.PrintStr(%t225)
  .loc 1 106 20
  %t226 = call @rt_sgn_i64(-5)
  .loc 1 106 20
  call @Viper.Terminal.PrintI64(%t226)
  .loc 1 106 20
  %t227 = const_str @.L1
  .loc 1 106 20
  call @Viper.Terminal.PrintStr(%t227)
  .loc 1 106 1
  br UL999999931
UL999999931:
  .loc 1 107 7
  %t228 = const_str @.L48
  .loc 1 107 7
  call @Viper.Terminal.PrintStr(%t228)
  .loc 1 107 19
  %t229 = call @rt_sgn_i64(0)
  .loc 1 107 19
  call @Viper.Terminal.PrintI64(%t229)
  .loc 1 107 19
  %t230 = const_str @.L1
  .loc 1 107 19
  call @Viper.Terminal.PrintStr(%t230)
  .loc 1 107 1
  br UL999999930
UL999999930:
  .loc 1 108 7
  %t231 = const_str @.L49
  .loc 1 108 7
  call @Viper.Terminal.PrintStr(%t231)
  .loc 1 108 19
  %t232 = call @rt_sgn_i64(5)
  .loc 1 108 19
  call @Viper.Terminal.PrintI64(%t232)
  .loc 1 108 19
  %t233 = const_str @.L1
  .loc 1 108 19
  call @Viper.Terminal.PrintStr(%t233)
  .loc 1 108 1
  br UL999999929
UL999999929:
  .loc 1 109 7
  %t234 = const_str @.L19
  .loc 1 109 7
  call @Viper.Terminal.PrintStr(%t234)
  .loc 1 109 7
  %t235 = const_str @.L1
  .loc 1 109 7
  call @Viper.Terminal.PrintStr(%t235)
  .loc 1 109 1
  br UL999999928
UL999999928:
  .loc 1 114 7
  %t236 = const_str @.L50
  .loc 1 114 7
  call @Viper.Terminal.PrintStr(%t236)
  .loc 1 114 7
  %t237 = const_str @.L1
  .loc 1 114 7
  call @Viper.Terminal.PrintStr(%t237)
  .loc 1 114 1
  br UL999999927
UL999999927:
  .loc 1 115 11
  call @rt_randomize_i64(12345)
  .loc 1 115 1
  br UL999999926
UL999999926:
  .loc 1 116 1
  br UL999999925
UL999999925:
  .loc 1 117 6
  %t238 = call @rt_rnd()
  .loc 1 117 1
  store f64, %t12, %t238
  .loc 1 117 1
  br UL999999924
UL999999924:
  .loc 1 118 6
  %t239 = call @rt_rnd()
  .loc 1 118 1
  store f64, %t11, %t239
  .loc 1 118 1
  br UL999999923
UL999999923:
  .loc 1 119 6
  %t240 = call @rt_rnd()
  .loc 1 119 1
  store f64, %t9, %t240
  .loc 1 119 1
  br UL999999922
UL999999922:
  .loc 1 120 7
  %t241 = const_str @.L51
  .loc 1 120 7
  call @Viper.Terminal.PrintStr(%t241)
  .loc 1 120 7
  %t242 = const_str @.L1
  .loc 1 120 7
  call @Viper.Terminal.PrintStr(%t242)
  .loc 1 120 1
  br UL999999921
UL999999921:
  .loc 1 121 7
  %t243 = const_str @.L52
  .loc 1 121 7
  call @Viper.Terminal.PrintStr(%t243)
  .loc 1 121 27
  %t244 = load f64, %t12
  .loc 1 121 30
  %t245 = sitofp 0
  .loc 1 121 30
  %t246 = fcmp_ge %t244, %t245
  .loc 1 121 30
  %t247 = zext1 %t246
  .loc 1 121 30
  %t248 = isub.ovf 0, %t247
  .loc 1 121 39
  %t249 = load f64, %t12
  .loc 1 121 42
  %t250 = sitofp 1
  .loc 1 121 42
  %t251 = fcmp_lt %t249, %t250
  .loc 1 121 42
  %t252 = zext1 %t251
  .loc 1 121 42
  %t253 = isub.ovf 0, %t252
  .loc 1 121 35
  %t254 = and %t248, %t253
  .loc 1 121 35
  call @Viper.Terminal.PrintI64(%t254)
  .loc 1 121 35
  %t255 = const_str @.L1
  .loc 1 121 35
  call @Viper.Terminal.PrintStr(%t255)
  .loc 1 121 1
  br UL999999920
UL999999920:
  .loc 1 122 7
  %t256 = const_str @.L53
  .loc 1 122 7
  call @Viper.Terminal.PrintStr(%t256)
  .loc 1 122 27
  %t257 = load f64, %t11
  .loc 1 122 30
  %t258 = sitofp 0
  .loc 1 122 30
  %t259 = fcmp_ge %t257, %t258
  .loc 1 122 30
  %t260 = zext1 %t259
  .loc 1 122 30
  %t261 = isub.ovf 0, %t260
  .loc 1 122 39
  %t262 = load f64, %t11
  .loc 1 122 42
  %t263 = sitofp 1
  .loc 1 122 42
  %t264 = fcmp_lt %t262, %t263
  .loc 1 122 42
  %t265 = zext1 %t264
  .loc 1 122 42
  %t266 = isub.ovf 0, %t265
  .loc 1 122 35
  %t267 = and %t261, %t266
  .loc 1 122 35
  call @Viper.Terminal.PrintI64(%t267)
  .loc 1 122 35
  %t268 = const_str @.L1
  .loc 1 122 35
  call @Viper.Terminal.PrintStr(%t268)
  .loc 1 122 1
  br UL999999919
UL999999919:
  .loc 1 123 7
  %t269 = const_str @.L54
  .loc 1 123 7
  call @Viper.Terminal.PrintStr(%t269)
  .loc 1 123 27
  %t270 = load f64, %t9
  .loc 1 123 30
  %t271 = sitofp 0
  .loc 1 123 30
  %t272 = fcmp_ge %t270, %t271
  .loc 1 123 30
  %t273 = zext1 %t272
  .loc 1 123 30
  %t274 = isub.ovf 0, %t273
  .loc 1 123 39
  %t275 = load f64, %t9
  .loc 1 123 42
  %t276 = sitofp 1
  .loc 1 123 42
  %t277 = fcmp_lt %t275, %t276
  .loc 1 123 42
  %t278 = zext1 %t277
  .loc 1 123 42
  %t279 = isub.ovf 0, %t278
  .loc 1 123 35
  %t280 = and %t274, %t279
  .loc 1 123 35
  call @Viper.Terminal.PrintI64(%t280)
  .loc 1 123 35
  %t281 = const_str @.L1
  .loc 1 123 35
  call @Viper.Terminal.PrintStr(%t281)
  .loc 1 123 1
  br UL999999918
UL999999918:
  .loc 1 126 11
  call @rt_randomize_i64(12345)
  .loc 1 126 1
  br UL999999917
UL999999917:
  .loc 1 127 1
  br UL999999916
UL999999916:
  .loc 1 128 7
  %t282 = call @rt_rnd()
  .loc 1 128 1
  store f64, %t8, %t282
  .loc 1 128 1
  br UL999999915
UL999999915:
  .loc 1 129 1
  br if_test_01
UL999999914:
  .loc 1 134 7
  %t293 = const_str @.L19
  .loc 1 134 7
  call @Viper.Terminal.PrintStr(%t293)
  .loc 1 134 7
  %t294 = const_str @.L1
  .loc 1 134 7
  call @Viper.Terminal.PrintStr(%t294)
  .loc 1 134 1
  br UL999999913
UL999999913:
  .loc 1 139 7
  %t295 = const_str @.L57
  .loc 1 139 7
  call @Viper.Terminal.PrintStr(%t295)
  .loc 1 139 7
  %t296 = const_str @.L1
  .loc 1 139 7
  call @Viper.Terminal.PrintStr(%t296)
  .loc 1 139 1
  br UL999999912
UL999999912:
  .loc 1 140 7
  %t297 = const_str @.L58
  .loc 1 140 7
  call @Viper.Terminal.PrintStr(%t297)
  .loc 1 140 7
  %t298 = const_str @.L1
  .loc 1 140 7
  call @Viper.Terminal.PrintStr(%t298)
  .loc 1 140 1
  br UL999999911
UL999999911:
  .loc 1 141 7
  %t299 = const_str @.L19
  .loc 1 141 7
  call @Viper.Terminal.PrintStr(%t299)
  .loc 1 141 7
  %t300 = const_str @.L1
  .loc 1 141 7
  call @Viper.Terminal.PrintStr(%t300)
  .loc 1 141 1
  br UL999999910
UL999999910:
  .loc 1 146 7
  %t301 = const_str @.L59
  .loc 1 146 7
  call @Viper.Terminal.PrintStr(%t301)
  .loc 1 146 7
  %t302 = const_str @.L1
  .loc 1 146 7
  call @Viper.Terminal.PrintStr(%t302)
  .loc 1 146 1
  br UL999999909
UL999999909:
  .loc 1 147 1
  br UL999999908
UL999999908:
  .loc 1 148 10
  %t303 = const_str @.L60
  .loc 1 148 1
  %t304 = load str, %t6
  .loc 1 148 1
  call @rt_str_release_maybe(%t304)
  .loc 1 148 1
  call @rt_str_retain_maybe(%t303)
  .loc 1 148 1
  store str, %t6, %t303
  .loc 1 148 1
  br UL999999907
UL999999907:
  .loc 1 149 1
  br UL999999906
UL999999906:
  .loc 1 150 14
  %t305 = load str, %t6
  .loc 1 150 14
  %t306 = call @rt_string_cstr(%t305)
  .loc 1 150 14
  %t307 = alloca 1
  .loc 1 150 14
  %t308 = call @rt_val_to_double(%t306, %t307)
  .loc 1 150 14
  %t309 = load i1, %t307
  .loc 1 150 14
  cbr %t309, val_ok, val_fail
UL999999905:
  .loc 1 151 7
  %t313 = const_str @.L61
  .loc 1 151 7
  call @Viper.Terminal.PrintStr(%t313)
  .loc 1 151 26
  %t314 = load f64, %t10
  .loc 1 151 26
  call @Viper.Terminal.PrintF64(%t314)
  .loc 1 151 26
  %t315 = const_str @.L1
  .loc 1 151 26
  call @Viper.Terminal.PrintStr(%t315)
  .loc 1 151 1
  br UL999999904
UL999999904:
  .loc 1 153 1
  br UL999999903
UL999999903:
  .loc 1 154 12
  %t316 = const_str @.L62
  .loc 1 154 1
  %t317 = load str, %t4
  .loc 1 154 1
  call @rt_str_release_maybe(%t317)
  .loc 1 154 1
  call @rt_str_retain_maybe(%t316)
  .loc 1 154 1
  store str, %t4, %t316
  .loc 1 154 1
  br UL999999902
UL999999902:
  .loc 1 155 1
  br UL999999901
UL999999901:
  .loc 1 156 16
  %t318 = load str, %t4
  .loc 1 156 16
  %t319 = call @rt_string_cstr(%t318)
  .loc 1 156 16
  %t320 = alloca 1
  .loc 1 156 16
  %t321 = call @rt_val_to_double(%t319, %t320)
  .loc 1 156 16
  %t322 = load i1, %t320
  .loc 1 156 16
  cbr %t322, val_ok1, val_fail1
UL999999900:
  .loc 1 157 7
  %t326 = const_str @.L63
  .loc 1 157 7
  call @Viper.Terminal.PrintStr(%t326)
  .loc 1 157 32
  %t327 = load f64, %t19
  .loc 1 157 32
  call @Viper.Terminal.PrintF64(%t327)
  .loc 1 157 32
  %t328 = const_str @.L1
  .loc 1 157 32
  call @Viper.Terminal.PrintStr(%t328)
  .loc 1 157 1
  br UL999999899
UL999999899:
  .loc 1 159 1
  br UL999999898
UL999999898:
  .loc 1 160 12
  %t329 = const_str @.L64
  .loc 1 160 1
  %t330 = load str, %t2
  .loc 1 160 1
  call @rt_str_release_maybe(%t330)
  .loc 1 160 1
  call @rt_str_retain_maybe(%t329)
  .loc 1 160 1
  store str, %t2, %t329
  .loc 1 160 1
  br UL999999897
UL999999897:
  .loc 1 161 7
  %t331 = const_str @.L65
  .loc 1 161 7
  call @Viper.Terminal.PrintStr(%t331)
  .loc 1 161 38
  %t332 = load str, %t2
  .loc 1 161 31
  %t333 = call @rt_ltrim(%t332)
  .loc 1 161 31
  call @Viper.Terminal.PrintStr(%t333)
  .loc 1 161 49
  %t334 = const_str @.L4
  .loc 1 161 49
  call @Viper.Terminal.PrintStr(%t334)
  .loc 1 161 49
  %t335 = const_str @.L1
  .loc 1 161 49
  call @Viper.Terminal.PrintStr(%t335)
  .loc 1 161 1
  br UL999999896
UL999999896:
  .loc 1 163 1
  br UL999999895
UL999999895:
  .loc 1 164 12
  %t336 = const_str @.L66
  .loc 1 164 1
  %t337 = load str, %t0
  .loc 1 164 1
  call @rt_str_release_maybe(%t337)
  .loc 1 164 1
  call @rt_str_retain_maybe(%t336)
  .loc 1 164 1
  store str, %t0, %t336
  .loc 1 164 1
  br UL999999894
UL999999894:
  .loc 1 165 7
  %t338 = const_str @.L67
  .loc 1 165 7
  call @Viper.Terminal.PrintStr(%t338)
  .loc 1 165 41
  %t339 = load str, %t0
  .loc 1 165 34
  %t340 = call @rt_ltrim(%t339)
  .loc 1 165 34
  call @Viper.Terminal.PrintStr(%t340)
  .loc 1 165 52
  %t341 = const_str @.L4
  .loc 1 165 52
  call @Viper.Terminal.PrintStr(%t341)
  .loc 1 165 52
  %t342 = const_str @.L1
  .loc 1 165 52
  call @Viper.Terminal.PrintStr(%t342)
  .loc 1 165 1
  br UL999999893
UL999999893:
  .loc 1 166 7
  %t343 = const_str @.L19
  .loc 1 166 7
  call @Viper.Terminal.PrintStr(%t343)
  .loc 1 166 7
  %t344 = const_str @.L1
  .loc 1 166 7
  call @Viper.Terminal.PrintStr(%t344)
  .loc 1 166 1
  br UL999999892
UL999999892:
  .loc 1 171 7
  %t345 = const_str @.L68
  .loc 1 171 7
  call @Viper.Terminal.PrintStr(%t345)
  .loc 1 171 7
  %t346 = const_str @.L1
  .loc 1 171 7
  call @Viper.Terminal.PrintStr(%t346)
  .loc 1 171 1
  br exit
exit:
  %t347 = load ptr, %t14
  %t348 = call @rt_obj_release_check0(%t347)
  cbr %t348, obj_epilogue_dtor, obj_epilogue_cont
if_test_0:
  .loc 1 35 1
  %t92 = trunc1 -1
  .loc 1 35 12
  cbr %t92, if_then_0, if_else
if_then_0:
  .loc 1 36 11
  %t93 = const_str @.L18
  .loc 1 36 11
  call @Viper.Terminal.PrintStr(%t93)
  .loc 1 36 11
  %t94 = const_str @.L1
  .loc 1 36 11
  call @Viper.Terminal.PrintStr(%t94)
  .loc 1 35 1
  br if_exit
if_else:
  .loc 1 35 1
  br if_exit
if_exit:
  .loc 1 35 1
  br UL999999977
obj_assign_dtor:
  .loc 1 46 1
  call @rt_obj_free(%t100)
  .loc 1 46 1
  br obj_assign_cont
obj_assign_cont:
  .loc 1 46 1
  call @rt_obj_retain_maybe(%t99)
  .loc 1 46 1
  store ptr, %t15, %t99
  .loc 1 46 1
  br UL999999973
obj_assign_dtor1:
  .loc 1 63 1
  call @rt_obj_free(%t141)
  .loc 1 63 1
  br obj_assign_cont1
obj_assign_cont1:
  .loc 1 63 1
  call @rt_obj_retain_maybe(%t140)
  .loc 1 63 1
  store ptr, %t14, %t140
  .loc 1 63 1
  br UL999999960
obj_assign_dtor2:
  .loc 1 68 1
  call @rt_obj_free(%t149)
  .loc 1 68 1
  br obj_assign_cont2
obj_assign_cont2:
  .loc 1 68 1
  call @rt_obj_retain_maybe(%t148)
  .loc 1 68 1
  store ptr, %t16, %t148
  .loc 1 68 1
  br UL999999957
if_test_01:
  .loc 1 129 4
  %t283 = load f64, %t12
  .loc 1 129 9
  %t284 = load f64, %t8
  .loc 1 129 7
  %t285 = fcmp_eq %t283, %t284
  .loc 1 129 7
  %t286 = zext1 %t285
  .loc 1 129 7
  %t287 = isub.ovf 0, %t286
  .loc 1 129 1
  %t288 = trunc1 %t287
  .loc 1 129 7
  cbr %t288, if_then_01, if_else1
if_then_01:
  .loc 1 130 11
  %t289 = const_str @.L55
  .loc 1 130 11
  call @Viper.Terminal.PrintStr(%t289)
  .loc 1 130 11
  %t290 = const_str @.L1
  .loc 1 130 11
  call @Viper.Terminal.PrintStr(%t290)
  .loc 1 129 1
  br if_exit1
if_else1:
  .loc 1 132 11
  %t291 = const_str @.L56
  .loc 1 132 11
  call @Viper.Terminal.PrintStr(%t291)
  .loc 1 132 11
  %t292 = const_str @.L1
  .loc 1 132 11
  call @Viper.Terminal.PrintStr(%t292)
  .loc 1 129 1
  br if_exit1
if_exit1:
  .loc 1 129 1
  br UL999999914
val_ok:
  .loc 1 150 1
  store f64, %t10, %t308
  .loc 1 150 1
  br UL999999905
val_fail:
  .loc 1 150 14
  %t310 = fcmp_ne %t308, %t308
  .loc 1 150 14
  cbr %t310, val_nan, val_over
val_nan:
  .loc 1 150 14
  %t311 = cast.fp_to_si.rte.chk NaN
  .loc 1 150 14
  trap
val_over:
  .loc 1 150 14
  %t312 = cast.fp_to_si.rte.chk 1.7976931348623157e+308
  .loc 1 150 14
  trap
val_ok1:
  .loc 1 156 1
  store f64, %t19, %t321
  .loc 1 156 1
  br UL999999900
val_fail1:
  .loc 1 156 16
  %t323 = fcmp_ne %t321, %t321
  .loc 1 156 16
  cbr %t323, val_nan1, val_over1
val_nan1:
  .loc 1 156 16
  %t324 = cast.fp_to_si.rte.chk NaN
  .loc 1 156 16
  trap
val_over1:
  .loc 1 156 16
  %t325 = cast.fp_to_si.rte.chk 1.7976931348623157e+308
  .loc 1 156 16
  trap
obj_epilogue_dtor:
  call @rt_obj_free(%t347)
  br obj_epilogue_cont
obj_epilogue_cont:
  store ptr, %t14, null
  %t349 = load ptr, %t15
  %t350 = call @rt_obj_release_check0(%t349)
  cbr %t350, obj_epilogue_dtor1, obj_epilogue_cont1
obj_epilogue_dtor1:
  call @rt_obj_free(%t349)
  br obj_epilogue_cont1
obj_epilogue_cont1:
  store ptr, %t15, null
  %t351 = load ptr, %t16
  %t352 = call @rt_obj_release_check0(%t351)
  cbr %t352, obj_epilogue_dtor2, obj_epilogue_cont2
obj_epilogue_dtor2:
  call @rt_obj_free(%t351)
  br obj_epilogue_cont2
obj_epilogue_cont2:
  store ptr, %t16, null
  ret 0
}
