' convert_demo.bas — Viper.Core.Convert + Viper.Core.Parse
PRINT "=== Viper.Core.Convert Demo ==="
PRINT Viper.Core.Convert.ToDouble("3.14")
PRINT Viper.Core.Convert.ToInt("42")
PRINT Viper.Core.Convert.ToInt64("100")
PRINT Viper.Core.Convert.ToString_Int(42)
PRINT Viper.Core.Convert.ToString_Double(3.14)
PRINT Viper.Core.Convert.NumToInt(3.9)

PRINT "--- Parsing ---"
PRINT Viper.Core.Parse.IntOr("abc", 99)
PRINT Viper.Core.Parse.IntOr("42", 99)
PRINT Viper.Core.Parse.NumOr("abc", 1.5)
PRINT Viper.Core.Parse.NumOr("2.7", 1.5)
PRINT Viper.Core.Parse.IsInt("42")
PRINT Viper.Core.Parse.IsInt("abc")
PRINT Viper.Core.Parse.IsNum("3.14")
PRINT Viper.Core.Parse.IsNum("xyz")
PRINT Viper.Core.Parse.IntRadix("ff", 16, 0)
PRINT Viper.Core.Parse.IntRadix("111", 2, 0)

PRINT "done"
END
