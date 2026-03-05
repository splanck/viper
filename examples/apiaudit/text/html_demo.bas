' Viper.Text.Html API Audit - HTML Parsing and Utilities
' Tests all Html functions

PRINT "=== Viper.Text.Html API Audit ==="

DIM html AS STRING
html = "<html><body><h1>Title</h1><p>Hello <b>World</b></p>"
html = html + "<a href=""https://example.com"">Link 1</a>"
html = html + "<a href=""https://viper.dev"">Link 2</a></body></html>"

' --- Parse ---
PRINT "--- Parse ---"
DIM doc AS OBJECT
doc = Viper.Text.Html.Parse(html)
PRINT "Parsed successfully"

' --- ToText ---
PRINT "--- ToText ---"
PRINT Viper.Text.Html.ToText(html)

' --- Escape ---
PRINT "--- Escape ---"
PRINT Viper.Text.Html.Escape("<script>alert('xss')</script>")
PRINT Viper.Text.Html.Escape("Hello & goodbye")
PRINT Viper.Text.Html.Escape("a < b > c")
PRINT Viper.Text.Html.Escape(CHR(34) + "quoted" + CHR(34))

' --- Unescape ---
PRINT "--- Unescape ---"
PRINT Viper.Text.Html.Unescape("&lt;script&gt;alert(&apos;xss&apos;)&lt;/script&gt;")
PRINT Viper.Text.Html.Unescape("Hello &amp; goodbye")
PRINT Viper.Text.Html.Unescape("a &lt; b &gt; c")
PRINT Viper.Text.Html.Unescape("&quot;quoted&quot;")

' --- StripTags ---
PRINT "--- StripTags ---"
PRINT Viper.Text.Html.StripTags("<p>Hello <b>World</b></p>")
PRINT Viper.Text.Html.StripTags("<div class=""test"">Content</div>")
PRINT Viper.Text.Html.StripTags("No tags here")

' --- ExtractLinks ---
PRINT "--- ExtractLinks ---"
DIM links AS OBJECT
links = Viper.Text.Html.ExtractLinks(html)
PRINT "Link count: "; Viper.Collections.Seq.get_Len(links)
DIM li AS INTEGER
FOR li = 0 TO Viper.Collections.Seq.get_Len(links) - 1
    PRINT "Link: "; links.Get(li)
NEXT li

' --- ExtractText ---
PRINT "--- ExtractText ---"
DIM texts AS OBJECT
texts = Viper.Text.Html.ExtractText(html, "p")
PRINT "P tag count: "; Viper.Collections.Seq.get_Len(texts)
DIM ti AS INTEGER
FOR ti = 0 TO Viper.Collections.Seq.get_Len(texts) - 1
    PRINT "P text: "; texts.Get(ti)
NEXT ti

' Extract h1 tags
DIM h1s AS OBJECT
h1s = Viper.Text.Html.ExtractText(html, "h1")
PRINT "H1 count: "; Viper.Collections.Seq.get_Len(h1s)
PRINT "H1 text: "; h1s.Get(0)

PRINT "=== Html Demo Complete ==="
END
