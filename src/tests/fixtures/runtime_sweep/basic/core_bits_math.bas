' EXPECT_OUT: RESULT: ok
' COVER: Zanna.Math.Bits.And
' COVER: Zanna.Math.Bits.Clear
' COVER: Zanna.Math.Bits.CountOnes
' COVER: Zanna.Math.Bits.Flip
' COVER: Zanna.Math.Bits.Get
' COVER: Zanna.Math.Bits.CountLeadingZeros
' COVER: Zanna.Math.Bits.Not
' COVER: Zanna.Math.Bits.Or
' COVER: Zanna.Math.Bits.RotateLeft
' COVER: Zanna.Math.Bits.RotateRight
' COVER: Zanna.Math.Bits.Set
' COVER: Zanna.Math.Bits.Shl
' COVER: Zanna.Math.Bits.Shr
' COVER: Zanna.Math.Bits.Swap
' COVER: Zanna.Math.Bits.Toggle
' COVER: Zanna.Math.Bits.CountTrailingZeros
' COVER: Zanna.Math.Bits.ShiftRightLogical
' COVER: Zanna.Math.Bits.Xor
' COVER: Zanna.Math.Euler
' COVER: Zanna.Math.Pi
' COVER: Zanna.Math.Tau
' COVER: Zanna.Math.Abs
' COVER: Zanna.Math.AbsInt
' COVER: Zanna.Math.Acos
' COVER: Zanna.Math.Asin
' COVER: Zanna.Math.Atan
' COVER: Zanna.Math.Atan2
' COVER: Zanna.Math.Ceil
' COVER: Zanna.Math.Clamp
' COVER: Zanna.Math.ClampInt
' COVER: Zanna.Math.Cos
' COVER: Zanna.Math.Cosh
' COVER: Zanna.Math.ToDegrees
' COVER: Zanna.Math.Exp
' COVER: Zanna.Math.FMod
' COVER: Zanna.Math.Floor
' COVER: Zanna.Math.Hypot
' COVER: Zanna.Math.Lerp
' COVER: Zanna.Math.Log
' COVER: Zanna.Math.Log10
' COVER: Zanna.Math.Log2
' COVER: Zanna.Math.Max
' COVER: Zanna.Math.MaxInt
' COVER: Zanna.Math.Min
' COVER: Zanna.Math.MinInt
' COVER: Zanna.Math.Pow
' COVER: Zanna.Math.ToRadians
' COVER: Zanna.Math.Round
' COVER: Zanna.Math.Sign
' COVER: Zanna.Math.SignInt
' COVER: Zanna.Math.Sin
' COVER: Zanna.Math.Sinh
' COVER: Zanna.Math.Sqrt
' COVER: Zanna.Math.Tan
' COVER: Zanna.Math.Tanh
' COVER: Zanna.Math.Truncate
' COVER: Zanna.Math.Wrap
' COVER: Zanna.Math.WrapInt
' COVER: Zanna.Math.Random.Seed
' COVER: Zanna.Math.Random.NextDouble
' COVER: Zanna.Math.Random.NextInt

SUB AssertApprox(actual AS DOUBLE, expected AS DOUBLE, eps AS DOUBLE, msg AS STRING)
    IF Zanna.Math.Abs(actual - expected) > eps THEN
        Zanna.Core.Diagnostics.Assert(FALSE, msg)
    END IF
END SUB

DIM a AS INTEGER
DIM b AS INTEGER
a = 6
b = 3
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.And(a, b), 2, "bits.and")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.Or(a, b), 7, "bits.or")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.Xor(a, b), 5, "bits.xor")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.Not(0), -1, "bits.not")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.Shl(1, 3), 8, "bits.shl")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.Shr(8, 2), 2, "bits.shr")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.ShiftRightLogical(8, 2), 2, "bits.ushr")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.Set(0, 1), 2, "bits.set")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.Clear(3, 0), 2, "bits.clear")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.Toggle(2, 1), 0, "bits.toggle")
Zanna.Core.Diagnostics.Assert(Zanna.Math.Bits.Get(2, 1), "bits.get")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.CountOnes(255), 8, "bits.count")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.CountLeadingZeros(1), 63, "bits.leadz")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.CountTrailingZeros(8), 3, "bits.trailz")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.Swap(72623859790382856), 578437695752307201, "bits.swap")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.RotateLeft(1, 1), 2, "bits.rotl")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.RotateRight(2, 1), 1, "bits.rotr")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.Bits.Flip(Zanna.Math.Bits.Flip(12345)), 12345, "bits.flip")

DIM eps AS DOUBLE
eps = 0.000001
AssertApprox(Zanna.Math.Pi, 3.14159265, 0.0001, "math.pi")
AssertApprox(Zanna.Math.Euler, 2.7182818, 0.0001, "math.e")
AssertApprox(Zanna.Math.Tau, 6.2831853, 0.0001, "math.tau")
AssertApprox(Zanna.Math.Abs(-1.5), 1.5, eps, "math.abs")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.AbsInt(-7), 7, "math.absint")
AssertApprox(Zanna.Math.Acos(1), 0, eps, "math.acos")
AssertApprox(Zanna.Math.Asin(0), 0, eps, "math.asin")
AssertApprox(Zanna.Math.Atan(0), 0, eps, "math.atan")
AssertApprox(Zanna.Math.Atan2(0, 1), 0, eps, "math.atan2")
AssertApprox(Zanna.Math.Ceil(1.2), 2, eps, "math.ceil")
AssertApprox(Zanna.Math.Clamp(5, 1, 3), 3, eps, "math.clamp")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.ClampInt(5, 1, 3), 3, "math.clampint")
AssertApprox(Zanna.Math.Cos(0), 1, eps, "math.cos")
AssertApprox(Zanna.Math.Cosh(0), 1, eps, "math.cosh")
AssertApprox(Zanna.Math.ToDegrees(Zanna.Math.Pi), 180, 0.01, "math.deg")
AssertApprox(Zanna.Math.Exp(1), Zanna.Math.Euler, 0.0001, "math.exp")
AssertApprox(Zanna.Math.FMod(5.5, 2), 1.5, eps, "math.fmod")
AssertApprox(Zanna.Math.Floor(1.9), 1, eps, "math.floor")
AssertApprox(Zanna.Math.Hypot(3, 4), 5, eps, "math.hypot")
AssertApprox(Zanna.Math.Lerp(0, 10, 0.5), 5, eps, "math.lerp")
AssertApprox(Zanna.Math.Log(Zanna.Math.Euler), 1, 0.0001, "math.log")
AssertApprox(Zanna.Math.Log10(100), 2, eps, "math.log10")
AssertApprox(Zanna.Math.Log2(8), 3, eps, "math.log2")
AssertApprox(Zanna.Math.Max(1, 2), 2, eps, "math.max")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.MaxInt(1, 2), 2, "math.maxint")
AssertApprox(Zanna.Math.Min(1, 2), 1, eps, "math.min")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.MinInt(1, 2), 1, "math.minint")
AssertApprox(Zanna.Math.Pow(2, 3), 8, eps, "math.pow")
AssertApprox(Zanna.Math.ToRadians(180), Zanna.Math.Pi, 0.0001, "math.rad")
AssertApprox(Zanna.Math.Round(1.6), 2, eps, "math.round")
AssertApprox(Zanna.Math.Sign(-3), -1, eps, "math.sgn")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.SignInt(-3), -1, "math.sgnint")
AssertApprox(Zanna.Math.Sin(0), 0, eps, "math.sin")
AssertApprox(Zanna.Math.Sinh(0), 0, eps, "math.sinh")
AssertApprox(Zanna.Math.Sqrt(9), 3, eps, "math.sqrt")
AssertApprox(Zanna.Math.Tan(0), 0, eps, "math.tan")
AssertApprox(Zanna.Math.Tanh(0), 0, eps, "math.tanh")
AssertApprox(Zanna.Math.Truncate(-2.7), -2, eps, "math.trunc")
AssertApprox(Zanna.Math.Wrap(370, 0, 360), 10, eps, "math.wrap")
Zanna.Core.Diagnostics.AssertEq(Zanna.Math.WrapInt(370, 0, 360), 10, "math.wrapint")

Zanna.Math.Random.Seed(123)
DIM r1 AS INTEGER
DIM r2 AS INTEGER
r1 = Zanna.Math.Random.NextInt(100)
r2 = Zanna.Math.Random.NextInt(100)
Zanna.Math.Random.Seed(123)
Zanna.Core.Diagnostics.AssertEq(r1, Zanna.Math.Random.NextInt(100), "rand.seq1")
Zanna.Core.Diagnostics.AssertEq(r2, Zanna.Math.Random.NextInt(100), "rand.seq2")
DIM rf AS DOUBLE
rf = Zanna.Math.Random.NextDouble()
Zanna.Core.Diagnostics.Assert(rf >= 0, "rand.next")

PRINT "RESULT: ok"
END
