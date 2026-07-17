' Investigate Template.Render bug

' Create map with values
DIM data AS Zanna.Collections.Map
data = Zanna.Collections.Map.New()
data.Set("name", "Alice")
data.Set("age", "30")

PRINT "=== Debug Map Contents ==="
PRINT "map.Count: "; data.Count
PRINT "map.Has('name'): "; data.Has("name")
PRINT "map.Has('age'): "; data.Has("age")

' Test Templates
PRINT ""
PRINT "=== Template Tests ==="

DIM tpl AS STRING
DIM result AS STRING

' Simple template
tpl = "Hello, {{name}}!"
PRINT "Template: "; tpl
PRINT "Has 'name': "; Zanna.Text.Template.Has(tpl, "name")
result = Zanna.Text.Template.Render(tpl, data)
PRINT "Rendered: '"; result; "'"
PRINT ""

' Template with multiple vars
tpl = "{{name}} is {{age}} years old"
PRINT "Template: "; tpl
result = Zanna.Text.Template.Render(tpl, data)
PRINT "Rendered: '"; result; "'"
PRINT ""

' Get keys from template
PRINT "=== Template Keys ==="
DIM keys AS Zanna.Collections.StringSet
keys = Zanna.Text.Template.Keys("Hello {{name}}, you are {{age}}")
PRINT "Keys count: "; keys.Count

' Test with empty map
PRINT ""
PRINT "=== Empty Map Test ==="
DIM emptyMap AS Zanna.Collections.Map
emptyMap = Zanna.Collections.Map.New()
result = Zanna.Text.Template.Render("Hello {{name}}", emptyMap)
PRINT "Render with empty map: '"; result; "'"

' Test escape
PRINT ""
PRINT "=== Escape Test ==="
result = Zanna.Text.Template.Escape("<script>alert('xss')</script>")
PRINT "Escaped: "; result

END
