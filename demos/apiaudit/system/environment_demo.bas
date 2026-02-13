' =============================================================================
' API Audit: Viper.Environment - Environment Access
' =============================================================================
' Tests: GetArgumentCount, GetCommandLine, GetVariable, HasVariable,
'        SetVariable, IsNative
' NOTE: Do NOT call EndProgram!
' =============================================================================

PRINT "=== API Audit: Viper.Environment ==="

' --- GetArgumentCount ---
PRINT "--- GetArgumentCount ---"
PRINT "GetArgumentCount: "; Viper.Environment.GetArgumentCount()

' --- GetCommandLine ---
PRINT "--- GetCommandLine ---"
PRINT "GetCommandLine: "; Viper.Environment.GetCommandLine()

' --- GetVariable ---
PRINT "--- GetVariable ---"
PRINT "GetVariable('PATH'): "; Viper.Environment.GetVariable("PATH")

' --- HasVariable ---
PRINT "--- HasVariable ---"
PRINT "HasVariable('PATH'): "; Viper.Environment.HasVariable("PATH")
PRINT "HasVariable('VIPER_TEST_NONEXISTENT_12345'): "; Viper.Environment.HasVariable("VIPER_TEST_NONEXISTENT_12345")

' --- SetVariable ---
PRINT "--- SetVariable ---"
Viper.Environment.SetVariable("VIPER_AUDIT_TEST", "hello_from_viper")
PRINT "SetVariable done"
PRINT "GetVariable('VIPER_AUDIT_TEST'): "; Viper.Environment.GetVariable("VIPER_AUDIT_TEST")
PRINT "HasVariable('VIPER_AUDIT_TEST'): "; Viper.Environment.HasVariable("VIPER_AUDIT_TEST")

' --- IsNative ---
PRINT "--- IsNative ---"
PRINT "IsNative: "; Viper.Environment.IsNative()

PRINT "=== Environment Demo Complete ==="
END
