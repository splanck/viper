10 DIM sb AS Zanna.Text.StringBuilder
20 sb = NEW Zanna.Text.StringBuilder()
30 sb = Zanna.Text.StringBuilder.AppendLine(sb, "a")
40 sb = Zanna.Text.StringBuilder.AppendLine(sb, "b")
50 PRINT sb.Length
60 DIM bytes AS Zanna.Collections.Bytes
70 bytes = Zanna.Collections.Bytes.FromStr(sb.ToString())
80 PRINT bytes.ToHex()
90 END
