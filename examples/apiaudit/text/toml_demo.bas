' =============================================================================
' API Audit: Viper.Data.Toml - TOML Processing
' =============================================================================
' Tests: Parse, IsValid, Format, Get, GetStr
' =============================================================================

PRINT "=== API Audit: Viper.Data.Toml ==="

DIM tomlStr AS STRING
tomlStr = "[package]" + CHR$(10) + "name = ""viper""" + CHR$(10) + "version = ""1.0.0""" + CHR$(10)

' --- IsValid ---
PRINT "--- IsValid ---"
PRINT "IsValid(valid): "; Viper.Data.Toml.IsValid(tomlStr)
PRINT "IsValid('not toml'): "; Viper.Data.Toml.IsValid("not toml {{{")
PRINT "IsValid('key = ""value""'): "; Viper.Data.Toml.IsValid("key = ""value""")

' --- Parse ---
PRINT "--- Parse ---"
DIM doc AS OBJECT
doc = Viper.Data.Toml.Parse(tomlStr)
PRINT "Parse done"

' --- Format ---
PRINT "--- Format ---"
PRINT "Format:"
PRINT Viper.Data.Toml.Format(doc)

' --- GetStr ---
PRINT "--- GetStr ---"
PRINT "GetStr('package.name'): "; Viper.Data.Toml.GetStr(doc, "package.name")
PRINT "GetStr('package.version'): "; Viper.Data.Toml.GetStr(doc, "package.version")

' --- Get ---
PRINT "--- Get ---"
DIM nameObj AS OBJECT
nameObj = Viper.Data.Toml.Get(doc, "package.name")
PRINT "Get('package.name') returned"

' Simple TOML
PRINT "--- Simple TOML ---"
DIM simple AS OBJECT
simple = Viper.Data.Toml.Parse("title = ""Hello""" + CHR$(10) + "count = 42" + CHR$(10))
PRINT "GetStr('title'): "; Viper.Data.Toml.GetStr(simple, "title")
PRINT "GetStr('count'): "; Viper.Data.Toml.GetStr(simple, "count")

PRINT "=== Toml Demo Complete ==="
END
