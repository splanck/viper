' =============================================================================
' API Audit: Viper.Log - Logging Utilities
' =============================================================================
' Tests: Debug, Info, Warn, Error, get_Level, set_Level, Enabled,
'        get_DEBUG, get_INFO, get_WARN, get_ERROR, get_OFF
' =============================================================================

PRINT "=== API Audit: Viper.Log ==="

' --- Level Constants ---
PRINT "--- Level Constants ---"
PRINT "get_DEBUG: "; Viper.Log.get_DEBUG()
PRINT "get_INFO: "; Viper.Log.get_INFO()
PRINT "get_WARN: "; Viper.Log.get_WARN()
PRINT "get_ERROR: "; Viper.Log.get_ERROR()
PRINT "get_OFF: "; Viper.Log.get_OFF()

' --- get_Level (default) ---
PRINT "--- get_Level (default) ---"
PRINT "Current level: "; Viper.Log.get_Level()

' --- set_Level to DEBUG ---
PRINT "--- set_Level to DEBUG ---"
Viper.Log.set_Level(Viper.Log.get_DEBUG())
PRINT "Level set to DEBUG"
PRINT "Current level: "; Viper.Log.get_Level()

' --- Enabled ---
PRINT "--- Enabled ---"
PRINT "Enabled(DEBUG): "; Viper.Log.Enabled(Viper.Log.get_DEBUG())
PRINT "Enabled(INFO): "; Viper.Log.Enabled(Viper.Log.get_INFO())
PRINT "Enabled(WARN): "; Viper.Log.Enabled(Viper.Log.get_WARN())
PRINT "Enabled(ERROR): "; Viper.Log.Enabled(Viper.Log.get_ERROR())

' --- Debug ---
PRINT "--- Debug ---"
Viper.Log.Debug("This is a debug message")

' --- Info ---
PRINT "--- Info ---"
Viper.Log.Info("This is an info message")

' --- Warn ---
PRINT "--- Warn ---"
Viper.Log.Warn("This is a warning message")

' --- Error ---
PRINT "--- Error ---"
Viper.Log.Error("This is an error message")

' --- Change level to ERROR only ---
PRINT "--- Level = ERROR ---"
Viper.Log.set_Level(Viper.Log.get_ERROR())
PRINT "Level set to ERROR"
PRINT "Enabled(DEBUG): "; Viper.Log.Enabled(Viper.Log.get_DEBUG())
PRINT "Enabled(ERROR): "; Viper.Log.Enabled(Viper.Log.get_ERROR())

' --- Level = OFF ---
PRINT "--- Level = OFF ---"
Viper.Log.set_Level(Viper.Log.get_OFF())
PRINT "Level set to OFF"
PRINT "Enabled(ERROR): "; Viper.Log.Enabled(Viper.Log.get_ERROR())

PRINT "=== Log Demo Complete ==="
END
