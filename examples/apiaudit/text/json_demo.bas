' Zanna.Data.Json API Audit - JSON Parsing and Formatting
' Tests all Json functions

PRINT "=== Zanna.Data.Json API Audit ==="

' --- IsValid ---
PRINT "--- IsValid ---"
PRINT "Valid object: "; Zanna.Data.Json.IsValid("{""name"":""Alice"",""age"":30}")
PRINT "Valid array: "; Zanna.Data.Json.IsValid("[1,2,3]")
PRINT "Invalid: "; Zanna.Data.Json.IsValid("not json")
PRINT "Empty: "; Zanna.Data.Json.IsValid("")

' --- Parse ---
PRINT "--- Parse ---"
DIM obj AS OBJECT
obj = Zanna.Data.Json.Parse("{""name"":""Alice"",""age"":30}")
PRINT "TypeOf: "; Zanna.Data.Json.TypeOf(obj)

' --- ParseObject ---
PRINT "--- ParseObject ---"
DIM obj2 AS OBJECT
obj2 = Zanna.Data.Json.ParseObject("{""x"":1,""y"":2}")
PRINT "TypeOf: "; Zanna.Data.Json.TypeOf(obj2)

' --- ParseArray ---
PRINT "--- ParseArray ---"
DIM arr AS OBJECT
arr = Zanna.Data.Json.ParseArray("[1,2,3,4,5]")
PRINT "TypeOf: "; Zanna.Data.Json.TypeOf(arr)

' --- Format ---
PRINT "--- Format ---"
DIM data AS OBJECT
data = Zanna.Data.Json.Parse("{""name"":""Bob"",""active"":true}")
PRINT Zanna.Data.Json.Format(data)

' --- FormatPretty ---
PRINT "--- FormatPretty ---"
DIM data2 AS OBJECT
data2 = Zanna.Data.Json.Parse("{""name"":""Carol"",""scores"":[90,85,92]}")
PRINT Zanna.Data.Json.FormatPretty(data2, 2)

' --- TypeOf ---
PRINT "--- TypeOf ---"
PRINT "String: "; Zanna.Data.Json.TypeOf(Zanna.Data.Json.Parse("""hello"""))
PRINT "Number: "; Zanna.Data.Json.TypeOf(Zanna.Data.Json.Parse("42"))
PRINT "Boolean: "; Zanna.Data.Json.TypeOf(Zanna.Data.Json.Parse("true"))
PRINT "Null: "; Zanna.Data.Json.TypeOf(Zanna.Data.Json.Parse("null"))
PRINT "Array: "; Zanna.Data.Json.TypeOf(Zanna.Data.Json.Parse("[1,2]"))
PRINT "Object: "; Zanna.Data.Json.TypeOf(Zanna.Data.Json.Parse("{""a"":1}"))

PRINT "=== Json Demo Complete ==="
END
