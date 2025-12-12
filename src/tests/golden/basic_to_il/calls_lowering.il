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
extern @Viper.String.IndexOfFrom(i64, str, str) -> i64
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
extern @rt_trap(str) -> void
global const str @.L0 = ""
global const str @.L1 = "\n"
global const str @.L2 = "HI"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @F#(f64 %X#) -> f64 {
entry_F#(%X#:f64):
  %t1 = alloca 8
  store f64, %t1, %X#
  br L-1000000000_F#
L-1000000000_F#:
  .loc 1 2 8
  %t2 = load f64, %t1
  .loc 1 2 8
  ret %t2
ret_F#:
  ret 0.0
}
func @ID$(str %S$) -> str {
entry_ID$(%S$:str):
  %t1 = alloca 8
  store str, %t1, %S$
  br L-1000000000_ID$
L-1000000000_ID$:
  .loc 1 5 8
  %t2 = load str, %t1
  .loc 1 5 8
  ret %t2
ret_ID$:
  %t3 = const_str @.L0
  ret %t3
}
func @FIRST#(i64 %N) -> f64 {
entry_FIRST#(%N:i64):
  %t1 = alloca 8
  store i64, %t1, %N
  br L-1000000000_FIRST#
L-1000000000_FIRST#:
  .loc 1 8 1
  br if_test_0_FIRST#
L-999999999_FIRST#:
  .loc 1 9 16
  %t7 = load i64, %t1
  .loc 1 9 8
  %t8 = sitofp %t7
  .loc 1 9 8
  %t9 = call @SECOND#(%t8)
  .loc 1 9 8
  ret %t9
ret_FIRST#:
  ret 0.0
if_test_0_FIRST#:
  .loc 1 8 4
  %t2 = load i64, %t1
  .loc 1 8 6
  %t3 = scmp_le %t2, 0
  .loc 1 8 6
  %t4 = zext1 %t3
  .loc 1 8 6
  %t5 = isub.ovf 0, %t4
  .loc 1 8 1
  %t6 = trunc1 %t5
  .loc 1 8 6
  cbr %t6, if_then_0_FIRST#, if_else_0_FIRST#
if_then_0_FIRST#:
  .loc 1 8 23
  ret 0.0
if_else_0_FIRST#:
  .loc 1 8 1
  br if_end_0_FIRST#
if_end_0_FIRST#:
  .loc 1 8 1
  br L-999999999_FIRST#
}
func @SECOND#(f64 %N#) -> f64 {
entry_SECOND#(%N#:f64):
  %t1 = alloca 8
  store f64, %t1, %N#
  br L-1000000000_SECOND#
L-1000000000_SECOND#:
  .loc 1 12 1
  br if_test_0_SECOND#
L-999999999_SECOND#:
  .loc 1 13 15
  %t8 = load f64, %t1
  .loc 1 13 18
  %t9 = sitofp 1
  .loc 1 13 18
  %t10 = fsub %t8, %t9
  .loc 1 13 8
  %t11 = cast.fp_to_si.rte.chk %t10
  .loc 1 13 8
  %t12 = call @FIRST#(%t11)
  .loc 1 13 8
  ret %t12
ret_SECOND#:
  ret 0.0
if_test_0_SECOND#:
  .loc 1 12 4
  %t2 = load f64, %t1
  .loc 1 12 7
  %t3 = sitofp 0
  .loc 1 12 7
  %t4 = fcmp_le %t2, %t3
  .loc 1 12 7
  %t5 = zext1 %t4
  .loc 1 12 7
  %t6 = isub.ovf 0, %t5
  .loc 1 12 1
  %t7 = trunc1 %t6
  .loc 1 12 7
  cbr %t7, if_then_0_SECOND#, if_else_0_SECOND#
if_then_0_SECOND#:
  .loc 1 12 24
  ret 0.0
if_else_0_SECOND#:
  .loc 1 12 1
  br if_end_0_SECOND#
if_end_0_SECOND#:
  .loc 1 12 1
  br L-999999999_SECOND#
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 15 10
  %t0 = sitofp 1
  .loc 1 15 10
  %t1 = call @F#(%t0)
  .loc 1 15 10
  call @Viper.Terminal.PrintF64(%t1)
  .loc 1 15 10
  %t2 = const_str @.L1
  .loc 1 15 10
  call @Viper.Terminal.PrintStr(%t2)
  .loc 1 15 4
  br L20
L20:
  .loc 1 16 14
  %t3 = const_str @.L2
  .loc 1 16 10
  %t4 = call @ID$(%t3)
  .loc 1 16 10
  call @Viper.Terminal.PrintStr(%t4)
  .loc 1 16 10
  %t5 = const_str @.L1
  .loc 1 16 10
  call @Viper.Terminal.PrintStr(%t5)
  .loc 1 16 4
  br L30
L30:
  .loc 1 17 10
  %t6 = call @FIRST#(2)
  .loc 1 17 10
  call @Viper.Terminal.PrintF64(%t6)
  .loc 1 17 10
  %t7 = const_str @.L1
  .loc 1 17 10
  call @Viper.Terminal.PrintStr(%t7)
  .loc 1 17 4
  br L40
L40:
  .loc 1 18 4
  ret 0
exit:
  ret 0
}
