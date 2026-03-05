' Test case for Bug #002: String literals in class method IF statements
' This test has NO backslashes to rule out Bug #007 corruption

CLASS TestClass
    PUBLIC value AS INTEGER

    PUBLIC FUNCTION CheckWord(word AS STRING) AS INTEGER
        IF word = "CREATE" THEN
            CheckWord = 1
            EXIT FUNCTION
        END IF
        IF word = "SELECT" THEN
            CheckWord = 2
            EXIT FUNCTION
        END IF
        CheckWord = 0
    END FUNCTION
END CLASS

DIM obj AS TestClass
LET obj = NEW TestClass()

PRINT "Testing Bug #002:"
PRINT "CheckWord(CREATE) = "; obj.CheckWord("CREATE"); " (expect 1)"
PRINT "CheckWord(SELECT) = "; obj.CheckWord("SELECT"); " (expect 2)"
PRINT "CheckWord(other) = "; obj.CheckWord("other"); " (expect 0)"
