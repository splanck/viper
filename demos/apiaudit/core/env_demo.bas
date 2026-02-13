' env_demo.bas — Viper.Environment
PRINT "=== Viper.Environment Demo ==="
PRINT Viper.Environment.IsNative()
PRINT Viper.Environment.GetArgumentCount()
PRINT Viper.Environment.GetCommandLine()

Viper.Environment.SetVariable("VIPER_TEST_VAR", "hello123")
PRINT Viper.Environment.HasVariable("VIPER_TEST_VAR")
PRINT Viper.Environment.GetVariable("VIPER_TEST_VAR")
PRINT Viper.Environment.HasVariable("NONEXISTENT_VAR_XYZ")
PRINT Viper.Environment.GetVariable("NONEXISTENT_VAR_XYZ")

PRINT "done"
END
