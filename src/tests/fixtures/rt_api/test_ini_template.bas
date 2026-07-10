' test_ini_template.bas — Text.Ini, Text.Template

' --- Ini: Parse and query ---
DIM ini_text AS STRING
ini_text = "[server]" + CHR$(10) + "host=localhost" + CHR$(10) + "port=8080" + CHR$(10) + "[db]" + CHR$(10) + "name=mydb"

DIM ini AS OBJECT
ini = Viper.Text.Ini.Parse(ini_text)
PRINT "ini has server: "; Viper.Text.Ini.HasSection(ini, "server")
PRINT "ini has missing: "; Viper.Text.Ini.HasSection(ini, "missing")
PRINT "ini get host: "; Viper.Text.Ini.Get(ini, "server", "host")
PRINT "ini get port: "; Viper.Text.Ini.Get(ini, "server", "port")
PRINT "ini get db name: "; Viper.Text.Ini.Get(ini, "db", "name")

' --- Ini: Set and Format ---
Viper.Text.Ini.Set(ini, "server", "debug", "true")
PRINT "ini get debug: "; Viper.Text.Ini.Get(ini, "server", "debug")

DIM formatted AS STRING
formatted = Viper.Text.Ini.Format(ini)
PRINT "ini format nonempty: "; (LEN(formatted) > 0)

' --- Ini: Remove ---
Viper.Text.Ini.Remove(ini, "server", "debug")

' --- Template: Render with Map ---
DIM tmap AS Viper.Collections.Map
tmap = Viper.Collections.Map.New()
tmap.Set("name", "World")
tmap.Set("lang", "BASIC")

DIM tmpl_result AS STRING
tmpl_result = Viper.Text.Template.Render("Hello {{name}}, welcome to {{lang}}!", tmap)
PRINT "template: "; tmpl_result

' --- Template: Has ---
PRINT "tmpl has name: "; Viper.Text.Template.Has("Hello {{name}}!", "name")
PRINT "tmpl has miss: "; Viper.Text.Template.Has("Hello {{name}}!", "missing")

' --- Template: Escape ---
DIM escaped AS STRING
escaped = Viper.Text.Template.Escape("use {{braces}}")
PRINT "escaped nonempty: "; (LEN(escaped) > 0)

PRINT "done"
END
