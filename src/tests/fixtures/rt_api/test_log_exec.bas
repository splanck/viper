' test_log_exec.bas — Log, Exec, NumberFormat
PRINT "log debug level: "; Zanna.Diagnostics.Log.LevelDebug
PRINT "log info level: "; Zanna.Diagnostics.Log.LevelInfo
PRINT "log warn level: "; Zanna.Diagnostics.Log.LevelWarn
PRINT "log error level: "; Zanna.Diagnostics.Log.LevelError
PRINT "log off level: "; Zanna.Diagnostics.Log.LevelOff
PRINT "log level: "; Zanna.Diagnostics.Log.Level
PRINT "log enabled debug: "; Zanna.Diagnostics.Log.Enabled(Zanna.Diagnostics.Log.LevelDebug)
Zanna.Diagnostics.Log.Info("test info message")
Zanna.Diagnostics.Log.Warn("test warn message")
Zanna.Diagnostics.Log.Error("test error message")

PRINT "exec echo: "; Zanna.System.Exec.ShellCapture("echo hello")

PRINT "numfmt bytes: "; Zanna.Text.InvariantNumberFormat.Bytes(1048576)
PRINT "numfmt ordinal 1: "; Zanna.Text.InvariantNumberFormat.Ordinal(1)
PRINT "numfmt ordinal 2: "; Zanna.Text.InvariantNumberFormat.Ordinal(2)
PRINT "numfmt ordinal 3: "; Zanna.Text.InvariantNumberFormat.Ordinal(3)
PRINT "numfmt ordinal 11: "; Zanna.Text.InvariantNumberFormat.Ordinal(11)
PRINT "numfmt pad: "; Zanna.Text.InvariantNumberFormat.Pad(42, 5)
PRINT "numfmt percent: "; Zanna.Text.InvariantNumberFormat.Percent(0.756)
PRINT "numfmt thousands: "; Zanna.Text.InvariantNumberFormat.Thousands(1234567, ",")
PRINT "numfmt towords: "; Zanna.Text.InvariantNumberFormat.ToWords(42)
PRINT "numfmt decimals: "; Zanna.Text.InvariantNumberFormat.Decimals(3.14159, 2)
PRINT "numfmt currency: "; Zanna.Text.InvariantNumberFormat.Currency(1234.56, "$")

PRINT "done"
END
