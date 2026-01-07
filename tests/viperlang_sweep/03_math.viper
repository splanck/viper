module TestMath;

import "./_support";

// Comprehensive math tests

func start() {
    Viper.Terminal.Say("=== Math Tests ===");

    testBasicArithmetic();
    testTrigonometry();
    testExponential();
    testRounding();
    testMinMax();
    testClamp();
    testLerp();

    report();
}

func testBasicArithmetic() {
    // Abs
    assertApprox(Viper.Math.Abs(-5.5), 5.5, 0.001, "abs negative");
    assertApprox(Viper.Math.Abs(3.3), 3.3, 0.001, "abs positive");
    assertApprox(Viper.Math.Abs(0.0), 0.0, 0.001, "abs zero");

    // AbsInt
    assertEqInt(Viper.Math.AbsInt(-10), 10, "absint negative");
    assertEqInt(Viper.Math.AbsInt(7), 7, "absint positive");
    assertEqInt(Viper.Math.AbsInt(0), 0, "absint zero");

    // Sqrt
    assertApprox(Viper.Math.Sqrt(4.0), 2.0, 0.001, "sqrt 4");
    assertApprox(Viper.Math.Sqrt(9.0), 3.0, 0.001, "sqrt 9");
    assertApprox(Viper.Math.Sqrt(2.0), 1.414, 0.01, "sqrt 2");

    // Pow
    assertApprox(Viper.Math.Pow(2.0, 3.0), 8.0, 0.001, "pow 2^3");
    assertApprox(Viper.Math.Pow(10.0, 2.0), 100.0, 0.001, "pow 10^2");
    assertApprox(Viper.Math.Pow(2.0, 0.5), 1.414, 0.01, "pow 2^0.5");
}

func testTrigonometry() {
    // Sin - 0, pi/2, pi
    assertApprox(Viper.Math.Sin(0.0), 0.0, 0.001, "sin 0");
    assertApprox(Viper.Math.Sin(1.5707963), 1.0, 0.001, "sin pi/2");

    // Cos
    assertApprox(Viper.Math.Cos(0.0), 1.0, 0.001, "cos 0");
    assertApprox(Viper.Math.Cos(3.1415926), -1.0, 0.001, "cos pi");

    // Tan
    assertApprox(Viper.Math.Tan(0.0), 0.0, 0.001, "tan 0");
    assertApprox(Viper.Math.Tan(0.785398), 1.0, 0.01, "tan pi/4");

    // Asin, Acos, Atan
    assertApprox(Viper.Math.Asin(0.0), 0.0, 0.001, "asin 0");
    assertApprox(Viper.Math.Acos(1.0), 0.0, 0.001, "acos 1");
    assertApprox(Viper.Math.Atan(0.0), 0.0, 0.001, "atan 0");

    // Atan2
    assertApprox(Viper.Math.Atan2(1.0, 1.0), 0.785398, 0.001, "atan2 1,1");

    // Hyperbolic
    assertApprox(Viper.Math.Sinh(0.0), 0.0, 0.001, "sinh 0");
    assertApprox(Viper.Math.Cosh(0.0), 1.0, 0.001, "cosh 0");
    assertApprox(Viper.Math.Tanh(0.0), 0.0, 0.001, "tanh 0");
}

func testExponential() {
    // Exp
    assertApprox(Viper.Math.Exp(0.0), 1.0, 0.001, "exp 0");
    assertApprox(Viper.Math.Exp(1.0), 2.718, 0.01, "exp 1");

    // Log (natural)
    assertApprox(Viper.Math.Log(1.0), 0.0, 0.001, "log 1");
    assertApprox(Viper.Math.Log(2.718281828), 1.0, 0.001, "log e");

    // Log10
    assertApprox(Viper.Math.Log10(10.0), 1.0, 0.001, "log10 10");
    assertApprox(Viper.Math.Log10(100.0), 2.0, 0.001, "log10 100");
}

func testRounding() {
    // Floor
    assertApprox(Viper.Math.Floor(3.7), 3.0, 0.001, "floor 3.7");
    assertApprox(Viper.Math.Floor(-3.7), -4.0, 0.001, "floor -3.7");
    assertApprox(Viper.Math.Floor(3.0), 3.0, 0.001, "floor exact");

    // Ceil
    assertApprox(Viper.Math.Ceil(3.2), 4.0, 0.001, "ceil 3.2");
    assertApprox(Viper.Math.Ceil(-3.2), -3.0, 0.001, "ceil -3.2");
    assertApprox(Viper.Math.Ceil(3.0), 3.0, 0.001, "ceil exact");

    // Round
    assertApprox(Viper.Math.Round(3.4), 3.0, 0.001, "round down");
    assertApprox(Viper.Math.Round(3.6), 4.0, 0.001, "round up");
    assertApprox(Viper.Math.Round(3.5), 4.0, 0.001, "round half");

    // Trunc
    assertApprox(Viper.Math.Trunc(3.7), 3.0, 0.001, "trunc positive");
    assertApprox(Viper.Math.Trunc(-3.7), -3.0, 0.001, "trunc negative");
}

func testMinMax() {
    // Min
    assertApprox(Viper.Math.Min(3.0, 5.0), 3.0, 0.001, "min first smaller");
    assertApprox(Viper.Math.Min(7.0, 2.0), 2.0, 0.001, "min second smaller");
    assertApprox(Viper.Math.Min(4.0, 4.0), 4.0, 0.001, "min equal");

    // Max
    assertApprox(Viper.Math.Max(3.0, 5.0), 5.0, 0.001, "max second larger");
    assertApprox(Viper.Math.Max(7.0, 2.0), 7.0, 0.001, "max first larger");
    assertApprox(Viper.Math.Max(4.0, 4.0), 4.0, 0.001, "max equal");

    // MinInt/MaxInt
    assertEqInt(Viper.Math.MinInt(3, 5), 3, "minint");
    assertEqInt(Viper.Math.MaxInt(3, 5), 5, "maxint");
}

func testClamp() {
    // Normal clamping
    assertApprox(Viper.Math.Clamp(5.0, 0.0, 10.0), 5.0, 0.001, "clamp in range");
    assertApprox(Viper.Math.Clamp(-5.0, 0.0, 10.0), 0.0, 0.001, "clamp below");
    assertApprox(Viper.Math.Clamp(15.0, 0.0, 10.0), 10.0, 0.001, "clamp above");

    // ClampInt
    assertEqInt(Viper.Math.ClampInt(5, 0, 10), 5, "clampint in range");
    assertEqInt(Viper.Math.ClampInt(-5, 0, 10), 0, "clampint below");
    assertEqInt(Viper.Math.ClampInt(15, 0, 10), 10, "clampint above");

    // Inverted bounds (should swap)
    assertApprox(Viper.Math.Clamp(5.0, 10.0, 0.0), 5.0, 0.001, "clamp inverted in range");
    assertEqInt(Viper.Math.ClampInt(5, 10, 0), 5, "clampint inverted in range");
}

func testLerp() {
    // Linear interpolation
    assertApprox(Viper.Math.Lerp(0.0, 10.0, 0.0), 0.0, 0.001, "lerp t=0");
    assertApprox(Viper.Math.Lerp(0.0, 10.0, 1.0), 10.0, 0.001, "lerp t=1");
    assertApprox(Viper.Math.Lerp(0.0, 10.0, 0.5), 5.0, 0.001, "lerp t=0.5");
    assertApprox(Viper.Math.Lerp(10.0, 20.0, 0.25), 12.5, 0.001, "lerp t=0.25");
}
