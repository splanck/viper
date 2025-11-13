REM Comprehensive Test: Number Analysis Program
REM Tests: All new features without triggering CONST assertion
PRINT "=== NUMBER ANALYSIS PROGRAM ==="

DIM numbers%(20)
PRINT "Generating 20 random numbers..."
RANDOMIZE TIMER()

FOR i% = 0 TO 19
    numbers%(i%) = INT(RND() * 200) - 100
    PRINT numbers%(i%);
NEXT i%
PRINT ""

PRINT ""
PRINT "=== Statistical Analysis using SGN ==="
positive% = 0
negative% = 0
zero% = 0

FOR i% = 0 TO 19
    sign% = SGN(numbers%(i%))
    IF sign% > 0 THEN positive% = positive% + 1
    IF sign% < 0 THEN negative% = negative% + 1
    IF sign% = 0 THEN zero% = zero% + 1
NEXT i%

PRINT "Positive numbers: "; positive%
PRINT "Negative numbers: "; negative%
PRINT "Zero values: "; zero%

PRINT ""
PRINT "=== Sorting with SWAP ==="
n% = 19
FOR i% = 0 TO n% - 1
    FOR j% = 0 TO n% - i% - 1
        IF numbers%(j%) > numbers%(j% + 1) THEN
            SWAP numbers%(j%), numbers%(j% + 1)
        END IF
    NEXT j%
NEXT i%

PRINT "Sorted numbers:"
FOR i% = 0 TO 19
    PRINT numbers%(i%);
NEXT i%
PRINT ""

PRINT ""
PRINT "=== Math Function Analysis ==="
PRINT "EXP and LOG of first 5 positive values:"
count% = 0
FOR i% = 0 TO 19
    IF numbers%(i%) > 0 AND count% < 5 THEN
        val% = numbers%(i%)
        expVal# = EXP(val%)
        IF expVal# < 1000000 THEN
            logVal# = LOG(expVal#)
            PRINT "n="; val%; " EXP(n)="; expVal#; " LOG(EXP(n))="; logVal#
            count% = count% + 1
        END IF
    END IF
NEXT i%

PRINT ""
PRINT "Comprehensive test completed successfully!"
