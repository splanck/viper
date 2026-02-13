' compiledpattern_demo.bas
PRINT "=== Viper.Text.CompiledPattern Demo ==="
DIM p AS OBJECT
p = NEW Viper.Text.CompiledPattern("[0-9]+")
PRINT p.Pattern
PRINT p.IsMatch("abc123")
PRINT p.IsMatch("abcdef")
PRINT p.Find("abc123def")
PRINT p.FindPos("abc123def")
PRINT p.Replace("a1b2c3", "X")
PRINT p.ReplaceFirst("a1b2c3", "X")
PRINT "done"
END
