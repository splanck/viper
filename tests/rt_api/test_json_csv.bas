' test_json_csv.bas — Viper.Text.Json + Csv + Uuid + Version
DIM j AS OBJECT
LET j = Viper.Text.Json.Parse("{""name"":""viper"",""ver"":1}")
PRINT Viper.Text.Json.GetStr(j, "name")
PRINT Viper.Text.Json.GetInt(j, "ver")
PRINT Viper.Text.Json.Has(j, "name")
PRINT Viper.Text.Json.Has(j, "missing")
DIM js AS STRING
LET js = Viper.Text.Json.Stringify(j)
PRINT Viper.String.Has(js, "viper")

DIM j2 AS OBJECT
LET j2 = Viper.Text.Json.NewObject()
Viper.Text.Json.SetStr(j2, "key", "value")
Viper.Text.Json.SetInt(j2, "num", 42)
Viper.Text.Json.SetBool(j2, "flag", TRUE)
PRINT Viper.Text.Json.GetStr(j2, "key")
PRINT Viper.Text.Json.GetInt(j2, "num")
PRINT Viper.Text.Json.GetBool(j2, "flag")
PRINT Viper.Text.Json.Stringify(j2)

' Uuid
DIM u1 AS STRING
LET u1 = Viper.Text.Uuid.V4()
PRINT Viper.String.Has(u1, "-")
DIM u2 AS STRING
LET u2 = Viper.Text.Uuid.V4()
PRINT u1 <> u2

' Version
PRINT Viper.Text.Version.Parse("1.2.3")
PRINT Viper.Text.Version.Compare("1.2.3", "1.3.0")
PRINT Viper.Text.Version.Compare("2.0.0", "1.9.9")
PRINT Viper.Text.Version.IsValid("1.2.3")
PRINT Viper.Text.Version.IsValid("abc")
PRINT Viper.Text.Version.Major("1.2.3")
PRINT Viper.Text.Version.Minor("1.2.3")
PRINT Viper.Text.Version.Patch("1.2.3")

PRINT "done"
END
