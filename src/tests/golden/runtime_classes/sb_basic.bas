10 DIM sb AS Viper.Text.StringBuilder
20 sb = NEW Viper.Text.StringBuilder()
30 sb = Viper.Text.StringBuilder.AppendLine(sb, "a")
40 sb = Viper.Text.StringBuilder.AppendLine(sb, "b")
50 PRINT sb.Length
60 DIM bytes AS Viper.Collections.Bytes
70 bytes = Viper.Collections.Bytes.FromStr(sb.ToString())
80 PRINT bytes.ToHex()
90 END
