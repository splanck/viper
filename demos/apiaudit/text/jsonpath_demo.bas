' Viper.Text.JsonPath API Audit - JSONPath Query Expressions
' Tests all JsonPath functions

PRINT "=== Viper.Text.JsonPath API Audit ==="

DIM json AS STRING
json = "{""store"":{""book"":[{""title"":""Moby Dick"",""price"":10},{""title"":""1984"",""price"":8}],""name"":""Books R Us""}}"
DIM doc AS OBJECT
doc = Viper.Text.Json.Parse(json)

' --- Get ---
PRINT "--- Get ---"
DIM result AS OBJECT
result = Viper.Text.JsonPath.Get(doc, "$.store.name")
PRINT Viper.Text.Json.Format(result)

' --- GetStr ---
PRINT "--- GetStr ---"
PRINT Viper.Text.JsonPath.GetStr(doc, "$.store.name")

' --- GetInt ---
PRINT "--- GetInt ---"
PRINT "Book 0 price: "; Viper.Text.JsonPath.GetInt(doc, "$.store.book[0].price")
PRINT "Book 1 price: "; Viper.Text.JsonPath.GetInt(doc, "$.store.book[1].price")

' --- Has ---
PRINT "--- Has ---"
PRINT "Has store.name: "; Viper.Text.JsonPath.Has(doc, "$.store.name")
PRINT "Has store.address: "; Viper.Text.JsonPath.Has(doc, "$.store.address")
PRINT "Has book[0].title: "; Viper.Text.JsonPath.Has(doc, "$.store.book[0].title")

' --- GetOr ---
PRINT "--- GetOr ---"
DIM fallback AS OBJECT
fallback = Viper.Text.Json.Parse("""default""")
DIM found AS OBJECT
found = Viper.Text.JsonPath.GetOr(doc, "$.store.name", fallback)
PRINT "Found: "; Viper.Text.Json.Format(found)
DIM missing AS OBJECT
missing = Viper.Text.JsonPath.GetOr(doc, "$.store.address", fallback)
PRINT "Missing: "; Viper.Text.Json.Format(missing)

' --- Query ---
PRINT "--- Query ---"
DIM titles AS OBJECT
titles = Viper.Text.JsonPath.Query(doc, "$.store.book[*].title")
PRINT Viper.Text.Json.Format(titles)

PRINT "=== JsonPath Demo Complete ==="
END
