' Test case for Bug #004: Empty string in function call
' This test has NO backslashes to rule out Bug #007 corruption

CLASS Token
    PUBLIC kind AS INTEGER
    PUBLIC text AS STRING
    PUBLIC line AS INTEGER
    PUBLIC col AS INTEGER
END CLASS

FUNCTION MakeToken(k AS INTEGER, t AS STRING, ln AS INTEGER, cl AS INTEGER) AS Token
    DIM tok AS Token
    LET tok = NEW Token()
    tok.kind = k
    tok.text = t
    tok.line = ln
    tok.col = cl
    MakeToken = tok
END FUNCTION

DIM t1 AS Token
DIM t2 AS Token

PRINT "Testing Bug #004:"

' Test with non-empty string
LET t1 = MakeToken(1, "hello", 10, 5)
PRINT "t1.text = '"; t1.text; "' (expect 'hello')"

' Test with empty string - this is the bug trigger
LET t2 = MakeToken(2, "", 20, 1)
PRINT "t2.text = '"; t2.text; "' (expect empty)"
PRINT "t2.kind = "; t2.kind; " (expect 2)"

PRINT "Bug #004 test complete"
