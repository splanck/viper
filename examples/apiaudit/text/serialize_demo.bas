' =============================================================================
' API Audit: Viper.Data.Serialize - Multi-Format Serialization
' =============================================================================
' Tests: ParseResult, Parse, Error, Format, FormatPretty, IsValid, Detect,
'        AutoParseResult, AutoParse, Convert, FormatName, MimeType, FormatFromName
' =============================================================================

PRINT "=== API Audit: Viper.Data.Serialize ==="

' Format constants: 0=JSON, 1=XML, 2=YAML, 3=TOML, 4=CSV

' --- FormatName ---
PRINT "--- FormatName ---"
PRINT "FormatName(0) JSON: "; Viper.Data.Serialize.FormatName(0)
PRINT "FormatName(1) XML: "; Viper.Data.Serialize.FormatName(1)
PRINT "FormatName(2) YAML: "; Viper.Data.Serialize.FormatName(2)
PRINT "FormatName(3) TOML: "; Viper.Data.Serialize.FormatName(3)
PRINT "FormatName(4) CSV: "; Viper.Data.Serialize.FormatName(4)

' --- MimeType ---
PRINT "--- MimeType ---"
PRINT "MimeType(0) JSON: "; Viper.Data.Serialize.MimeType(0)
PRINT "MimeType(1) XML: "; Viper.Data.Serialize.MimeType(1)
PRINT "MimeType(2) YAML: "; Viper.Data.Serialize.MimeType(2)
PRINT "MimeType(3) TOML: "; Viper.Data.Serialize.MimeType(3)
PRINT "MimeType(4) CSV: "; Viper.Data.Serialize.MimeType(4)

' --- FormatFromName ---
PRINT "--- FormatFromName ---"
PRINT "FormatFromName('json'): "; Viper.Data.Serialize.FormatFromName("json")
PRINT "FormatFromName('yaml'): "; Viper.Data.Serialize.FormatFromName("yaml")
PRINT "FormatFromName('toml'): "; Viper.Data.Serialize.FormatFromName("toml")
PRINT "FormatFromName('xml'): "; Viper.Data.Serialize.FormatFromName("xml")
PRINT "FormatFromName('csv'): "; Viper.Data.Serialize.FormatFromName("csv")

' --- IsValid ---
PRINT "--- IsValid ---"
DIM jsonStr AS STRING
jsonStr = "{""name"":""Alice"",""age"":30}"
PRINT "IsValid(JSON, 0): "; Viper.Data.Serialize.IsValid(jsonStr, 0)
PRINT "IsValid('not json', 0): "; Viper.Data.Serialize.IsValid("not json {{", 0)

' --- Detect ---
PRINT "--- Detect ---"
PRINT "Detect(JSON): "; Viper.Data.Serialize.Detect(jsonStr)
PRINT "Detect(YAML): "; Viper.Data.Serialize.Detect("name: Alice" & Chr(10) & "age: 30")
PRINT "Detect(TOML): "; Viper.Data.Serialize.Detect("[person]" & Chr(10) & "name = ""Alice""")
PRINT "Detect(CSV): "; Viper.Data.Serialize.Detect("name,age" & Chr(10) & "Alice,30")
PRINT "Detect(plain text): "; Viper.Data.Serialize.Detect("plain text")

' --- ParseResult ---
PRINT "--- ParseResult ---"
DIM doc AS OBJECT
DIM parsed AS OBJECT
parsed = Viper.Data.Serialize.ParseResult(jsonStr, 0)
PRINT "ParseResult IsOk: "; parsed.IsOk
doc = parsed.Unwrap()
PRINT "ParseResult(JSON) done"

DIM badParse AS OBJECT
badParse = Viper.Data.Serialize.ParseResult("{", 0)
PRINT "Bad ParseResult IsErr: "; badParse.IsErr
PRINT "Bad ParseResult Err: "; badParse.UnwrapErrStr()

' --- Parse / Error compatibility ---
PRINT "--- Parse / Error compatibility ---"
DIM legacyDoc AS OBJECT
legacyDoc = Viper.Data.Serialize.Parse(jsonStr, 0)
PRINT "Legacy Parse done"
DIM legacyBad AS OBJECT
legacyBad = Viper.Data.Serialize.Parse("{", 0)
PRINT "Legacy Error: "; Viper.Data.Serialize.Error()

' --- Format ---
PRINT "--- Format ---"
PRINT "Format(doc, 0): "; Viper.Data.Serialize.Format(doc, 0)

' --- FormatPretty ---
PRINT "--- FormatPretty ---"
PRINT "FormatPretty(doc, 0, 2):"
PRINT Viper.Data.Serialize.FormatPretty(doc, 0, 2)

' --- AutoParseResult ---
PRINT "--- AutoParseResult ---"
DIM auto AS OBJECT
DIM autoResult AS OBJECT
autoResult = Viper.Data.Serialize.AutoParseResult(jsonStr)
PRINT "AutoParseResult IsOk: "; autoResult.IsOk
auto = autoResult.Unwrap()
PRINT "AutoParseResult done"
PRINT "Format: "; Viper.Data.Serialize.Format(auto, 0)

DIM autoBad AS OBJECT
autoBad = Viper.Data.Serialize.AutoParseResult("plain text")
PRINT "AutoParseResult bad IsErr: "; autoBad.IsErr
PRINT "AutoParseResult bad Err: "; autoBad.UnwrapErrStr()

' --- AutoParse compatibility ---
PRINT "--- AutoParse compatibility ---"
DIM autoLegacy AS OBJECT
autoLegacy = Viper.Data.Serialize.AutoParse(jsonStr)
PRINT "AutoParse compatibility done"

' --- Convert ---
PRINT "--- Convert ---"
PRINT "Convert JSON to YAML:"
PRINT Viper.Data.Serialize.Convert(jsonStr, 0, 2)
PRINT "Convert JSON to XML:"
PRINT Viper.Data.Serialize.Convert(jsonStr, 0, 1)
PRINT "Convert JSON to TOML:"
PRINT Viper.Data.Serialize.Convert(jsonStr, 0, 3)
PRINT "Convert JSON to CSV:"
PRINT Viper.Data.Serialize.Convert(jsonStr, 0, 4)

PRINT "=== Serialize Demo Complete ==="
END
