'==============================================================================
' CHESS GAME LOOP
' Main game loop, user input handling, and game state management
'==============================================================================

' Game settings
DIM gPlayerColor AS INTEGER     ' Human plays this color
DIM gAIEnabled AS INTEGER       ' AI enabled flag
DIM gThinkTime AS INTEGER       ' AI think time in ms
DIM gShowCoords AS INTEGER      ' Show coordinates on board
DIM gUseColor AS INTEGER        ' Use ANSI colors

' Game state
DIM gGameOver AS INTEGER
DIM gGameResult AS INTEGER

' Undo stack (for undo command)
DIM gUndoMoves(511) AS INTEGER
DIM gUndoInfo(511) AS INTEGER
DIM gUndoCount AS INTEGER

' Initialize game settings
SUB InitGame()
    gPlayerColor = WHITE
    gAIEnabled = 1
    gThinkTime = 3000           ' 3 seconds default
    gShowCoords = 1
    gUseColor = 1
    gGameOver = 0
    gGameResult = STATE_PLAYING
    gUndoCount = 0
END SUB

' Display the game header
SUB DisplayHeader()
    DIM sb AS Viper.Text.StringBuilder
    sb = NEW Viper.Text.StringBuilder()

    sb.Append(CHR$(10))
    sb.Append("======================================")
    sb.Append(CHR$(10))
    sb.Append("       VIPER CHESS ENGINE v1.0")
    sb.Append(CHR$(10))
    sb.Append("======================================")
    sb.Append(CHR$(10))

    PRINT sb.ToString()
END SUB

' Display game status
SUB DisplayStatus()
    DIM sb AS Viper.Text.StringBuilder
    DIM sideStr AS STRING
    DIM checkStr AS STRING

    sb = NEW Viper.Text.StringBuilder()

    IF gSideToMove = WHITE THEN
        sideStr = "White"
    ELSE
        sideStr = "Black"
    END IF

    sb.Append(CHR$(10))
    sb.Append(sideStr)
    sb.Append(" to move")

    IF IsInCheck() <> 0 THEN
        sb.Append(" (CHECK!)")
    END IF

    sb.Append(CHR$(10))
    sb.Append("Move: ")
    sb.Append(STR$(gFullMoveNumber))
    sb.Append("  Half-moves: ")
    sb.Append(STR$(gHalfMoveClock))
    sb.Append(CHR$(10))

    PRINT sb.ToString()
END SUB

' Display help
SUB DisplayHelp()
    DIM sb AS Viper.Text.StringBuilder
    sb = NEW Viper.Text.StringBuilder()

    sb.Append(CHR$(10))
    sb.Append("Commands:")
    sb.Append(CHR$(10))
    sb.Append("  e2e4, Nf3  - Enter move in algebraic notation")
    sb.Append(CHR$(10))
    sb.Append("  new        - Start new game")
    sb.Append(CHR$(10))
    sb.Append("  flip       - Switch sides (play as other color)")
    sb.Append(CHR$(10))
    sb.Append("  fen        - Show FEN of current position")
    sb.Append(CHR$(10))
    sb.Append("  setfen X   - Set position from FEN string X")
    sb.Append(CHR$(10))
    sb.Append("  ai on/off  - Toggle AI opponent")
    sb.Append(CHR$(10))
    sb.Append("  time N     - Set AI think time to N seconds")
    sb.Append(CHR$(10))
    sb.Append("  go         - Make AI play current side")
    sb.Append(CHR$(10))
    sb.Append("  undo       - Undo last move")
    sb.Append(CHR$(10))
    sb.Append("  moves      - Show all legal moves")
    sb.Append(CHR$(10))
    sb.Append("  resign     - Resign the game")
    sb.Append(CHR$(10))
    sb.Append("  quit       - Exit program")
    sb.Append(CHR$(10))
    sb.Append("  help       - Show this help")
    sb.Append(CHR$(10))

    PRINT sb.ToString()
END SUB

' Parse user move input
FUNCTION ParseMoveInput(userInput AS STRING) AS INTEGER
    DIM move AS INTEGER
    DIM count AS INTEGER
    DIM i AS INTEGER
    DIM fromSq AS INTEGER
    DIM toSq AS INTEGER
    DIM piece AS INTEGER
    DIM promo AS INTEGER
    DIM inputUpper AS STRING
    DIM moveAlg AS STRING
    DIM inputLen AS INTEGER
    DIM undoInfo AS INTEGER

    move = 0
    inputUpper = UCASE$(TRIM$(userInput))
    inputLen = LEN(inputUpper)

    IF inputLen < 2 THEN
        ParseMoveInput = 0
        EXIT FUNCTION
    END IF

    ' Generate all moves
    GenAllMoves()
    count = gMoveCount

    ' Try coordinate notation first (e2e4, e2-e4, etc.)
    IF inputLen >= 4 THEN
        ' Extract from/to squares
        DIM fromStr AS STRING
        DIM toStr AS STRING
        DIM promoChar AS STRING

        ' Handle formats: e2e4, e2-e4, e2 e4
        IF MID$(inputUpper, 3, 1) = "-" THEN
            fromStr = LCASE$(LEFT$(inputUpper, 2))
            toStr = LCASE$(MID$(inputUpper, 4, 2))
            IF inputLen >= 6 THEN
                promoChar = LCASE$(MID$(inputUpper, 6, 1))
            ELSE
                promoChar = ""
            END IF
        ELSEIF MID$(inputUpper, 3, 1) = " " THEN
            fromStr = LCASE$(LEFT$(inputUpper, 2))
            toStr = LCASE$(MID$(inputUpper, 4, 2))
            IF inputLen >= 6 THEN
                promoChar = LCASE$(MID$(inputUpper, 6, 1))
            ELSE
                promoChar = ""
            END IF
        ELSE
            fromStr = LCASE$(LEFT$(inputUpper, 2))
            toStr = LCASE$(MID$(inputUpper, 3, 2))
            IF inputLen >= 5 THEN
                promoChar = LCASE$(MID$(inputUpper, 5, 1))
            ELSE
                promoChar = ""
            END IF
        END IF

        fromSq = AlgToSquare(fromStr)
        toSq = AlgToSquare(toStr)

        IF fromSq >= 0 THEN
            IF toSq >= 0 THEN
                ' Find matching move
                FOR i = 0 TO count - 1
                    IF MoveFrom(gMoves(i)) = fromSq THEN
                        IF MoveTo(gMoves(i)) = toSq THEN
                            ' Check promotion
                            IF MoveFlags(gMoves(i)) = MOVE_PROMOTION THEN
                                promo = MovePromo(gMoves(i))
                                IF promoChar = "q" THEN
                                    IF promo = QUEEN THEN move = gMoves(i)
                                ELSEIF promoChar = "r" THEN
                                    IF promo = ROOK THEN move = gMoves(i)
                                ELSEIF promoChar = "b" THEN
                                    IF promo = BISHOP THEN move = gMoves(i)
                                ELSEIF promoChar = "n" THEN
                                    IF promo = KNIGHT THEN move = gMoves(i)
                                ELSEIF promoChar = "" THEN
                                    IF promo = QUEEN THEN move = gMoves(i)  ' Default to queen
                                END IF
                            ELSE
                                move = gMoves(i)
                            END IF

                            ' Verify move is legal
                            IF move <> 0 THEN
                                undoInfo = MakeMove(move)
                                IF IsInCheck() <> 0 THEN
                                    UnmakeMove(move, undoInfo)
                                    move = 0
                                ELSE
                                    UnmakeMove(move, undoInfo)
                                    EXIT FOR
                                END IF
                            END IF
                        END IF
                    END IF
                NEXT i
            END IF
        END IF
    END IF

    ParseMoveInput = move
END FUNCTION

' Convert move to Standard Algebraic Notation
FUNCTION MoveToSAN(move AS INTEGER) AS STRING
    DIM fromSq AS INTEGER
    DIM toSq AS INTEGER
    DIM piece AS INTEGER
    DIM captured AS INTEGER
    DIM flag AS INTEGER
    DIM promo AS INTEGER
    DIM sb AS Viper.Text.StringBuilder
    DIM ambigFile AS INTEGER
    DIM ambigRank AS INTEGER
    DIM count AS INTEGER
    DIM i AS INTEGER
    DIM otherFrom AS INTEGER
    DIM undoInfo AS INTEGER
    DIM legalCount AS INTEGER
    DIM j AS INTEGER
    DIM testUndoInfo AS INTEGER

    sb = NEW Viper.Text.StringBuilder()

    fromSq = MoveFrom(move)
    toSq = MoveTo(move)
    piece = GetPiece(fromSq)
    flag = MoveFlags(move)

    ' Castling
    IF flag = MOVE_CASTLE_KS THEN
        MoveToSAN = "O-O"
        EXIT FUNCTION
    ELSEIF flag = MOVE_CASTLE_QS THEN
        MoveToSAN = "O-O-O"
        EXIT FUNCTION
    END IF

    ' Piece letter (except pawns)
    IF piece <> PAWN THEN
        sb.Append(PieceChar(piece, WHITE))

        ' Check for ambiguity
        GenAllMoves()
        count = gMoveCount
        ambigFile = 0
        ambigRank = 0
        FOR i = 0 TO count - 1
            otherFrom = MoveFrom(gMoves(i))
            IF otherFrom <> fromSq THEN
                IF MoveTo(gMoves(i)) = toSq THEN
                    IF GetPiece(otherFrom) = piece THEN
                        IF FILE_OF(otherFrom) = FILE_OF(fromSq) THEN
                            ambigRank = 1
                        ELSEIF RANK_OF(otherFrom) = RANK_OF(fromSq) THEN
                            ambigFile = 1
                        ELSE
                            ambigFile = 1
                        END IF
                    END IF
                END IF
            END IF
        NEXT i

        IF ambigFile <> 0 THEN
            sb.Append(CHR$(97 + FILE_OF(fromSq)))
        END IF
        IF ambigRank <> 0 THEN
            sb.Append(CHR$(49 + RANK_OF(fromSq)))
        END IF
    END IF

    ' Capture
    captured = GetPiece(toSq)
    IF captured <> EMPTY THEN
        IF piece = PAWN THEN
            sb.Append(CHR$(97 + FILE_OF(fromSq)))
        END IF
        sb.Append("x")
    ELSEIF flag = MOVE_EP THEN
        IF piece = PAWN THEN
            sb.Append(CHR$(97 + FILE_OF(fromSq)))
        END IF
        sb.Append("x")
    END IF

    ' Destination square
    sb.Append(SquareToAlg(toSq))

    ' Promotion
    IF flag = MOVE_PROMOTION THEN
        promo = MovePromo(move)
        sb.Append("=")
        sb.Append(PieceChar(promo, WHITE))
    END IF

    ' Check for check/checkmate
    undoInfo = MakeMove(move)
    IF IsInCheck() <> 0 THEN
        ' Check if it's checkmate
        GenAllMoves()
        legalCount = 0
        FOR j = 0 TO gMoveCount - 1
            testUndoInfo = MakeMove(gMoves(j))
            IF IsInCheck() = 0 THEN
                legalCount = legalCount + 1
            END IF
            UnmakeMove(gMoves(j), testUndoInfo)
        NEXT j

        IF legalCount = 0 THEN
            sb.Append("#")
        ELSE
            sb.Append("+")
        END IF
    END IF
    UnmakeMove(move, undoInfo)

    MoveToSAN = sb.ToString()
END FUNCTION

' Show all legal moves
SUB ShowLegalMoves()
    DIM count AS INTEGER
    DIM legalCount AS INTEGER
    DIM i AS INTEGER
    DIM sb AS Viper.Text.StringBuilder
    DIM undoInfo AS INTEGER

    sb = NEW Viper.Text.StringBuilder()

    GenAllMoves()
    count = gMoveCount
    legalCount = 0

    sb.Append(CHR$(10))
    sb.Append("Legal moves: ")

    FOR i = 0 TO count - 1
        undoInfo = MakeMove(gMoves(i))
        IF IsInCheck() = 0 THEN
            IF legalCount > 0 THEN
                sb.Append(", ")
            END IF
            UnmakeMove(gMoves(i), undoInfo)
            sb.Append(MoveToAlg(gMoves(i)))
            legalCount = legalCount + 1
        ELSE
            UnmakeMove(gMoves(i), undoInfo)
        END IF
    NEXT i

    sb.Append(CHR$(10))
    sb.Append("Total: ")
    sb.Append(STR$(legalCount))
    sb.Append(" moves")
    sb.Append(CHR$(10))

    PRINT sb.ToString()
END SUB

' Display game over message
SUB DisplayGameOver()
    DIM sb AS Viper.Text.StringBuilder
    sb = NEW Viper.Text.StringBuilder()

    sb.Append(CHR$(10))
    sb.Append("========== GAME OVER ==========")
    sb.Append(CHR$(10))

    IF gGameResult = STATE_CHECKMATE THEN
        IF gSideToMove = WHITE THEN
            sb.Append("Black wins by checkmate!")
        ELSE
            sb.Append("White wins by checkmate!")
        END IF
    ELSEIF gGameResult = STATE_STALEMATE THEN
        sb.Append("Draw by stalemate.")
    ELSEIF gGameResult = STATE_DRAW_50 THEN
        sb.Append("Draw by 50-move rule.")
    ELSEIF gGameResult = STATE_DRAW_REPETITION THEN
        sb.Append("Draw by threefold repetition.")
    ELSEIF gGameResult = STATE_DRAW_MATERIAL THEN
        sb.Append("Draw by insufficient material.")
    ELSEIF gGameResult = STATE_RESIGNED THEN
        IF gSideToMove = WHITE THEN
            sb.Append("White resigns. Black wins!")
        ELSE
            sb.Append("Black resigns. White wins!")
        END IF
    END IF

    sb.Append(CHR$(10))
    sb.Append("===============================")
    sb.Append(CHR$(10))

    PRINT sb.ToString()
END SUB

' Store move for undo
SUB StoreUndo(move AS INTEGER, undoInfo AS INTEGER)
    IF gUndoCount < 512 THEN
        gUndoMoves(gUndoCount) = move
        gUndoInfo(gUndoCount) = undoInfo
        gUndoCount = gUndoCount + 1
    END IF
END SUB

' Make AI move
SUB DoAIMove()
    DIM move AS INTEGER
    DIM moveStr AS STRING
    DIM undoInfo AS INTEGER

    IF gGameOver <> 0 THEN EXIT SUB

    move = GetAIMove(gThinkTime)

    IF move <> 0 THEN
        moveStr = MoveToAlg(move)
        PRINT "AI plays: " + moveStr

        undoInfo = MakeMove(move)
        StoreUndo(move, undoInfo)
        StorePosition()

        ' Check game state
        gGameResult = GetGameState()
        IF gGameResult <> STATE_PLAYING THEN
            gGameOver = 1
        END IF
    ELSE
        PRINT "AI has no legal moves!"
        gGameOver = 1
    END IF
END SUB

' Process user command
FUNCTION ProcessCommand(cmd AS STRING) AS INTEGER
    DIM cmdUpper AS STRING
    DIM parts AS STRING
    DIM move AS INTEGER
    DIM fenStr AS STRING
    DIM undoInfo AS INTEGER
    DIM timeVal AS INTEGER
    DIM lastMove AS INTEGER
    DIM lastUndo AS INTEGER

    cmdUpper = UCASE$(TRIM$(cmd))

    IF cmdUpper = "" THEN
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' Quit
    IF cmdUpper = "QUIT" THEN
        ProcessCommand = 0
        EXIT FUNCTION
    ELSEIF cmdUpper = "EXIT" THEN
        ProcessCommand = 0
        EXIT FUNCTION
    ELSEIF cmdUpper = "Q" THEN
        ProcessCommand = 0
        EXIT FUNCTION
    END IF

    ' Help
    IF cmdUpper = "HELP" THEN
        DisplayHelp()
        ProcessCommand = 1
        EXIT FUNCTION
    ELSEIF cmdUpper = "?" THEN
        DisplayHelp()
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' New game
    IF cmdUpper = "NEW" THEN
        InitBoard()
        InitSearch()
        gGameOver = 0
        gGameResult = STATE_PLAYING
        gUndoCount = 0
        PRINT "New game started."
        DisplayBoard()
        DisplayStatus()
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' Flip sides
    IF cmdUpper = "FLIP" THEN
        IF gPlayerColor = WHITE THEN
            gPlayerColor = BLACK
            PRINT "You are now playing Black."
        ELSE
            gPlayerColor = WHITE
            PRINT "You are now playing White."
        END IF
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' Show FEN
    IF cmdUpper = "FEN" THEN
        fenStr = GetFEN()
        PRINT "FEN: " + fenStr
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' Set FEN
    IF LEFT$(cmdUpper, 7) = "SETFEN " THEN
        fenStr = MID$(cmd, 8)
        SetFEN(fenStr)
        gGameOver = 0
        gGameResult = STATE_PLAYING
        gUndoCount = 0
        PRINT "Position set."
        DisplayBoard()
        DisplayStatus()
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' Toggle AI
    IF cmdUpper = "AI ON" THEN
        gAIEnabled = 1
        PRINT "AI enabled."
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    IF cmdUpper = "AI OFF" THEN
        gAIEnabled = 0
        PRINT "AI disabled."
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' Set think time
    IF LEFT$(cmdUpper, 5) = "TIME " THEN
        timeVal = VAL(MID$(cmd, 6))
        IF timeVal > 0 THEN
            gThinkTime = timeVal * 1000
            PRINT "Think time set to " + STR$(timeVal) + " seconds."
        ELSE
            PRINT "Invalid time value."
        END IF
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' Force AI move
    IF cmdUpper = "GO" THEN
        DoAIMove()
        IF gGameOver <> 0 THEN
            DisplayBoard()
            DisplayGameOver()
        ELSE
            DisplayBoard()
            DisplayStatus()
        END IF
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' Show legal moves
    IF cmdUpper = "MOVES" THEN
        ShowLegalMoves()
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' Resign
    IF cmdUpper = "RESIGN" THEN
        gGameResult = STATE_RESIGNED
        gGameOver = 1
        DisplayGameOver()
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' Undo
    IF cmdUpper = "UNDO" THEN
        IF gUndoCount > 0 THEN
            gUndoCount = gUndoCount - 1
            lastMove = gUndoMoves(gUndoCount)
            lastUndo = gUndoInfo(gUndoCount)
            UnmakeMove(lastMove, lastUndo)
            gPosHistoryCount = gPosHistoryCount - 1

            IF gAIEnabled <> 0 THEN
                IF gUndoCount > 0 THEN
                    ' Undo AI move too
                    gUndoCount = gUndoCount - 1
                    lastMove = gUndoMoves(gUndoCount)
                    lastUndo = gUndoInfo(gUndoCount)
                    UnmakeMove(lastMove, lastUndo)
                    gPosHistoryCount = gPosHistoryCount - 1
                END IF
            END IF

            gGameOver = 0
            gGameResult = STATE_PLAYING
            PRINT "Move undone."
            DisplayBoard()
            DisplayStatus()
        ELSE
            PRINT "Nothing to undo."
        END IF
        ProcessCommand = 1
        EXIT FUNCTION
    END IF

    ' Try to parse as a move
    IF gGameOver = 0 THEN
        move = ParseMoveInput(cmd)
        IF move <> 0 THEN
            undoInfo = MakeMove(move)
            StoreUndo(move, undoInfo)
            StorePosition()

            ' Check game state
            gGameResult = GetGameState()
            IF gGameResult <> STATE_PLAYING THEN
                gGameOver = 1
                DisplayBoard()
                DisplayGameOver()
            ELSE
                DisplayBoard()
                DisplayStatus()

                ' AI's turn
                IF gAIEnabled <> 0 THEN
                    IF gSideToMove <> gPlayerColor THEN
                        DoAIMove()
                        IF gGameOver <> 0 THEN
                            DisplayBoard()
                            DisplayGameOver()
                        ELSE
                            DisplayBoard()
                            DisplayStatus()
                        END IF
                    END IF
                END IF
            END IF

            ProcessCommand = 1
            EXIT FUNCTION
        ELSE
            PRINT "Invalid move or command. Type 'help' for help."
            ProcessCommand = 1
            EXIT FUNCTION
        END IF
    ELSE
        PRINT "Game is over. Type 'new' to start a new game."
        ProcessCommand = 1
        EXIT FUNCTION
    END IF
END FUNCTION

' Main game loop
SUB GameLoop()
    DIM running AS INTEGER
    DIM userCmd AS STRING

    running = 1

    DisplayHeader()
    DisplayBoard()
    DisplayStatus()

    ' If AI plays white and it's white's turn, make AI move
    IF gAIEnabled <> 0 THEN
        IF gPlayerColor = BLACK THEN
            IF gSideToMove = WHITE THEN
                DoAIMove()
                DisplayBoard()
                DisplayStatus()
            END IF
        END IF
    END IF

    DO WHILE running <> 0
        PRINT "> ";
        INPUT userCmd
        running = ProcessCommand(userCmd)
    LOOP

    PRINT "Thanks for playing!"
END SUB

