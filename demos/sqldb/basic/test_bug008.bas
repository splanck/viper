' Test file for Bug #008 investigation
' Testing single-line IF with colon separator behavior

' Test 1: Single-line IF with colon (BUGGY pattern)
FUNCTION TestSingleLineIF(word AS STRING) AS INTEGER
    IF word = "SELECT" THEN TestSingleLineIF = 30 : EXIT FUNCTION
    IF word = "INSERT" THEN TestSingleLineIF = 31 : EXIT FUNCTION
    TestSingleLineIF = 13
END FUNCTION

' Test 2: Multi-line IF (WORKING pattern)
FUNCTION TestMultiLineIF(word AS STRING) AS INTEGER
    IF word = "SELECT" THEN
        TestMultiLineIF = 30
        EXIT FUNCTION
    END IF
    IF word = "INSERT" THEN
        TestMultiLineIF = 31
        EXIT FUNCTION
    END IF
    TestMultiLineIF = 13
END FUNCTION

' Test 3: Single-line IF without EXIT FUNCTION
FUNCTION TestSingleNoExit(word AS STRING) AS INTEGER
    TestSingleNoExit = 13
    IF word = "SELECT" THEN TestSingleNoExit = 30
    IF word = "INSERT" THEN TestSingleNoExit = 31
END FUNCTION

' Test 4: ELSEIF chain
FUNCTION TestElseIf(word AS STRING) AS INTEGER
    IF word = "SELECT" THEN
        TestElseIf = 30
    ELSEIF word = "INSERT" THEN
        TestElseIf = 31
    ELSE
        TestElseIf = 13
    END IF
END FUNCTION

' Run all tests
PRINT "=== Bug #008 Test Suite ==="
PRINT ""

PRINT "Test 1: Single-line IF with colon (EXPECTED BUG)"
PRINT "  TestSingleLineIF(SELECT) = "; TestSingleLineIF("SELECT"); " (expected 30)"
PRINT "  TestSingleLineIF(INSERT) = "; TestSingleLineIF("INSERT"); " (expected 31)"
PRINT "  TestSingleLineIF(other)  = "; TestSingleLineIF("other"); " (expected 13)"
PRINT ""

PRINT "Test 2: Multi-line IF (WORKAROUND)"
PRINT "  TestMultiLineIF(SELECT) = "; TestMultiLineIF("SELECT"); " (expected 30)"
PRINT "  TestMultiLineIF(INSERT) = "; TestMultiLineIF("INSERT"); " (expected 31)"
PRINT "  TestMultiLineIF(other)  = "; TestMultiLineIF("other"); " (expected 13)"
PRINT ""

PRINT "Test 3: Single-line IF without EXIT FUNCTION"
PRINT "  TestSingleNoExit(SELECT) = "; TestSingleNoExit("SELECT"); " (expected 30)"
PRINT "  TestSingleNoExit(INSERT) = "; TestSingleNoExit("INSERT"); " (expected 31)"
PRINT "  TestSingleNoExit(other)  = "; TestSingleNoExit("other"); " (expected 13)"
PRINT ""

PRINT "Test 4: ELSEIF chain"
PRINT "  TestElseIf(SELECT) = "; TestElseIf("SELECT"); " (expected 30)"
PRINT "  TestElseIf(INSERT) = "; TestElseIf("INSERT"); " (expected 31)"
PRINT "  TestElseIf(other)  = "; TestElseIf("other"); " (expected 13)"
PRINT ""

PRINT "=== End Test Suite ==="
