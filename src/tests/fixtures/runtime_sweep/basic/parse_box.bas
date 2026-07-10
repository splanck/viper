' EXPECT_OUT: RESULT: ok
' COVER: Viper.Core.Parse.TryInt
' COVER: Viper.Core.Parse.TryNum
' COVER: Viper.Core.Parse.TryBool
' COVER: Viper.Core.Parse.IntOr
' COVER: Viper.Core.Parse.NumOr
' COVER: Viper.Core.Parse.BoolOr
' COVER: Viper.Core.Parse.IsInt
' COVER: Viper.Core.Parse.IsNum
' COVER: Viper.Core.Parse.IntRadix
' COVER: Viper.Core.Box.I64
' COVER: Viper.Core.Box.F64
' COVER: Viper.Core.Box.I1
' COVER: Viper.Core.Box.Str
' COVER: Viper.Core.Box.ToI64
' COVER: Viper.Core.Box.ToF64
' COVER: Viper.Core.Box.ToI1
' COVER: Viper.Core.Box.ToStr
' COVER: Viper.Core.Box.Type
' COVER: Viper.Core.Box.EqI64
' COVER: Viper.Core.Box.EqF64
' COVER: Viper.Core.Box.EqStr

DIM ok AS INTEGER
ok = Viper.Core.Parse.TryInt("42", NOTHING)
Viper.Core.Diagnostics.Assert(ok = 0, "parse.tryint.null")
ok = Viper.Core.Parse.TryNum("3.25", NOTHING)
Viper.Core.Diagnostics.Assert(ok = 0, "parse.trynum.null")
ok = Viper.Core.Parse.TryBool("true", NOTHING)
Viper.Core.Diagnostics.Assert(ok = 0, "parse.trybool.null")

Viper.Core.Diagnostics.AssertEq(Viper.Core.Parse.IntOr("42", -1), 42, "parse.intor")
Viper.Core.Diagnostics.AssertEqNum(Viper.Core.Parse.NumOr("2.5", -1.0), 2.5, "parse.numor")
Viper.Core.Diagnostics.Assert(Viper.Core.Parse.BoolOr("yes", FALSE), "parse.boolor")
Viper.Core.Diagnostics.Assert(Viper.Core.Parse.IsInt(" -7 "), "parse.isint")
Viper.Core.Diagnostics.Assert(Viper.Core.Parse.IsNum("3.14"), "parse.isnum")
Viper.Core.Diagnostics.AssertEq(Viper.Core.Parse.IntRadix("ff", 16, -1), 255, "parse.intradix")

DIM bInt AS OBJECT
DIM bNum AS OBJECT
DIM bBool AS OBJECT
DIM bStr AS OBJECT
bInt = Viper.Core.Box.I64(123)
bNum = Viper.Core.Box.F64(4.5)
bBool = Viper.Core.Box.I1(1)
bStr = Viper.Core.Box.Str("hi")

Viper.Core.Diagnostics.AssertEq(Viper.Core.Box.ToI64(bInt), 123, "box.toi64")
Viper.Core.Diagnostics.AssertEqNum(Viper.Core.Box.ToF64(bNum), 4.5, "box.tof64")
Viper.Core.Diagnostics.AssertEq(Viper.Core.Box.ToI1(bBool), 1, "box.toi1")
Viper.Core.Diagnostics.AssertEqStr(Viper.Core.Box.ToStr(bStr), "hi", "box.tostr")

Viper.Core.Diagnostics.AssertEq(Viper.Core.Box.Type(bInt), 0, "box.type.i64")
Viper.Core.Diagnostics.AssertEq(Viper.Core.Box.Type(bNum), 1, "box.type.f64")
Viper.Core.Diagnostics.AssertEq(Viper.Core.Box.Type(bBool), 2, "box.type.i1")
Viper.Core.Diagnostics.AssertEq(Viper.Core.Box.Type(bStr), 3, "box.type.str")

Viper.Core.Diagnostics.AssertEq(Viper.Core.Box.EqI64(bInt, 123), 1, "box.eqi64")
Viper.Core.Diagnostics.AssertEq(Viper.Core.Box.EqF64(bNum, 4.5), 1, "box.eqf64")
Viper.Core.Diagnostics.AssertEq(Viper.Core.Box.EqStr(bStr, "hi"), 1, "box.eqstr")

PRINT "RESULT: ok"
END
