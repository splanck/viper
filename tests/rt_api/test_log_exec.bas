' test_log_exec.bas — Log, Exec, NumberFormat
PRINT "log debug level: "; Viper.Log.DEBUG
PRINT "log info level: "; Viper.Log.INFO
PRINT "log warn level: "; Viper.Log.WARN
PRINT "log error level: "; Viper.Log.ERROR
PRINT "log off level: "; Viper.Log.OFF
PRINT "log level: "; Viper.Log.Level
PRINT "log enabled debug: "; Viper.Log.Enabled(Viper.Log.DEBUG)
Viper.Log.Info("test info message")
Viper.Log.Warn("test warn message")
Viper.Log.Error("test error message")

PRINT "exec echo: "; Viper.Exec.ShellCapture("echo hello")

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
