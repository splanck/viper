' =============================================================================
' API Audit: Viper.Diagnostics.Log - Logging Utilities
' =============================================================================
' Tests: Debug, Info, Warn, Error, get_Level, set_Level, Enabled,
'        get_LevelDebug, get_LevelInfo, get_LevelWarn, get_LevelError, get_LevelOff
' =============================================================================

PRINT "=== API Audit: Viper.Diagnostics.Log ==="

' --- Level Constants ---
PRINT "--- Level Constants ---"
PRINT "get_LevelDebug: "; Viper.Diagnostics.Log.LevelDebug
PRINT "get_LevelInfo: "; Viper.Diagnostics.Log.LevelInfo
PRINT "get_LevelWarn: "; Viper.Diagnostics.Log.LevelWarn
PRINT "get_LevelError: "; Viper.Diagnostics.Log.LevelError
PRINT "get_LevelOff: "; Viper.Diagnostics.Log.LevelOff

' --- get_Level (default) ---
PRINT "--- get_Level (default) ---"
PRINT "Current level: "; Viper.Diagnostics.Log.get_Level()

' --- set_Level to DEBUG ---
PRINT "--- set_Level to DEBUG ---"
Viper.Diagnostics.Log.set_Level(Viper.Diagnostics.Log.LevelDebug)
PRINT "Level set to DEBUG"
PRINT "Current level: "; Viper.Diagnostics.Log.get_Level()

' --- Enabled ---
PRINT "--- Enabled ---"
PRINT "Enabled(DEBUG): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.LevelDebug)
PRINT "Enabled(INFO): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.LevelInfo)
PRINT "Enabled(WARN): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.LevelWarn)
PRINT "Enabled(ERROR): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.LevelError)

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
Viper.Diagnostics.Log.set_Level(Viper.Diagnostics.Log.LevelError)
PRINT "Level set to ERROR"
PRINT "Enabled(DEBUG): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.LevelDebug)
PRINT "Enabled(ERROR): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.LevelError)

' --- Level = OFF ---
PRINT "--- Level = OFF ---"
Viper.Diagnostics.Log.set_Level(Viper.Diagnostics.Log.LevelOff)
PRINT "Level set to OFF"
PRINT "Enabled(ERROR): "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.LevelError)

PRINT "=== Log Demo Complete ==="
END
