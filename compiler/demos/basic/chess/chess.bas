' ╔══════════════════════════════════════════════════════════╗
' ║  VIPER CHESS - A Complete Chess Game Demo               ║
' ║  Features: ANSI Graphics, AI Opponent, Full Rules       ║
' ╚══════════════════════════════════════════════════════════╝

AddFile "pieces.bas"
AddFile "board.bas"
AddFile "moves.bas"
AddFile "ai.bas"

' === GLOBAL VARIABLES ===
DIM GameBoard AS Board
DIM Validator AS MoveValidator
DIM Computer AS ChessAI
DIM WhiteTurn AS INTEGER
DIM GameOver AS INTEGER
DIM PlayerIsWhite AS INTEGER
DIM AIDepth AS INTEGER
DIM SelectedRow AS INTEGER
DIM SelectedCol AS INTEGER
DIM InputMode AS INTEGER  ' 0=select piece, 1=select destination
DIM WatchMode AS INTEGER  ' 1 if AI vs AI mode

' === MAIN MENU ===
SUB ShowMainMenu()
    DIM choice AS STRING
    DIM i AS INTEGER

    COLOR CLR_WHITE, CLR_BLACK
    CLS

    ' Draw fancy border
    COLOR CLR_BRIGHT_CYAN, CLR_BLACK
    PRINT
    PRINT "              "; CHR(201);
    FOR i = 1 TO 30
        PRINT CHR(205);
    NEXT i
    PRINT CHR(187)

    PRINT "              "; CHR(186); "                              "; CHR(186)

    PRINT "              "; CHR(186); "         ";
    COLOR CLR_BRIGHT_WHITE, CLR_BLACK
    PRINT "VIPER CHESS";
    COLOR CLR_BRIGHT_CYAN, CLR_BLACK
    PRINT "          "; CHR(186)

    PRINT "              "; CHR(186); "                              "; CHR(186)

    PRINT "              "; CHR(186); "   ";
    COLOR CLR_BRIGHT_WHITE, CLR_BLACK
    PRINT "K Q R B N P";
    COLOR CLR_GRAY, CLR_BLACK
    PRINT "  ";
    PRINT "k q r b n p";
    COLOR CLR_BRIGHT_CYAN, CLR_BLACK
    PRINT "   "; CHR(186)

    PRINT "              "; CHR(186); "                              "; CHR(186)

    PRINT "              "; CHR(200);
    FOR i = 1 TO 30
        PRINT CHR(205);
    NEXT i
    PRINT CHR(188)
    PRINT

    ' Menu options
    COLOR CLR_BRIGHT_GREEN, CLR_BLACK
    PRINT "                   [1]";
    COLOR CLR_WHITE, CLR_BLACK
    PRINT " Play as White"
    PRINT

    COLOR CLR_BRIGHT_GREEN, CLR_BLACK
    PRINT "                   [2]";
    COLOR CLR_WHITE, CLR_BLACK
    PRINT " Play as Black"
    PRINT

    COLOR CLR_BRIGHT_GREEN, CLR_BLACK
    PRINT "                   [3]";
    COLOR CLR_WHITE, CLR_BLACK
    PRINT " Two Players"
    PRINT

    COLOR CLR_BRIGHT_GREEN, CLR_BLACK
    PRINT "                   [4]";
    COLOR CLR_WHITE, CLR_BLACK
    PRINT " Watch AI vs AI"
    PRINT

    COLOR CLR_BRIGHT_GREEN, CLR_BLACK
    PRINT "                   [5]";
    COLOR CLR_WHITE, CLR_BLACK
    PRINT " Set AI Difficulty"
    PRINT

    COLOR CLR_BRIGHT_GREEN, CLR_BLACK
    PRINT "                   [Q]";
    COLOR CLR_WHITE, CLR_BLACK
    PRINT " Quit"
    PRINT

    ' Show current difficulty
    COLOR CLR_GRAY, CLR_BLACK
    PRINT "              AI Difficulty: ";
    IF AIDepth = 1 THEN PRINT "Easy (depth 1)"
    IF AIDepth = 2 THEN PRINT "Medium (depth 2)"
    IF AIDepth = 3 THEN PRINT "Hard (depth 3)"
    IF AIDepth = 4 THEN PRINT "Expert (depth 4)"
    PRINT

    COLOR CLR_BRIGHT_WHITE, CLR_BLACK
    PRINT "              Enter choice: ";

    INPUT choice

    IF choice = "1" THEN
        PlayerIsWhite = 1
        StartGame(1, 0)
    ELSEIF choice = "2" THEN
        PlayerIsWhite = 0
        StartGame(1, 0)
    ELSEIF choice = "3" THEN
        StartGame(0, 0)
    ELSEIF choice = "4" THEN
        StartGame(0, 1)
    ELSEIF choice = "5" THEN
        SetDifficulty()
    ELSEIF choice = "q" OR choice = "Q" THEN
        COLOR CLR_WHITE, CLR_BLACK
        CLS
        PRINT
        PRINT
        PRINT "                         Thanks for playing!"
        PRINT
        PRINT "                      Viper BASIC Chess Demo"
        PRINT
    ELSE
        ShowMainMenu()
    END IF
END SUB

' === SET DIFFICULTY ===
SUB SetDifficulty()
    DIM choice AS STRING

    COLOR CLR_WHITE, CLR_BLACK
    CLS

    PRINT
    COLOR CLR_BRIGHT_CYAN, CLR_BLACK
    PRINT "                   === AI DIFFICULTY ==="
    PRINT
    PRINT

    COLOR CLR_BRIGHT_GREEN, CLR_BLACK
    PRINT "              [1]";
    COLOR CLR_WHITE, CLR_BLACK
    PRINT " Easy   - Quick moves, weak play"
    PRINT

    COLOR CLR_BRIGHT_YELLOW, CLR_BLACK
    PRINT "              [2]";
    COLOR CLR_WHITE, CLR_BLACK
    PRINT " Medium - Balanced (recommended)"
    PRINT

    COLOR CLR_BRIGHT_RED, CLR_BLACK
    PRINT "              [3]";
    COLOR CLR_WHITE, CLR_BLACK
    PRINT " Hard   - Thinks ahead more"
    PRINT

    COLOR CLR_BRIGHT_MAGENTA, CLR_BLACK
    PRINT "              [4]";
    COLOR CLR_WHITE, CLR_BLACK
    PRINT " Expert - May take a while..."
    PRINT
    PRINT

    PRINT "              Current: ";
    COLOR CLR_BRIGHT_YELLOW, CLR_BLACK
    PRINT "Depth"; STR$(AIDepth)
    PRINT

    COLOR CLR_WHITE, CLR_BLACK
    PRINT "              Enter choice (1-4): ";
    INPUT choice

    IF choice = "1" THEN AIDepth = 1
    IF choice = "2" THEN AIDepth = 2
    IF choice = "3" THEN AIDepth = 3
    IF choice = "4" THEN AIDepth = 4

    ShowMainMenu()
END SUB

' === START GAME ===
SUB StartGame(vsComputer AS INTEGER, aiVsAi AS INTEGER)
    GameBoard = NEW Board()
    Validator = NEW MoveValidator()
    Computer = NEW ChessAI()
    WhiteTurn = 1
    GameOver = 0
    SelectedRow = -1
    SelectedCol = -1
    InputMode = 0

    GameLoop(vsComputer, aiVsAi)
END SUB

' === GAME LOOP ===
SUB GameLoop(vsComputer AS INTEGER, aiVsAi AS INTEGER)
    DIM moveStr AS STRING
    DIM fromCol AS INTEGER
    DIM fromRow AS INTEGER
    DIM toCol AS INTEGER
    DIM toRow AS INTEGER
    DIM isComputerTurn AS INTEGER
    DIM c1 AS STRING
    DIM c2 AS STRING
    DIM c3 AS STRING
    DIM c4 AS STRING
    DIM keyPress AS STRING

    DO WHILE GameOver = 0
        ' Draw the board
        GameBoard.Draw(SelectedRow, SelectedCol)

        ' Show game status
        ShowStatus()

        ' Check for checkmate/stalemate
        IF Validator.HasLegalMoves(GameBoard, WhiteTurn) = 0 THEN
            IF Validator.IsInCheck(GameBoard, WhiteTurn) = 1 THEN
                ShowGameOver(1)  ' Checkmate
            ELSE
                ShowGameOver(0)  ' Stalemate
            END IF
            GameOver = 1
            EXIT DO
        END IF

        ' AI vs AI mode
        IF aiVsAi = 1 THEN
            LOCATE 24, 1
            COLOR CLR_BRIGHT_YELLOW, CLR_BLACK
            IF WhiteTurn = 1 THEN
                PRINT "White (AI) thinking...                              ";
            ELSE
                PRINT "Black (AI) thinking...                              ";
            END IF
            DoComputerMove()
            Sleep 1000
            ' Check for key press to exit
            keyPress = INKEY$()
            IF keyPress = "q" OR keyPress = "Q" OR keyPress = CHR$(27) THEN
                GameOver = 1
                ShowMainMenu()
                EXIT SUB
            END IF
        ELSE
            ' Determine if it's computer's turn
            isComputerTurn = 0
            IF vsComputer = 1 THEN
                IF WhiteTurn = 1 AND PlayerIsWhite = 0 THEN isComputerTurn = 1
                IF WhiteTurn = 0 AND PlayerIsWhite = 1 THEN isComputerTurn = 1
            END IF

            IF isComputerTurn = 1 THEN
                ' Computer's turn
                DoComputerMove()
            ELSE
            ' Human's turn - get input
            LOCATE 24, 1
            COLOR CLR_BRIGHT_WHITE, CLR_BLACK
            IF WhiteTurn = 1 THEN
                PRINT "White's move";
            ELSE
                PRINT "Black's move";
            END IF
            PRINT " (e.g. e2e4, or 'q' to quit): ";

            INPUT moveStr

            IF moveStr = "q" OR moveStr = "Q" THEN
                GameOver = 1
                ShowMainMenu()
                EXIT SUB
            END IF

            IF moveStr = "m" OR moveStr = "M" THEN
                ShowMainMenu()
                EXIT SUB
            END IF

            ' Parse the move
            IF LEN(moveStr) >= 4 THEN
                c1 = MID$(moveStr, 1, 1)
                c2 = MID$(moveStr, 2, 1)
                c3 = MID$(moveStr, 3, 1)
                c4 = MID$(moveStr, 4, 1)

                fromCol = ASC(c1) - 97
                fromRow = ASC(c2) - 49
                toCol = ASC(c3) - 97
                toRow = ASC(c4) - 49

                ' Handle uppercase
                IF fromCol < 0 OR fromCol > 7 THEN
                    fromCol = ASC(c1) - 65
                END IF
                IF toCol < 0 OR toCol > 7 THEN
                    toCol = ASC(c3) - 65
                END IF

                ' Validate and make move
                IF Validator.IsLegalMove(GameBoard, fromRow, fromCol, toRow, toCol, WhiteTurn) = 1 THEN
                    GameBoard.MakeMove(fromRow, fromCol, toRow, toCol)
                    WhiteTurn = 1 - WhiteTurn
                ELSE
                    LOCATE 25, 1
                    COLOR CLR_BRIGHT_RED, CLR_BLACK
                    PRINT "Invalid move! Press Enter...";
                    INPUT moveStr
                END IF
            ELSE
                LOCATE 25, 1
                COLOR CLR_BRIGHT_RED, CLR_BLACK
                PRINT "Enter move as: e2e4  Press Enter...";
                INPUT moveStr
            END IF
            END IF
        END IF
    LOOP

    ' Return to menu after game
    LOCATE 25, 1
    COLOR CLR_BRIGHT_WHITE, CLR_BLACK
    PRINT "Press Enter to return to menu...";
    INPUT moveStr
    ShowMainMenu()
END SUB

' === SHOW STATUS ===
SUB ShowStatus()
    LOCATE 5, 45
    COLOR CLR_BRIGHT_WHITE, CLR_BLACK
    IF WhiteTurn = 1 THEN
        PRINT "Turn: WHITE"
    ELSE
        PRINT "Turn: BLACK"
    END IF

    ' Show if in check
    IF Validator.IsInCheck(GameBoard, WhiteTurn) = 1 THEN
        LOCATE 7, 45
        COLOR CLR_BRIGHT_RED, CLR_BLACK
        PRINT "*** CHECK! ***"
    END IF

    ' Instructions
    LOCATE 10, 45
    COLOR CLR_GRAY, CLR_BLACK
    PRINT "Commands:"
    LOCATE 11, 45
    PRINT "  e2e4 - Move piece"
    LOCATE 12, 45
    PRINT "  m    - Main menu"
    LOCATE 13, 45
    PRINT "  q    - Quit game"

    ' Castling info
    LOCATE 16, 45
    COLOR CLR_GRAY, CLR_BLACK
    PRINT "Castling: e1g1 or e1c1"
END SUB

' === COMPUTER MOVE ===
SUB DoComputerMove()
    ' Note: Using short var names to avoid shadowing bug with class member names
    DIM fr AS INTEGER
    DIM fc AS INTEGER
    DIM tr AS INTEGER
    DIM tc AS INTEGER

    LOCATE 24, 1
    COLOR CLR_BRIGHT_YELLOW, CLR_BLACK
    PRINT "Computer thinking...";

    Computer.FindBestMove(GameBoard, WhiteTurn, AIDepth)

    fr = Computer.GetBestFromRow()
    fc = Computer.GetBestFromCol()
    tr = Computer.GetBestToRow()
    tc = Computer.GetBestToCol()

    IF fr >= 0 THEN
        GameBoard.MakeMove(fr, fc, tr, tc)
        WhiteTurn = 1 - WhiteTurn

        ' Show what the computer played
        LOCATE 24, 1
        COLOR CLR_BRIGHT_CYAN, CLR_BLACK
        PRINT "Computer played: ";
        PRINT CHR(97 + fc); STR$(fr + 1);
        PRINT CHR(97 + tc); STR$(tr + 1);
        PRINT "  ("; STR$(Computer.GetNodesSearched()); " nodes)    "
    ELSE
        ' No legal moves (shouldn't happen if game isn't over)
        GameOver = 1
    END IF
END SUB

' === GAME OVER ===
SUB ShowGameOver(isCheckmate AS INTEGER)
    LOCATE 24, 1
    COLOR CLR_BRIGHT_WHITE, CLR_BLACK

    IF isCheckmate = 1 THEN
        COLOR CLR_BRIGHT_RED, CLR_BLACK
        PRINT "CHECKMATE! ";
        IF WhiteTurn = 1 THEN
            PRINT "Black wins!"
        ELSE
            PRINT "White wins!"
        END IF
    ELSE
        COLOR CLR_BRIGHT_YELLOW, CLR_BLACK
        PRINT "STALEMATE! The game is a draw."
    END IF
END SUB

' === MAIN PROGRAM ===
AIDepth = 2  ' Default medium difficulty

ShowMainMenu()
