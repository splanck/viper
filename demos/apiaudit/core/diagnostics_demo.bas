' =============================================================================
' API Audit: Viper.Core.Diagnostics - Assertions
' =============================================================================
' Tests: Assert(true), AssertEq, AssertNeq, AssertEqNum, AssertEqStr,
'        AssertGt, AssertLt, AssertGte, AssertLte
' NOTE: Do NOT test AssertFail or Assert(false) - those trap/crash!
' =============================================================================

PRINT "=== API Audit: Viper.Core.Diagnostics ==="

' --- Assert (true only) ---
PRINT "--- Assert ---"
Viper.Core.Diagnostics.Assert(TRUE, "true should be true")
PRINT "Assert(true) passed"

' --- AssertEq ---
PRINT "--- AssertEq ---"
Viper.Core.Diagnostics.AssertEq(42, 42, "42 should equal 42")
PRINT "AssertEq(42, 42) passed"
Viper.Core.Diagnostics.AssertEq(0, 0, "0 should equal 0")
PRINT "AssertEq(0, 0) passed"
Viper.Core.Diagnostics.AssertEq(-1, -1, "-1 should equal -1")
PRINT "AssertEq(-1, -1) passed"

' --- AssertNeq ---
PRINT "--- AssertNeq ---"
Viper.Core.Diagnostics.AssertNeq(1, 2, "1 should not equal 2")
PRINT "AssertNeq(1, 2) passed"
Viper.Core.Diagnostics.AssertNeq(0, 1, "0 should not equal 1")
PRINT "AssertNeq(0, 1) passed"

' --- AssertEqNum ---
PRINT "--- AssertEqNum ---"
Viper.Core.Diagnostics.AssertEqNum(3.14, 3.14, "pi should equal pi")
PRINT "AssertEqNum(3.14, 3.14) passed"
Viper.Core.Diagnostics.AssertEqNum(0.0, 0.0, "0.0 should equal 0.0")
PRINT "AssertEqNum(0.0, 0.0) passed"

' --- AssertEqStr ---
PRINT "--- AssertEqStr ---"
Viper.Core.Diagnostics.AssertEqStr("hello", "hello", "hello should equal hello")
PRINT "AssertEqStr(hello, hello) passed"
Viper.Core.Diagnostics.AssertEqStr("", "", "empty should equal empty")
PRINT "AssertEqStr(empty, empty) passed"

' --- AssertGt ---
PRINT "--- AssertGt ---"
Viper.Core.Diagnostics.AssertGt(10, 5, "10 should be > 5")
PRINT "AssertGt(10, 5) passed"
Viper.Core.Diagnostics.AssertGt(1, 0, "1 should be > 0")
PRINT "AssertGt(1, 0) passed"

' --- AssertLt ---
PRINT "--- AssertLt ---"
Viper.Core.Diagnostics.AssertLt(3, 7, "3 should be < 7")
PRINT "AssertLt(3, 7) passed"
Viper.Core.Diagnostics.AssertLt(-1, 0, "-1 should be < 0")
PRINT "AssertLt(-1, 0) passed"

' --- AssertGte ---
PRINT "--- AssertGte ---"
Viper.Core.Diagnostics.AssertGte(5, 5, "5 should be >= 5")
PRINT "AssertGte(5, 5) passed"
Viper.Core.Diagnostics.AssertGte(10, 5, "10 should be >= 5")
PRINT "AssertGte(10, 5) passed"

' --- AssertLte ---
PRINT "--- AssertLte ---"
Viper.Core.Diagnostics.AssertLte(5, 5, "5 should be <= 5")
PRINT "AssertLte(5, 5) passed"
Viper.Core.Diagnostics.AssertLte(3, 10, "3 should be <= 10")
PRINT "AssertLte(3, 10) passed"

PRINT "=== Diagnostics Demo Complete ==="
END
