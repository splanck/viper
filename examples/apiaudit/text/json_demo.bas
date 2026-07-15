' Viper.Data.Json API Audit - JSON Parsing and Formatting
' Tests all Json functions

PRINT "=== Viper.Data.Json API Audit ==="

' --- IsValid ---
PRINT "--- IsValid ---"
PRINT "Valid object: "; Viper.Data.Json.IsValid("{""name"":""Alice"",""age"":30}")
PRINT "Valid array: "; Viper.Data.Json.IsValid("[1,2,3]")
PRINT "Invalid: "; Viper.Data.Json.IsValid("not json")
PRINT "Empty: "; Viper.Data.Json.IsValid("")

' --- Parse ---
PRINT "--- Parse ---"
DIM obj AS OBJECT
obj = Viper.Data.Json.Parse("{""name"":""Alice"",""age"":30}")
PRINT "TypeOf: "; Viper.Data.Json.TypeOf(obj)

' --- ParseObject ---
PRINT "--- ParseObject ---"
DIM obj2 AS OBJECT
obj2 = Viper.Data.Json.ParseObject("{""x"":1,""y"":2}")
PRINT "TypeOf: "; Viper.Data.Json.TypeOf(obj2)

' --- ParseArray ---
PRINT "--- ParseArray ---"
DIM arr AS OBJECT
arr = Viper.Data.Json.ParseArray("[1,2,3,4,5]")
PRINT "TypeOf: "; Viper.Data.Json.TypeOf(arr)

' --- Format ---
PRINT "--- Format ---"
DIM data AS OBJECT
data = Viper.Data.Json.Parse("{""name"":""Bob"",""active"":true}")
PRINT Viper.Data.Json.Format(data)

' --- FormatPretty ---
PRINT "--- FormatPretty ---"
DIM data2 AS OBJECT
data2 = Viper.Data.Json.Parse("{""name"":""Carol"",""scores"":[90,85,92]}")
PRINT Viper.Data.Json.FormatPretty(data2, 2)

' --- TypeOf ---
PRINT "--- TypeOf ---"
PRINT "String: "; Viper.Data.Json.TypeOf(Viper.Data.Json.Parse("""hello"""))
PRINT "Number: "; Viper.Data.Json.TypeOf(Viper.Data.Json.Parse("42"))
PRINT "Boolean: "; Viper.Data.Json.TypeOf(Viper.Data.Json.Parse("true"))
PRINT "Null: "; Viper.Data.Json.TypeOf(Viper.Data.Json.Parse("null"))
PRINT "Array: "; Viper.Data.Json.TypeOf(Viper.Data.Json.Parse("[1,2]"))
PRINT "Object: "; Viper.Data.Json.TypeOf(Viper.Data.Json.Parse("{""a"":1}"))

PRINT "=== Json Demo Complete ==="
END
