' Test USING statement for automatic resource cleanup
' The USING statement ensures the object is destroyed at scope exit

CLASS Resource
    PRIVATE value AS INTEGER

    SUB NEW(v AS INTEGER)
        ME.value = v
        PRINT "Resource created"
    END SUB

    SUB DESTROY()
        PRINT "Resource destroyed"
    END SUB
END CLASS

' Test 1: Basic USING statement
PRINT "Test 1: Basic USING"
USING res AS Resource = NEW Resource(42)
    PRINT "Inside USING"
END USING
PRINT "After USING"
PRINT ""

' Test 2: Multiple USING blocks
PRINT "Test 2: Multiple USING blocks"
USING r1 AS Resource = NEW Resource(100)
    PRINT "First block"
END USING

USING r2 AS Resource = NEW Resource(200)
    PRINT "Second block"
END USING
PRINT ""

' Test 3: USING with operations inside
PRINT "Test 3: Operations inside USING"
DIM total AS INTEGER = 0
USING r AS Resource = NEW Resource(10)
    total = total + 1
    total = total + 1
END USING
PRINT "Total:"; total
PRINT ""

PRINT "Done"
