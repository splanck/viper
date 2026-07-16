' test_json_csv.bas — Viper.Data.Json + Csv + Uuid + Version
DIM j AS OBJECT
LET j = Viper.Data.Json.Parse("{""name"":""viper"",""ver"":1}")
PRINT Viper.Collections.Map.GetStr(j, "name")
PRINT Viper.Collections.Map.GetInt(j, "ver")
PRINT Viper.Collections.Map.Has(j, "name")
PRINT Viper.Collections.Map.Has(j, "missing")
DIM js AS STRING
LET js = Viper.Data.Json.Format(j)
PRINT Viper.String.Has(js, "viper")

DIM j2 AS OBJECT
LET j2 = Viper.Data.Json.NewObject()
Viper.Collections.Map.SetStr(j2, "key", "value")
Viper.Collections.Map.SetInt(j2, "num", 42)
Viper.Collections.Map.SetBool(j2, "flag", TRUE)
PRINT Viper.Collections.Map.GetStr(j2, "key")
PRINT Viper.Collections.Map.GetInt(j2, "num")
PRINT Viper.Collections.Map.GetBool(j2, "flag")
PRINT Viper.Data.Json.Format(j2)

' Uuid
DIM u1 AS STRING
LET u1 = Viper.Text.Uuid.Generate()
PRINT Viper.String.Has(u1, "-")
DIM u2 AS STRING
LET u2 = Viper.Text.Uuid.Generate()
PRINT u1 <> u2

' Version
PRINT Viper.Text.Version.Parse("1.2.3")
PRINT Viper.Text.Version.Compare("1.2.3", "1.3.0")
PRINT Viper.Text.Version.Compare("2.0.0", "1.9.9")
PRINT Viper.Text.Version.IsValid("1.2.3")
PRINT Viper.Text.Version.IsValid("abc")
PRINT Viper.Text.Version.ParseMajor("1.2.3")
PRINT Viper.Text.Version.ParseMinor("1.2.3")
PRINT Viper.Text.Version.ParsePatch("1.2.3")

PRINT "done"
END
