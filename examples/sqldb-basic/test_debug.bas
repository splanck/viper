' Debug the lexer issue

DIM gLexSource AS STRING
DIM gLexPos AS INTEGER
DIM gLexLen AS INTEGER

SUB LexerInit(src AS STRING)
    LET gLexSource = src
    LET gLexPos = 0
    LET gLexLen = LEN(src)
    PRINT "Init: source='"; gLexSource; "'"
    PRINT "Init: len="; gLexLen; " pos="; gLexPos
END SUB

FUNCTION LexerAtEnd() AS INTEGER
    PRINT "AtEnd check: pos="; gLexPos; " len="; gLexLen
    IF gLexPos >= gLexLen THEN
        LexerAtEnd = -1
    ELSE
        LexerAtEnd = 0
    END IF
END FUNCTION

LexerInit("SELECT")
PRINT "After call: gLexLen="; gLexLen
PRINT "AtEnd result:"; LexerAtEnd()
