' =============================================================================
' API Audit: Viper.Diagnostics.Log - Logging Utilities
' =============================================================================
' Tests: Debug, Info, Warn, Error, get_Level, set_Level, Enabled,
'        get_DEBUG, get_INFO, get_WARN, get_ERROR, get_OFF
' =============================================================================

PRINT "=== API Audit: Viper.Diagnostics.Log ==="

' --- Level Constants ---
PRINT "--- Level Constants ---"
PRINT "get_DEBUG: "; Viper.Diagnostics.Log.get_DEBUG()
PRINT "get_INFO: "; Viper.Diagnostics.Log.get_INFO()
PRINT "get_WARN: "; Viper.Diagnostics.Log.get_WARN()
PRINT "get_ERROR: "; Viper.Diagnostics.Log.get_ERROR()
PRINT "get_OFF: "; Viper.Diagnostics.Log.get_OFF()

' --- get_Level (default) ---
PRINT "--- get_Level (default) ---"
PRINT "Current level: "; Viper.Diagnostics.Log.get_Level()

' --- set_Level to DEBUG ---
PRINT "--- set_Level to DEBUG ---"
Viper.Diagnostics.Log.set_Level(Viper.Diagnostics.Log.get_DEBUG())
PRINT "Level set to DEBUG"
PRINT "Current level: "; Viper.Diagnostics.Log.get_Level()

' --- Enabled ---
PRINT "--- Enabled ---"
PRINT "Enabled(DEBUG): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.get_DEBUG())
PRINT "Enabled(INFO): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.get_INFO())
PRINT "Enabled(WARN): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.get_WARN())
PRINT "Enabled(ERROR): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.get_ERROR())

' --- Debug ---
PRINT "--- Debug ---"
Viper.Diagnostics.Log.Debug("This is a debug message")

' --- Info ---
PRINT "--- Info ---"
Viper.Diagnostics.Log.Info("This is an info message")

' --- Warn ---
PRINT "--- Warn ---"
Viper.Diagnostics.Log.Warn("This is a warning message")

' --- Error ---
PRINT "--- Error ---"
Viper.Diagnostics.Log.Error("This is an error message")

' --- Change level to ERROR only ---
PRINT "--- Level = ERROR ---"
Viper.Diagnostics.Log.set_Level(Viper.Diagnostics.Log.get_ERROR())
PRINT "Level set to ERROR"
PRINT "Enabled(DEBUG): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.get_DEBUG())
PRINT "Enabled(ERROR): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.get_ERROR())

' --- Level = OFF ---
PRINT "--- Level = OFF ---"
Viper.Diagnostics.Log.set_Level(Viper.Diagnostics.Log.get_OFF())
PRINT "Level set to OFF"
PRINT "Enabled(ERROR): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.get_ERROR())

PRINT "=== Log Demo Complete ==="
END
