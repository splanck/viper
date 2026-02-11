' test_text_codec.bas — Codec, Pattern, CompiledPattern, StringBuilder, TextWrapper, Pluralize, Diff
PRINT "b64enc: "; Viper.Text.Codec.Base64Enc("hello world")
PRINT "b64dec: "; Viper.Text.Codec.Base64Dec("aGVsbG8gd29ybGQ=")
PRINT "hexenc: "; Viper.Text.Codec.HexEnc("AB")
PRINT "hexdec: "; Viper.Text.Codec.HexDec("4142")
PRINT "urlenc: "; Viper.Text.Codec.UrlEncode("hello world&foo=bar")
PRINT "urldec: "; Viper.Text.Codec.UrlDecode("hello%20world%26foo%3Dbar")

PRINT "pat match: "; Viper.Text.Pattern.IsMatch("[0-9]+", "abc123def")
PRINT "pat find: "; Viper.Text.Pattern.Find("[0-9]+", "abc123def")
PRINT "pat findpos: "; Viper.Text.Pattern.FindPos("[0-9]+", "abc123def")
PRINT "pat replace: "; Viper.Text.Pattern.Replace("[0-9]+", "abc123def456", "NUM")
PRINT "pat replacefirst: "; Viper.Text.Pattern.ReplaceFirst("[0-9]+", "abc123def456", "NUM")
PRINT "pat escape: "; Viper.Text.Pattern.Escape("a.b+c")

DIM sb AS Viper.Text.StringBuilder
sb = Viper.Text.StringBuilder.New()
sb.Append("hello")
sb.Append(" ")
sb.Append("world")
PRINT "sb tostring: "; sb.ToString()
PRINT "sb length: "; sb.Length
sb.AppendLine("!")
PRINT "sb after appendline: "; sb.ToString()
sb.Clear()
PRINT "sb length after clear: "; sb.Length

' NOTE: TextWrapper, Pluralize, Diff are not recognized by BASIC frontend (BUG-009)
' Viper.Text.TextWrapper.Wrap/Truncate/etc — unknown procedure
' Viper.Text.Pluralize.Plural/Singular/Count — unknown procedure
' Viper.Text.Diff.CountChanges — unknown procedure

PRINT "done"
END
