'==============================================================================
' CHESS SEARCH ENGINE
' Alpha-beta search with iterative deepening, move ordering, and quiescence
'==============================================================================

' Search statistics
DIM gNodes AS INTEGER           ' Nodes searched
DIM gQNodes AS INTEGER          ' Quiescence nodes
DIM gCutoffs AS INTEGER         ' Beta cutoffs
DIM gBestMove AS INTEGER        ' Best move found

' Killer moves for move ordering (2 per ply)
DIM gKillers(127) AS INTEGER    ' MAX_DEPTH * 2

' History heuristic for move ordering
DIM gHistory(4095) AS INTEGER   ' 64 * 64 (from-to pairs)

' Principal variation
DIM gPV(63) AS INTEGER          ' PV moves
DIM gPVLength AS INTEGER

' Time control
DIM gStartTime AS INTEGER
DIM gTimeLimit AS INTEGER       ' Time limit in milliseconds
DIM gStopSearch AS INTEGER      ' Flag to stop search

' Move stack for recursive search (each ply needs its own copy)
' Store up to 16 plies, 256 moves each
DIM gMoveStack(4095) AS INTEGER ' 16 * 256 moves
DIM gMoveCountStack(15) AS INTEGER

' Save moves to stack at given ply
SUB SaveMoves(ply AS INTEGER)
    DIM i AS INTEGER
    DIM baseIdx AS INTEGER

    baseIdx = ply * 256
    gMoveCountStack(ply) = gMoveCount
    FOR i = 0 TO gMoveCount - 1
        gMoveStack(baseIdx + i) = gMoves(i)
    NEXT i
END SUB

' Restore moves from stack at given ply
SUB RestoreMoves(ply AS INTEGER)
    DIM i AS INTEGER
    DIM baseIdx AS INTEGER

    baseIdx = ply * 256
    gMoveCount = gMoveCountStack(ply)
    FOR i = 0 TO gMoveCount - 1
        gMoves(i) = gMoveStack(baseIdx + i)
    NEXT i
END SUB

' Initialize search
SUB InitSearch()
    DIM i AS INTEGER

    gNodes = 0
    gQNodes = 0
    gCutoffs = 0
    gBestMove = 0
    gPVLength = 0
    gStopSearch = 0

    ' Clear killer moves
    FOR i = 0 TO 127
        gKillers(i) = 0
    NEXT i

    ' Clear history
    FOR i = 0 TO 4095
        gHistory(i) = 0
    NEXT i

    ' Clear PV
    FOR i = 0 TO 63
        gPV(i) = 0
    NEXT i
END SUB

' Check if time is up
FUNCTION TimeUp() AS INTEGER
    DIM elapsed AS INTEGER
    elapsed = Viper.Time.GetTickCount() - gStartTime
    IF elapsed >= gTimeLimit THEN
        TimeUp = 1
    ELSE
        TimeUp = 0
    END IF
END FUNCTION

' Get history score for move ordering
FUNCTION GetHistoryScore(move AS INTEGER) AS INTEGER
    DIM fromSq AS INTEGER
    DIM toSq AS INTEGER
    DIM idx AS INTEGER

    fromSq = MoveFrom(move)
    toSq = MoveTo(move)
    idx = fromSq * 64 + toSq

    GetHistoryScore = gHistory(idx)
END FUNCTION

' Update history on beta cutoff
SUB UpdateHistory(move AS INTEGER, depth AS INTEGER)
    DIM fromSq AS INTEGER
    DIM toSq AS INTEGER
    DIM idx AS INTEGER
    DIM bonus AS INTEGER

    fromSq = MoveFrom(move)
    toSq = MoveTo(move)
    idx = fromSq * 64 + toSq

    bonus = depth * depth
    gHistory(idx) = gHistory(idx) + bonus

    ' Cap history values
    IF gHistory(idx) > 10000 THEN
        gHistory(idx) = 10000
    END IF
END SUB

' Update killer moves
SUB UpdateKillers(move AS INTEGER, ply AS INTEGER)
    DIM idx AS INTEGER
    idx = ply * 2

    ' Don't store captures as killers
    IF MoveFlags(move) = MOVE_CAPTURE THEN EXIT SUB
    IF MoveFlags(move) = MOVE_EP THEN EXIT SUB

    ' Shift killers
    IF gKillers(idx) <> move THEN
        gKillers(idx + 1) = gKillers(idx)
        gKillers(idx) = move
    END IF
END SUB

' Check if move is a killer
FUNCTION IsKiller(move AS INTEGER, ply AS INTEGER) AS INTEGER
    DIM idx AS INTEGER
    idx = ply * 2

    IF move = gKillers(idx) THEN
        IsKiller = 1
    ELSEIF move = gKillers(idx + 1) THEN
        IsKiller = 1
    ELSE
        IsKiller = 0
    END IF
END FUNCTION

' Score move for ordering (higher = better)
FUNCTION ScoreMoveForOrdering(move AS INTEGER, ply AS INTEGER) AS INTEGER
    DIM score AS INTEGER
    DIM flag AS INTEGER
    DIM captured AS INTEGER
    DIM piece AS INTEGER
    DIM fromSq AS INTEGER
    DIM toSq AS INTEGER

    score = 0
    flag = MoveFlags(move)
    fromSq = MoveFrom(move)
    toSq = MoveTo(move)

    ' MVV-LVA for captures
    IF flag = MOVE_CAPTURE THEN
        captured = GetPiece(toSq)
        piece = GetPiece(fromSq)
        ' MVV-LVA: capture value * 10 - attacker value
        score = GetPieceValue(captured) * 10 - GetPieceValue(piece) + 10000
    ELSEIF flag = MOVE_EP THEN
        piece = GetPiece(fromSq)
        score = PAWN_VALUE * 10 - GetPieceValue(piece) + 10000
    ' Promotions
    ELSEIF flag = MOVE_PROMOTION THEN
        score = 9000 + MovePromo(move) * 100
    ' Killer moves
    ELSEIF IsKiller(move, ply) <> 0 THEN
        score = 8000
    ' History heuristic
    ELSE
        score = GetHistoryScore(move)
    END IF

    ScoreMoveForOrdering = score
END FUNCTION

' Sort global moves array by score (simple selection sort for small lists)
SUB SortGlobalMoves(ply AS INTEGER)
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM best AS INTEGER
    DIM bestIdx AS INTEGER
    DIM temp AS INTEGER
    DIM scores(255) AS INTEGER

    ' Score all moves first
    FOR i = 0 TO gMoveCount - 1
        scores(i) = ScoreMoveForOrdering(gMoves(i), ply)
    NEXT i

    ' Selection sort (descending)
    FOR i = 0 TO gMoveCount - 2
        bestIdx = i
        best = scores(i)
        FOR j = i + 1 TO gMoveCount - 1
            IF scores(j) > best THEN
                best = scores(j)
                bestIdx = j
            END IF
        NEXT j
        IF bestIdx <> i THEN
            temp = gMoves(i)
            gMoves(i) = gMoves(bestIdx)
            gMoves(bestIdx) = temp
            temp = scores(i)
            scores(i) = scores(bestIdx)
            scores(bestIdx) = temp
        END IF
    NEXT i
END SUB

' Quiescence search - search captures to avoid horizon effect
FUNCTION Quiesce(alpha AS INTEGER, beta AS INTEGER) AS INTEGER
    DIM standPat AS INTEGER
    DIM score AS INTEGER
    DIM i AS INTEGER
    DIM count AS INTEGER
    DIM flag AS INTEGER
    DIM move AS INTEGER
    DIM undoInfo AS INTEGER
    DIM doSearch AS INTEGER

    gQNodes = gQNodes + 1

    ' Check for time (every 1024 nodes)
    IF (gQNodes MOD 1024) = 0 THEN
        IF TimeUp() <> 0 THEN
            gStopSearch = 1
            Quiesce = 0
            EXIT FUNCTION
        END IF
    END IF

    ' Stand pat
    standPat = Evaluate()

    IF standPat >= beta THEN
        Quiesce = beta
        EXIT FUNCTION
    END IF

    IF standPat > alpha THEN
        alpha = standPat
    END IF

    ' Generate all moves (we'll filter for captures)
    GenAllMoves()
    count = gMoveCount

    ' Only search captures
    FOR i = 0 TO count - 1
        move = gMoves(i)
        flag = MoveFlags(move)
        doSearch = 0

        ' Only search captures and promotions
        IF flag = MOVE_CAPTURE THEN
            doSearch = 1
        ELSEIF flag = MOVE_EP THEN
            doSearch = 1
        ELSEIF flag = MOVE_PROMOTION THEN
            doSearch = 1
        END IF

        IF doSearch <> 0 THEN
            undoInfo = MakeMove(move)

            ' Check if move was legal (side that moved is not in check)
            IF IsInCheck() = 0 THEN
                score = -Quiesce(-beta, -alpha)

                UnmakeMove(move, undoInfo)

                IF gStopSearch <> 0 THEN
                    Quiesce = 0
                    EXIT FUNCTION
                END IF

                IF score >= beta THEN
                    Quiesce = beta
                    EXIT FUNCTION
                END IF

                IF score > alpha THEN
                    alpha = score
                END IF
            ELSE
                UnmakeMove(move, undoInfo)
            END IF
        END IF
    NEXT i

    Quiesce = alpha
END FUNCTION

' Alpha-beta search
FUNCTION AlphaBeta(depth AS INTEGER, alpha AS INTEGER, beta AS INTEGER, ply AS INTEGER) AS INTEGER
    DIM score AS INTEGER
    DIM bestScore AS INTEGER
    DIM i AS INTEGER
    DIM count AS INTEGER
    DIM legalMoves AS INTEGER
    DIM inCheck AS INTEGER
    DIM move AS INTEGER
    DIM undoInfo AS INTEGER
    DIM isLegal AS INTEGER

    gNodes = gNodes + 1

    ' Check for time (every 4096 nodes)
    IF (gNodes MOD 4096) = 0 THEN
        IF TimeUp() <> 0 THEN
            gStopSearch = 1
            AlphaBeta = 0
            EXIT FUNCTION
        END IF
    END IF

    ' Check for repetition
    IF IsRepetition() <> 0 THEN
        AlphaBeta = 0
        EXIT FUNCTION
    END IF

    ' Check for 50-move rule
    IF gHalfMoveClock >= 100 THEN
        AlphaBeta = 0
        EXIT FUNCTION
    END IF

    ' Leaf node - quiescence search
    IF depth <= 0 THEN
        AlphaBeta = Quiesce(alpha, beta)
        EXIT FUNCTION
    END IF

    inCheck = IsInCheck()

    ' Check extension
    IF inCheck <> 0 THEN
        depth = depth + 1
    END IF

    ' Generate moves
    GenAllMoves()

    ' Sort moves for better ordering
    SortGlobalMoves(ply)

    ' Save moves for this ply (since recursion will overwrite gMoves)
    SaveMoves(ply)
    count = gMoveCount

    bestScore = -INFINITY
    legalMoves = 0

    FOR i = 0 TO count - 1
        ' Restore moves (may have been overwritten by recursive call)
        RestoreMoves(ply)
        move = gMoves(i)

        undoInfo = MakeMove(move)

        ' Check if move was legal (side that moved is not in check)
        isLegal = 0
        IF IsInCheck() = 0 THEN
            isLegal = 1
        END IF

        IF isLegal <> 0 THEN
            legalMoves = legalMoves + 1

            ' Store position for repetition detection
            StorePosition()

            score = -AlphaBeta(depth - 1, -beta, -alpha, ply + 1)

            ' Remove position from history
            gPosHistoryCount = gPosHistoryCount - 1

            UnmakeMove(move, undoInfo)

            IF gStopSearch <> 0 THEN
                AlphaBeta = 0
                EXIT FUNCTION
            END IF

            IF score > bestScore THEN
                bestScore = score

                IF score > alpha THEN
                    alpha = score

                    ' Update PV at root
                    IF ply = 0 THEN
                        gBestMove = move
                    END IF

                    IF score >= beta THEN
                        ' Beta cutoff
                        gCutoffs = gCutoffs + 1
                        UpdateKillers(move, ply)
                        UpdateHistory(move, depth)
                        AlphaBeta = beta
                        EXIT FUNCTION
                    END IF
                END IF
            END IF
        ELSE
            UnmakeMove(move, undoInfo)
        END IF
    NEXT i

    ' Check for checkmate or stalemate
    IF legalMoves = 0 THEN
        IF inCheck <> 0 THEN
            ' Checkmate
            bestScore = -MATE_SCORE + ply
        ELSE
            ' Stalemate
            bestScore = 0
        END IF
    END IF

    AlphaBeta = bestScore
END FUNCTION

' Iterative deepening search
FUNCTION Search(maxDepth AS INTEGER, timeMs AS INTEGER) AS INTEGER
    DIM depth AS INTEGER
    DIM score AS INTEGER
    DIM prevBest AS INTEGER

    InitSearch()

    gStartTime = Viper.Time.GetTickCount()
    gTimeLimit = timeMs
    gBestMove = 0
    prevBest = 0

    ' Iterative deepening
    FOR depth = 1 TO maxDepth
        gNodes = 0
        gQNodes = 0
        gCutoffs = 0

        score = AlphaBeta(depth, -INFINITY, INFINITY, 0)

        IF gStopSearch <> 0 THEN
            ' Use previous iteration's best move if search was stopped
            IF gBestMove = 0 THEN gBestMove = prevBest
            EXIT FOR
        END IF

        prevBest = gBestMove

        ' Print search info
        PrintSearchInfo(depth, score)

        ' Stop if we found a mate
        IF score > MATE_SCORE - 100 THEN
            EXIT FOR
        ELSEIF score < -MATE_SCORE + 100 THEN
            EXIT FOR
        END IF
    NEXT depth

    Search = gBestMove
END FUNCTION

' Print search information
SUB PrintSearchInfo(depth AS INTEGER, score AS INTEGER)
    DIM elapsed AS INTEGER
    DIM nps AS INTEGER
    DIM scoreStr AS STRING
    DIM sb AS Viper.Text.StringBuilder

    sb = NEW Viper.Text.StringBuilder()

    elapsed = Viper.Time.GetTickCount() - gStartTime
    IF elapsed > 0 THEN
        nps = ((gNodes + gQNodes) * 1000) / elapsed
    ELSE
        nps = 0
    END IF

    ' Format score
    IF score > MATE_SCORE - 100 THEN
        scoreStr = "mate " + STR$((MATE_SCORE - score + 1) / 2)
    ELSEIF score < -MATE_SCORE + 100 THEN
        scoreStr = "mate " + STR$(-(MATE_SCORE + score + 1) / 2)
    ELSE
        scoreStr = STR$(score) + " cp"
    END IF

    sb.Append("info depth ")
    sb.Append(STR$(depth))
    sb.Append(" score ")
    sb.Append(scoreStr)
    sb.Append(" nodes ")
    sb.Append(STR$(gNodes + gQNodes))
    sb.Append(" nps ")
    sb.Append(STR$(nps))
    sb.Append(" time ")
    sb.Append(STR$(elapsed))

    IF gBestMove <> 0 THEN
        sb.Append(" pv ")
        sb.Append(MoveToAlg(gBestMove))
    END IF

    PRINT sb.ToString()
END SUB

' Get computer's move for current position
FUNCTION GetAIMove(thinkTime AS INTEGER) AS INTEGER
    DIM move AS INTEGER
    DIM depth AS INTEGER
    DIM i AS INTEGER
    DIM count AS INTEGER
    DIM undoInfo AS INTEGER

    ' Use reasonable depth limit based on think time
    IF thinkTime < 1000 THEN
        depth = 4
    ELSEIF thinkTime < 5000 THEN
        depth = 6
    ELSEIF thinkTime < 15000 THEN
        depth = 8
    ELSE
        depth = 10
    END IF

    PRINT "Thinking..."
    move = Search(depth, thinkTime)

    IF move = 0 THEN
        ' No move found - try to find any legal move
        GenAllMoves()
        count = gMoveCount
        FOR i = 0 TO count - 1
            undoInfo = MakeMove(gMoves(i))
            IF IsInCheck() = 0 THEN
                UnmakeMove(gMoves(i), undoInfo)
                move = gMoves(i)
                EXIT FOR
            END IF
            UnmakeMove(gMoves(i), undoInfo)
        NEXT i
    END IF

    GetAIMove = move
END FUNCTION

' Check if the game is over
FUNCTION GetGameState() AS INTEGER
    DIM i AS INTEGER
    DIM count AS INTEGER
    DIM hasLegal AS INTEGER
    DIM inCheck AS INTEGER
    DIM undoInfo AS INTEGER

    ' Check for 50-move rule
    IF gHalfMoveClock >= 100 THEN
        GetGameState = STATE_DRAW_50
        EXIT FUNCTION
    END IF

    ' Check for repetition
    IF IsRepetition() <> 0 THEN
        GetGameState = STATE_DRAW_REPETITION
        EXIT FUNCTION
    END IF

    ' Check for insufficient material
    IF IsInsufficientMaterial() <> 0 THEN
        GetGameState = STATE_DRAW_MATERIAL
        EXIT FUNCTION
    END IF

    ' Generate moves and check for legal ones
    GenAllMoves()
    count = gMoveCount
    hasLegal = 0

    FOR i = 0 TO count - 1
        undoInfo = MakeMove(gMoves(i))
        IF IsInCheck() = 0 THEN
            hasLegal = 1
            UnmakeMove(gMoves(i), undoInfo)
            EXIT FOR
        END IF
        UnmakeMove(gMoves(i), undoInfo)
    NEXT i

    IF hasLegal = 0 THEN
        inCheck = IsInCheck()
        IF inCheck <> 0 THEN
            GetGameState = STATE_CHECKMATE
        ELSE
            GetGameState = STATE_STALEMATE
        END IF
        EXIT FUNCTION
    END IF

    GetGameState = STATE_PLAYING
END FUNCTION

' Check for insufficient mating material
FUNCTION IsInsufficientMaterial() AS INTEGER
    DIM whitePieces AS INTEGER
    DIM blackPieces AS INTEGER
    DIM whiteKnights AS INTEGER
    DIM blackKnights AS INTEGER
    DIM whiteBishops AS INTEGER
    DIM blackBishops AS INTEGER
    DIM sq AS INTEGER
    DIM piece AS INTEGER
    DIM clr AS INTEGER

    whitePieces = 0
    blackPieces = 0
    whiteKnights = 0
    blackKnights = 0
    whiteBishops = 0
    blackBishops = 0

    FOR sq = 0 TO 63
        piece = GetPiece(sq)
        IF piece <> EMPTY THEN
            IF piece <> KING THEN
                clr = GetColor(sq)
                IF clr = WHITE THEN
                    whitePieces = whitePieces + 1
                    IF piece = KNIGHT THEN whiteKnights = whiteKnights + 1
                    IF piece = BISHOP THEN whiteBishops = whiteBishops + 1
                ELSE
                    blackPieces = blackPieces + 1
                    IF piece = KNIGHT THEN blackKnights = blackKnights + 1
                    IF piece = BISHOP THEN blackBishops = blackBishops + 1
                END IF

                ' Any pawn, rook, or queen = sufficient material
                IF piece = PAWN THEN
                    IsInsufficientMaterial = 0
                    EXIT FUNCTION
                ELSEIF piece = ROOK THEN
                    IsInsufficientMaterial = 0
                    EXIT FUNCTION
                ELSEIF piece = QUEEN THEN
                    IsInsufficientMaterial = 0
                    EXIT FUNCTION
                END IF
            END IF
        END IF
    NEXT sq

    ' K vs K
    IF whitePieces = 0 THEN
        IF blackPieces = 0 THEN
            IsInsufficientMaterial = 1
            EXIT FUNCTION
        END IF
    END IF

    ' K+B vs K or K+N vs K
    IF whitePieces = 0 THEN
        IF blackPieces = 1 THEN
            IF blackKnights = 1 THEN
                IsInsufficientMaterial = 1
                EXIT FUNCTION
            ELSEIF blackBishops = 1 THEN
                IsInsufficientMaterial = 1
                EXIT FUNCTION
            END IF
        END IF
    END IF

    IF blackPieces = 0 THEN
        IF whitePieces = 1 THEN
            IF whiteKnights = 1 THEN
                IsInsufficientMaterial = 1
                EXIT FUNCTION
            ELSEIF whiteBishops = 1 THEN
                IsInsufficientMaterial = 1
                EXIT FUNCTION
            END IF
        END IF
    END IF

    ' K+N+N vs K (can't force checkmate)
    IF whitePieces = 0 THEN
        IF blackPieces = 2 THEN
            IF blackKnights = 2 THEN
                IsInsufficientMaterial = 1
                EXIT FUNCTION
            END IF
        END IF
    END IF

    IF blackPieces = 0 THEN
        IF whitePieces = 2 THEN
            IF whiteKnights = 2 THEN
                IsInsufficientMaterial = 1
                EXIT FUNCTION
            END IF
        END IF
    END IF

    IsInsufficientMaterial = 0
END FUNCTION

