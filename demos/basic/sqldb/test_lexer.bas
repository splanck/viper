' Minimal lexer test with more components

DIM gLexSource AS STRING
DIM gLexPos AS INTEGER
DIM gLexLine AS INTEGER
DIM gLexCol AS INTEGER
DIM gLexLen AS INTEGER

CONST TK_EOF = 0
CONST TK_IDENTIFIER = 13

CLASS Token
    PUBLIC kind AS INTEGER
    PUBLIC text AS STRING
    PUBLIC SUB Init(k AS INTEGER, t AS STRING)
        kind = k
        text = t
    END SUB
END CLASS

DIM gTok AS Token

SUB LexerInit(src AS STRING)
    LET gLexSource = src
    LET gLexPos = 0
    LET gLexLine = 1
    LET gLexCol = 1
    LET gLexLen = LEN(src)
END SUB

FUNCTION LexerAtEnd() AS INTEGER
    IF gLexPos >= gLexLen THEN
        LexerAtEnd = -1
    ELSE
        LexerAtEnd = 0
    END IF
END FUNCTION

FUNCTION LexerPeek() AS STRING
    IF LexerAtEnd() <> 0 THEN
        LexerPeek = " "
    ELSE
        LexerPeek = MID$(gLexSource, gLexPos + 1, 1)
    END IF
END FUNCTION

FUNCTION LexerAdvance() AS STRING
    DIM ch AS STRING
    LET ch = LexerPeek()
    LET gLexPos = gLexPos + 1
    LexerAdvance = ch
END FUNCTION

SUB LexerMakeToken(k AS INTEGER, t AS STRING)
    LET gTok = NEW Token()
    gTok.Init(k, t)
END SUB

SUB LexerSkipWhitespace()
    DIM ch AS STRING
    WHILE LexerAtEnd() = 0
        LET ch = LexerPeek()
        IF ch = " " OR ch = CHR$(9) THEN
            LexerAdvance()
        ELSE
            EXIT WHILE
        END IF
    WEND
END SUB

FUNCTION IsAlphaCh(ch AS STRING) AS INTEGER
    DIM code AS INTEGER
    LET code = ASC(ch)
    IF (code >= 65) AND (code <= 90) THEN
        IsAlphaCh = -1
    ELSEIF (code >= 97) AND (code <= 122) THEN
        IsAlphaCh = -1
    ELSE
        IsAlphaCh = 0
    END IF
END FUNCTION

FUNCTION IsAlphaNumCh(ch AS STRING) AS INTEGER
    IF IsAlphaCh(ch) <> 0 THEN
        IsAlphaNumCh = -1
    ELSE
        IsAlphaNumCh = 0
    END IF
END FUNCTION

SUB LexerReadIdentifier()
    DIM startPos AS INTEGER
    DIM text AS STRING

    LET startPos = gLexPos
    WHILE (LexerAtEnd() = 0) AND (IsAlphaNumCh(LexerPeek()) <> 0)
        LexerAdvance()
    WEND
    LET text = MID$(gLexSource, startPos + 1, gLexPos - startPos)
    LexerMakeToken(TK_IDENTIFIER, text)
END SUB

SUB LexerNextToken()
    DIM ch AS STRING
    DIM emptyStr AS STRING

    LET emptyStr = " "
    LexerSkipWhitespace()

    IF LexerAtEnd() <> 0 THEN
        LexerMakeToken(TK_EOF, emptyStr)
        EXIT SUB
    END IF

    LET ch = LexerPeek()

    IF IsAlphaCh(ch) <> 0 THEN
        LexerReadIdentifier()
        EXIT SUB
    END IF

    LexerAdvance()
    LexerMakeToken(TK_EOF, ch)
END SUB

LexerInit("SELECT id FROM users")
LexerNextToken()
PRINT "Token 1: kind="; gTok.kind; " text='"; gTok.text; "'"
WHILE gTok.kind <> TK_EOF
    LexerNextToken()
    PRINT "Token: kind="; gTok.kind; " text='"; gTok.text; "'"
WEND
PRINT "Done"
