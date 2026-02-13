' stringbuilder_demo.bas
PRINT "=== Viper.Text.StringBuilder Demo ==="
DIM sb AS OBJECT
sb = NEW Viper.Text.StringBuilder()
sb.Append("Hello")
sb.Append(" ")
sb.Append("World")
PRINT sb.Length
PRINT sb.ToString()
sb.Clear()
PRINT sb.Length
PRINT "done"
END
