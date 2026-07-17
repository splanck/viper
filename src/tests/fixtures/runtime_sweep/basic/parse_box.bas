' EXPECT_OUT: RESULT: ok
' COVER: Zanna.Core.Parse.TryInt
' COVER: Zanna.Core.Parse.TryDouble
' COVER: Zanna.Core.Parse.TryBool
' COVER: Zanna.Core.Parse.IntOr
' COVER: Zanna.Core.Parse.DoubleOr
' COVER: Zanna.Core.Parse.BoolOr
' COVER: Zanna.Core.Parse.IsInt
' COVER: Zanna.Core.Parse.IsNum
' COVER: Zanna.Core.Parse.IntRadix
' COVER: Zanna.Core.Box.I64
' COVER: Zanna.Core.Box.F64
' COVER: Zanna.Core.Box.I1
' COVER: Zanna.Core.Box.Str
' COVER: Zanna.Core.Box.ToI64
' COVER: Zanna.Core.Box.ToF64
' COVER: Zanna.Core.Box.ToI1
' COVER: Zanna.Core.Box.ToStr
' COVER: Zanna.Core.Box.Type
' COVER: Zanna.Core.Box.EqI64
' COVER: Zanna.Core.Box.EqF64
' COVER: Zanna.Core.Box.EqStr

Zanna.Core.Diagnostics.Assert(Zanna.Option.get_IsSome(Zanna.Core.Parse.TryInt("42")), "parse.tryint")
Zanna.Core.Diagnostics.Assert(Zanna.Option.get_IsSome(Zanna.Core.Parse.TryDouble("3.25")), "parse.trynum")
Zanna.Core.Diagnostics.Assert(Zanna.Option.get_IsSome(Zanna.Core.Parse.TryBool("true")), "parse.trybool")

Zanna.Core.Diagnostics.AssertEq(Zanna.Core.Parse.IntOr("42", -1), 42, "parse.intor")
Zanna.Core.Diagnostics.AssertEqNum(Zanna.Core.Parse.DoubleOr("2.5", -1.0), 2.5, "parse.numor")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Parse.BoolOr("yes", FALSE), "parse.boolor")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Parse.IsInt(" -7 "), "parse.isint")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Parse.IsNum("3.14"), "parse.isnum")
Zanna.Core.Diagnostics.AssertEq(Zanna.Core.Parse.IntRadix("ff", 16, -1), 255, "parse.intradix")

DIM bInt AS OBJECT
DIM bNum AS OBJECT
DIM bBool AS OBJECT
DIM bStr AS OBJECT
bInt = Zanna.Core.Box.I64(123)
bNum = Zanna.Core.Box.F64(4.5)
bBool = Zanna.Core.Box.I1(1)
bStr = Zanna.Core.Box.Str("hi")

Zanna.Core.Diagnostics.AssertEq(Zanna.Core.Box.ToI64(bInt), 123, "box.toi64")
Zanna.Core.Diagnostics.AssertEqNum(Zanna.Core.Box.ToF64(bNum), 4.5, "box.tof64")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Box.ToI1(bBool), "box.toi1")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(bStr), "hi", "box.tostr")

Zanna.Core.Diagnostics.AssertEq(Zanna.Core.Box.Type(bInt), 0, "box.type.i64")
Zanna.Core.Diagnostics.AssertEq(Zanna.Core.Box.Type(bNum), 1, "box.type.f64")
Zanna.Core.Diagnostics.AssertEq(Zanna.Core.Box.Type(bBool), 2, "box.type.i1")
Zanna.Core.Diagnostics.AssertEq(Zanna.Core.Box.Type(bStr), 3, "box.type.str")

Zanna.Core.Diagnostics.Assert(Zanna.Core.Box.EqI64(bInt, 123), "box.eqi64")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Box.EqF64(bNum, 4.5), "box.eqf64")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Box.EqStr(bStr, "hi"), "box.eqstr")

PRINT "RESULT: ok"
END
