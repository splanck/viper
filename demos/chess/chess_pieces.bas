REM ============================================================================
REM  CHESS PIECES - Piece and Move classes
REM ============================================================================

REM ============================================================================
REM  PIECE CLASS
REM ============================================================================
CLASS Piece
    DIM pieceType AS INTEGER
    DIM pieceColor AS INTEGER
    DIM pieceMoved AS INTEGER

    SUB Init(pType AS INTEGER, pColor AS INTEGER)
        pieceType = pType
        pieceColor = pColor
        pieceMoved = 0
    END SUB

    SUB SetMoved()
        pieceMoved = 1
    END SUB

    FUNCTION GetType() AS INTEGER
        GetType = pieceType
    END FUNCTION

    FUNCTION GetColor() AS INTEGER
        GetColor = pieceColor
    END FUNCTION

    FUNCTION GetMoved() AS INTEGER
        GetMoved = pieceMoved
    END FUNCTION

    FUNCTION IsEmpty() AS INTEGER
        IF pieceType = 0 THEN
            IsEmpty = 1
        ELSE
            IsEmpty = 0
        END IF
    END FUNCTION

    FUNCTION GetSymbol() AS STRING
        DIM sym AS STRING
        sym = "."

        IF pieceType = 1 THEN
            IF pieceColor = 1 THEN
                sym = "P"
            ELSE
                sym = "p"
            END IF
        ELSE IF pieceType = 2 THEN
            IF pieceColor = 1 THEN
                sym = "N"
            ELSE
                sym = "n"
            END IF
        ELSE IF pieceType = 3 THEN
            IF pieceColor = 1 THEN
                sym = "B"
            ELSE
                sym = "b"
            END IF
        ELSE IF pieceType = 4 THEN
            IF pieceColor = 1 THEN
                sym = "R"
            ELSE
                sym = "r"
            END IF
        ELSE IF pieceType = 5 THEN
            IF pieceColor = 1 THEN
                sym = "Q"
            ELSE
                sym = "q"
            END IF
        ELSE IF pieceType = 6 THEN
            IF pieceColor = 1 THEN
                sym = "K"
            ELSE
                sym = "k"
            END IF
        END IF

        GetSymbol = sym
    END FUNCTION

    FUNCTION GetValue() AS INTEGER
        DIM val AS INTEGER
        val = 0

        IF pieceType = 1 THEN
            val = 100
        ELSE IF pieceType = 2 THEN
            val = 320
        ELSE IF pieceType = 3 THEN
            val = 330
        ELSE IF pieceType = 4 THEN
            val = 500
        ELSE IF pieceType = 5 THEN
            val = 900
        ELSE IF pieceType = 6 THEN
            val = 20000
        END IF

        GetValue = val
    END FUNCTION
END CLASS

REM ============================================================================
REM  MOVE CLASS
REM ============================================================================
CLASS Move
    DIM fromRow AS INTEGER
    DIM fromCol AS INTEGER
    DIM toRow AS INTEGER
    DIM toCol AS INTEGER
    DIM capturedType AS INTEGER
    DIM capturedColor AS INTEGER
    DIM moveFlag AS INTEGER
    DIM promotionType AS INTEGER
    DIM score AS INTEGER

    SUB Init(fr AS INTEGER, fc AS INTEGER, tr AS INTEGER, tc AS INTEGER)
        fromRow = fr
        fromCol = fc
        toRow = tr
        toCol = tc
        capturedType = 0
        capturedColor = 0
        moveFlag = 0
        promotionType = 0
        score = 0
    END SUB

    SUB SetCapture(cType AS INTEGER, cColor AS INTEGER)
        capturedType = cType
        capturedColor = cColor
    END SUB

    SUB SetFlag(flag AS INTEGER)
        moveFlag = flag
    END SUB

    SUB SetPromotion(pType AS INTEGER)
        promotionType = pType
        moveFlag = 4
    END SUB

    SUB SetScore(s AS INTEGER)
        score = s
    END SUB

    FUNCTION GetFromRow() AS INTEGER
        GetFromRow = fromRow
    END FUNCTION

    FUNCTION GetFromCol() AS INTEGER
        GetFromCol = fromCol
    END FUNCTION

    FUNCTION GetToRow() AS INTEGER
        GetToRow = toRow
    END FUNCTION

    FUNCTION GetToCol() AS INTEGER
        GetToCol = toCol
    END FUNCTION

    FUNCTION GetCapturedType() AS INTEGER
        GetCapturedType = capturedType
    END FUNCTION

    FUNCTION GetCapturedColor() AS INTEGER
        GetCapturedColor = capturedColor
    END FUNCTION

    FUNCTION GetFlag() AS INTEGER
        GetFlag = moveFlag
    END FUNCTION

    FUNCTION GetPromotionType() AS INTEGER
        GetPromotionType = promotionType
    END FUNCTION

    FUNCTION GetScore() AS INTEGER
        GetScore = score
    END FUNCTION

    FUNCTION ToAlgebraic() AS STRING
        DIM result AS STRING
        DIM fromFile AS STRING
        DIM toFile AS STRING

        fromFile = CHR$(97 + fromCol)
        toFile = CHR$(97 + toCol)

        result = fromFile + LTRIM$(STR$(fromRow + 1)) + toFile + LTRIM$(STR$(toRow + 1))

        IF moveFlag = 4 AND promotionType > 0 THEN
            IF promotionType = 5 THEN
                result = result + "q"
            ELSE IF promotionType = 4 THEN
                result = result + "r"
            ELSE IF promotionType = 3 THEN
                result = result + "b"
            ELSE IF promotionType = 2 THEN
                result = result + "n"
            END IF
        END IF

        ToAlgebraic = result
    END FUNCTION
END CLASS
