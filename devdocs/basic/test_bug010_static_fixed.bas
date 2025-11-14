REM Test BUG-010 FIX: STATIC keyword - procedure-local persistent variables
REM Tests that STATIC variables persist across calls and are isolated per procedure

REM Test 1: Basic integer counter
SUB Counter()
    STATIC count AS INTEGER
    count = count + 1
    PRINT "Counter: "; count
END SUB

PRINT "=== Test 1: Basic Counter ==="
Counter()
Counter()
Counter()
PRINT ""

REM Test 2: Multiple STATIC variables in one SUB
SUB Multi()
    STATIC x AS INTEGER
    STATIC y AS SINGLE
    x = x + 1
    y = y + 0.5
    PRINT "x="; x; " y="; y
END SUB

PRINT "=== Test 2: Multiple STATIC Variables ==="
Multi()
Multi()
Multi()
PRINT ""

REM Test 3: Same variable name in different procedures (isolation test)
SUB ProcA()
    STATIC val AS INTEGER
    val = val + 10
    PRINT "ProcA val="; val
END SUB

SUB ProcB()
    STATIC val AS INTEGER
    val = val + 100
    PRINT "ProcB val="; val
END SUB

PRINT "=== Test 3: Isolation Between Procedures ==="
ProcA()
ProcB()
ProcA()
ProcB()
PRINT ""

REM Test 4: STATIC with type suffix
SUB Typed()
    STATIC f#
    f# = f# + 1.5
    PRINT "f#="; f#
END SUB

PRINT "=== Test 4: Type Suffix ==="
Typed()
Typed()
PRINT ""

REM Test 5: STATIC in FUNCTION (returns value)
FUNCTION GetNext() AS INTEGER
    STATIC seq AS INTEGER
    seq = seq + 1
    RETURN seq
END FUNCTION

PRINT "=== Test 5: STATIC in FUNCTION ==="
DIM a AS INTEGER
DIM b AS INTEGER
DIM c AS INTEGER
a = GetNext()
b = GetNext()
c = GetNext()
PRINT "Sequence: "; a; ", "; b; ", "; c
PRINT ""

REM Test 6: Mixed local and STATIC
SUB Mixed()
    DIM local AS INTEGER
    STATIC persistent AS INTEGER
    local = 5
    persistent = persistent + 1
    PRINT "local="; local; " persistent="; persistent
END SUB

PRINT "=== Test 6: Mixed Local and STATIC ==="
Mixed()
Mixed()
Mixed()
PRINT ""

PRINT "All BUG-010 tests passed!"
END
