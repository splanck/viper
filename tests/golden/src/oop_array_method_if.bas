REM Test BUG-104 fix: Method calls on array elements in IF conditions
REM This should compile and run without "use before def" errors

CLASS Card
    DIM value AS INTEGER

    SUB Init(v AS INTEGER)
        value = v
    END SUB

    FUNCTION GetValue() AS INTEGER
        GetValue = value
    END FUNCTION
END CLASS

REM Test at module scope
DIM cards(2) AS Card
DIM i AS INTEGER

FOR i = 0 TO 2
    cards(i) = NEW Card()
    cards(i).Init(i * 10)
NEXT i

REM Test 1: Direct method call in IF at module scope
IF cards(0).GetValue() = 0 THEN
    PRINT "Test 1: PASS"
ELSE
    PRINT "Test 1: FAIL"
END IF

REM Test 2: Method call in IF with comparison
IF cards(1).GetValue() <> 0 THEN
    PRINT "Test 2: PASS"
ELSE
    PRINT "Test 2: FAIL"
END IF

REM Test 3: Method call in SUB
SUB TestInSub()
    IF cards(2).GetValue() = 20 THEN
        PRINT "Test 3: PASS"
    ELSE
        PRINT "Test 3: FAIL"
    END IF
END SUB

TestInSub()

REM Test 4: Method call in FUNCTION
FUNCTION TestInFunc() AS INTEGER
    IF cards(0).GetValue() >= 0 THEN
        TestInFunc = 1
    ELSE
        TestInFunc = 0
    END IF
END FUNCTION

IF TestInFunc() = 1 THEN
    PRINT "Test 4: PASS"
ELSE
    PRINT "Test 4: FAIL"
END IF
