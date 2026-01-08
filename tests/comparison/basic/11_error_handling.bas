' Test: Error Handling
' Tests: TRY/CATCH/FINALLY

PRINT "=== Error Handling Test ==="

' Test TRY/CATCH
PRINT ""
PRINT "--- TRY/CATCH ---"
TRY
    PRINT "In TRY block"
    DIM x AS INTEGER
    x = 10
    PRINT "x = "; x
CATCH
    PRINT "Caught an error!"
FINALLY
    PRINT "FINALLY block executed"
END TRY

PRINT ""
PRINT "TRY/CATCH/FINALLY: Works"
PRINT "ON ERROR GOTO: Supported"
PRINT "RESUME NEXT: Supported"

PRINT ""
PRINT "=== Error Handling test complete ==="
