' EXPECT_OUT: RESULT: ok
' COVER: Viper.Core.Convert.ToDouble
' COVER: Viper.Core.Convert.ToInt64
' COVER: Viper.Core.Convert.ToString_Double
' COVER: Viper.Core.Convert.ToString_Int
' COVER: Viper.Text.Fmt.Bin
' COVER: Viper.Text.Fmt.Bool
' COVER: Viper.Text.Fmt.BoolYN
' COVER: Viper.Text.Fmt.Hex
' COVER: Viper.Text.Fmt.HexPad
' COVER: Viper.Text.Fmt.Int
' COVER: Viper.Text.Fmt.IntPad
' COVER: Viper.Text.Fmt.IntRadix
' COVER: Viper.Text.Fmt.Num
' COVER: Viper.Text.Fmt.NumFixed
' COVER: Viper.Text.Fmt.NumPct
' COVER: Viper.Text.Fmt.NumSci
' COVER: Viper.Text.Fmt.Oct
' COVER: Viper.Text.Fmt.Size

SUB AssertApprox(actual AS DOUBLE, expected AS DOUBLE, eps AS DOUBLE, msg AS STRING)
    IF Viper.Math.Abs(actual - expected) > eps THEN
        Viper.Core.Diagnostics.Assert(FALSE, msg)
    END IF
END SUB

DIM d AS DOUBLE
d = Viper.Core.Convert.ToDouble("3.5")
AssertApprox(d, 3.5, 0.000001, "conv.double")
Viper.Core.Diagnostics.AssertEq(Viper.Core.Convert.ToInt64("42"), 42, "conv.int")
Viper.Core.Diagnostics.AssertEqStr(Viper.Core.Convert.ToString_Int(42), "42", "conv.toStrInt")
Viper.Core.Diagnostics.AssertEqStr(Viper.Core.Convert.ToString_Double(2.5), "2.5", "conv.toStrDouble")

Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.Int(12345), "12345", "fmt.int")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.IntRadix(255, 16), "ff", "fmt.radix")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.IntPad(42, 5, "0"), "00042", "fmt.intpad")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.Num(3.14159), "3.14159", "fmt.num")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.NumFixed(3.14159, 2), "3.14", "fmt.numfixed")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.NumSci(1234.5, 2), "1.23e+03", "fmt.numsci")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.NumPct(0.756, 1), "75.6%", "fmt.numpct")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.Bool(TRUE), "true", "fmt.bool")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.BoolYN(FALSE), "No", "fmt.boolyn")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.Size(1024), "1.0 KB", "fmt.size")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.Hex(255), "ff", "fmt.hex")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.HexPad(255, 4), "00ff", "fmt.hexpad")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.Bin(10), "1010", "fmt.bin")
Viper.Core.Diagnostics.AssertEqStr(Viper.Text.Fmt.Oct(64), "100", "fmt.oct")

PRINT "RESULT: ok"
END
