' ini_demo.bas
PRINT "=== Viper.Text.Ini Demo ==="
DIM doc AS OBJECT
doc = Viper.Text.Ini.Parse("[section]" + CHR$(10) + "key = value" + CHR$(10))
PRINT Viper.Text.Ini.Get(doc, "section", "key")
PRINT Viper.Text.Ini.HasSection(doc, "section")
PRINT Viper.Text.Ini.HasSection(doc, "other")
PRINT "done"
END
