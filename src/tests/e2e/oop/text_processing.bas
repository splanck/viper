REM Text processing demo: File + StringBuilder + String (procedural to avoid keyword collisions)

DIM sb AS Viper.System.Text.StringBuilder
sb = NEW Viper.System.Text.StringBuilder()

REM Build content using procedural helper to avoid APPEND keyword
sb = Viper.Text.StringBuilder.Append(sb, "hello")
sb = Viper.Text.StringBuilder.Append(sb, " ")
sb = Viper.Text.StringBuilder.Append(sb, "world")
sb = Viper.Text.StringBuilder.Append(sb, "!")

DIM content AS STRING
content = sb.ToString()

REM Write to a temp file, read back, and verify using Viper.IO.File.*
Viper.IO.File.WriteAllText("oop_text_proc.tmp", content)

DIM roundtrip AS STRING
roundtrip = Viper.IO.File.ReadAllText("oop_text_proc.tmp")

REM Show content and its length
PRINT content
PRINT content.Length

REM Print roundtrip and its length
PRINT roundtrip
PRINT roundtrip.Length

REM Report file existence (no Delete to avoid DELETE keyword)
PRINT Viper.IO.File.Exists("oop_text_proc.tmp")
