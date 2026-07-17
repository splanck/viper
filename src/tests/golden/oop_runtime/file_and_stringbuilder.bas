REM Use procedural helpers to avoid BASIC keyword collisions and binding gaps

DIM sb AS Zanna.Text.StringBuilder
sb = NEW Zanna.Text.StringBuilder()

Zanna.IO.File.WriteAllText("tmp_oop.txt", "hi")

DIM s AS STRING
s = Zanna.IO.File.ReadAllText("tmp_oop.txt")

sb = Zanna.Text.StringBuilder.Append(sb, s)
sb = Zanna.Text.StringBuilder.Append(sb, "!")

PRINT sb.Length
PRINT sb.ToString()

REM Avoid DELETE keyword; leave file for cleanup by harness if needed
REM Zanna.IO.File.Delete("tmp_oop.txt")

END
