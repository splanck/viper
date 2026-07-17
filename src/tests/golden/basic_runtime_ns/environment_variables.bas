REM BASIC: Verify Zanna.System.Environment variable APIs
DIM baseName AS STRING
DIM name AS STRING
DIM hadOriginal AS INTEGER
DIM original AS STRING
DIM hasText AS STRING

baseName = "ZANNA_ENV_API_TEST_93D0E345C66B4C63AE72"
name = baseName
IF Zanna.System.Environment.HasVariable(name) THEN
    name = baseName + "_ALT"
END IF

hadOriginal = Zanna.System.Environment.HasVariable(name)
original = Zanna.System.Environment.GetVariable(name)
IF hadOriginal THEN
    hasText = "1"
ELSE
    hasText = "0"
END IF
PRINT "initial-has=" + hasText
PRINT "initial-val=" + original

Zanna.System.Environment.SetVariable(name, "abc")
IF Zanna.System.Environment.HasVariable(name) THEN
    hasText = "1"
ELSE
    hasText = "0"
END IF
PRINT "after-set-has=" + hasText
PRINT "after-set-val=" + Zanna.System.Environment.GetVariable(name)

Zanna.System.Environment.SetVariable(name, "")
IF Zanna.System.Environment.HasVariable(name) THEN
    hasText = "1"
ELSE
    hasText = "0"
END IF
PRINT "after-clear-has=" + hasText
PRINT "after-clear-val=" + Zanna.System.Environment.GetVariable(name)

IF hadOriginal THEN
    Zanna.System.Environment.SetVariable(name, original)
ELSE
    Zanna.System.Environment.SetVariable(name, "")
END IF
