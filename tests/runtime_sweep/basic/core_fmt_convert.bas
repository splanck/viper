' EXPECT_OUT: RESULT: ok
' COVER: Viper.Convert.ToDouble
' COVER: Viper.Convert.ToInt64
' COVER: Viper.Convert.ToString_Double
' COVER: Viper.Convert.ToString_Int
' COVER: Viper.Fmt.Bin
' COVER: Viper.Fmt.Bool
' COVER: Viper.Fmt.BoolYN
' COVER: Viper.Fmt.Hex
' COVER: Viper.Fmt.HexPad
' COVER: Viper.Fmt.Int
' COVER: Viper.Fmt.IntPad
' COVER: Viper.Fmt.IntRadix
' COVER: Viper.Fmt.Num
' COVER: Viper.Fmt.NumFixed
' COVER: Viper.Fmt.NumPct
' COVER: Viper.Fmt.NumSci
' COVER: Viper.Fmt.Oct
' COVER: Viper.Fmt.Size

SUB AssertApprox(actual AS DOUBLE, expected AS DOUBLE, eps AS DOUBLE, msg AS STRING)
    IF Viper.Math.Abs(actual - expected) > eps THEN
        Viper.Diagnostics.Assert(0, msg)
    END IF
END SUB

DIM d AS DOUBLE
d = Viper.Convert.ToDouble("3.5")
AssertApprox(d, 3.5, 0.000001, "conv.double")
Viper.Diagnostics.AssertEq(Viper.Convert.ToInt64("42"), 42, "conv.int")
Viper.Diagnostics.AssertEqStr(Viper.Convert.ToString_Int(42), "42", "conv.toStrInt")
Viper.Diagnostics.AssertEqStr(Viper.Convert.ToString_Double(2.5), "2.5", "conv.toStrDouble")

Viper.Diagnostics.AssertEqStr(Viper.Fmt.Int(12345), "12345", "fmt.int")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.IntRadix(255, 16), "ff", "fmt.radix")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.IntPad(42, 5, "0"), "00042", "fmt.intpad")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.Num(3.14159), "3.14159", "fmt.num")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.NumFixed(3.14159, 2), "3.14", "fmt.numfixed")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.NumSci(1234.5, 2), "1.23e+03", "fmt.numsci")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.NumPct(0.756, 1), "75.6%", "fmt.numpct")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.Bool(1), "true", "fmt.bool")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.BoolYN(0), "No", "fmt.boolyn")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.Size(1024), "1 KB", "fmt.size")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.Hex(255), "ff", "fmt.hex")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.HexPad(255, 4), "00ff", "fmt.hexpad")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.Bin(10), "1010", "fmt.bin")
Viper.Diagnostics.AssertEqStr(Viper.Fmt.Oct(64), "100", "fmt.oct")

PRINT "RESULT: ok"
END
