' math_demo.bas — Viper.Math
PRINT "=== Viper.Math Demo ==="

PRINT "--- Constants ---"
PRINT Viper.Math.Pi
PRINT Viper.Math.E
PRINT Viper.Math.Tau

PRINT "--- Abs/Sign ---"
PRINT Viper.Math.Abs(-5.0)
PRINT Viper.Math.AbsInt(-42)
PRINT Viper.Math.Sgn(-3.0)
PRINT Viper.Math.Sgn(0.0)
PRINT Viper.Math.Sgn(7.0)
PRINT Viper.Math.SgnInt(-3)
PRINT Viper.Math.SgnInt(0)
PRINT Viper.Math.SgnInt(3)

PRINT "--- Rounding ---"
PRINT Viper.Math.Floor(3.7)
PRINT Viper.Math.Ceil(3.2)
PRINT Viper.Math.Round(3.5)
PRINT Viper.Math.Trunc(3.9)

PRINT "--- Min/Max/Clamp ---"
PRINT Viper.Math.Min(3.0, 7.0)
PRINT Viper.Math.Max(3.0, 7.0)
PRINT Viper.Math.MinInt(3, 7)
PRINT Viper.Math.MaxInt(3, 7)
PRINT Viper.Math.Clamp(10.0, 0.0, 5.0)
PRINT Viper.Math.Clamp(-1.0, 0.0, 5.0)
PRINT Viper.Math.ClampInt(10, 0, 5)

PRINT "--- Powers/Roots/Logs ---"
PRINT Viper.Math.Sqrt(16.0)
PRINT Viper.Math.Pow(2.0, 10.0)
PRINT Viper.Math.Exp(1.0)
PRINT Viper.Math.Log(1.0)
PRINT Viper.Math.Log10(100.0)
PRINT Viper.Math.Log2(8.0)

PRINT "--- Trig ---"
PRINT Viper.Math.Sin(0.0)
PRINT Viper.Math.Cos(0.0)
PRINT Viper.Math.Tan(0.0)
PRINT Viper.Math.Asin(0.0)
PRINT Viper.Math.Acos(1.0)
PRINT Viper.Math.Atan(0.0)
PRINT Viper.Math.Atan2(1.0, 1.0)

PRINT "--- Hyperbolic ---"
PRINT Viper.Math.Sinh(0.0)
PRINT Viper.Math.Cosh(0.0)
PRINT Viper.Math.Tanh(0.0)

PRINT "--- Misc ---"
PRINT Viper.Math.Hypot(3.0, 4.0)
PRINT Viper.Math.FMod(7.0, 3.0)
PRINT Viper.Math.Lerp(0.0, 10.0, 0.5)
PRINT Viper.Math.Deg(3.14159265358979)
PRINT Viper.Math.Rad(180.0)
PRINT Viper.Math.Wrap(5.0, 0.0, 3.0)
PRINT Viper.Math.WrapInt(5, 0, 3)

PRINT "done"
END
