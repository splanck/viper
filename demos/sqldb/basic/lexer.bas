' lexer.bas - SQL Lexer
' Part of SQLite Clone - Viper Basic Implementation
' Requires: token.bas (AddFile before this)

'=============================================================================
' LEXER STATE - Global variables
'=============================================================================

DIM gLexSource AS STRING
DIM gLexPos AS INTEGER
DIM gLexLine AS INTEGER
DIM gLexCol AS INTEGER
DIM gLexLen AS INTEGER

' Global token for returning results
DIM gTok AS Token

'=============================================================================
' LEXER INITIALIZATION AND NAVIGATION
'=============================================================================

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

FUNCTION LexerPeekNext() AS STRING
    IF gLexPos + 1 >= gLexLen THEN
        LexerPeekNext = " "
    ELSE
        LexerPeekNext = MID$(gLexSource, gLexPos + 2, 1)
    END IF
END FUNCTION

FUNCTION LexerAdvance() AS STRING
    DIM ch AS STRING
    LET ch = LexerPeek()
    LET gLexPos = gLexPos + 1
    IF ch = CHR$(10) THEN
        LET gLexLine = gLexLine + 1
        LET gLexCol = 1
    ELSE
        LET gLexCol = gLexCol + 1
    END IF
    LexerAdvance = ch
END FUNCTION

SUB LexerSkipWhitespace()
    DIM ch AS STRING
    WHILE LexerAtEnd() = 0
        LET ch = LexerPeek()
        IF ch = " " OR ch = CHR$(9) OR ch = CHR$(13) OR ch = CHR$(10) THEN
            LexerAdvance()
        ELSEIF ch = "-" THEN
            IF LexerPeekNext() = "-" THEN
                WHILE (LexerAtEnd() = 0) AND (LexerPeek() <> CHR$(10))
                    LexerAdvance()
                WEND
            ELSE
                EXIT WHILE
            END IF
        ELSE
            EXIT WHILE
        END IF
    WEND
END SUB

'=============================================================================
' CHARACTER CLASSIFICATION
'=============================================================================

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
    ELSEIF (code >= 97) AND (code <= 122) THEN
        IsAlphaCh = -1
    ELSEIF ch = "_" THEN
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

'=============================================================================
' TOKEN CREATION
'=============================================================================

SUB LexerMakeToken(k AS INTEGER, t AS STRING, ln AS INTEGER, col AS INTEGER)
    LET gTok = NEW Token()
    gTok.Init(k, t, ln, col)
END SUB

SUB LexerReadNumber()
    DIM startLine AS INTEGER
    DIM startCol AS INTEGER
    DIM startPos AS INTEGER
    DIM text AS STRING
    DIM isFloat AS INTEGER

    LET startLine = gLexLine
    LET startCol = gLexCol
    LET startPos = gLexPos
    LET isFloat = 0

    WHILE (LexerAtEnd() = 0) AND (IsDigitCh(LexerPeek()) <> 0)
        LexerAdvance()
    WEND

    IF (LexerPeek() = ".") AND (IsDigitCh(LexerPeekNext()) <> 0) THEN
        LexerAdvance()
        LET isFloat = -1
        WHILE (LexerAtEnd() = 0) AND (IsDigitCh(LexerPeek()) <> 0)
            LexerAdvance()
        WEND
    END IF

    LET text = MID$(gLexSource, startPos + 1, gLexPos - startPos)
    IF isFloat <> 0 THEN
        LexerMakeToken(TK_NUMBER, text, startLine, startCol)
    ELSE
        LexerMakeToken(TK_INTEGER, text, startLine, startCol)
    END IF
END SUB

SUB LexerReadString()
    DIM startLine AS INTEGER
    DIM startCol AS INTEGER
    DIM quote AS STRING
    DIM startPos AS INTEGER
    DIM text AS STRING

    LET startLine = gLexLine
    LET startCol = gLexCol
    LET quote = LexerAdvance()
    LET startPos = gLexPos

    WHILE (LexerAtEnd() = 0) AND (LexerPeek() <> quote)
        IF LexerPeek() = CHR$(92) THEN
            LexerAdvance()
            IF LexerAtEnd() = 0 THEN LexerAdvance()
        ELSE
            LexerAdvance()
        END IF
    WEND

    LET text = MID$(gLexSource, startPos + 1, gLexPos - startPos)
    IF LexerAtEnd() = 0 THEN LexerAdvance()

    LexerMakeToken(TK_STRING, text, startLine, startCol)
END SUB

SUB LexerReadIdentifier()
    DIM startLine AS INTEGER
    DIM startCol AS INTEGER
    DIM startPos AS INTEGER
    DIM text AS STRING
    DIM upper AS STRING
    DIM kind AS INTEGER

    LET startLine = gLexLine
    LET startCol = gLexCol
    LET startPos = gLexPos

    WHILE (LexerAtEnd() = 0) AND (IsAlphaNumCh(LexerPeek()) <> 0)
        LexerAdvance()
    WEND

    LET text = MID$(gLexSource, startPos + 1, gLexPos - startPos)
    LET upper = UCASE$(text)
    LET kind = LookupKeyword(upper)

    LexerMakeToken(kind, text, startLine, startCol)
END SUB

'=============================================================================
' MAIN LEXER FUNCTION
'=============================================================================

SUB LexerNextToken()
    DIM startLine AS INTEGER
    DIM startCol AS INTEGER
    DIM ch AS STRING
    DIM nextCh AS STRING
    DIM emptyStr AS STRING

    LET emptyStr = " "
    LexerSkipWhitespace()

    IF LexerAtEnd() <> 0 THEN
        LexerMakeToken(TK_EOF, emptyStr, gLexLine, gLexCol)
        EXIT SUB
    END IF

    LET startLine = gLexLine
    LET startCol = gLexCol
    LET ch = LexerPeek()

    IF IsDigitCh(ch) <> 0 THEN
        LexerReadNumber()
        EXIT SUB
    END IF

    IF IsAlphaCh(ch) <> 0 THEN
        LexerReadIdentifier()
        EXIT SUB
    END IF

    IF ch = "'" OR ch = CHR$(34) THEN
        LexerReadString()
        EXIT SUB
    END IF

    LexerAdvance()
    LET nextCh = LexerPeek()

    ' Two-character operators
    IF (ch = "<") AND (nextCh = "=") THEN
        LexerAdvance()
        LexerMakeToken(TK_LE, "<=", startLine, startCol)
        EXIT SUB
    END IF
    IF (ch = ">") AND (nextCh = "=") THEN
        LexerAdvance()
        LexerMakeToken(TK_GE, ">=", startLine, startCol)
        EXIT SUB
    END IF
    IF (ch = "<") AND (nextCh = ">") THEN
        LexerAdvance()
        LexerMakeToken(TK_NE, "<>", startLine, startCol)
        EXIT SUB
    END IF
    IF (ch = "!") AND (nextCh = "=") THEN
        LexerAdvance()
        LexerMakeToken(TK_NE, "!=", startLine, startCol)
        EXIT SUB
    END IF
    IF (ch = "|") AND (nextCh = "|") THEN
        LexerAdvance()
        LexerMakeToken(TK_CONCAT, "||", startLine, startCol)
        EXIT SUB
    END IF

    ' Single-character operators and punctuation
    IF ch = "+" THEN
        LexerMakeToken(TK_PLUS, "+", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "-" THEN
        LexerMakeToken(TK_MINUS, "-", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "*" THEN
        LexerMakeToken(TK_STAR, "*", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "/" THEN
        LexerMakeToken(TK_SLASH, "/", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "%" THEN
        LexerMakeToken(TK_PERCENT, "%", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "=" THEN
        LexerMakeToken(TK_EQ, "=", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "<" THEN
        LexerMakeToken(TK_LT, "<", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = ">" THEN
        LexerMakeToken(TK_GT, ">", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "(" THEN
        LexerMakeToken(TK_LPAREN, "(", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = ")" THEN
        LexerMakeToken(TK_RPAREN, ")", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "," THEN
        LexerMakeToken(TK_COMMA, ",", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = ";" THEN
        LexerMakeToken(TK_SEMICOLON, ";", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "." THEN
        LexerMakeToken(TK_DOT, ".", startLine, startCol)
        EXIT SUB
    END IF

    LexerMakeToken(TK_ERROR, ch, startLine, startCol)
END SUB
