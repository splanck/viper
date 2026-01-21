' Test with full structure

CONST TK_EOF = 0
CONST TK_THEN = 113

CLASS Token
    PUBLIC kind AS INTEGER
    PUBLIC text AS STRING
    PUBLIC SUB Init(k AS INTEGER, t AS STRING)
        kind = k
        text = t
    END SUB
END CLASS

DIM gTok AS Token
DIM gLexLine AS INTEGER
DIM gLexCol AS INTEGER

SUB LexerMakeToken(k AS INTEGER, t AS STRING, ln AS INTEGER, col AS INTEGER)
    LET gTok = NEW Token()
    gTok.Init(k, t)
END SUB

FUNCTION LexerAtEnd() AS INTEGER
    LexerAtEnd = -1
END FUNCTION

SUB LexerSkipWhitespace()
    DIM ch AS STRING
    LET ch = " "
END SUB

FUNCTION IsDigitCh(ch AS STRING) AS INTEGER
    DIM code AS INTEGER
    LET code = ASC(ch)
    IF (code >= 48) AND (code <= 57) THEN
        IsDigitCh = -1
    ELSE
        IsDigitCh = 0
    END IF
END FUNCTION

FUNCTION IsAlphaCh(ch AS STRING) AS INTEGER
    DIM code AS INTEGER
    LET code = ASC(ch)
    IF (code >= 65) AND (code <= 90) THEN
        IsAlphaCh = -1
    ELSE
        IsAlphaCh = 0
    END IF
END FUNCTION

FUNCTION IsAlphaNumCh(ch AS STRING) AS INTEGER
    IF IsAlphaCh(ch) <> 0 THEN
        IsAlphaNumCh = -1
    ELSEIF IsDigitCh(ch) <> 0 THEN
        IsAlphaNumCh = -1
    ELSE
        IsAlphaNumCh = 0
    END IF
END FUNCTION

FUNCTION LookupKeyword(word AS STRING) AS INTEGER
    IF word = "SELECT" THEN LookupKeyword = 30 : EXIT FUNCTION
    IF word = "THEN" THEN LookupKeyword = TK_THEN : EXIT FUNCTION
    LookupKeyword = 13
END FUNCTION

SUB LexerReadNumber()
    PRINT "Reading number"
END SUB

SUB LexerReadIdentifier()
    PRINT "Reading identifier"
END SUB

SUB LexerReadString()
    PRINT "Reading string"
END SUB

SUB LexerNextToken()
    DIM ch AS STRING
    DIM emptyStr AS STRING

    LET emptyStr = " "
    LexerSkipWhitespace()

    IF LexerAtEnd() <> 0 THEN
        LexerMakeToken(TK_EOF, emptyStr, gLexLine, gLexCol)
        EXIT SUB
    END IF

    LET ch = "+"
END SUB

PRINT "Testing..."
LexerNextToken()
PRINT "Done"
