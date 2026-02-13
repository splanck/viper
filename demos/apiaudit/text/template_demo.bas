' template_demo.bas
PRINT "=== Viper.Text.Template Demo ==="
DIM m AS OBJECT
m = NEW Viper.Collections.Map()
m.Set("name", "viper")
m.Set("ver", "1.0")
PRINT Viper.Text.Template.Render("Hello {{name}} v{{ver}}", m)
PRINT Viper.Text.Template.Has("Hello {{name}}", "name")
PRINT Viper.Text.Template.Has("Hello {{name}}", "age")
PRINT Viper.Text.Template.Escape("a {{b}} c")
PRINT "done"
END
