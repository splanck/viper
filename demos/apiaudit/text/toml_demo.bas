' =============================================================================
' API Audit: Viper.Text.Toml - TOML Processing
' =============================================================================
' Tests: Parse, IsValid, Format, Get, GetStr
' =============================================================================

PRINT "=== API Audit: Viper.Text.Toml ==="

DIM tomlStr AS STRING
tomlStr = "[package]" + CHR$(10) + "name = ""viper""" + CHR$(10) + "version = ""1.0.0""" + CHR$(10)

' --- IsValid ---
PRINT "--- IsValid ---"
PRINT "IsValid(valid): "; Viper.Text.Toml.IsValid(tomlStr)
PRINT "IsValid('not toml'): "; Viper.Text.Toml.IsValid("not toml {{{")
PRINT "IsValid('key = ""value""'): "; Viper.Text.Toml.IsValid("key = ""value""")

' --- Parse ---
PRINT "--- Parse ---"
DIM doc AS OBJECT
doc = Viper.Text.Toml.Parse(tomlStr)
PRINT "Parse done"

' --- Format ---
PRINT "--- Format ---"
PRINT "Format:"
PRINT Viper.Text.Toml.Format(doc)

' --- GetStr ---
PRINT "--- GetStr ---"
PRINT "GetStr('package.name'): "; Viper.Text.Toml.GetStr(doc, "package.name")
PRINT "GetStr('package.version'): "; Viper.Text.Toml.GetStr(doc, "package.version")

' --- Get ---
PRINT "--- Get ---"
DIM nameObj AS OBJECT
nameObj = Viper.Text.Toml.Get(doc, "package.name")
PRINT "Get('package.name') returned"

' Simple TOML
PRINT "--- Simple TOML ---"
DIM simple AS OBJECT
simple = Viper.Text.Toml.Parse("title = ""Hello""" + CHR$(10) + "count = 42" + CHR$(10))
PRINT "GetStr('title'): "; Viper.Text.Toml.GetStr(simple, "title")
PRINT "GetStr('count'): "; Viper.Text.Toml.GetStr(simple, "count")

PRINT "=== Toml Demo Complete ==="
END
