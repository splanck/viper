' =============================================================================
' API Audit: Zanna.System.Environment - Environment Access
' =============================================================================
' Tests: GetArgumentCount, GetCommandLine, GetVariable, HasVariable,
'        SetVariable, IsNative
' NOTE: Do NOT call EndProgram!
' =============================================================================

PRINT "=== API Audit: Zanna.System.Environment ==="

' --- GetArgumentCount ---
PRINT "--- GetArgumentCount ---"
PRINT "GetArgumentCount: "; Zanna.System.Environment.GetArgumentCount()

' --- GetCommandLine ---
PRINT "--- GetCommandLine ---"
PRINT "GetCommandLine: "; Zanna.System.Environment.GetCommandLine()

' --- GetVariable ---
PRINT "--- GetVariable ---"
PRINT "GetVariable('PATH'): "; Zanna.System.Environment.GetVariable("PATH")

' --- HasVariable ---
PRINT "--- HasVariable ---"
PRINT "HasVariable('PATH'): "; Zanna.System.Environment.HasVariable("PATH")
PRINT "HasVariable('ZANNA_TEST_NONEXISTENT_12345'): "; Zanna.System.Environment.HasVariable("ZANNA_TEST_NONEXISTENT_12345")

' --- SetVariable ---
PRINT "--- SetVariable ---"
Zanna.System.Environment.SetVariable("ZANNA_AUDIT_TEST", "hello_from_zanna")
PRINT "SetVariable done"
PRINT "GetVariable('ZANNA_AUDIT_TEST'): "; Zanna.System.Environment.GetVariable("ZANNA_AUDIT_TEST")
PRINT "HasVariable('ZANNA_AUDIT_TEST'): "; Zanna.System.Environment.HasVariable("ZANNA_AUDIT_TEST")

' --- IsNative ---
PRINT "--- IsNative ---"
PRINT "IsNative: "; Zanna.System.Environment.IsNative()

PRINT "=== Environment Demo Complete ==="
END
