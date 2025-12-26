' Test 1: FINALLY runs on normal path (no exception)
DIM result AS STRING
DIM x AS INTEGER
result = ""
x = 5

TRY
    result = result + "TRY,"
    x = x + 1   ' No exception here
    OPEN "nonexistent_xyz.txt" FOR INPUT AS #1  ' This will trap, making handler reachable
    result = result + "NEVER,"
CATCH e
    result = result + "CATCH,"
FINALLY
    result = result + "FINALLY,"
END TRY
result = result + "DONE"

IF result = "TRY,CATCH,FINALLY,DONE" THEN
    PRINT "PASS: finally runs after catch"
ELSE
    PRINT "FAIL: got " + result
END IF

' Test 2: Multiple TRY-CATCH-FINALLY blocks work correctly
result = ""
TRY
    result = result + "A,"
    OPEN "another_nonexistent.txt" FOR INPUT AS #2
    result = result + "NEVER,"
CATCH
    result = result + "B,"
FINALLY
    result = result + "C,"
END TRY

TRY
    result = result + "D,"
    OPEN "yet_another.txt" FOR INPUT AS #3
    result = result + "NEVER,"
CATCH
    result = result + "E,"
FINALLY
    result = result + "F,"
END TRY
result = result + "DONE"

IF result = "A,B,C,D,E,F,DONE" THEN
    PRINT "PASS: multiple try-catch-finally work"
ELSE
    PRINT "FAIL: got " + result
END IF

PRINT "=== TRY/FINALLY TESTS COMPLETE ==="
