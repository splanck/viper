' test_log_exec.bas — Log, Exec, NumberFormat
PRINT "log debug level: "; Viper.Diagnostics.Log.LevelDebug
PRINT "log info level: "; Viper.Diagnostics.Log.LevelInfo
PRINT "log warn level: "; Viper.Diagnostics.Log.LevelWarn
PRINT "log error level: "; Viper.Diagnostics.Log.LevelError
PRINT "log off level: "; Viper.Diagnostics.Log.LevelOff
PRINT "log level: "; Viper.Diagnostics.Log.Level
PRINT "log enabled debug: "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.LevelDebug)
Viper.Diagnostics.Log.Info("test info message")
Viper.Diagnostics.Log.Warn("test warn message")
Viper.Diagnostics.Log.Error("test error message")

PRINT "exec echo: "; Viper.System.Exec.ShellCapture("echo hello")

PRINT "numfmt bytes: "; Viper.Text.InvariantNumberFormat.Bytes(1048576)
PRINT "numfmt ordinal 1: "; Viper.Text.InvariantNumberFormat.Ordinal(1)
PRINT "numfmt ordinal 2: "; Viper.Text.InvariantNumberFormat.Ordinal(2)
PRINT "numfmt ordinal 3: "; Viper.Text.InvariantNumberFormat.Ordinal(3)
PRINT "numfmt ordinal 11: "; Viper.Text.InvariantNumberFormat.Ordinal(11)
PRINT "numfmt pad: "; Viper.Text.InvariantNumberFormat.Pad(42, 5)
PRINT "numfmt percent: "; Viper.Text.InvariantNumberFormat.Percent(0.756)
PRINT "numfmt thousands: "; Viper.Text.InvariantNumberFormat.Thousands(1234567, ",")
PRINT "numfmt towords: "; Viper.Text.InvariantNumberFormat.ToWords(42)
PRINT "numfmt decimals: "; Viper.Text.InvariantNumberFormat.Decimals(3.14159, 2)
PRINT "numfmt currency: "; Viper.Text.InvariantNumberFormat.Currency(1234.56, "$")

PRINT "done"
END
