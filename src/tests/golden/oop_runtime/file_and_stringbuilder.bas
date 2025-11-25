REM Use procedural helpers to avoid BASIC keyword collisions and binding gaps

DIM sb AS Viper.System.Text.StringBuilder
sb = NEW Viper.System.Text.StringBuilder()

Viper.IO.File.WriteAllText("tmp_oop.txt", "hi")

DIM s AS STRING
s = Viper.IO.File.ReadAllText("tmp_oop.txt")

sb = Viper.Text.StringBuilder.Append(sb, s)
sb = Viper.Text.StringBuilder.Append(sb, "!")

PRINT sb.Length
PRINT sb.ToString()

REM Avoid DELETE keyword; leave file for cleanup by harness if needed
REM Viper.IO.File.Delete("tmp_oop.txt")

END
