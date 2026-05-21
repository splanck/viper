' =============================================================================
' API Audit: Viper.System.Environment - Environment Access
' =============================================================================
' Tests: GetArgumentCount, GetCommandLine, GetVariable, HasVariable,
'        SetVariable, IsNative
' NOTE: Do NOT call EndProgram!
' =============================================================================

PRINT "=== API Audit: Viper.System.Environment ==="

' --- GetArgumentCount ---
PRINT "--- GetArgumentCount ---"
PRINT "GetArgumentCount: "; Viper.System.Environment.GetArgumentCount()

' --- GetCommandLine ---
PRINT "--- GetCommandLine ---"
PRINT "GetCommandLine: "; Viper.System.Environment.GetCommandLine()

' --- GetVariable ---
PRINT "--- GetVariable ---"
PRINT "GetVariable('PATH'): "; Viper.System.Environment.GetVariable("PATH")

' --- HasVariable ---
PRINT "--- HasVariable ---"
PRINT "HasVariable('PATH'): "; Viper.System.Environment.HasVariable("PATH")
PRINT "HasVariable('VIPER_TEST_NONEXISTENT_12345'): "; Viper.System.Environment.HasVariable("VIPER_TEST_NONEXISTENT_12345")

' --- SetVariable ---
PRINT "--- SetVariable ---"
Viper.System.Environment.SetVariable("VIPER_AUDIT_TEST", "hello_from_viper")
PRINT "SetVariable done"
PRINT "GetVariable('VIPER_AUDIT_TEST'): "; Viper.System.Environment.GetVariable("VIPER_AUDIT_TEST")
PRINT "HasVariable('VIPER_AUDIT_TEST'): "; Viper.System.Environment.HasVariable("VIPER_AUDIT_TEST")

' --- IsNative ---
PRINT "--- IsNative ---"
PRINT "IsNative: "; Viper.System.Environment.IsNative()

PRINT "=== Environment Demo Complete ==="
END
