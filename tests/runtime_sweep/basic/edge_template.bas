' Investigate Template.Render bug

' Create map with values
DIM data AS Viper.Collections.Map
data = Viper.Collections.Map.New()
data.Set("name", "Alice")
data.Set("age", "30")

PRINT "=== Debug Map Contents ==="
PRINT "map.Len: "; data.Len
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
PRINT "Has 'name': "; Viper.Text.Template.Has(tpl, "name")
result = Viper.Text.Template.Render(tpl, data)
PRINT "Rendered: '"; result; "'"
PRINT ""

' Template with multiple vars
tpl = "{{name}} is {{age}} years old"
PRINT "Template: "; tpl
result = Viper.Text.Template.Render(tpl, data)
PRINT "Rendered: '"; result; "'"
PRINT ""

' Get keys from template
PRINT "=== Template Keys ==="
DIM keys AS Viper.Collections.Bag
keys = Viper.Text.Template.Keys("Hello {{name}}, you are {{age}}")
PRINT "Keys count: "; keys.Len

' Test with empty map
PRINT ""
PRINT "=== Empty Map Test ==="
DIM emptyMap AS Viper.Collections.Map
emptyMap = Viper.Collections.Map.New()
result = Viper.Text.Template.Render("Hello {{name}}", emptyMap)
PRINT "Render with empty map: '"; result; "'"

' Test escape
PRINT ""
PRINT "=== Escape Test ==="
result = Viper.Text.Template.Escape("<script>alert('xss')</script>")
PRINT "Escaped: "; result

END
