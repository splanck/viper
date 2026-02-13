' jsonpath_demo.bas
PRINT "=== Viper.Text.JsonPath Demo ==="
DIM doc AS OBJECT
doc = Viper.Text.Json.Parse("{""name"":""viper"",""ver"":1}")
PRINT Viper.Text.JsonPath.Has(doc, "$.name")
PRINT Viper.Text.JsonPath.Has(doc, "$.missing")
PRINT Viper.Text.JsonPath.GetStr(doc, "$.name")
PRINT Viper.Text.JsonPath.GetInt(doc, "$.ver")
PRINT "done"
END
