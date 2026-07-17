' =============================================================================
' API Audit: Zanna.Diagnostics.Log - Logging Utilities
' =============================================================================
' Tests: Debug, Info, Warn, Error, get_Level, set_Level, Enabled,
'        get_LevelDebug, get_LevelInfo, get_LevelWarn, get_LevelError, get_LevelOff
' =============================================================================

PRINT "=== API Audit: Zanna.Diagnostics.Log ==="

' --- Level Constants ---
PRINT "--- Level Constants ---"
PRINT "get_LevelDebug: "; Zanna.Diagnostics.Log.LevelDebug
PRINT "get_LevelInfo: "; Zanna.Diagnostics.Log.LevelInfo
PRINT "get_LevelWarn: "; Zanna.Diagnostics.Log.LevelWarn
PRINT "get_LevelError: "; Zanna.Diagnostics.Log.LevelError
PRINT "get_LevelOff: "; Zanna.Diagnostics.Log.LevelOff

' --- get_Level (default) ---
PRINT "--- get_Level (default) ---"
PRINT "Current level: "; Zanna.Diagnostics.Log.get_Level()

' --- set_Level to DEBUG ---
PRINT "--- set_Level to DEBUG ---"
Zanna.Diagnostics.Log.set_Level(Zanna.Diagnostics.Log.LevelDebug)
PRINT "Level set to DEBUG"
PRINT "Current level: "; Zanna.Diagnostics.Log.get_Level()

' --- Enabled ---
PRINT "--- Enabled ---"
PRINT "Enabled(DEBUG): "; Zanna.Diagnostics.Log.Enabled(Zanna.Diagnostics.Log.LevelDebug)
PRINT "Enabled(INFO): "; Zanna.Diagnostics.Log.Enabled(Zanna.Diagnostics.Log.LevelInfo)
PRINT "Enabled(WARN): "; Zanna.Diagnostics.Log.Enabled(Zanna.Diagnostics.Log.LevelWarn)
PRINT "Enabled(ERROR): "; Zanna.Diagnostics.Log.Enabled(Zanna.Diagnostics.Log.LevelError)

' --- Debug ---
PRINT "--- Debug ---"
Zanna.Diagnostics.Log.Debug("This is a debug message")

' --- Info ---
PRINT "--- Info ---"
Zanna.Diagnostics.Log.Info("This is an info message")

' --- Warn ---
PRINT "--- Warn ---"
Zanna.Diagnostics.Log.Warn("This is a warning message")

' --- Error ---
PRINT "--- Error ---"
Zanna.Diagnostics.Log.Error("This is an error message")

' --- Change level to ERROR only ---
PRINT "--- Level = ERROR ---"
Zanna.Diagnostics.Log.set_Level(Zanna.Diagnostics.Log.LevelError)
PRINT "Level set to ERROR"
PRINT "Enabled(DEBUG): "; Zanna.Diagnostics.Log.Enabled(Zanna.Diagnostics.Log.LevelDebug)
PRINT "Enabled(ERROR): "; Zanna.Diagnostics.Log.Enabled(Zanna.Diagnostics.Log.LevelError)

' --- Level = OFF ---
PRINT "--- Level = OFF ---"
Zanna.Diagnostics.Log.set_Level(Zanna.Diagnostics.Log.LevelOff)
PRINT "Level set to OFF"
PRINT "Enabled(ERROR): "; Zanna.Diagnostics.Log.Enabled(Zanna.Diagnostics.Log.LevelError)

PRINT "=== Log Demo Complete ==="
END
