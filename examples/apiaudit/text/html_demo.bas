' Zanna.Text.Html API Audit - HTML Parsing and Utilities
' Tests all Html functions

PRINT "=== Zanna.Text.Html API Audit ==="

DIM html AS STRING
html = "<html><body><h1>Title</h1><p>Hello <b>World</b></p>"
html = html + "<a href=""https://example.com"">Link 1</a>"
html = html + "<a href=""https://zanna.dev"">Link 2</a></body></html>"

' --- Parse ---
PRINT "--- Parse ---"
DIM doc AS OBJECT
doc = Zanna.Text.Html.Parse(html)
PRINT "Parsed successfully"

' --- ToText ---
PRINT "--- ToText ---"
PRINT Zanna.Text.Html.ToText(html)

' --- Escape ---
PRINT "--- Escape ---"
PRINT Zanna.Text.Html.Escape("<script>alert('xss')</script>")
PRINT Zanna.Text.Html.Escape("Hello & goodbye")
PRINT Zanna.Text.Html.Escape("a < b > c")
PRINT Zanna.Text.Html.Escape(CHR(34) + "quoted" + CHR(34))

' --- Unescape ---
PRINT "--- Unescape ---"
PRINT Zanna.Text.Html.Unescape("&lt;script&gt;alert(&apos;xss&apos;)&lt;/script&gt;")
PRINT Zanna.Text.Html.Unescape("Hello &amp; goodbye")
PRINT Zanna.Text.Html.Unescape("a &lt; b &gt; c")
PRINT Zanna.Text.Html.Unescape("&quot;quoted&quot;")

' --- StripTags ---
PRINT "--- StripTags ---"
PRINT Zanna.Text.Html.StripTags("<p>Hello <b>World</b></p>")
PRINT Zanna.Text.Html.StripTags("<div class=""test"">Content</div>")
PRINT Zanna.Text.Html.StripTags("No tags here")

' --- ExtractLinks ---
PRINT "--- ExtractLinks ---"
DIM links AS OBJECT
links = Zanna.Text.Html.ExtractLinks(html)
PRINT "Link count: "; Zanna.Collections.Seq.get_Count(links)
DIM li AS INTEGER
FOR li = 0 TO Zanna.Collections.Seq.get_Count(links) - 1
    PRINT "Link: "; links.Get(li)
NEXT li

' --- ExtractText ---
PRINT "--- ExtractText ---"
DIM texts AS OBJECT
texts = Zanna.Text.Html.ExtractText(html, "p")
PRINT "P tag count: "; Zanna.Collections.Seq.get_Count(texts)
DIM ti AS INTEGER
FOR ti = 0 TO Zanna.Collections.Seq.get_Count(texts) - 1
    PRINT "P text: "; texts.Get(ti)
NEXT ti

' Extract h1 tags
DIM h1s AS OBJECT
h1s = Zanna.Text.Html.ExtractText(html, "h1")
PRINT "H1 count: "; Zanna.Collections.Seq.get_Count(h1s)
PRINT "H1 text: "; h1s.Get(0)

PRINT "=== Html Demo Complete ==="
END
