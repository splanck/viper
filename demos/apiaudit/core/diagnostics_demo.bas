' diagnostics_demo.bas
PRINT "=== Viper.Core.Diagnostics Demo ==="
Viper.Core.Diagnostics.Assert(-1, "should pass")
Viper.Core.Diagnostics.Assert(1 = 1, "equality check")
PRINT "asserts passed"
PRINT "done"
END
