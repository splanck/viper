REM BASIC: Verify Viper.IO.File high-level helpers

DIM p1 AS STRING
p1 = "tmp_io_file_appendline.txt"
Viper.IO.File.WriteAllText(p1, "")

Viper.IO.File.AppendLine(p1, "a")
Viper.IO.File.AppendLine(p1, "b")

DIM b1 AS Viper.Collections.Bytes
b1 = Viper.IO.File.ReadAllBytes(p1)
PRINT b1.ToHex()

DIM p2 AS STRING
p2 = "tmp_io_file_lines.bin"

DIM crlf AS STRING
crlf = CHR$(13) + CHR$(10)

DIM content AS STRING
content = "one" + crlf + "two" + CHR$(10) + "three" + crlf + "four"

DIM b2 AS Viper.Collections.Bytes
b2 = Viper.Collections.Bytes.FromStr(content)
Viper.IO.File.WriteAllBytes(p2, b2)

DIM b3 AS Viper.Collections.Bytes
b3 = Viper.IO.File.ReadAllBytes(p2)
PRINT b3.ToHex()

DIM lines AS Viper.Collections.Seq
lines = Viper.IO.File.ReadAllLines(p2)
PRINT lines.Len
PRINT Viper.Strings.Join("|", lines)
