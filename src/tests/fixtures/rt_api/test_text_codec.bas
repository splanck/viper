' test_text_codec.bas — Codec, Pattern, CompiledPattern, StringBuilder, TextWrapper, Pluralize, Diff
PRINT "b64enc: "; Zanna.Text.Codec.Base64Encode("hello world")
PRINT "b64dec: "; Zanna.Text.Codec.Base64Decode("aGVsbG8gd29ybGQ=")
PRINT "hexenc: "; Zanna.Text.Codec.HexEncode("AB")
PRINT "hexdec: "; Zanna.Text.Codec.HexDecode("4142")
PRINT "urlenc: "; Zanna.Text.Codec.UrlEncode("hello world&foo=bar")
PRINT "urldec: "; Zanna.Text.Codec.UrlDecode("hello%20world%26foo%3Dbar")

PRINT "pat match: "; Zanna.Text.Pattern.IsMatch("[0-9]+", "abc123def")
PRINT "pat find: "; Zanna.Text.Pattern.Find("[0-9]+", "abc123def")
PRINT "pat findpos: "; Zanna.Text.Pattern.FindPos("[0-9]+", "abc123def")
PRINT "pat replace: "; Zanna.Text.Pattern.Replace("[0-9]+", "abc123def456", "NUM")
PRINT "pat replacefirst: "; Zanna.Text.Pattern.ReplaceFirst("[0-9]+", "abc123def456", "NUM")
PRINT "pat escape: "; Zanna.Text.Pattern.Escape("a.b+c")

DIM sb AS Zanna.Text.StringBuilder
sb = Zanna.Text.StringBuilder.New()
sb.Append("hello")
sb.Append(" ")
sb.Append("world")
PRINT "sb tostring: "; sb.ToString()
PRINT "sb length: "; sb.Length
sb.AppendLine("!")
PRINT "sb after appendline: "; sb.ToString()
sb.Clear()
PRINT "sb length after clear: "; sb.Length

PRINT "plural: "; Zanna.Text.Pluralize.Plural("city")
PRINT "count: "; Zanna.Text.Pluralize.Count(2, "apple")
PRINT "diff changes: "; Zanna.Text.Diff.CountChanges("kitten", "sitting")
PRINT "wrap: "; Zanna.Text.TextWrapper.Wrap("one two three four", 7)

PRINT "done"
END
