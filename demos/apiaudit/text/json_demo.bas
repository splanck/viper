' Viper.Text.Json API Audit - JSON Parsing and Formatting
' Tests all Json functions

PRINT "=== Viper.Text.Json API Audit ==="

' --- IsValid ---
PRINT "--- IsValid ---"
PRINT "Valid object: "; Viper.Text.Json.IsValid("{""name"":""Alice"",""age"":30}")
PRINT "Valid array: "; Viper.Text.Json.IsValid("[1,2,3]")
PRINT "Invalid: "; Viper.Text.Json.IsValid("not json")
PRINT "Empty: "; Viper.Text.Json.IsValid("")

' --- Parse ---
PRINT "--- Parse ---"
DIM obj AS OBJECT
obj = Viper.Text.Json.Parse("{""name"":""Alice"",""age"":30}")
PRINT "TypeOf: "; Viper.Text.Json.TypeOf(obj)

' --- ParseObject ---
PRINT "--- ParseObject ---"
DIM obj2 AS OBJECT
obj2 = Viper.Text.Json.ParseObject("{""x"":1,""y"":2}")
PRINT "TypeOf: "; Viper.Text.Json.TypeOf(obj2)

' --- ParseArray ---
PRINT "--- ParseArray ---"
DIM arr AS OBJECT
arr = Viper.Text.Json.ParseArray("[1,2,3,4,5]")
PRINT "TypeOf: "; Viper.Text.Json.TypeOf(arr)

' --- Format ---
PRINT "--- Format ---"
DIM data AS OBJECT
data = Viper.Text.Json.Parse("{""name"":""Bob"",""active"":true}")
PRINT Viper.Text.Json.Format(data)

' --- FormatPretty ---
PRINT "--- FormatPretty ---"
DIM data2 AS OBJECT
data2 = Viper.Text.Json.Parse("{""name"":""Carol"",""scores"":[90,85,92]}")
PRINT Viper.Text.Json.FormatPretty(data2, 2)

' --- TypeOf ---
PRINT "--- TypeOf ---"
PRINT "String: "; Viper.Text.Json.TypeOf(Viper.Text.Json.Parse("""hello"""))
PRINT "Number: "; Viper.Text.Json.TypeOf(Viper.Text.Json.Parse("42"))
PRINT "Boolean: "; Viper.Text.Json.TypeOf(Viper.Text.Json.Parse("true"))
PRINT "Null: "; Viper.Text.Json.TypeOf(Viper.Text.Json.Parse("null"))
PRINT "Array: "; Viper.Text.Json.TypeOf(Viper.Text.Json.Parse("[1,2]"))
PRINT "Object: "; Viper.Text.Json.TypeOf(Viper.Text.Json.Parse("{""a"":1}"))

PRINT "=== Json Demo Complete ==="
END
