' dir_demo.bas
PRINT "=== Viper.IO.Dir Demo ==="
PRINT Viper.IO.Dir.Current()
PRINT Viper.IO.Dir.Exists("/tmp")
PRINT Viper.IO.Dir.Exists("/nonexistent_xyz")
PRINT "done"
END
