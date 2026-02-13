' textwrapper_demo.bas
PRINT "=== Viper.Text.TextWrapper Demo ==="
PRINT Viper.Text.TextWrapper.Truncate("Hello World", 8)
PRINT Viper.Text.TextWrapper.Center("hi", 10)
PRINT Viper.Text.TextWrapper.Left("hi", 10)
PRINT Viper.Text.TextWrapper.Right("hi", 10)
PRINT Viper.Text.TextWrapper.LineCount("a" + CHR$(10) + "b" + CHR$(10) + "c")
PRINT "done"
END
