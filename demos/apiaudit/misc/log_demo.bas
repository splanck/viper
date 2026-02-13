' log_demo.bas
PRINT "=== Viper.Log Demo ==="
PRINT Viper.Log.Level
PRINT Viper.Log.DEBUG
PRINT Viper.Log.INFO
PRINT Viper.Log.Enabled(Viper.Log.INFO)
Viper.Log.Info("test info message")
Viper.Log.Debug("test debug message")
PRINT "done"
END
