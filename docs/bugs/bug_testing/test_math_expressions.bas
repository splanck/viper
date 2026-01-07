REM ╔════════════════════════════════════════════════════════╗
REM ║     MATHEMATICAL EXPRESSIONS STRESS TEST               ║
REM ╚════════════════════════════════════════════════════════╝

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         MATH EXPRESSIONS TEST                          ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

REM Test 1: Order of operations
PRINT "Test 1: Order of Operations"
DIM result AS INTEGER

result = 2 + 3 * 4
PRINT "  2 + 3 * 4 = "; result
IF result = 14 THEN
    PRINT "  ✓ Correct (multiplication first)"
ELSE
    PRINT "  ✗ FAILED: Expected 14"
END IF

result = (2 + 3) * 4
PRINT "  (2 + 3) * 4 = "; result
IF result = 20 THEN
    PRINT "  ✓ Correct (parentheses first)"
ELSE
    PRINT "  ✗ FAILED: Expected 20"
END IF

result = 10 - 3 + 2
PRINT "  10 - 3 + 2 = "; result
IF result = 9 THEN
    PRINT "  ✓ Correct (left to right)"
ELSE
    PRINT "  ✗ FAILED: Expected 9"
END IF
PRINT

REM Test 2: Integer division and MOD
PRINT "Test 2: Division and MOD"
result = 17 / 5
PRINT "  17 / 5 = "; result; " (integer division)"

result = 17 MOD 5
PRINT "  17 MOD 5 = "; result
IF result = 2 THEN
    PRINT "  ✓ Correct remainder"
ELSE
    PRINT "  ✗ FAILED"
END IF

result = 100 MOD 7
PRINT "  100 MOD 7 = "; result
IF result = 2 THEN
    PRINT "  ✓ Correct"
ELSE
    PRINT "  ✗ FAILED"
END IF
PRINT

REM Test 3: Negative numbers
PRINT "Test 3: Negative Numbers"
DIM negNum AS INTEGER
negNum = -5

result = negNum * 3
PRINT "  -5 * 3 = "; result
IF result = -15 THEN
    PRINT "  ✓ Correct"
ELSE
    PRINT "  ✗ FAILED"
END IF

result = 10 + negNum
PRINT "  10 + (-5) = "; result
IF result = 5 THEN
    PRINT "  ✓ Correct"
ELSE
    PRINT "  ✗ FAILED"
END IF

result = negNum * negNum
PRINT "  -5 * -5 = "; result
IF result = 25 THEN
    PRINT "  ✓ Correct"
ELSE
    PRINT "  ✗ FAILED"
END IF
PRINT

REM Test 4: Complex expressions
PRINT "Test 4: Complex Expressions"
result = (10 + 5) * 2 - 8 / 4
PRINT "  (10 + 5) * 2 - 8 / 4 = "; result
REM (15) * 2 - 2 = 30 - 2 = 28
IF result = 28 THEN
    PRINT "  ✓ Correct"
ELSE
    PRINT "  ✗ FAILED: Expected 28"
END IF

result = 100 - 25 * 2 + 10
PRINT "  100 - 25 * 2 + 10 = "; result
REM 100 - 50 + 10 = 60
IF result = 60 THEN
    PRINT "  ✓ Correct"
ELSE
    PRINT "  ✗ FAILED: Expected 60"
END IF
PRINT

REM Test 5: FOR with STEP
PRINT "Test 5: FOR Loop with STEP"
PRINT "  Count by 2s from 0 to 10:"
PRINT "  ";
DIM i AS INTEGER
FOR i = 0 TO 10 STEP 2
    PRINT i; " ";
NEXT i
PRINT

PRINT "  Count down from 10 to 0 by -2:"
PRINT "  ";
FOR i = 10 TO 0 STEP -2
    PRINT i; " ";
NEXT i
PRINT
PRINT

REM Test 6: FOR with STEP in calculations
PRINT "Test 6: STEP in Calculations"
DIM sum AS INTEGER
sum = 0
FOR i = 1 TO 20 STEP 3
    sum = sum + i
NEXT i
PRINT "  Sum of 1,4,7,10,13,16,19 = "; sum
REM 1+4+7+10+13+16+19 = 70
IF sum = 70 THEN
    PRINT "  ✓ Correct"
ELSE
    PRINT "  ✗ FAILED: Expected 70"
END IF
PRINT

REM Test 7: Comparison operators with complex expressions
PRINT "Test 7: Comparisons with Expressions"
IF (5 + 3) > (2 * 3) THEN
    PRINT "  ✓ (5+3) > (2*3) is TRUE"
ELSE
    PRINT "  ✗ FAILED"
END IF

IF (10 MOD 3) = 1 THEN
    PRINT "  ✓ (10 MOD 3) = 1 is TRUE"
ELSE
    PRINT "  ✗ FAILED"
END IF

IF (20 / 4) <= (15 / 3) THEN
    PRINT "  ✓ (20/4) <= (15/3) is TRUE"
ELSE
    PRINT "  ✗ FAILED"
END IF
PRINT

REM Test 8: Expression in array indices
PRINT "Test 8: Array Index Expressions"
DIM numbers(10) AS INTEGER
FOR i = 0 TO 9
    numbers(i) = i * 10
NEXT i

DIM idx AS INTEGER
idx = 2 + 3
PRINT "  numbers(2+3) = numbers("; idx; ") = "; numbers(idx)
IF numbers(idx) = 50 THEN
    PRINT "  ✓ Correct"
ELSE
    PRINT "  ✗ FAILED"
END IF

idx = 8 - 2
PRINT "  numbers(8-2) = numbers("; idx; ") = "; numbers(idx)
IF numbers(idx) = 60 THEN
    PRINT "  ✓ Correct"
ELSE
    PRINT "  ✗ FAILED"
END IF
PRINT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  MATH EXPRESSIONS TEST COMPLETE!                       ║"
PRINT "╚════════════════════════════════════════════════════════╝"
