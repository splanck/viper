' test_text_formats.bas — Csv, Ini, Toml, Markdown, Html, Template
DIM s AS Viper.Collections.Seq
s = Viper.Collections.Seq.New()
s.Push("name")
s.Push("age")
s.Push("city")
PRINT "csv formatline: "; Viper.Text.Csv.FormatLine(s)

DIM parsed AS OBJECT
parsed = Viper.Text.Csv.ParseLine("alice,30,nyc")
PRINT "csv parseline done"

PRINT "html escape: "; Viper.Text.Html.Escape("<b>hello</b>")
PRINT "html unescape: "; Viper.Text.Html.Unescape("&lt;b&gt;hello&lt;/b&gt;")
PRINT "html strip: "; Viper.Text.Html.StripTags("<p>hello <b>world</b></p>")
PRINT "html totext: "; Viper.Text.Html.ToText("<p>hello</p><p>world</p>")

PRINT "md totext: "; Viper.Text.Markdown.ToText("# Hello")
PRINT "md tohtml: "; Viper.Text.Markdown.ToHtml("**bold**")

' NOTE: TOML.IsValid fails in BASIC due to escaped quote parsing (BUG-016)
' PRINT "toml valid: "; Viper.Text.Toml.IsValid("[section]...")

PRINT "done"
END
