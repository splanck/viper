' =============================================================================
' API Audit: Viper.Data.Yaml - YAML Processing
' =============================================================================
' Tests: Parse, IsValid, Format, FormatIndent, TypeOf
' =============================================================================

PRINT "=== API Audit: Viper.Data.Yaml ==="

' --- IsValid ---
PRINT "--- IsValid ---"
PRINT "IsValid('name: test'): "; Viper.Data.Yaml.IsValid("name: test")
PRINT "IsValid('key: value'): "; Viper.Data.Yaml.IsValid("key: value")

' --- Parse ---
PRINT "--- Parse ---"
DIM yamlStr AS STRING
yamlStr = "name: Alice" + CHR$(10) + "age: 30" + CHR$(10) + "active: true" + CHR$(10)
DIM doc AS OBJECT
doc = Viper.Data.Yaml.Parse(yamlStr)
PRINT "Parse done"

' --- TypeOf ---
PRINT "--- TypeOf ---"
PRINT "TypeOf(doc): "; Viper.Data.Yaml.TypeOf(doc)

' --- Format ---
PRINT "--- Format ---"
PRINT "Format:"
PRINT Viper.Data.Yaml.Format(doc)

' --- FormatIndent ---
PRINT "--- FormatIndent ---"
PRINT "FormatIndent(4):"
PRINT Viper.Data.Yaml.FormatIndent(doc, 4)

' Parse a scalar
PRINT "--- Scalar ---"
DIM scalar AS OBJECT
scalar = Viper.Data.Yaml.Parse("42")
PRINT "TypeOf(scalar): "; Viper.Data.Yaml.TypeOf(scalar)

' Parse a list
PRINT "--- List ---"
DIM lst AS OBJECT
lst = Viper.Data.Yaml.Parse("- one" + CHR$(10) + "- two" + CHR$(10) + "- three" + CHR$(10))
PRINT "TypeOf(list): "; Viper.Data.Yaml.TypeOf(lst)
PRINT "Format(list):"
PRINT Viper.Data.Yaml.Format(lst)

PRINT "=== Yaml Demo Complete ==="
END
