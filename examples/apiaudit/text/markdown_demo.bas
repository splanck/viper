' Viper.Text.Markdown API Audit - Markdown Conversion and Extraction
' Tests all Markdown functions

PRINT "=== Viper.Text.Markdown API Audit ==="

DIM md AS STRING
md = "# Hello World" + CHR(10) + CHR(10)
md = md + "This is a **bold** paragraph with a [link](https://example.com)." + CHR(10) + CHR(10)
md = md + "## Section Two" + CHR(10) + CHR(10)
md = md + "A list:" + CHR(10)
md = md + "- item 1" + CHR(10)
md = md + "- item 2" + CHR(10)
md = md + "- item 3" + CHR(10) + CHR(10)
md = md + "### Sub Section" + CHR(10) + CHR(10)
md = md + "Another [reference](https://viper.dev) here." + CHR(10) + CHR(10)
md = md + "Some `inline code` and text."

' --- ToHtml ---
PRINT "--- ToHtml ---"
PRINT Viper.Text.Markdown.ToHtml(md)

' --- ToText ---
PRINT "--- ToText ---"
PRINT Viper.Text.Markdown.ToText(md)

' --- ExtractLinks ---
PRINT "--- ExtractLinks ---"
DIM links AS OBJECT
links = Viper.Text.Markdown.ExtractLinks(md)
PRINT "Link count: "; Viper.Collections.Seq.get_Len(links)
DIM li AS INTEGER
FOR li = 0 TO Viper.Collections.Seq.get_Len(links) - 1
    PRINT "Link: "; links.Get(li)
NEXT li

' --- ExtractHeadings ---
PRINT "--- ExtractHeadings ---"
DIM headings AS OBJECT
headings = Viper.Text.Markdown.ExtractHeadings(md)
PRINT "Heading count: "; Viper.Collections.Seq.get_Len(headings)
DIM hi AS INTEGER
FOR hi = 0 TO Viper.Collections.Seq.get_Len(headings) - 1
    PRINT "Heading: "; headings.Get(hi)
NEXT hi

' Minimal markdown
PRINT "--- Minimal ---"
PRINT Viper.Text.Markdown.ToHtml("Just **bold** text.")
PRINT Viper.Text.Markdown.ToText("Just **bold** text.")

' No links
DIM noLinks AS OBJECT
noLinks = Viper.Text.Markdown.ExtractLinks("No links in this text.")
PRINT "No links count: "; Viper.Collections.Seq.get_Len(noLinks)

PRINT "=== Markdown Demo Complete ==="
END
