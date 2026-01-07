REM BASIC: Verify Viper.Environment variable APIs
DIM baseName AS STRING
DIM name AS STRING
DIM hadOriginal AS INTEGER
DIM original AS STRING
DIM hasText AS STRING

baseName = "VIPER_ENV_API_TEST_93D0E345C66B4C63AE72"
name = baseName
IF Viper.Environment.HasVariable(name) THEN
    name = baseName + "_ALT"
END IF

hadOriginal = Viper.Environment.HasVariable(name)
original = Viper.Environment.GetVariable(name)
IF hadOriginal THEN
    hasText = "1"
ELSE
    hasText = "0"
END IF
PRINT "initial-has=" + hasText
PRINT "initial-val=" + original

Viper.Environment.SetVariable(name, "abc")
IF Viper.Environment.HasVariable(name) THEN
    hasText = "1"
ELSE
    hasText = "0"
END IF
PRINT "after-set-has=" + hasText
PRINT "after-set-val=" + Viper.Environment.GetVariable(name)

Viper.Environment.SetVariable(name, "")
IF Viper.Environment.HasVariable(name) THEN
    hasText = "1"
ELSE
    hasText = "0"
END IF
PRINT "after-clear-has=" + hasText
PRINT "after-clear-val=" + Viper.Environment.GetVariable(name)

IF hadOriginal THEN
    Viper.Environment.SetVariable(name, original)
ELSE
    Viper.Environment.SetVariable(name, "")
END IF
