' =============================================================================
' API Audit: Viper.Data.Xml - XML Processing
' =============================================================================
' Tests: Parse, IsValid, Element, Text, Comment, Cdata, NodeType, Tag, Content,
'        TextContent, Attr, HasAttr, SetAttr, RemoveAttr, AttrNames, Children,
'        ChildCount, ChildAt, Append, Remove, Find, FindAll, Format,
'        FormatPretty, Escape, Unescape
' =============================================================================

PRINT "=== API Audit: Viper.Data.Xml ==="

' --- IsValid ---
PRINT "--- IsValid ---"
PRINT "IsValid('<root/>): "; Viper.Data.Xml.IsValid("<root><item/></root>")
PRINT "IsValid('not xml'): "; Viper.Data.Xml.IsValid("not xml")

' --- Parse ---
PRINT "--- Parse ---"
DIM doc AS OBJECT
doc = Viper.Data.Xml.Parse("<root><item id=""1"">Hello</item><item id=""2"">World</item></root>")
PRINT "Parse done"

' --- NodeType ---
PRINT "--- NodeType ---"
PRINT "NodeType: "; Viper.Data.Xml.NodeType(doc)

' --- Tag ---
PRINT "--- Tag ---"
PRINT "Tag: "; Viper.Data.Xml.Tag(doc)

' --- TextContent ---
PRINT "--- TextContent ---"
PRINT "TextContent: "; Viper.Data.Xml.TextContent(doc)

' --- ChildCount ---
PRINT "--- ChildCount ---"
PRINT "ChildCount: "; Viper.Data.Xml.ChildCount(doc)

' --- ChildAt ---
PRINT "--- ChildAt ---"
DIM child0 AS OBJECT
child0 = Viper.Data.Xml.ChildAt(doc, 0)
PRINT "ChildAt(0) Tag: "; Viper.Data.Xml.Tag(child0)
PRINT "ChildAt(0) Text: "; Viper.Data.Xml.TextContent(child0)

' --- Attr ---
PRINT "--- Attr ---"
PRINT "Attr(child0, 'id'): "; Viper.Data.Xml.Attr(child0, "id")

' --- HasAttr ---
PRINT "--- HasAttr ---"
PRINT "HasAttr('id'): "; Viper.Data.Xml.HasAttr(child0, "id")
PRINT "HasAttr('class'): "; Viper.Data.Xml.HasAttr(child0, "class")

' --- SetAttr ---
PRINT "--- SetAttr ---"
Viper.Data.Xml.SetAttr(child0, "class", "primary")
PRINT "SetAttr done, Attr('class'): "; Viper.Data.Xml.Attr(child0, "class")

' --- RemoveAttr ---
PRINT "--- RemoveAttr ---"
PRINT "RemoveAttr('class'): "; Viper.Data.Xml.RemoveAttr(child0, "class")
PRINT "HasAttr after: "; Viper.Data.Xml.HasAttr(child0, "class")

' --- AttrNames ---
PRINT "--- AttrNames ---"
DIM names AS OBJECT
names = Viper.Data.Xml.AttrNames(child0)
PRINT "AttrNames returned"

' --- Element ---
PRINT "--- Element ---"
DIM elem AS OBJECT
elem = Viper.Data.Xml.Element("newTag")
PRINT "Element('newTag') Tag: "; Viper.Data.Xml.Tag(elem)

' --- Text ---
PRINT "--- Text ---"
DIM textNode AS OBJECT
textNode = Viper.Data.Xml.Text("some text")
PRINT "Text node Content: "; Viper.Data.Xml.Content(textNode)

' --- Comment ---
PRINT "--- Comment ---"
DIM commentNode AS OBJECT
commentNode = Viper.Data.Xml.Comment("a comment")
PRINT "Comment Content: "; Viper.Data.Xml.Content(commentNode)

' --- Cdata ---
PRINT "--- Cdata ---"
DIM cdataNode AS OBJECT
cdataNode = Viper.Data.Xml.Cdata("raw <data>")
PRINT "Cdata Content: "; Viper.Data.Xml.Content(cdataNode)

' --- Append ---
PRINT "--- Append ---"
DIM newChild AS OBJECT
newChild = Viper.Data.Xml.Element("item")
Viper.Data.Xml.SetAttr(newChild, "id", "3")
Viper.Data.Xml.Append(doc, newChild)
PRINT "ChildCount after append: "; Viper.Data.Xml.ChildCount(doc)

' --- Remove ---
PRINT "--- Remove ---"
PRINT "Remove: "; Viper.Data.Xml.Remove(doc, newChild)
PRINT "ChildCount after remove: "; Viper.Data.Xml.ChildCount(doc)

' --- Find ---
PRINT "--- Find ---"
DIM found AS OBJECT
found = Viper.Data.Xml.Find(doc, "item")
PRINT "Find('item') Tag: "; Viper.Data.Xml.Tag(found)

' --- FindAll ---
PRINT "--- FindAll ---"
DIM allItems AS OBJECT
allItems = Viper.Data.Xml.FindAll(doc, "item")
PRINT "FindAll returned"

' --- Format ---
PRINT "--- Format ---"
PRINT "Format: "; Viper.Data.Xml.Format(doc)

' --- FormatPretty ---
PRINT "--- FormatPretty ---"
PRINT "FormatPretty:"
PRINT Viper.Data.Xml.FormatPretty(doc, 2)

' --- Escape ---
PRINT "--- Escape ---"
PRINT "Escape: "; Viper.Data.Xml.Escape("<tag>&")

' --- Unescape ---
PRINT "--- Unescape ---"
PRINT "Unescape: "; Viper.Data.Xml.Unescape("&lt;tag&gt;")

PRINT "=== Xml Demo Complete ==="
END
