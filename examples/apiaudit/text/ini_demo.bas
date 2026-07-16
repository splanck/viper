' Viper.Data.Ini API Audit - INI Config File Parsing
' Tests all Ini functions

PRINT "=== Viper.Data.Ini API Audit ==="

DIM iniText AS STRING
iniText = "[database]" + CHR(10)
iniText = iniText + "host=localhost" + CHR(10)
iniText = iniText + "port=5432" + CHR(10)
iniText = iniText + "name=mydb" + CHR(10) + CHR(10)
iniText = iniText + "[server]" + CHR(10)
iniText = iniText + "host=0.0.0.0" + CHR(10)
iniText = iniText + "port=8080" + CHR(10)
iniText = iniText + "workers=4"

' --- Parse ---
PRINT "--- Parse ---"
DIM cfg AS OBJECT
cfg = Viper.Data.Ini.Parse(iniText)
PRINT "Parsed successfully"

' --- Get ---
PRINT "--- Get ---"
PRINT "db host: "; Viper.Data.Ini.Get(cfg, "database", "host")
PRINT "db port: "; Viper.Data.Ini.Get(cfg, "database", "port")
PRINT "db name: "; Viper.Data.Ini.Get(cfg, "database", "name")
PRINT "srv host: "; Viper.Data.Ini.Get(cfg, "server", "host")
PRINT "srv port: "; Viper.Data.Ini.Get(cfg, "server", "port")
PRINT "srv workers: "; Viper.Data.Ini.Get(cfg, "server", "workers")

' --- Set ---
PRINT "--- Set ---"
Viper.Data.Ini.Set(cfg, "database", "host", "db.example.com")
PRINT "Updated host: "; Viper.Data.Ini.Get(cfg, "database", "host")

' Add new key to existing section
Viper.Data.Ini.Set(cfg, "database", "ssl", "true")
PRINT "New key ssl: "; Viper.Data.Ini.Get(cfg, "database", "ssl")

' Add new section
Viper.Data.Ini.Set(cfg, "logging", "level", "info")
PRINT "New section logging.level: "; Viper.Data.Ini.Get(cfg, "logging", "level")

' --- HasSection ---
PRINT "--- HasSection ---"
PRINT "Has database: "; Viper.Data.Ini.HasSection(cfg, "database")
PRINT "Has server: "; Viper.Data.Ini.HasSection(cfg, "server")
PRINT "Has logging: "; Viper.Data.Ini.HasSection(cfg, "logging")
PRINT "Has nonexistent: "; Viper.Data.Ini.HasSection(cfg, "nonexistent")

' --- Sections ---
PRINT "--- Sections ---"
DIM sections AS OBJECT
sections = Viper.Data.Ini.Sections(cfg)
PRINT "Section count: "; Viper.Collections.Seq.get_Count(sections)
DIM si AS INTEGER
FOR si = 0 TO Viper.Collections.Seq.get_Count(sections) - 1
    PRINT "Section: "; sections.Get(si)
NEXT si

' --- Remove ---
PRINT "--- Remove ---"
PRINT "Remove ssl: "; Viper.Data.Ini.Remove(cfg, "database", "ssl")
PRINT "Remove nonexistent: "; Viper.Data.Ini.Remove(cfg, "database", "nonexistent")

' --- Format ---
PRINT "--- Format ---"
PRINT Viper.Data.Ini.Format(cfg)

PRINT "=== Ini Demo Complete ==="
END
