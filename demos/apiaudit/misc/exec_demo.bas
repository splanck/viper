' exec_demo.bas
PRINT "=== Viper.Exec Demo ==="
PRINT Viper.Exec.Capture("echo hello")
PRINT Viper.Exec.Run("true")
PRINT Viper.Exec.Run("false")
PRINT "done"
END
