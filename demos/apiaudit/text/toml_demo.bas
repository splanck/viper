' toml_demo.bas
PRINT "=== Viper.Text.Toml Demo ==="
PRINT Viper.Text.Toml.IsValid("[section]" + CHR$(10) + "key = ""value""")
PRINT Viper.Text.Toml.IsValid("not valid toml [[[")
PRINT "done"
END
