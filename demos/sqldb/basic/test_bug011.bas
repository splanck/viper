' Test case for Bug #011: Class member fields cannot be passed directly to methods
PRINT "=== Bug #011 Test ==="

CLASS Container
    PUBLIC name AS STRING
END CLASS

CLASS Processor
    PUBLIC FUNCTION ProcessName(aName AS STRING) AS INTEGER
        PRINT "Processing: "; aName
        ProcessName = LEN(aName)
    END FUNCTION
END CLASS

DIM gProcessor AS Processor
LET gProcessor = NEW Processor()

' This works - calling at module level
DIM testContainer AS Container
LET testContainer = NEW Container()
testContainer.name = "Hello"
PRINT "Direct call result: "; gProcessor.ProcessName(testContainer.name)

' This is the problematic case - calling from within a SUB with parameter
SUB TestWithParam(c AS Container)
    DIM result AS INTEGER
    ' This should work but may fail with "no viable overload"
    result = gProcessor.ProcessName(c.name)
    PRINT "SUB call result: "; result
END SUB

' Also test with workaround
SUB TestWithWorkaround(c AS Container)
    DIM tempName AS STRING
    DIM result AS INTEGER
    tempName = c.name
    result = gProcessor.ProcessName(tempName)
    PRINT "Workaround result: "; result
END SUB

TestWithParam(testContainer)
TestWithWorkaround(testContainer)

PRINT "=== Test Complete ==="
