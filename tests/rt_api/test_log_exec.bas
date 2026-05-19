' test_log_exec.bas — Log, Exec, NumberFormat
PRINT "log debug level: "; Viper.Diagnostics.Log.DEBUG
PRINT "log info level: "; Viper.Diagnostics.Log.INFO
PRINT "log warn level: "; Viper.Diagnostics.Log.WARN
PRINT "log error level: "; Viper.Diagnostics.Log.ERROR
PRINT "log off level: "; Viper.Diagnostics.Log.OFF
PRINT "log level: "; Viper.Diagnostics.Log.Level
PRINT "log enabled debug: "; Viper.Diagnostics.Log.Enabled(Viper.Diagnostics.Log.DEBUG)
Viper.Diagnostics.Log.Info("test info message")
Viper.Diagnostics.Log.Warn("test warn message")
Viper.Diagnostics.Log.Error("test error message")

PRINT "exec echo: "; Viper.System.Exec.ShellCapture("echo hello")

PRINT "numfmt bytes: "; Viper.Text.NumberFormat.Bytes(1048576)
PRINT "numfmt ordinal 1: "; Viper.Text.NumberFormat.Ordinal(1)
PRINT "numfmt ordinal 2: "; Viper.Text.NumberFormat.Ordinal(2)
PRINT "numfmt ordinal 3: "; Viper.Text.NumberFormat.Ordinal(3)
PRINT "numfmt ordinal 11: "; Viper.Text.NumberFormat.Ordinal(11)
PRINT "numfmt pad: "; Viper.Text.NumberFormat.Pad(42, 5)
PRINT "numfmt percent: "; Viper.Text.NumberFormat.Percent(0.756)
PRINT "numfmt thousands: "; Viper.Text.NumberFormat.Thousands(1234567, ",")
PRINT "numfmt towords: "; Viper.Text.NumberFormat.ToWords(42)
PRINT "numfmt decimals: "; Viper.Text.NumberFormat.Decimals(3.14159, 2)
PRINT "numfmt currency: "; Viper.Text.NumberFormat.Currency(1234.56, "$")

PRINT "done"
END
