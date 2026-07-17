' Zanna.Data.JsonPath API Audit - JSONPath Query Expressions
' Tests all JsonPath functions

PRINT "=== Zanna.Data.JsonPath API Audit ==="

DIM json AS STRING
json = "{""store"":{""book"":[{""title"":""Moby Dick"",""price"":10},{""title"":""1984"",""price"":8}],""name"":""Books R Us""}}"
DIM doc AS OBJECT
doc = Zanna.Data.Json.Parse(json)

' --- Get ---
PRINT "--- Get ---"
DIM result AS OBJECT
result = Zanna.Data.JsonPath.Get(doc, "$.store.name")
PRINT Zanna.Data.Json.Format(result)

' --- GetStr ---
PRINT "--- GetStr ---"
PRINT Zanna.Data.JsonPath.GetStr(doc, "$.store.name")

' --- GetInt ---
PRINT "--- GetInt ---"
PRINT "Book 0 price: "; Zanna.Data.JsonPath.GetInt(doc, "$.store.book[0].price")
PRINT "Book 1 price: "; Zanna.Data.JsonPath.GetInt(doc, "$.store.book[1].price")

' --- Has ---
PRINT "--- Has ---"
PRINT "Has store.name: "; Zanna.Data.JsonPath.Has(doc, "$.store.name")
PRINT "Has store.address: "; Zanna.Data.JsonPath.Has(doc, "$.store.address")
PRINT "Has book[0].title: "; Zanna.Data.JsonPath.Has(doc, "$.store.book[0].title")

' --- GetOr ---
PRINT "--- GetOr ---"
DIM fallback AS OBJECT
fallback = Zanna.Data.Json.Parse("""default""")
DIM found AS OBJECT
found = Zanna.Data.JsonPath.GetOr(doc, "$.store.name", fallback)
PRINT "Found: "; Zanna.Data.Json.Format(found)
DIM missing AS OBJECT
missing = Zanna.Data.JsonPath.GetOr(doc, "$.store.address", fallback)
PRINT "Missing: "; Zanna.Data.Json.Format(missing)

' --- Query ---
PRINT "--- Query ---"
DIM titles AS OBJECT
titles = Zanna.Data.JsonPath.Query(doc, "$.store.book[*].title")
PRINT Zanna.Data.Json.Format(titles)

PRINT "=== JsonPath Demo Complete ==="
END
