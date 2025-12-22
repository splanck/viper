' EXPECT_OUT: RESULT: ok
' COVER: Viper.Bits.And
' COVER: Viper.Bits.Clear
' COVER: Viper.Bits.Count
' COVER: Viper.Bits.Flip
' COVER: Viper.Bits.Get
' COVER: Viper.Bits.LeadZ
' COVER: Viper.Bits.Not
' COVER: Viper.Bits.Or
' COVER: Viper.Bits.Rotl
' COVER: Viper.Bits.Rotr
' COVER: Viper.Bits.Set
' COVER: Viper.Bits.Shl
' COVER: Viper.Bits.Shr
' COVER: Viper.Bits.Swap
' COVER: Viper.Bits.Toggle
' COVER: Viper.Bits.TrailZ
' COVER: Viper.Bits.Ushr
' COVER: Viper.Bits.Xor
' COVER: Viper.Math.E
' COVER: Viper.Math.Pi
' COVER: Viper.Math.Tau
' COVER: Viper.Math.Abs
' COVER: Viper.Math.AbsInt
' COVER: Viper.Math.Acos
' COVER: Viper.Math.Asin
' COVER: Viper.Math.Atan
' COVER: Viper.Math.Atan2
' COVER: Viper.Math.Ceil
' COVER: Viper.Math.Clamp
' COVER: Viper.Math.ClampInt
' COVER: Viper.Math.Cos
' COVER: Viper.Math.Cosh
' COVER: Viper.Math.Deg
' COVER: Viper.Math.Exp
' COVER: Viper.Math.FMod
' COVER: Viper.Math.Floor
' COVER: Viper.Math.Hypot
' COVER: Viper.Math.Lerp
' COVER: Viper.Math.Log
' COVER: Viper.Math.Log10
' COVER: Viper.Math.Log2
' COVER: Viper.Math.Max
' COVER: Viper.Math.MaxInt
' COVER: Viper.Math.Min
' COVER: Viper.Math.MinInt
' COVER: Viper.Math.Pow
' COVER: Viper.Math.Rad
' COVER: Viper.Math.Round
' COVER: Viper.Math.Sgn
' COVER: Viper.Math.SgnInt
' COVER: Viper.Math.Sin
' COVER: Viper.Math.Sinh
' COVER: Viper.Math.Sqrt
' COVER: Viper.Math.Tan
' COVER: Viper.Math.Tanh
' COVER: Viper.Math.Trunc
' COVER: Viper.Math.Wrap
' COVER: Viper.Math.WrapInt
' COVER: Viper.Random.Seed
' COVER: Viper.Random.Next
' COVER: Viper.Random.NextInt

SUB AssertApprox(actual AS DOUBLE, expected AS DOUBLE, eps AS DOUBLE, msg AS STRING)
    IF Viper.Math.Abs(actual - expected) > eps THEN
        Viper.Diagnostics.Assert(FALSE, msg)
    END IF
END SUB

DIM a AS INTEGER
DIM b AS INTEGER
a = 6
b = 3
Viper.Diagnostics.AssertEq(Viper.Bits.And(a, b), 2, "bits.and")
Viper.Diagnostics.AssertEq(Viper.Bits.Or(a, b), 7, "bits.or")
Viper.Diagnostics.AssertEq(Viper.Bits.Xor(a, b), 5, "bits.xor")
Viper.Diagnostics.AssertEq(Viper.Bits.Not(0), -1, "bits.not")
Viper.Diagnostics.AssertEq(Viper.Bits.Shl(1, 3), 8, "bits.shl")
Viper.Diagnostics.AssertEq(Viper.Bits.Shr(8, 2), 2, "bits.shr")
Viper.Diagnostics.AssertEq(Viper.Bits.Ushr(8, 2), 2, "bits.ushr")
Viper.Diagnostics.AssertEq(Viper.Bits.Set(0, 1), 2, "bits.set")
Viper.Diagnostics.AssertEq(Viper.Bits.Clear(3, 0), 2, "bits.clear")
Viper.Diagnostics.AssertEq(Viper.Bits.Toggle(2, 1), 0, "bits.toggle")
Viper.Diagnostics.Assert(Viper.Bits.Get(2, 1), "bits.get")
Viper.Diagnostics.AssertEq(Viper.Bits.Count(255), 8, "bits.count")
Viper.Diagnostics.AssertEq(Viper.Bits.LeadZ(1), 63, "bits.leadz")
Viper.Diagnostics.AssertEq(Viper.Bits.TrailZ(8), 3, "bits.trailz")
Viper.Diagnostics.AssertEq(Viper.Bits.Swap(72623859790382856), 578437695752307201, "bits.swap")
Viper.Diagnostics.AssertEq(Viper.Bits.Rotl(1, 1), 2, "bits.rotl")
Viper.Diagnostics.AssertEq(Viper.Bits.Rotr(2, 1), 1, "bits.rotr")
Viper.Diagnostics.AssertEq(Viper.Bits.Flip(Viper.Bits.Flip(12345)), 12345, "bits.flip")

DIM eps AS DOUBLE
eps = 0.000001
AssertApprox(Viper.Math.Pi, 3.14159265, 0.0001, "math.pi")
AssertApprox(Viper.Math.E, 2.7182818, 0.0001, "math.e")
AssertApprox(Viper.Math.Tau, 6.2831853, 0.0001, "math.tau")
AssertApprox(Viper.Math.Abs(-1.5), 1.5, eps, "math.abs")
Viper.Diagnostics.AssertEq(Viper.Math.AbsInt(-7), 7, "math.absint")
AssertApprox(Viper.Math.Acos(1), 0, eps, "math.acos")
AssertApprox(Viper.Math.Asin(0), 0, eps, "math.asin")
AssertApprox(Viper.Math.Atan(0), 0, eps, "math.atan")
AssertApprox(Viper.Math.Atan2(0, 1), 0, eps, "math.atan2")
AssertApprox(Viper.Math.Ceil(1.2), 2, eps, "math.ceil")
AssertApprox(Viper.Math.Clamp(5, 1, 3), 3, eps, "math.clamp")
Viper.Diagnostics.AssertEq(Viper.Math.ClampInt(5, 1, 3), 3, "math.clampint")
AssertApprox(Viper.Math.Cos(0), 1, eps, "math.cos")
AssertApprox(Viper.Math.Cosh(0), 1, eps, "math.cosh")
AssertApprox(Viper.Math.Deg(Viper.Math.Pi), 180, 0.01, "math.deg")
AssertApprox(Viper.Math.Exp(1), Viper.Math.E, 0.0001, "math.exp")
AssertApprox(Viper.Math.FMod(5.5, 2), 1.5, eps, "math.fmod")
AssertApprox(Viper.Math.Floor(1.9), 1, eps, "math.floor")
AssertApprox(Viper.Math.Hypot(3, 4), 5, eps, "math.hypot")
AssertApprox(Viper.Math.Lerp(0, 10, 0.5), 5, eps, "math.lerp")
AssertApprox(Viper.Math.Log(Viper.Math.E), 1, 0.0001, "math.log")
AssertApprox(Viper.Math.Log10(100), 2, eps, "math.log10")
AssertApprox(Viper.Math.Log2(8), 3, eps, "math.log2")
AssertApprox(Viper.Math.Max(1, 2), 2, eps, "math.max")
Viper.Diagnostics.AssertEq(Viper.Math.MaxInt(1, 2), 2, "math.maxint")
AssertApprox(Viper.Math.Min(1, 2), 1, eps, "math.min")
Viper.Diagnostics.AssertEq(Viper.Math.MinInt(1, 2), 1, "math.minint")
AssertApprox(Viper.Math.Pow(2, 3), 8, eps, "math.pow")
AssertApprox(Viper.Math.Rad(180), Viper.Math.Pi, 0.0001, "math.rad")
AssertApprox(Viper.Math.Round(1.6), 2, eps, "math.round")
AssertApprox(Viper.Math.Sgn(-3), -1, eps, "math.sgn")
Viper.Diagnostics.AssertEq(Viper.Math.SgnInt(-3), -1, "math.sgnint")
AssertApprox(Viper.Math.Sin(0), 0, eps, "math.sin")
AssertApprox(Viper.Math.Sinh(0), 0, eps, "math.sinh")
AssertApprox(Viper.Math.Sqrt(9), 3, eps, "math.sqrt")
AssertApprox(Viper.Math.Tan(0), 0, eps, "math.tan")
AssertApprox(Viper.Math.Tanh(0), 0, eps, "math.tanh")
AssertApprox(Viper.Math.Trunc(-2.7), -2, eps, "math.trunc")
AssertApprox(Viper.Math.Wrap(370, 0, 360), 10, eps, "math.wrap")
Viper.Diagnostics.AssertEq(Viper.Math.WrapInt(370, 0, 360), 10, "math.wrapint")

Viper.Random.Seed(123)
DIM r1 AS INTEGER
DIM r2 AS INTEGER
r1 = Viper.Random.NextInt(100)
r2 = Viper.Random.NextInt(100)
Viper.Random.Seed(123)
Viper.Diagnostics.AssertEq(r1, Viper.Random.NextInt(100), "rand.seq1")
Viper.Diagnostics.AssertEq(r2, Viper.Random.NextInt(100), "rand.seq2")
DIM rf AS DOUBLE
rf = Viper.Random.Next()
Viper.Diagnostics.Assert(rf >= 0, "rand.next")

PRINT "RESULT: ok"
END
