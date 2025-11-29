'==============================================================================
' CHESS EVALUATION
' Position evaluation using material counting, piece-square tables,
' pawn structure, mobility, and king safety
'==============================================================================

' Piece-square tables (from white's perspective, mirrored for black)
' Values are in centipawns, added to base piece value

' Pawn piece-square table
DIM gPawnPST(63) AS INTEGER
' Knight piece-square table
DIM gKnightPST(63) AS INTEGER
' Bishop piece-square table
DIM gBishopPST(63) AS INTEGER
' Rook piece-square table
DIM gRookPST(63) AS INTEGER
' Queen piece-square table
DIM gQueenPST(63) AS INTEGER
' King middle-game piece-square table
DIM gKingMidPST(63) AS INTEGER
' King end-game piece-square table
DIM gKingEndPST(63) AS INTEGER

' Initialize piece-square tables
SUB InitEval()
    DIM i AS INTEGER

    ' Pawn PST - encourage center control and advancement
    ' Rank 1 (squares 0-7) - never used for pawns
    FOR i = 0 TO 7: gPawnPST(i) = 0: NEXT i
    ' Rank 2 (squares 8-15)
    gPawnPST(8) = 0: gPawnPST(9) = 0: gPawnPST(10) = 0: gPawnPST(11) = 0
    gPawnPST(12) = 0: gPawnPST(13) = 0: gPawnPST(14) = 0: gPawnPST(15) = 0
    ' Rank 3 (squares 16-23)
    gPawnPST(16) = 5: gPawnPST(17) = 10: gPawnPST(18) = 10: gPawnPST(19) = -20
    gPawnPST(20) = -20: gPawnPST(21) = 10: gPawnPST(22) = 10: gPawnPST(23) = 5
    ' Rank 4 (squares 24-31)
    gPawnPST(24) = 5: gPawnPST(25) = -5: gPawnPST(26) = -10: gPawnPST(27) = 20
    gPawnPST(28) = 20: gPawnPST(29) = -10: gPawnPST(30) = -5: gPawnPST(31) = 5
    ' Rank 5 (squares 32-39)
    gPawnPST(32) = 0: gPawnPST(33) = 0: gPawnPST(34) = 0: gPawnPST(35) = 25
    gPawnPST(36) = 25: gPawnPST(37) = 0: gPawnPST(38) = 0: gPawnPST(39) = 0
    ' Rank 6 (squares 40-47)
    gPawnPST(40) = 10: gPawnPST(41) = 10: gPawnPST(42) = 20: gPawnPST(43) = 30
    gPawnPST(44) = 30: gPawnPST(45) = 20: gPawnPST(46) = 10: gPawnPST(47) = 10
    ' Rank 7 (squares 48-55)
    gPawnPST(48) = 50: gPawnPST(49) = 50: gPawnPST(50) = 50: gPawnPST(51) = 50
    gPawnPST(52) = 50: gPawnPST(53) = 50: gPawnPST(54) = 50: gPawnPST(55) = 50
    ' Rank 8 (squares 56-63) - never used for pawns
    FOR i = 56 TO 63: gPawnPST(i) = 0: NEXT i

    ' Knight PST - knights are better in the center
    gKnightPST(0) = -50: gKnightPST(1) = -40: gKnightPST(2) = -30: gKnightPST(3) = -30
    gKnightPST(4) = -30: gKnightPST(5) = -30: gKnightPST(6) = -40: gKnightPST(7) = -50
    gKnightPST(8) = -40: gKnightPST(9) = -20: gKnightPST(10) = 0: gKnightPST(11) = 5
    gKnightPST(12) = 5: gKnightPST(13) = 0: gKnightPST(14) = -20: gKnightPST(15) = -40
    gKnightPST(16) = -30: gKnightPST(17) = 5: gKnightPST(18) = 10: gKnightPST(19) = 15
    gKnightPST(20) = 15: gKnightPST(21) = 10: gKnightPST(22) = 5: gKnightPST(23) = -30
    gKnightPST(24) = -30: gKnightPST(25) = 0: gKnightPST(26) = 15: gKnightPST(27) = 20
    gKnightPST(28) = 20: gKnightPST(29) = 15: gKnightPST(30) = 0: gKnightPST(31) = -30
    gKnightPST(32) = -30: gKnightPST(33) = 5: gKnightPST(34) = 15: gKnightPST(35) = 20
    gKnightPST(36) = 20: gKnightPST(37) = 15: gKnightPST(38) = 5: gKnightPST(39) = -30
    gKnightPST(40) = -30: gKnightPST(41) = 0: gKnightPST(42) = 10: gKnightPST(43) = 15
    gKnightPST(44) = 15: gKnightPST(45) = 10: gKnightPST(46) = 0: gKnightPST(47) = -30
    gKnightPST(48) = -40: gKnightPST(49) = -20: gKnightPST(50) = 0: gKnightPST(51) = 0
    gKnightPST(52) = 0: gKnightPST(53) = 0: gKnightPST(54) = -20: gKnightPST(55) = -40
    gKnightPST(56) = -50: gKnightPST(57) = -40: gKnightPST(58) = -30: gKnightPST(59) = -30
    gKnightPST(60) = -30: gKnightPST(61) = -30: gKnightPST(62) = -40: gKnightPST(63) = -50

    ' Bishop PST - bishops like long diagonals
    gBishopPST(0) = -20: gBishopPST(1) = -10: gBishopPST(2) = -10: gBishopPST(3) = -10
    gBishopPST(4) = -10: gBishopPST(5) = -10: gBishopPST(6) = -10: gBishopPST(7) = -20
    gBishopPST(8) = -10: gBishopPST(9) = 5: gBishopPST(10) = 0: gBishopPST(11) = 0
    gBishopPST(12) = 0: gBishopPST(13) = 0: gBishopPST(14) = 5: gBishopPST(15) = -10
    gBishopPST(16) = -10: gBishopPST(17) = 10: gBishopPST(18) = 10: gBishopPST(19) = 10
    gBishopPST(20) = 10: gBishopPST(21) = 10: gBishopPST(22) = 10: gBishopPST(23) = -10
    gBishopPST(24) = -10: gBishopPST(25) = 0: gBishopPST(26) = 10: gBishopPST(27) = 10
    gBishopPST(28) = 10: gBishopPST(29) = 10: gBishopPST(30) = 0: gBishopPST(31) = -10
    gBishopPST(32) = -10: gBishopPST(33) = 5: gBishopPST(34) = 5: gBishopPST(35) = 10
    gBishopPST(36) = 10: gBishopPST(37) = 5: gBishopPST(38) = 5: gBishopPST(39) = -10
    gBishopPST(40) = -10: gBishopPST(41) = 0: gBishopPST(42) = 5: gBishopPST(43) = 10
    gBishopPST(44) = 10: gBishopPST(45) = 5: gBishopPST(46) = 0: gBishopPST(47) = -10
    gBishopPST(48) = -10: gBishopPST(49) = 0: gBishopPST(50) = 0: gBishopPST(51) = 0
    gBishopPST(52) = 0: gBishopPST(53) = 0: gBishopPST(54) = 0: gBishopPST(55) = -10
    gBishopPST(56) = -20: gBishopPST(57) = -10: gBishopPST(58) = -10: gBishopPST(59) = -10
    gBishopPST(60) = -10: gBishopPST(61) = -10: gBishopPST(62) = -10: gBishopPST(63) = -20

    ' Rook PST - rooks like open files and 7th rank
    gRookPST(0) = 0: gRookPST(1) = 0: gRookPST(2) = 5: gRookPST(3) = 10
    gRookPST(4) = 10: gRookPST(5) = 5: gRookPST(6) = 0: gRookPST(7) = 0
    gRookPST(8) = -5: gRookPST(9) = 0: gRookPST(10) = 0: gRookPST(11) = 0
    gRookPST(12) = 0: gRookPST(13) = 0: gRookPST(14) = 0: gRookPST(15) = -5
    gRookPST(16) = -5: gRookPST(17) = 0: gRookPST(18) = 0: gRookPST(19) = 0
    gRookPST(20) = 0: gRookPST(21) = 0: gRookPST(22) = 0: gRookPST(23) = -5
    gRookPST(24) = -5: gRookPST(25) = 0: gRookPST(26) = 0: gRookPST(27) = 0
    gRookPST(28) = 0: gRookPST(29) = 0: gRookPST(30) = 0: gRookPST(31) = -5
    gRookPST(32) = -5: gRookPST(33) = 0: gRookPST(34) = 0: gRookPST(35) = 0
    gRookPST(36) = 0: gRookPST(37) = 0: gRookPST(38) = 0: gRookPST(39) = -5
    gRookPST(40) = -5: gRookPST(41) = 0: gRookPST(42) = 0: gRookPST(43) = 0
    gRookPST(44) = 0: gRookPST(45) = 0: gRookPST(46) = 0: gRookPST(47) = -5
    gRookPST(48) = 5: gRookPST(49) = 10: gRookPST(50) = 10: gRookPST(51) = 10
    gRookPST(52) = 10: gRookPST(53) = 10: gRookPST(54) = 10: gRookPST(55) = 5
    gRookPST(56) = 0: gRookPST(57) = 0: gRookPST(58) = 0: gRookPST(59) = 0
    gRookPST(60) = 0: gRookPST(61) = 0: gRookPST(62) = 0: gRookPST(63) = 0

    ' Queen PST - queen generally likes center, but not too aggressive early
    gQueenPST(0) = -20: gQueenPST(1) = -10: gQueenPST(2) = -10: gQueenPST(3) = -5
    gQueenPST(4) = -5: gQueenPST(5) = -10: gQueenPST(6) = -10: gQueenPST(7) = -20
    gQueenPST(8) = -10: gQueenPST(9) = 0: gQueenPST(10) = 5: gQueenPST(11) = 0
    gQueenPST(12) = 0: gQueenPST(13) = 0: gQueenPST(14) = 0: gQueenPST(15) = -10
    gQueenPST(16) = -10: gQueenPST(17) = 5: gQueenPST(18) = 5: gQueenPST(19) = 5
    gQueenPST(20) = 5: gQueenPST(21) = 5: gQueenPST(22) = 0: gQueenPST(23) = -10
    gQueenPST(24) = 0: gQueenPST(25) = 0: gQueenPST(26) = 5: gQueenPST(27) = 5
    gQueenPST(28) = 5: gQueenPST(29) = 5: gQueenPST(30) = 0: gQueenPST(31) = -5
    gQueenPST(32) = -5: gQueenPST(33) = 0: gQueenPST(34) = 5: gQueenPST(35) = 5
    gQueenPST(36) = 5: gQueenPST(37) = 5: gQueenPST(38) = 0: gQueenPST(39) = -5
    gQueenPST(40) = -10: gQueenPST(41) = 0: gQueenPST(42) = 5: gQueenPST(43) = 5
    gQueenPST(44) = 5: gQueenPST(45) = 5: gQueenPST(46) = 0: gQueenPST(47) = -10
    gQueenPST(48) = -10: gQueenPST(49) = 0: gQueenPST(50) = 0: gQueenPST(51) = 0
    gQueenPST(52) = 0: gQueenPST(53) = 0: gQueenPST(54) = 0: gQueenPST(55) = -10
    gQueenPST(56) = -20: gQueenPST(57) = -10: gQueenPST(58) = -10: gQueenPST(59) = -5
    gQueenPST(60) = -5: gQueenPST(61) = -10: gQueenPST(62) = -10: gQueenPST(63) = -20

    ' King middle-game PST - king should stay castled and safe
    gKingMidPST(0) = 20: gKingMidPST(1) = 30: gKingMidPST(2) = 10: gKingMidPST(3) = 0
    gKingMidPST(4) = 0: gKingMidPST(5) = 10: gKingMidPST(6) = 30: gKingMidPST(7) = 20
    gKingMidPST(8) = 20: gKingMidPST(9) = 20: gKingMidPST(10) = 0: gKingMidPST(11) = 0
    gKingMidPST(12) = 0: gKingMidPST(13) = 0: gKingMidPST(14) = 20: gKingMidPST(15) = 20
    gKingMidPST(16) = -10: gKingMidPST(17) = -20: gKingMidPST(18) = -20: gKingMidPST(19) = -20
    gKingMidPST(20) = -20: gKingMidPST(21) = -20: gKingMidPST(22) = -20: gKingMidPST(23) = -10
    gKingMidPST(24) = -20: gKingMidPST(25) = -30: gKingMidPST(26) = -30: gKingMidPST(27) = -40
    gKingMidPST(28) = -40: gKingMidPST(29) = -30: gKingMidPST(30) = -30: gKingMidPST(31) = -20
    gKingMidPST(32) = -30: gKingMidPST(33) = -40: gKingMidPST(34) = -40: gKingMidPST(35) = -50
    gKingMidPST(36) = -50: gKingMidPST(37) = -40: gKingMidPST(38) = -40: gKingMidPST(39) = -30
    gKingMidPST(40) = -30: gKingMidPST(41) = -40: gKingMidPST(42) = -40: gKingMidPST(43) = -50
    gKingMidPST(44) = -50: gKingMidPST(45) = -40: gKingMidPST(46) = -40: gKingMidPST(47) = -30
    gKingMidPST(48) = -30: gKingMidPST(49) = -40: gKingMidPST(50) = -40: gKingMidPST(51) = -50
    gKingMidPST(52) = -50: gKingMidPST(53) = -40: gKingMidPST(54) = -40: gKingMidPST(55) = -30
    gKingMidPST(56) = -30: gKingMidPST(57) = -40: gKingMidPST(58) = -40: gKingMidPST(59) = -50
    gKingMidPST(60) = -50: gKingMidPST(61) = -40: gKingMidPST(62) = -40: gKingMidPST(63) = -30

    ' King end-game PST - king should be active in endgame
    gKingEndPST(0) = -50: gKingEndPST(1) = -30: gKingEndPST(2) = -30: gKingEndPST(3) = -30
    gKingEndPST(4) = -30: gKingEndPST(5) = -30: gKingEndPST(6) = -30: gKingEndPST(7) = -50
    gKingEndPST(8) = -30: gKingEndPST(9) = -30: gKingEndPST(10) = 0: gKingEndPST(11) = 0
    gKingEndPST(12) = 0: gKingEndPST(13) = 0: gKingEndPST(14) = -30: gKingEndPST(15) = -30
    gKingEndPST(16) = -30: gKingEndPST(17) = -10: gKingEndPST(18) = 20: gKingEndPST(19) = 30
    gKingEndPST(20) = 30: gKingEndPST(21) = 20: gKingEndPST(22) = -10: gKingEndPST(23) = -30
    gKingEndPST(24) = -30: gKingEndPST(25) = -10: gKingEndPST(26) = 30: gKingEndPST(27) = 40
    gKingEndPST(28) = 40: gKingEndPST(29) = 30: gKingEndPST(30) = -10: gKingEndPST(31) = -30
    gKingEndPST(32) = -30: gKingEndPST(33) = -10: gKingEndPST(34) = 30: gKingEndPST(35) = 40
    gKingEndPST(36) = 40: gKingEndPST(37) = 30: gKingEndPST(38) = -10: gKingEndPST(39) = -30
    gKingEndPST(40) = -30: gKingEndPST(41) = -10: gKingEndPST(42) = 20: gKingEndPST(43) = 30
    gKingEndPST(44) = 30: gKingEndPST(45) = 20: gKingEndPST(46) = -10: gKingEndPST(47) = -30
    gKingEndPST(48) = -30: gKingEndPST(49) = -20: gKingEndPST(50) = -10: gKingEndPST(51) = 0
    gKingEndPST(52) = 0: gKingEndPST(53) = -10: gKingEndPST(54) = -20: gKingEndPST(55) = -30
    gKingEndPST(56) = -50: gKingEndPST(57) = -40: gKingEndPST(58) = -30: gKingEndPST(59) = -20
    gKingEndPST(60) = -20: gKingEndPST(61) = -30: gKingEndPST(62) = -40: gKingEndPST(63) = -50
END SUB

' Get mirrored square for black pieces
FUNCTION MirrorSquare(sq AS INTEGER) AS INTEGER
    DIM f AS INTEGER
    DIM r AS INTEGER
    f = FILE_OF(sq)
    r = RANK_OF(sq)
    MirrorSquare = SQ(f, 7 - r)
END FUNCTION

' Check if we're in endgame (few pieces remain)
FUNCTION IsEndgame() AS INTEGER
    DIM queens AS INTEGER
    DIM minors AS INTEGER
    DIM sq AS INTEGER
    DIM piece AS INTEGER

    queens = 0
    minors = 0

    FOR sq = 0 TO 63
        piece = GetPiece(sq)
        IF piece = QUEEN THEN
            queens = queens + 1
        ELSEIF piece = KNIGHT OR piece = BISHOP THEN
            minors = minors + 1
        END IF
    NEXT sq

    ' Endgame if no queens, or queen but no minors
    IF queens = 0 THEN
        IsEndgame = 1
    ELSEIF queens <= 2 AND minors <= 2 THEN
        IsEndgame = 1
    ELSE
        IsEndgame = 0
    END IF
END FUNCTION

' Count material for a side
FUNCTION CountMaterial(side AS INTEGER) AS INTEGER
    DIM total AS INTEGER
    DIM sq AS INTEGER
    DIM piece AS INTEGER
    DIM clr AS INTEGER

    total = 0
    FOR sq = 0 TO 63
        piece = GetPiece(sq)
        IF piece <> EMPTY THEN
            clr = GetColor(sq)
            IF clr = side THEN
                total = total + GetPieceValue(piece)
            END IF
        END IF
    NEXT sq

    CountMaterial = total
END FUNCTION

' Get piece-square table value for a piece at a square
FUNCTION GetPSTValue(piece AS INTEGER, sq AS INTEGER, clr AS INTEGER, endgame AS INTEGER) AS INTEGER
    DIM pstSq AS INTEGER
    DIM val AS INTEGER

    ' Mirror square for black
    IF clr = BLACK THEN
        pstSq = MirrorSquare(sq)
    ELSE
        pstSq = sq
    END IF

    SELECT CASE piece
        CASE PAWN
            val = gPawnPST(pstSq)
        CASE KNIGHT
            val = gKnightPST(pstSq)
        CASE BISHOP
            val = gBishopPST(pstSq)
        CASE ROOK
            val = gRookPST(pstSq)
        CASE QUEEN
            val = gQueenPST(pstSq)
        CASE KING
            IF endgame <> 0 THEN
                val = gKingEndPST(pstSq)
            ELSE
                val = gKingMidPST(pstSq)
            END IF
        CASE ELSE
            val = 0
    END SELECT

    GetPSTValue = val
END FUNCTION

' Evaluate pawn structure
FUNCTION EvalPawnStructure(side AS INTEGER) AS INTEGER
    DIM score AS INTEGER
    DIM sq AS INTEGER
    DIM f AS INTEGER
    DIM r AS INTEGER
    DIM piece AS INTEGER
    DIM clr AS INTEGER
    DIM doubled AS INTEGER
    DIM isolated AS INTEGER
    DIM passed AS INTEGER
    DIM hasLeft AS INTEGER
    DIM hasRight AS INTEGER
    DIM blocked AS INTEGER
    DIM pawnOnFile(7) AS INTEGER
    DIM enemyPawnOnFile(7) AS INTEGER
    DIM i AS INTEGER
    DIM enemy AS INTEGER

    score = 0
    enemy = 1 - side

    ' Initialize pawn file counts
    FOR i = 0 TO 7
        pawnOnFile(i) = 0
        enemyPawnOnFile(i) = 0
    NEXT i

    ' Count pawns on each file
    FOR sq = 0 TO 63
        piece = GetPiece(sq)
        IF piece = PAWN THEN
            clr = GetColor(sq)
            f = FILE_OF(sq)
            IF clr = side THEN
                pawnOnFile(f) = pawnOnFile(f) + 1
            ELSE
                enemyPawnOnFile(f) = enemyPawnOnFile(f) + 1
            END IF
        END IF
    NEXT sq

    ' Evaluate each pawn
    FOR sq = 0 TO 63
        piece = GetPiece(sq)
        IF piece = PAWN THEN
            clr = GetColor(sq)
            IF clr = side THEN
                f = FILE_OF(sq)
                r = RANK_OF(sq)

                ' Doubled pawns penalty
                IF pawnOnFile(f) > 1 THEN
                    score = score - 10
                END IF

                ' Isolated pawn penalty
                hasLeft = 0
                hasRight = 0
                IF f > 0 THEN
                    IF pawnOnFile(f - 1) > 0 THEN hasLeft = 1
                END IF
                IF f < 7 THEN
                    IF pawnOnFile(f + 1) > 0 THEN hasRight = 1
                END IF
                IF hasLeft = 0 AND hasRight = 0 THEN
                    score = score - 20
                END IF

                ' Passed pawn bonus
                passed = 1
                IF side = WHITE THEN
                    ' Check if any enemy pawn can block or capture
                    FOR i = r + 1 TO 7
                        IF f > 0 THEN
                            IF GetPiece(SQ(f - 1, i)) = PAWN THEN
                                IF GetColor(SQ(f - 1, i)) = enemy THEN
                                    passed = 0
                                END IF
                            END IF
                        END IF
                        IF GetPiece(SQ(f, i)) = PAWN THEN
                            IF GetColor(SQ(f, i)) = enemy THEN
                                passed = 0
                            END IF
                        END IF
                        IF f < 7 THEN
                            IF GetPiece(SQ(f + 1, i)) = PAWN THEN
                                IF GetColor(SQ(f + 1, i)) = enemy THEN
                                    passed = 0
                                END IF
                            END IF
                        END IF
                    NEXT i
                    IF passed <> 0 THEN
                        score = score + 20 + r * 10  ' Bonus increases as pawn advances
                    END IF
                ELSE
                    ' Black pawn
                    FOR i = r - 1 TO 0 STEP -1
                        IF f > 0 THEN
                            IF GetPiece(SQ(f - 1, i)) = PAWN THEN
                                IF GetColor(SQ(f - 1, i)) = enemy THEN
                                    passed = 0
                                END IF
                            END IF
                        END IF
                        IF GetPiece(SQ(f, i)) = PAWN THEN
                            IF GetColor(SQ(f, i)) = enemy THEN
                                passed = 0
                            END IF
                        END IF
                        IF f < 7 THEN
                            IF GetPiece(SQ(f + 1, i)) = PAWN THEN
                                IF GetColor(SQ(f + 1, i)) = enemy THEN
                                    passed = 0
                                END IF
                            END IF
                        END IF
                    NEXT i
                    IF passed <> 0 THEN
                        score = score + 20 + (7 - r) * 10
                    END IF
                END IF
            END IF
        END IF
    NEXT sq

    EvalPawnStructure = score
END FUNCTION

' Evaluate bishop pair bonus
FUNCTION EvalBishopPair(side AS INTEGER) AS INTEGER
    DIM bishops AS INTEGER
    DIM sq AS INTEGER
    DIM piece AS INTEGER
    DIM clr AS INTEGER

    bishops = 0
    FOR sq = 0 TO 63
        piece = GetPiece(sq)
        IF piece = BISHOP THEN
            clr = GetColor(sq)
            IF clr = side THEN
                bishops = bishops + 1
            END IF
        END IF
    NEXT sq

    IF bishops >= 2 THEN
        EvalBishopPair = 30
    ELSE
        EvalBishopPair = 0
    END IF
END FUNCTION

' Evaluate rook on open file
FUNCTION EvalRookOpenFile(side AS INTEGER) AS INTEGER
    DIM score AS INTEGER
    DIM sq AS INTEGER
    DIM f AS INTEGER
    DIM r AS INTEGER
    DIM piece AS INTEGER
    DIM clr AS INTEGER
    DIM isOpen AS INTEGER
    DIM isSemiOpen AS INTEGER
    DIM checkSq AS INTEGER
    DIM checkPiece AS INTEGER
    DIM checkClr AS INTEGER

    score = 0

    FOR sq = 0 TO 63
        piece = GetPiece(sq)
        IF piece = ROOK THEN
            clr = GetColor(sq)
            IF clr = side THEN
                f = FILE_OF(sq)
                isOpen = 1
                isSemiOpen = 1

                ' Check the file for pawns
                FOR r = 0 TO 7
                    checkSq = SQ(f, r)
                    checkPiece = GetPiece(checkSq)
                    IF checkPiece = PAWN THEN
                        checkClr = GetColor(checkSq)
                        IF checkClr = side THEN
                            isSemiOpen = 0
                            isOpen = 0
                        ELSE
                            isOpen = 0
                        END IF
                    END IF
                NEXT r

                IF isOpen <> 0 THEN
                    score = score + 20
                ELSEIF isSemiOpen <> 0 THEN
                    score = score + 10
                END IF
            END IF
        END IF
    NEXT sq

    EvalRookOpenFile = score
END FUNCTION

' Evaluate king safety (simple version)
FUNCTION EvalKingSafety(side AS INTEGER) AS INTEGER
    DIM score AS INTEGER
    DIM kingSq AS INTEGER
    DIM f AS INTEGER
    DIM r AS INTEGER
    DIM shieldPawns AS INTEGER
    DIM pawnSq AS INTEGER
    DIM piece AS INTEGER
    DIM clr AS INTEGER

    score = 0
    kingSq = gKingSquare(side)
    f = FILE_OF(kingSq)
    r = RANK_OF(kingSq)

    ' Only evaluate if king is on back ranks (castled)
    IF side = WHITE THEN
        IF r <= 1 THEN
            ' Count pawn shield
            shieldPawns = 0
            IF r < 7 THEN
                IF f > 0 THEN
                    pawnSq = SQ(f - 1, r + 1)
                    piece = GetPiece(pawnSq)
                    IF piece = PAWN THEN
                        clr = GetColor(pawnSq)
                        IF clr = WHITE THEN shieldPawns = shieldPawns + 1
                    END IF
                END IF
                pawnSq = SQ(f, r + 1)
                piece = GetPiece(pawnSq)
                IF piece = PAWN THEN
                    clr = GetColor(pawnSq)
                    IF clr = WHITE THEN shieldPawns = shieldPawns + 1
                END IF
                IF f < 7 THEN
                    pawnSq = SQ(f + 1, r + 1)
                    piece = GetPiece(pawnSq)
                    IF piece = PAWN THEN
                        clr = GetColor(pawnSq)
                        IF clr = WHITE THEN shieldPawns = shieldPawns + 1
                    END IF
                END IF
            END IF
            score = shieldPawns * 10
        END IF
    ELSE
        IF r >= 6 THEN
            shieldPawns = 0
            IF r > 0 THEN
                IF f > 0 THEN
                    pawnSq = SQ(f - 1, r - 1)
                    piece = GetPiece(pawnSq)
                    IF piece = PAWN THEN
                        clr = GetColor(pawnSq)
                        IF clr = BLACK THEN shieldPawns = shieldPawns + 1
                    END IF
                END IF
                pawnSq = SQ(f, r - 1)
                piece = GetPiece(pawnSq)
                IF piece = PAWN THEN
                    clr = GetColor(pawnSq)
                    IF clr = BLACK THEN shieldPawns = shieldPawns + 1
                END IF
                IF f < 7 THEN
                    pawnSq = SQ(f + 1, r - 1)
                    piece = GetPiece(pawnSq)
                    IF piece = PAWN THEN
                        clr = GetColor(pawnSq)
                        IF clr = BLACK THEN shieldPawns = shieldPawns + 1
                    END IF
                END IF
            END IF
            score = shieldPawns * 10
        END IF
    END IF

    EvalKingSafety = score
END FUNCTION

' Main evaluation function
' Returns score from white's perspective (positive = white better)
FUNCTION Evaluate() AS INTEGER
    DIM score AS INTEGER
    DIM sq AS INTEGER
    DIM piece AS INTEGER
    DIM clr AS INTEGER
    DIM endgame AS INTEGER

    score = 0
    endgame = IsEndgame()

    ' Material and piece-square tables
    FOR sq = 0 TO 63
        piece = GetPiece(sq)
        IF piece <> EMPTY THEN
            clr = GetColor(sq)
            IF clr = WHITE THEN
                score = score + GetPieceValue(piece)
                score = score + GetPSTValue(piece, sq, clr, endgame)
            ELSE
                score = score - GetPieceValue(piece)
                score = score - GetPSTValue(piece, sq, clr, endgame)
            END IF
        END IF
    NEXT sq

    ' Pawn structure
    score = score + EvalPawnStructure(WHITE)
    score = score - EvalPawnStructure(BLACK)

    ' Bishop pair
    score = score + EvalBishopPair(WHITE)
    score = score - EvalBishopPair(BLACK)

    ' Rook on open file
    score = score + EvalRookOpenFile(WHITE)
    score = score - EvalRookOpenFile(BLACK)

    ' King safety (only in middlegame)
    IF endgame = 0 THEN
        score = score + EvalKingSafety(WHITE)
        score = score - EvalKingSafety(BLACK)
    END IF

    ' Return from perspective of side to move
    IF gSideToMove = WHITE THEN
        Evaluate = score
    ELSE
        Evaluate = -score
    END IF
END FUNCTION
