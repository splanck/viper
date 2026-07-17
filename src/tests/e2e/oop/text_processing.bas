REM Text processing demo: File + StringBuilder + String (procedural to avoid keyword collisions)

DIM sb AS Zanna.Text.StringBuilder
sb = NEW Zanna.Text.StringBuilder()

REM Build content using procedural helper to avoid APPEND keyword
sb = Zanna.Text.StringBuilder.Append(sb, "hello")
sb = Zanna.Text.StringBuilder.Append(sb, " ")
sb = Zanna.Text.StringBuilder.Append(sb, "world")
sb = Zanna.Text.StringBuilder.Append(sb, "!")

DIM content AS STRING
content = sb.ToString()

REM Write to a temp file, read back, and verify using Zanna.IO.File.*
Zanna.IO.File.WriteAllText("oop_text_proc.tmp", content)

DIM roundtrip AS STRING
roundtrip = Zanna.IO.File.ReadAllText("oop_text_proc.tmp")

REM Show content and its length
PRINT content
PRINT content.Length

REM Print roundtrip and its length
PRINT roundtrip
PRINT roundtrip.Length

REM Report file existence (no Delete to avoid DELETE keyword)
PRINT Zanna.IO.File.Exists("oop_text_proc.tmp")
