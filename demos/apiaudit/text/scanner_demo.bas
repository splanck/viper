' scanner_demo.bas
PRINT "=== Viper.Text.Scanner Demo ==="
DIM sc AS OBJECT
sc = NEW Viper.Text.Scanner("hello 123 world")
PRINT sc.Len
PRINT sc.Pos
PRINT sc.ReadIdent()
PRINT sc.SkipWhitespace()
PRINT sc.ReadInt()
PRINT sc.Remaining
PRINT sc.IsEnd
sc.Reset()
PRINT sc.Pos
PRINT "done"
END
