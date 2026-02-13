' =============================================================================
' API Audit: Viper.Data.Serialize - Multi-Format Serialization
' =============================================================================
' Tests: Parse, Format, FormatPretty, IsValid, Detect, AutoParse, Convert,
'        FormatName, MimeType, FormatFromName
' =============================================================================

PRINT "=== API Audit: Viper.Data.Serialize ==="

' Format constants: 0=JSON, 1=YAML, 2=TOML, 3=XML

' --- FormatName ---
PRINT "--- FormatName ---"
PRINT "FormatName(0) JSON: "; Viper.Data.Serialize.FormatName(0)
PRINT "FormatName(1) YAML: "; Viper.Data.Serialize.FormatName(1)
PRINT "FormatName(2) TOML: "; Viper.Data.Serialize.FormatName(2)
PRINT "FormatName(3) XML: "; Viper.Data.Serialize.FormatName(3)

' --- MimeType ---
PRINT "--- MimeType ---"
PRINT "MimeType(0) JSON: "; Viper.Data.Serialize.MimeType(0)
PRINT "MimeType(1) YAML: "; Viper.Data.Serialize.MimeType(1)
PRINT "MimeType(2) TOML: "; Viper.Data.Serialize.MimeType(2)
PRINT "MimeType(3) XML: "; Viper.Data.Serialize.MimeType(3)

' --- FormatFromName ---
PRINT "--- FormatFromName ---"
PRINT "FormatFromName('json'): "; Viper.Data.Serialize.FormatFromName("json")
PRINT "FormatFromName('yaml'): "; Viper.Data.Serialize.FormatFromName("yaml")
PRINT "FormatFromName('toml'): "; Viper.Data.Serialize.FormatFromName("toml")
PRINT "FormatFromName('xml'): "; Viper.Data.Serialize.FormatFromName("xml")

' --- IsValid ---
PRINT "--- IsValid ---"
DIM jsonStr AS STRING
jsonStr = "{""name"":""Alice"",""age"":30}"
PRINT "IsValid(JSON, 0): "; Viper.Data.Serialize.IsValid(jsonStr, 0)
PRINT "IsValid('not json', 0): "; Viper.Data.Serialize.IsValid("not json {{", 0)

' --- Detect ---
PRINT "--- Detect ---"
PRINT "Detect(JSON): "; Viper.Data.Serialize.Detect(jsonStr)

' --- Parse ---
PRINT "--- Parse ---"
DIM doc AS OBJECT
doc = Viper.Data.Serialize.Parse(jsonStr, 0)
PRINT "Parse(JSON) done"

' --- Format ---
PRINT "--- Format ---"
PRINT "Format(doc, 0): "; Viper.Data.Serialize.Format(doc, 0)

' --- FormatPretty ---
PRINT "--- FormatPretty ---"
PRINT "FormatPretty(doc, 0, 2):"
PRINT Viper.Data.Serialize.FormatPretty(doc, 0, 2)

' --- AutoParse ---
PRINT "--- AutoParse ---"
DIM auto AS OBJECT
auto = Viper.Data.Serialize.AutoParse(jsonStr)
PRINT "AutoParse done"
PRINT "Format: "; Viper.Data.Serialize.Format(auto, 0)

' --- Convert ---
PRINT "--- Convert ---"
PRINT "Convert JSON to YAML:"
PRINT Viper.Data.Serialize.Convert(jsonStr, 0, 1)

PRINT "=== Serialize Demo Complete ==="
END
