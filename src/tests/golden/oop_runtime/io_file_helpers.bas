REM BASIC: Verify Zanna.IO.File high-level helpers

DIM p1 AS STRING
p1 = "tmp_io_file_appendline.txt"
Zanna.IO.File.WriteAllText(p1, "")

Zanna.IO.File.AppendLine(p1, "a")
Zanna.IO.File.AppendLine(p1, "b")

DIM b1 AS Zanna.Collections.Bytes
b1 = Zanna.IO.File.ReadAllBytes(p1)
PRINT b1.ToHex()

DIM p2 AS STRING
p2 = "tmp_io_file_lines.bin"

DIM crlf AS STRING
crlf = CHR$(13) + CHR$(10)

DIM content AS STRING
content = "one" + crlf + "two" + CHR$(10) + "three" + crlf + "four"

DIM b2 AS Zanna.Collections.Bytes
b2 = Zanna.Collections.Bytes.FromStr(content)
Zanna.IO.File.WriteAllBytes(p2, b2)

DIM b3 AS Zanna.Collections.Bytes
b3 = Zanna.IO.File.ReadAllBytes(p2)
PRINT b3.ToHex()

DIM lines AS Zanna.Collections.Seq
lines = Zanna.IO.File.ReadAllLines(p2)
PRINT lines.Count
PRINT Zanna.String.Join("|", lines)
