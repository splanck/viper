REM BUG-108 Test: Local boolean array in class method
REM Should not confuse local array with field array

CLASS Hand
    DIM cards(5) AS Integer
    Public count AS Integer

    Sub New()
        LET ME.count = 5
        LET ME.cards(0) = 1
        LET ME.cards(1) = 2
        LET ME.cards(2) = 3
        LET ME.cards(3) = 4
        LET ME.cards(4) = 5
    End Sub

    Function TestLocalArray() AS Integer
        DIM i AS Integer
        DIM counted(15) AS Boolean

        REM Initialize local array with large index
        FOR i = 0 TO 14
            LET counted(i) = FALSE
        NEXT i

        REM Use larger indices that would fail if using cards array (len=5)
        LET counted(10) = TRUE
        LET counted(12) = TRUE
        LET counted(14) = TRUE

        REM Count how many are true
        DIM result AS Integer
        LET result = 0
        FOR i = 0 TO 14
            IF counted(i) THEN
                LET result = result + 1
            END IF
        NEXT i

        LET TestLocalArray = result
    End Function
END CLASS

DIM h AS Hand
LET h = NEW Hand()

DIM result AS Integer
LET result = h.TestLocalArray()

PRINT "Result: "; result; " (expect 3)"

IF result = 3 THEN
    PRINT "PASS"
ELSE
    PRINT "FAIL"
END IF
