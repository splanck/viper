' Viper.Text.Template API Audit - String Templating
' Tests all Template functions

PRINT "=== Viper.Text.Template API Audit ==="

' --- Render ---
PRINT "--- Render ---"
DIM m AS OBJECT
m = Viper.Collections.Map.New()
m.Set("name", Viper.Core.Box.Str("Alice"))
m.Set("age", Viper.Core.Box.Str("30"))
m.Set("city", Viper.Core.Box.Str("Boston"))

DIM tpl AS STRING
tpl = "Hello, {{name}}! You are {{age}} years old from {{city}}."
PRINT Viper.Text.Template.Render(tpl, m)

' Template with no variables
DIM tpl2 AS STRING
tpl2 = "No variables here."
PRINT Viper.Text.Template.Render(tpl2, m)

' --- RenderSeq ---
PRINT "--- RenderSeq ---"
DIM items AS OBJECT
items = Viper.Collections.Seq.New()
items.Push(Viper.Core.Box.Str("apple"))
items.Push(Viper.Core.Box.Str("banana"))
items.Push(Viper.Core.Box.Str("cherry"))
DIM seqTpl AS STRING
seqTpl = "Item 0: {{0}}, Item 1: {{1}}, Item 2: {{2}}"
PRINT Viper.Text.Template.RenderSeq(seqTpl, items)

' --- Has ---
PRINT "--- Has ---"
PRINT "Has 'name': "; Viper.Text.Template.Has(tpl, "name")
PRINT "Has 'age': "; Viper.Text.Template.Has(tpl, "age")
PRINT "Has 'email': "; Viper.Text.Template.Has(tpl, "email")
PRINT "Has 'city': "; Viper.Text.Template.Has(tpl, "city")

' --- Keys ---
PRINT "--- Keys ---"
DIM keys AS OBJECT
keys = Viper.Text.Template.Keys(tpl)
PRINT "Key count: "; Viper.Collections.Seq.get_Len(keys)
DIM ki AS INTEGER
FOR ki = 0 TO Viper.Collections.Seq.get_Len(keys) - 1
    PRINT "Key: "; keys.Get(ki)
NEXT ki

' --- Escape ---
PRINT "--- Escape ---"
PRINT Viper.Text.Template.Escape("Hello {{name}}, use \{{literal}} braces")
PRINT Viper.Text.Template.Escape("No braces here")

PRINT "=== Template Demo Complete ==="
END
