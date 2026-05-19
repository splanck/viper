REM BASIC: Verify Viper.System.Environment variable APIs
DIM baseName AS STRING
DIM name AS STRING
DIM hadOriginal AS INTEGER
DIM original AS STRING
DIM hasText AS STRING

baseName = "VIPER_ENV_API_TEST_93D0E345C66B4C63AE72"
name = baseName
IF Viper.System.Environment.HasVariable(name) THEN
    name = baseName + "_ALT"
END IF

hadOriginal = Viper.System.Environment.HasVariable(name)
original = Viper.System.Environment.GetVariable(name)
IF hadOriginal THEN
    hasText = "1"
ELSE
    hasText = "0"
END IF
PRINT "initial-has=" + hasText
PRINT "initial-val=" + original

Viper.System.Environment.SetVariable(name, "abc")
IF Viper.System.Environment.HasVariable(name) THEN
    hasText = "1"
ELSE
    hasText = "0"
END IF
PRINT "after-set-has=" + hasText
PRINT "after-set-val=" + Viper.System.Environment.GetVariable(name)

Viper.System.Environment.SetVariable(name, "")
IF Viper.System.Environment.HasVariable(name) THEN
    hasText = "1"
ELSE
    hasText = "0"
END IF
PRINT "after-clear-has=" + hasText
PRINT "after-clear-val=" + Viper.System.Environment.GetVariable(name)

IF hadOriginal THEN
    Viper.System.Environment.SetVariable(name, original)
ELSE
    Viper.System.Environment.SetVariable(name, "")
END IF
