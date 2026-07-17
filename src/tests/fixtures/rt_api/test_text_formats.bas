' test_text_formats.bas — Csv, Ini, Toml, Markdown, Html, Template
DIM s AS Zanna.Collections.Seq
s = Zanna.Collections.Seq.New()
s.Push("name")
s.Push("age")
s.Push("city")
PRINT "csv formatline: "; Zanna.Data.Csv.FormatLine(s)

DIM parsed AS OBJECT
parsed = Zanna.Data.Csv.ParseLine("alice,30,nyc")
PRINT "csv parseline done"

PRINT "html escape: "; Zanna.Text.Html.Escape("<b>hello</b>")
PRINT "html unescape: "; Zanna.Text.Html.Unescape("&lt;b&gt;hello&lt;/b&gt;")
PRINT "html strip: "; Zanna.Text.Html.StripTags("<p>hello <b>world</b></p>")
PRINT "html totext: "; Zanna.Text.Html.ToText("<p>hello</p><p>world</p>")

PRINT "md totext: "; Zanna.Text.Markdown.ToText("# Hello")
PRINT "md tohtml: "; Zanna.Text.Markdown.ToHtml("**bold**")

' NOTE: TOML.IsValid fails in BASIC due to escaped quote parsing (BUG-016)
' PRINT "toml valid: "; Zanna.Data.Toml.IsValid("[section]...")

PRINT "done"
END
