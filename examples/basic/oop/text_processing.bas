REM Example: Text processing using StringBuilder and File static methods
REM Note: Some BASIC keywords (APPEND, DELETE) may conflict with member names.
REM If your build treats these as reserved, use the procedural helpers as shown.

REM Variant A: Object members (may collide with keywords)
'USING Viper.System.IO
'
'DIM sb AS Viper.System.Text.StringBuilder
'sb = NEW Viper.System.Text.StringBuilder()
'
'sb.Append("hello")
'sb.Append(" ")
'sb.Append("world")
'sb.Append("!")
'
'DIM content AS STRING
'content = sb.ToString()
'
'File.WriteAllText("oop_text_proc.tmp", content)
'PRINT File.ReadAllText("oop_text_proc.tmp")
'File.Delete("oop_text_proc.tmp")

REM Variant B: Procedural helpers (portable across builds)
DIM sb AS Viper.System.Text.StringBuilder
sb = NEW Viper.System.Text.StringBuilder()
sb = Viper.Text.StringBuilder.Append(sb, "hello")
sb = Viper.Text.StringBuilder.Append(sb, " ")
sb = Viper.Text.StringBuilder.Append(sb, "world")
sb = Viper.Text.StringBuilder.Append(sb, "!")
PRINT sb.ToString()

REM Use Viper.IO.File.* helpers when available in your build
'Viper.IO.File.WriteAllText("oop_text_proc.tmp", sb.ToString())
'PRINT Viper.IO.File.ReadAllText("oop_text_proc.tmp")
'Viper.IO.File.Delete("oop_text_proc.tmp")

