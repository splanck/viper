' =============================================================================
' API Audit: Zanna.Data.Toml - TOML Processing
' =============================================================================
' Tests: Parse, IsValid, Format, Get, GetStr
' =============================================================================

PRINT "=== API Audit: Zanna.Data.Toml ==="

DIM tomlStr AS STRING
tomlStr = "[package]" + CHR$(10) + "name = ""zanna""" + CHR$(10) + "version = ""1.0.0""" + CHR$(10)

' --- IsValid ---
PRINT "--- IsValid ---"
PRINT "IsValid(valid): "; Zanna.Data.Toml.IsValid(tomlStr)
PRINT "IsValid('not toml'): "; Zanna.Data.Toml.IsValid("not toml {{{")
PRINT "IsValid('key = ""value""'): "; Zanna.Data.Toml.IsValid("key = ""value""")

' --- Parse ---
PRINT "--- Parse ---"
DIM doc AS OBJECT
doc = Zanna.Data.Toml.Parse(tomlStr)
PRINT "Parse done"

' --- Format ---
PRINT "--- Format ---"
PRINT "Format:"
PRINT Zanna.Data.Toml.Format(doc)

' --- GetStr ---
PRINT "--- GetStr ---"
PRINT "GetStr('package.name'): "; Zanna.Data.Toml.GetStr(doc, "package.name")
PRINT "GetStr('package.version'): "; Zanna.Data.Toml.GetStr(doc, "package.version")

' --- Get ---
PRINT "--- Get ---"
DIM nameObj AS OBJECT
nameObj = Zanna.Data.Toml.Get(doc, "package.name")
PRINT "Get('package.name') returned"

' Simple TOML
PRINT "--- Simple TOML ---"
DIM simple AS OBJECT
simple = Zanna.Data.Toml.Parse("title = ""Hello""" + CHR$(10) + "count = 42" + CHR$(10))
PRINT "GetStr('title'): "; Zanna.Data.Toml.GetStr(simple, "title")
PRINT "GetStr('count'): "; Zanna.Data.Toml.GetStr(simple, "count")

PRINT "=== Toml Demo Complete ==="
END
