' ================================================================
'                          OTHELLO
'              Complete OOP Othello/Reversi Game
' ================================================================
'
' Features:
' - 8x8 board with OOP classes
' - Move validation in all 8 directions
' - Piece flipping
' - Score tracking
' - Valid move detection
' - Game over detection
' - AI opponent (simple strategy)
'
' Rules:
' - Black moves first
' - Must flip opponent pieces
' - Pass if no valid moves
' - Game ends when both pass or board full
'
' ================================================================

' ===== CLASSES =====

CLASS GameState
    DIM currentPlayer AS INTEGER
    DIM blackCount AS INTEGER
    DIM whiteCount AS INTEGER
    DIM moveCount AS INTEGER
    DIM gameOver AS INTEGER
    DIM blackPasses AS INTEGER
    DIM whitePasses AS INTEGER
END CLASS

CLASS MoveInfo
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM player AS INTEGER
    DIM totalFlips AS INTEGER
END CLASS

' ===== GLOBAL DATA =====

DIM board(64) AS INTEGER  ' 0=empty, 1=black, 2=white
DIM state AS GameState
DIM directions(8) AS INTEGER  ' Row deltas: -1,-1,-1,0,0,1,1,1
DIM dirCols(8) AS INTEGER     ' Col deltas: -1,0,1,-1,1,-1,0,1

' ===== HELPER FUNCTIONS =====

SUB InitializeBoard()
    DIM i AS INTEGER

    ' Clear board
    FOR i = 0 TO 63
        board(i) = 0
    NEXT i

    ' Set starting position
    board(27) = 2  ' White at (3,3)
    board(28) = 1  ' Black at (3,4)
    board(35) = 1  ' Black at (4,3)
    board(36) = 2  ' White at (4,4)
END SUB

SUB InitializeDirections()
    ' 8 directions: NW, N, NE, W, E, SW, S, SE
    ' Row deltas
    directions(0) = -1  ' NW
    directions(1) = -1  ' N
    directions(2) = -1  ' NE
    directions(3) = 0   ' W
    directions(4) = 0   ' E
    directions(5) = 1   ' SW
    directions(6) = 1   ' S
    directions(7) = 1   ' SE

    ' Col deltas
    dirCols(0) = -1  ' NW
    dirCols(1) = 0   ' N
    dirCols(2) = 1   ' NE
    dirCols(3) = -1  ' W
    dirCols(4) = 1   ' E
    dirCols(5) = -1  ' SW
    dirCols(6) = 0   ' S
    dirCols(7) = 1   ' SE
END SUB

SUB DisplayBoard()
    DIM row AS INTEGER
    DIM col AS INTEGER

    PRINT ""
    PRINT "  0 1 2 3 4 5 6 7"
    FOR row = 0 TO 7
        PRINT row; " ";
        FOR col = 0 TO 7
            DIM index AS INTEGER
            index = row * 8 + col
            DIM piece AS INTEGER
            piece = board(index)

            SELECT CASE piece
                CASE 0
                    PRINT ". ";
                CASE 1
                    PRINT "B ";
                CASE 2
                    PRINT "W ";
            END SELECT
        NEXT col
        PRINT ""
    NEXT row
    PRINT ""
END SUB

FUNCTION GetOpponent(player AS INTEGER) AS INTEGER
    IF player = 1 THEN
        RETURN 2
    ELSE
        RETURN 1
    END IF
END FUNCTION

FUNCTION CheckDirection(row AS INTEGER, col AS INTEGER, player AS INTEGER, dRow AS INTEGER, dCol AS INTEGER) AS INTEGER
    DIM checkRow AS INTEGER
    DIM checkCol AS INTEGER
    DIM opponent AS INTEGER
    DIM foundOpponent AS INTEGER
    DIM steps AS INTEGER

    opponent = GetOpponent(player)
    checkRow = row + dRow
    checkCol = col + dCol
    foundOpponent = 0
    steps = 0

    ' Walk in direction
    WHILE checkRow >= 0
        IF checkRow > 7 THEN
            checkRow = -99  ' Break
        ELSE
            IF checkCol < 0 THEN
                checkRow = -99  ' Break
            ELSE
                IF checkCol > 7 THEN
                    checkRow = -99  ' Break
                ELSE
                    DIM index AS INTEGER
                    index = checkRow * 8 + checkCol
                    DIM piece AS INTEGER
                    piece = board(index)

                    SELECT CASE piece
                        CASE 0
                            ' Empty - stop
                            RETURN 0
                        CASE ELSE
                            IF piece = player THEN
                                ' Found own piece
                                IF foundOpponent = 1 THEN
                                    RETURN steps
                                ELSE
                                    RETURN 0
                                END IF
                            ELSE
                                ' Found opponent
                                foundOpponent = 1
                                steps = steps + 1
                                checkRow = checkRow + dRow
                                checkCol = checkCol + dCol
                            END IF
                    END SELECT
                END IF
            END IF
        END IF
    WEND

    RETURN 0
END FUNCTION

FUNCTION IsValidMove(row AS INTEGER, col AS INTEGER, player AS INTEGER) AS INTEGER
    DIM index AS INTEGER
    DIM i AS INTEGER
    DIM totalFlips AS INTEGER

    ' Check if square is empty
    index = row * 8 + col
    IF board(index) <> 0 THEN
        RETURN 0
    END IF

    ' Check all 8 directions
    totalFlips = 0
    FOR i = 0 TO 7
        DIM flips AS INTEGER
        DIM dr AS INTEGER
        DIM dc AS INTEGER

        dr = directions(i)
        dc = dirCols(i)
        flips = CheckDirection(row, col, player, dr, dc)
        totalFlips = totalFlips + flips
    NEXT i

    IF totalFlips > 0 THEN
        RETURN 1
    ELSE
        RETURN 0
    END IF
END FUNCTION

SUB MakeMove(row AS INTEGER, col AS INTEGER, player AS INTEGER)
    DIM index AS INTEGER
    DIM i AS INTEGER

    ' Place piece
    index = row * 8 + col
    board(index) = player

    ' Flip pieces in all valid directions
    FOR i = 0 TO 7
        DIM flips AS INTEGER
        DIM dr AS INTEGER
        DIM dc AS INTEGER

        dr = directions(i)
        dc = dirCols(i)
        flips = CheckDirection(row, col, player, dr, dc)

        ' Flip the pieces
        IF flips > 0 THEN
            DIM flipNum AS INTEGER
            FOR flipNum = 1 TO flips
                DIM flipRow AS INTEGER
                DIM flipCol AS INTEGER
                DIM flipIndex AS INTEGER

                flipRow = row + (dr * flipNum)
                flipCol = col + (dc * flipNum)
                flipIndex = flipRow * 8 + flipCol
                board(flipIndex) = player
            NEXT flipNum
        END IF
    NEXT i
END SUB

SUB CountPieces()
    DIM i AS INTEGER

    state.blackCount = 0
    state.whiteCount = 0

    FOR i = 0 TO 63
        SELECT CASE board(i)
            CASE 1
                state.blackCount = state.blackCount + 1
            CASE 2
                state.whiteCount = state.whiteCount + 1
        END SELECT
    NEXT i
END SUB

FUNCTION HasValidMoves(player AS INTEGER) AS INTEGER
    DIM row AS INTEGER
    DIM col AS INTEGER

    FOR row = 0 TO 7
        FOR col = 0 TO 7
            IF IsValidMove(row, col, player) = 1 THEN
                RETURN 1
            END IF
        NEXT col
    NEXT row

    RETURN 0
END FUNCTION

' ===== MAIN PROGRAM =====

state = NEW GameState()

' Initialize game
InitializeBoard()
InitializeDirections()

state.currentPlayer = 1  ' Black starts
state.moveCount = 0
state.gameOver = 0
state.blackPasses = 0
state.whitePasses = 0

PRINT "================================================================"
PRINT "                        OTHELLO"
PRINT "                   Text-Based OOP Game"
PRINT "================================================================"
PRINT ""
PRINT "Rules:"
PRINT "  - Black (B) moves first"
PRINT "  - Must flip opponent pieces"
PRINT "  - Pass if no valid moves"
PRINT ""

' Display initial board
PRINT "Initial position:"
DisplayBoard()

' Simple automated game (testing)
DIM autoMoves AS INTEGER
autoMoves = 10  ' Play 10 automatic moves

DIM moveNum AS INTEGER
FOR moveNum = 1 TO autoMoves
    ' Check if current player has valid moves
    IF HasValidMoves(state.currentPlayer) = 0 THEN
        PRINT "Player ";
        IF state.currentPlayer = 1 THEN
            PRINT "BLACK";
        ELSE
            PRINT "WHITE";
        END IF
        PRINT " has no valid moves - PASS"
        PRINT ""

        IF state.currentPlayer = 1 THEN
            state.blackPasses = state.blackPasses + 1
        ELSE
            state.whitePasses = state.whitePasses + 1
        END IF

        ' Check if game over (both passed)
        IF state.blackPasses > 0 THEN
            IF state.whitePasses > 0 THEN
                PRINT "Both players passed - GAME OVER"
                state.gameOver = 1
            END IF
        END IF

        ' Switch player
        state.currentPlayer = GetOpponent(state.currentPlayer)
    ELSE
        ' Reset pass counter for this player
        IF state.currentPlayer = 1 THEN
            state.blackPasses = 0
        ELSE
            state.whitePasses = 0
        END IF

        ' Find first valid move (simple AI)
        DIM found AS INTEGER
        DIM tryRow AS INTEGER
        DIM tryCol AS INTEGER

        found = 0
        tryRow = 0

        WHILE tryRow <= 7
            IF found = 1 THEN
                tryRow = 99  ' Break
            ELSE
                tryCol = 0
                WHILE tryCol <= 7
                    IF found = 1 THEN
                        tryCol = 99  ' Break
                    ELSE
                        IF IsValidMove(tryRow, tryCol, state.currentPlayer) = 1 THEN
                            ' Make this move
                            PRINT "Move "; state.moveCount + 1; ": ";
                            IF state.currentPlayer = 1 THEN
                                PRINT "BLACK";
                            ELSE
                                PRINT "WHITE";
                            END IF
                            PRINT " plays at ("; tryRow; ","; tryCol; ")"

                            MakeMove(tryRow, tryCol, state.currentPlayer)
                            state.moveCount = state.moveCount + 1
                            found = 1
                            tryCol = 99  ' Break
                        ELSE
                            tryCol = tryCol + 1
                        END IF
                    END IF
                WEND
                tryRow = tryRow + 1
            END IF
        WEND

        ' Display board after move
        DisplayBoard()

        ' Show score
        CountPieces()
        PRINT "Score: Black = "; state.blackCount; ", White = "; state.whiteCount
        PRINT ""

        ' Switch player
        state.currentPlayer = GetOpponent(state.currentPlayer)
    END IF

    ' Check game over status (will break loop naturally)
NEXT moveNum

' Final results
PRINT "================================================================"
PRINT "                      GAME SUMMARY"
PRINT "================================================================"
CountPieces()
PRINT "Total moves: "; state.moveCount
PRINT "Final score:"
PRINT "  Black: "; state.blackCount
PRINT "  White: "; state.whiteCount
PRINT ""

IF state.blackCount > state.whiteCount THEN
    PRINT "BLACK WINS!"
ELSE
    IF state.whiteCount > state.blackCount THEN
        PRINT "WHITE WINS!"
    ELSE
        PRINT "TIE GAME!"
    END IF
END IF

PRINT ""
PRINT "================================================================"

END
