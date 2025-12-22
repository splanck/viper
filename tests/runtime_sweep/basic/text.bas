' EXPECT_OUT: RESULT: ok
' COVER: Viper.Text.Codec.Base64Dec
' COVER: Viper.Text.Codec.Base64Enc
' COVER: Viper.Text.Codec.HexDec
' COVER: Viper.Text.Codec.HexEnc
' COVER: Viper.Text.Codec.UrlDecode
' COVER: Viper.Text.Codec.UrlEncode
' COVER: Viper.Text.Csv.Format
' COVER: Viper.Text.Csv.FormatLine
' COVER: Viper.Text.Csv.FormatLineWith
' COVER: Viper.Text.Csv.FormatWith
' COVER: Viper.Text.Csv.Parse
' COVER: Viper.Text.Csv.ParseLine
' COVER: Viper.Text.Csv.ParseLineWith
' COVER: Viper.Text.Csv.ParseWith
' COVER: Viper.Text.Guid.Empty
' COVER: Viper.Text.Guid.FromBytes
' COVER: Viper.Text.Guid.IsValid
' COVER: Viper.Text.Guid.New
' COVER: Viper.Text.Guid.ToBytes
' COVER: Viper.Text.Pattern.Escape
' COVER: Viper.Text.Pattern.Find
' COVER: Viper.Text.Pattern.FindAll
' COVER: Viper.Text.Pattern.FindFrom
' COVER: Viper.Text.Pattern.FindPos
' COVER: Viper.Text.Pattern.IsMatch
' COVER: Viper.Text.Pattern.Replace
' COVER: Viper.Text.Pattern.ReplaceFirst
' COVER: Viper.Text.Pattern.Split
' COVER: Viper.Text.StringBuilder.New
' COVER: Viper.Text.StringBuilder.Capacity
' COVER: Viper.Text.StringBuilder.Length
' COVER: Viper.Text.StringBuilder.Append
' COVER: Viper.Text.StringBuilder.AppendLine
' COVER: Viper.Text.StringBuilder.Clear
' COVER: Viper.Text.StringBuilder.ToString
' COVER: Viper.Text.Template.Escape
' COVER: Viper.Text.Template.Has
' COVER: Viper.Text.Template.Keys
' COVER: Viper.Text.Template.Render
' COVER: Viper.Text.Template.RenderSeq
' COVER: Viper.Text.Template.RenderWith

DIM encoded AS STRING
encoded = Viper.Text.Codec.Base64Enc("Hello")
Viper.Diagnostics.AssertEqStr(encoded, "SGVsbG8=", "codec.b64enc")
Viper.Diagnostics.AssertEqStr(Viper.Text.Codec.Base64Dec(encoded), "Hello", "codec.b64dec")
Viper.Diagnostics.AssertEqStr(Viper.Text.Codec.HexEnc("ABC"), "414243", "codec.hexenc")
Viper.Diagnostics.AssertEqStr(Viper.Text.Codec.HexDec("414243"), "ABC", "codec.hexdec")
Viper.Diagnostics.AssertEqStr(Viper.Text.Codec.UrlEncode("hello world"), "hello%20world", "codec.urlenc")
Viper.Diagnostics.AssertEqStr(Viper.Text.Codec.UrlDecode("hello%20world"), "hello world", "codec.urldec")

DIM fields AS Viper.Collections.Seq
fields = Viper.Text.Csv.ParseLine("a,b,c")
Viper.Diagnostics.AssertEq(fields.Len, 3, "csv.parseline.len")
Viper.Diagnostics.AssertEqStr(fields.Get(1), "b", "csv.parseline.get")

DIM row AS Viper.Collections.Seq
row = Viper.Text.Csv.ParseLine("\"He said \"\"Hi\"\"\"")
Viper.Diagnostics.AssertEqStr(row.Get(0), "He said \"Hi\"", "csv.quotes")

DIM rows AS Viper.Collections.Seq
rows = Viper.Text.Csv.Parse("a,b" + Viper.String.Chr(10) + "c,d")
Viper.Diagnostics.AssertEq(rows.Len, 2, "csv.parse.len")
DIM row0 AS Viper.Collections.Seq
row0 = rows.Get(0)
Viper.Diagnostics.AssertEqStr(row0.Get(0), "a", "csv.parse.row0")

DIM line AS STRING
line = Viper.Text.Csv.FormatLine(fields)
Viper.Diagnostics.Assert(line <> "", "csv.formatline")
DIM line2 AS STRING
line2 = Viper.Text.Csv.FormatLineWith(fields, "|")
Viper.Diagnostics.Assert(line2 <> "", "csv.formatlinewith")

DIM rowsOut AS STRING
rowsOut = Viper.Text.Csv.Format(rows)
Viper.Diagnostics.Assert(rowsOut <> "", "csv.format")
DIM rowsOut2 AS STRING
rowsOut2 = Viper.Text.Csv.FormatWith(rows, "|")
Viper.Diagnostics.Assert(rowsOut2 <> "", "csv.formatwith")

DIM fields2 AS Viper.Collections.Seq
fields2 = Viper.Text.Csv.ParseLineWith("a|b|c", "|")
Viper.Diagnostics.AssertEq(fields2.Len, 3, "csv.parselinewith")

DIM rows2 AS Viper.Collections.Seq
rows2 = Viper.Text.Csv.ParseWith("a|b" + Viper.String.Chr(10) + "c|d", "|")
Viper.Diagnostics.AssertEq(rows2.Len, 2, "csv.parsewith")

DIM id AS STRING
id = Viper.Text.Guid.New()
Viper.Diagnostics.Assert(Viper.Text.Guid.IsValid(id), "guid.valid")
Viper.Diagnostics.Assert(Viper.Text.Guid.IsValid("not-a-guid") = 0, "guid.invalid")
Viper.Diagnostics.AssertEqStr(Viper.Text.Guid.Empty, "00000000-0000-0000-0000-000000000000", "guid.empty")
DIM gidBytes AS Viper.Collections.Bytes
gidBytes = Viper.Text.Guid.ToBytes(id)
DIM id2 AS STRING
id2 = Viper.Text.Guid.FromBytes(gidBytes)
Viper.Diagnostics.Assert(Viper.Text.Guid.IsValid(id2), "guid.frombytes")

DIM text AS STRING
text = "abc123def456"
Viper.Diagnostics.Assert(Viper.Text.Pattern.IsMatch("\\d+", text), "pat.ismatch")
Viper.Diagnostics.AssertEqStr(Viper.Text.Pattern.Find("\\d+", text), "123", "pat.find")
Viper.Diagnostics.AssertEqStr(Viper.Text.Pattern.FindFrom("\\d+", text, 3), "123", "pat.findfrom")
Viper.Diagnostics.AssertEq(Viper.Text.Pattern.FindPos("World", "Hello World"), 6, "pat.findpos")
DIM matches AS Viper.Collections.Seq
matches = Viper.Text.Pattern.FindAll("\\d+", text)
Viper.Diagnostics.AssertEq(matches.Len, 2, "pat.findall")
Viper.Diagnostics.AssertEqStr(Viper.Text.Pattern.Replace("\\d+", text, "X"), "abcXdefX", "pat.replace")
Viper.Diagnostics.AssertEqStr(Viper.Text.Pattern.ReplaceFirst("\\d+", text, "X"), "abcXdef456", "pat.replacefirst")
DIM parts AS Viper.Collections.Seq
parts = Viper.Text.Pattern.Split("\\s+", "hello   world  test")
Viper.Diagnostics.AssertEq(parts.Len, 3, "pat.split")
Viper.Diagnostics.AssertEqStr(Viper.Text.Pattern.Escape("file.txt"), "file\\.txt", "pat.escape")

DIM values AS Viper.Collections.Map
values = Viper.Collections.Map.New()
values.Set("name", "Alice")
values.Set("count", "5")
DIM templ AS STRING
templ = "Hello {{name}}, you have {{count}} messages."
DIM rendered AS STRING
rendered = Viper.Text.Template.Render(templ, values)
Viper.Diagnostics.AssertEqStr(rendered, "Hello Alice, you have 5 messages.", "tmpl.render")
DIM seqVals AS Viper.Collections.Seq
seqVals = Viper.Collections.Seq.New()
seqVals.Push("Alice")
seqVals.Push("Bob")
seqVals.Push("Charlie")
DIM renderedSeq AS STRING
renderedSeq = Viper.Text.Template.RenderSeq("{{0}} and {{1}} meet {{2}}", seqVals)
Viper.Diagnostics.AssertEqStr(renderedSeq, "Alice and Bob meet Charlie", "tmpl.renderseq")
DIM renderedWith AS STRING
renderedWith = Viper.Text.Template.RenderWith("Hello $name$!", values, "$", "$")
Viper.Diagnostics.AssertEqStr(renderedWith, "Hello Alice!", "tmpl.renderwith")
Viper.Diagnostics.Assert(Viper.Text.Template.Has(templ, "name"), "tmpl.has")
DIM keys AS Viper.Collections.Bag
keys = Viper.Text.Template.Keys(templ)
Viper.Diagnostics.Assert(keys.Has("name"), "tmpl.keys")
DIM escaped AS STRING
escaped = Viper.Text.Template.Escape("Use {{name}} for placeholders")
DIM renderedEsc AS STRING
renderedEsc = Viper.Text.Template.Render(escaped, values)
Viper.Diagnostics.AssertEqStr(renderedEsc, "Use {{name}} for placeholders", "tmpl.escape")

DIM sb AS Viper.Text.StringBuilder
sb = NEW Viper.Text.StringBuilder()
Viper.Diagnostics.AssertEq(sb.Length, 0, "sb.len0")
Viper.Diagnostics.Assert(sb.Capacity >= sb.Length, "sb.capacity")
sb.Append("a")
sb.AppendLine("b")
DIM sbText AS STRING
sbText = sb.ToString()
Viper.Diagnostics.Assert(sbText <> "", "sb.tostring")
sb.Clear()
Viper.Diagnostics.AssertEq(sb.Length, 0, "sb.clear")

PRINT "RESULT: ok"
END
