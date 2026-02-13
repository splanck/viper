' version_demo.bas
PRINT "=== Viper.Text.Version Demo ==="
DIM v AS OBJECT
v = NEW Viper.Text.Version("1.2.3-beta+build42")
PRINT v.Major
PRINT v.Minor
PRINT v.Patch
PRINT v.Prerelease
PRINT v.Build
PRINT v.ToString()
PRINT v.BumpPatch()
PRINT Viper.Text.Version.IsValid("1.0.0")
PRINT Viper.Text.Version.IsValid("bad")
PRINT "done"
END
