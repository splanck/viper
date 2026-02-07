REM BASIC: Assert should use default message when empty string is provided
Viper.Core.Diagnostics.Assert(1 = 0, "")
PRINT "unreached"
