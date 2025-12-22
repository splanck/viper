' EXPECT_OUT: RESULT: ok
' COVER: Viper.Parse.TryInt
' COVER: Viper.Parse.TryNum
' COVER: Viper.Parse.TryBool
' COVER: Viper.Parse.IntOr
' COVER: Viper.Parse.NumOr
' COVER: Viper.Parse.BoolOr
' COVER: Viper.Parse.IsInt
' COVER: Viper.Parse.IsNum
' COVER: Viper.Parse.IntRadix
' COVER: Viper.Box.I64
' COVER: Viper.Box.F64
' COVER: Viper.Box.I1
' COVER: Viper.Box.Str
' COVER: Viper.Box.ToI64
' COVER: Viper.Box.ToF64
' COVER: Viper.Box.ToI1
' COVER: Viper.Box.ToStr
' COVER: Viper.Box.Type
' COVER: Viper.Box.EqI64
' COVER: Viper.Box.EqF64
' COVER: Viper.Box.EqStr

DIM ok AS INTEGER
ok = Viper.Parse.TryInt("42", NOTHING)
Viper.Diagnostics.Assert(ok = 0, "parse.tryint.null")
ok = Viper.Parse.TryNum("3.25", NOTHING)
Viper.Diagnostics.Assert(ok = 0, "parse.trynum.null")
ok = Viper.Parse.TryBool("true", NOTHING)
Viper.Diagnostics.Assert(ok = 0, "parse.trybool.null")

Viper.Diagnostics.AssertEq(Viper.Parse.IntOr("42", -1), 42, "parse.intor")
Viper.Diagnostics.AssertEqNum(Viper.Parse.NumOr("2.5", -1.0), 2.5, "parse.numor")
Viper.Diagnostics.Assert(Viper.Parse.BoolOr("yes", FALSE), "parse.boolor")
Viper.Diagnostics.Assert(Viper.Parse.IsInt(" -7 "), "parse.isint")
Viper.Diagnostics.Assert(Viper.Parse.IsNum("3.14"), "parse.isnum")
Viper.Diagnostics.AssertEq(Viper.Parse.IntRadix("ff", 16, -1), 255, "parse.intradix")

DIM bInt AS OBJECT
DIM bNum AS OBJECT
DIM bBool AS OBJECT
DIM bStr AS OBJECT
bInt = Viper.Box.I64(123)
bNum = Viper.Box.F64(4.5)
bBool = Viper.Box.I1(1)
bStr = Viper.Box.Str("hi")

Viper.Diagnostics.AssertEq(Viper.Box.ToI64(bInt), 123, "box.toi64")
Viper.Diagnostics.AssertEqNum(Viper.Box.ToF64(bNum), 4.5, "box.tof64")
Viper.Diagnostics.AssertEq(Viper.Box.ToI1(bBool), 1, "box.toi1")
Viper.Diagnostics.AssertEqStr(Viper.Box.ToStr(bStr), "hi", "box.tostr")

Viper.Diagnostics.AssertEq(Viper.Box.Type(bInt), 0, "box.type.i64")
Viper.Diagnostics.AssertEq(Viper.Box.Type(bNum), 1, "box.type.f64")
Viper.Diagnostics.AssertEq(Viper.Box.Type(bBool), 2, "box.type.i1")
Viper.Diagnostics.AssertEq(Viper.Box.Type(bStr), 3, "box.type.str")

Viper.Diagnostics.AssertEq(Viper.Box.EqI64(bInt, 123), 1, "box.eqi64")
Viper.Diagnostics.AssertEq(Viper.Box.EqF64(bNum, 4.5), 1, "box.eqf64")
Viper.Diagnostics.AssertEq(Viper.Box.EqStr(bStr, "hi"), 1, "box.eqstr")

PRINT "RESULT: ok"
END
