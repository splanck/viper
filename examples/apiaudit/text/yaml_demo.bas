' =============================================================================
' API Audit: Viper.Data.Yaml - YAML Processing
' =============================================================================
' Tests: ParseResult, Parse, Error, IsValid, Format, FormatIndent, TypeOf
' =============================================================================

PRINT "=== API Audit: Viper.Data.Yaml ==="

' --- IsValid ---
PRINT "--- IsValid ---"
PRINT "IsValid('name: test'): "; Viper.Data.Yaml.IsValid("name: test")
PRINT "IsValid('key: value'): "; Viper.Data.Yaml.IsValid("key: value")

' --- ParseResult ---
PRINT "--- ParseResult ---"
DIM yamlStr AS STRING
yamlStr = "name: Alice" + CHR$(10) + "age: 30" + CHR$(10) + "active: true" + CHR$(10)
DIM doc AS OBJECT
DIM parsed AS OBJECT
parsed = Viper.Data.Yaml.ParseResult(yamlStr)
PRINT "ParseResult IsOk: "; parsed.IsOk
doc = parsed.Unwrap()
PRINT "ParseResult done"

DIM nullDoc AS OBJECT
nullDoc = Viper.Data.Yaml.ParseResult("null")
PRINT "Null ParseResult IsOk: "; nullDoc.IsOk

DIM badYaml AS OBJECT
badYaml = Viper.Data.Yaml.ParseResult("name: [unterminated" + CHR$(10))
PRINT "Bad ParseResult IsErr: "; badYaml.IsErr
PRINT "Bad ParseResult Err: "; badYaml.UnwrapErrStr()

' --- Parse / Error compatibility ---
PRINT "--- Parse / Error compatibility ---"
DIM legacyDoc AS OBJECT
legacyDoc = Viper.Data.Yaml.Parse("name: Bob" + CHR$(10))
PRINT "Legacy TypeOf: "; Viper.Data.Yaml.TypeOf(legacyDoc)
DIM legacyBad AS OBJECT
legacyBad = Viper.Data.Yaml.Parse("name: [unterminated" + CHR$(10))
PRINT "Legacy Error: "; Viper.Data.Yaml.Error()

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
DIM scalarResult AS OBJECT
scalarResult = Viper.Data.Yaml.ParseResult("42")
scalar = scalarResult.Unwrap()
PRINT "TypeOf(scalar): "; Viper.Data.Yaml.TypeOf(scalar)

' Parse a list
PRINT "--- List ---"
DIM lst AS OBJECT
DIM listResult AS OBJECT
listResult = Viper.Data.Yaml.ParseResult("- one" + CHR$(10) + "- two" + CHR$(10) + "- three" + CHR$(10))
lst = listResult.Unwrap()
PRINT "TypeOf(list): "; Viper.Data.Yaml.TypeOf(lst)
PRINT "Format(list):"
PRINT Viper.Data.Yaml.Format(lst)

PRINT "=== Yaml Demo Complete ==="
END
