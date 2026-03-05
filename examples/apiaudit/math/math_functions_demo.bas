' =============================================================================
' API Audit: Viper.Math - Core Math Functions (BASIC)
' =============================================================================
' Tests: Pi, E, Tau, Abs, AbsInt, Sgn, SgnInt, Floor, Ceil, Round, Trunc,
'        Min, Max, MinInt, MaxInt, Clamp, ClampInt, Sqrt, Pow, Exp, Log,
'        Log10, Log2, Sin, Cos, Tan, Asin, Acos, Atan, Atan2, Sinh, Cosh,
'        Tanh, Hypot, FMod, Lerp, Deg, Rad, Wrap, WrapInt
' =============================================================================

PRINT "=== API Audit: Viper.Math Functions ==="

' --- Constants ---
PRINT "--- Constants ---"
PRINT "Pi: "; Viper.Math.Pi
PRINT "E: "; Viper.Math.E
PRINT "Tau: "; Viper.Math.Tau

' --- Abs / Sign ---
PRINT "--- Abs / Sign ---"
PRINT "Abs(-5.0): "; Viper.Math.Abs(-5.0)
PRINT "Abs(0.0): "; Viper.Math.Abs(0.0)
PRINT "Abs(3.14): "; Viper.Math.Abs(3.14)
PRINT "AbsInt(-42): "; Viper.Math.AbsInt(-42)
PRINT "AbsInt(0): "; Viper.Math.AbsInt(0)
PRINT "AbsInt(7): "; Viper.Math.AbsInt(7)
PRINT "Sgn(-3.0): "; Viper.Math.Sgn(-3.0)
PRINT "Sgn(0.0): "; Viper.Math.Sgn(0.0)
PRINT "Sgn(7.0): "; Viper.Math.Sgn(7.0)
PRINT "SgnInt(-100): "; Viper.Math.SgnInt(-100)
PRINT "SgnInt(0): "; Viper.Math.SgnInt(0)
PRINT "SgnInt(42): "; Viper.Math.SgnInt(42)

' --- Rounding ---
PRINT "--- Rounding ---"
PRINT "Floor(3.7): "; Viper.Math.Floor(3.7)
PRINT "Floor(-3.7): "; Viper.Math.Floor(-3.7)
PRINT "Floor(4.0): "; Viper.Math.Floor(4.0)
PRINT "Ceil(3.2): "; Viper.Math.Ceil(3.2)
PRINT "Ceil(-3.2): "; Viper.Math.Ceil(-3.2)
PRINT "Ceil(4.0): "; Viper.Math.Ceil(4.0)
PRINT "Round(3.5): "; Viper.Math.Round(3.5)
PRINT "Round(3.4): "; Viper.Math.Round(3.4)
PRINT "Round(-2.5): "; Viper.Math.Round(-2.5)
PRINT "Trunc(3.9): "; Viper.Math.Trunc(3.9)
PRINT "Trunc(-3.9): "; Viper.Math.Trunc(-3.9)
PRINT "Trunc(4.0): "; Viper.Math.Trunc(4.0)

' --- Min / Max ---
PRINT "--- Min / Max ---"
PRINT "Min(3.0, 7.0): "; Viper.Math.Min(3.0, 7.0)
PRINT "Min(-1.0, -5.0): "; Viper.Math.Min(-1.0, -5.0)
PRINT "Min(0.0, 0.0): "; Viper.Math.Min(0.0, 0.0)
PRINT "Max(3.0, 7.0): "; Viper.Math.Max(3.0, 7.0)
PRINT "Max(-1.0, -5.0): "; Viper.Math.Max(-1.0, -5.0)
PRINT "Max(0.0, 0.0): "; Viper.Math.Max(0.0, 0.0)
PRINT "MinInt(3, 7): "; Viper.Math.MinInt(3, 7)
PRINT "MinInt(-10, 5): "; Viper.Math.MinInt(-10, 5)
PRINT "MaxInt(3, 7): "; Viper.Math.MaxInt(3, 7)
PRINT "MaxInt(-10, 5): "; Viper.Math.MaxInt(-10, 5)

' --- Clamp ---
PRINT "--- Clamp ---"
PRINT "Clamp(10.0, 0.0, 5.0): "; Viper.Math.Clamp(10.0, 0.0, 5.0)
PRINT "Clamp(-1.0, 0.0, 5.0): "; Viper.Math.Clamp(-1.0, 0.0, 5.0)
PRINT "Clamp(3.0, 0.0, 5.0): "; Viper.Math.Clamp(3.0, 0.0, 5.0)
PRINT "ClampInt(10, 0, 5): "; Viper.Math.ClampInt(10, 0, 5)
PRINT "ClampInt(-3, 0, 5): "; Viper.Math.ClampInt(-3, 0, 5)
PRINT "ClampInt(3, 0, 5): "; Viper.Math.ClampInt(3, 0, 5)

' --- Powers / Roots / Logs ---
PRINT "--- Powers / Roots / Logs ---"
PRINT "Sqrt(16.0): "; Viper.Math.Sqrt(16.0)
PRINT "Sqrt(2.0): "; Viper.Math.Sqrt(2.0)
PRINT "Sqrt(0.0): "; Viper.Math.Sqrt(0.0)
PRINT "Pow(2.0, 10.0): "; Viper.Math.Pow(2.0, 10.0)
PRINT "Pow(3.0, 3.0): "; Viper.Math.Pow(3.0, 3.0)
PRINT "Pow(10.0, 0.0): "; Viper.Math.Pow(10.0, 0.0)
PRINT "Exp(0.0): "; Viper.Math.Exp(0.0)
PRINT "Exp(1.0): "; Viper.Math.Exp(1.0)
PRINT "Log(1.0): "; Viper.Math.Log(1.0)
PRINT "Log(2.718281828): "; Viper.Math.Log(2.718281828)
PRINT "Log10(100.0): "; Viper.Math.Log10(100.0)
PRINT "Log10(1000.0): "; Viper.Math.Log10(1000.0)
PRINT "Log2(8.0): "; Viper.Math.Log2(8.0)
PRINT "Log2(1.0): "; Viper.Math.Log2(1.0)

' --- Trigonometry ---
PRINT "--- Trigonometry ---"
PRINT "Sin(0.0): "; Viper.Math.Sin(0.0)
PRINT "Sin(Pi/2): "; Viper.Math.Sin(Viper.Math.Pi / 2.0)
PRINT "Cos(0.0): "; Viper.Math.Cos(0.0)
PRINT "Cos(Pi): "; Viper.Math.Cos(Viper.Math.Pi)
PRINT "Tan(0.0): "; Viper.Math.Tan(0.0)
PRINT "Tan(Pi/4): "; Viper.Math.Tan(Viper.Math.Pi / 4.0)
PRINT "Asin(0.0): "; Viper.Math.Asin(0.0)
PRINT "Asin(1.0): "; Viper.Math.Asin(1.0)
PRINT "Acos(1.0): "; Viper.Math.Acos(1.0)
PRINT "Acos(0.0): "; Viper.Math.Acos(0.0)
PRINT "Atan(0.0): "; Viper.Math.Atan(0.0)
PRINT "Atan(1.0): "; Viper.Math.Atan(1.0)
PRINT "Atan2(1.0, 1.0): "; Viper.Math.Atan2(1.0, 1.0)
PRINT "Atan2(0.0, 1.0): "; Viper.Math.Atan2(0.0, 1.0)
PRINT "Atan2(1.0, 0.0): "; Viper.Math.Atan2(1.0, 0.0)

' --- Hyperbolic ---
PRINT "--- Hyperbolic ---"
PRINT "Sinh(0.0): "; Viper.Math.Sinh(0.0)
PRINT "Sinh(1.0): "; Viper.Math.Sinh(1.0)
PRINT "Cosh(0.0): "; Viper.Math.Cosh(0.0)
PRINT "Cosh(1.0): "; Viper.Math.Cosh(1.0)
PRINT "Tanh(0.0): "; Viper.Math.Tanh(0.0)
PRINT "Tanh(1.0): "; Viper.Math.Tanh(1.0)

' --- Misc ---
PRINT "--- Misc ---"
PRINT "Hypot(3.0, 4.0): "; Viper.Math.Hypot(3.0, 4.0)
PRINT "Hypot(5.0, 12.0): "; Viper.Math.Hypot(5.0, 12.0)
PRINT "FMod(7.0, 3.0): "; Viper.Math.FMod(7.0, 3.0)
PRINT "FMod(10.5, 3.0): "; Viper.Math.FMod(10.5, 3.0)
PRINT "Lerp(0.0, 10.0, 0.0): "; Viper.Math.Lerp(0.0, 10.0, 0.0)
PRINT "Lerp(0.0, 10.0, 0.5): "; Viper.Math.Lerp(0.0, 10.0, 0.5)
PRINT "Lerp(0.0, 10.0, 1.0): "; Viper.Math.Lerp(0.0, 10.0, 1.0)
PRINT "Deg(3.14159265358979): "; Viper.Math.Deg(3.14159265358979)
PRINT "Deg(0.0): "; Viper.Math.Deg(0.0)
PRINT "Rad(180.0): "; Viper.Math.Rad(180.0)
PRINT "Rad(90.0): "; Viper.Math.Rad(90.0)
PRINT "Rad(0.0): "; Viper.Math.Rad(0.0)
PRINT "Wrap(5.0, 0.0, 3.0): "; Viper.Math.Wrap(5.0, 0.0, 3.0)
PRINT "Wrap(-1.0, 0.0, 3.0): "; Viper.Math.Wrap(-1.0, 0.0, 3.0)
PRINT "Wrap(2.0, 0.0, 3.0): "; Viper.Math.Wrap(2.0, 0.0, 3.0)
PRINT "WrapInt(5, 0, 3): "; Viper.Math.WrapInt(5, 0, 3)
PRINT "WrapInt(-1, 0, 3): "; Viper.Math.WrapInt(-1, 0, 3)
PRINT "WrapInt(2, 0, 3): "; Viper.Math.WrapInt(2, 0, 3)

PRINT "=== Math Functions Audit Complete ==="
END
