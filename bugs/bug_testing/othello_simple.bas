' ================================================================
'                      OTHELLO SIMPLE
'           Simplified Othello without Global Array Access
' ================================================================
' Workaround for BUG-053: Cannot access global arrays in SUBs
' All array logic done at module level
'
' ================================================================

' ===== CLASSES =====

CLASS GameState
    DIM currentPlayer AS INTEGER
    DIM blackCount AS INTEGER
    DIM whiteCount AS INTEGER
    DIM moveCount AS INTEGER
END CLASS

' ===== GLOBAL DATA =====

DIM board(64) AS INTEGER  ' 0=empty, 1=black, 2=white
DIM state AS GameState

' ===== HELPER FUNCTIONS (NO ARRAY ACCESS) =====

FUNCTION GetOpponent(player AS INTEGER) AS INTEGER
    IF player = 1 THEN
        RETURN 2
    ELSE
        RETURN 1
    END IF
END FUNCTION

' ===== MAIN PROGRAM =====

state = NEW GameState()

DIM i AS INTEGER

' Initialize empty board
FOR i = 0 TO 63
    board(i) = 0
NEXT i

' Set starting position
board(27) = 2  ' White at (3,3)
board(28) = 1  ' Black at (3,4)
board(35) = 1  ' Black at (4,3)
board(36) = 2  ' White at (4,4)

state.currentPlayer = 1  ' Black starts
state.blackCount = 2
state.whiteCount = 2
state.moveCount = 0

PRINT "================================================================"
PRINT "                    OTHELLO SIMPLE"
PRINT "================================================================"
PRINT ""

' Display initial board
PRINT "Initial position:"
PRINT ""
PRINT "  0 1 2 3 4 5 6 7"

DIM row AS INTEGER
DIM col AS INTEGER

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

' Simulate a few moves manually (no functions)
PRINT "Making test moves..."
PRINT ""

' Move 1: Black plays at (2,3)
DIM move1Row AS INTEGER
DIM move1Col AS INTEGER
DIM move1Index AS INTEGER

move1Row = 2
move1Col = 3
move1Index = move1Row * 8 + move1Col

PRINT "Move 1: Black plays at ("; move1Row; ","; move1Col; ")"

' Place piece
board(move1Index) = 1

' Flip white piece at (3,3) manually
board(27) = 1

state.moveCount = state.moveCount + 1

' Display board
PRINT ""
PRINT "  0 1 2 3 4 5 6 7"
FOR row = 0 TO 7
    PRINT row; " ";
    FOR col = 0 TO 7
        index = row * 8 + col
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

' Count pieces
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

PRINT "Score: Black = "; state.blackCount; ", White = "; state.whiteCount
PRINT ""

' Move 2: White plays at (2,2)
DIM move2Row AS INTEGER
DIM move2Col AS INTEGER
DIM move2Index AS INTEGER

move2Row = 2
move2Col = 2
move2Index = move2Row * 8 + move2Col

PRINT "Move 2: White plays at ("; move2Row; ","; move2Col; ")"

board(move2Index) = 2
board(19) = 2  ' Flip (2,3)

state.moveCount = state.moveCount + 1

' Display final board
PRINT ""
PRINT "  0 1 2 3 4 5 6 7"
FOR row = 0 TO 7
    PRINT row; " ";
    FOR col = 0 TO 7
        index = row * 8 + col
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

' Final count
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

PRINT "================================================================"
PRINT "                      GAME SUMMARY"
PRINT "================================================================"
PRINT "Moves played: "; state.moveCount
PRINT "Final score:"
PRINT "  Black: "; state.blackCount
PRINT "  White: "; state.whiteCount
PRINT ""
PRINT "This demonstrates basic Othello mechanics:"
PRINT "  - 8x8 board representation"
PRINT "  - Piece placement"
PRINT "  - Piece flipping"
PRINT "  - Score counting"
PRINT ""
PRINT "Limitation: Cannot use SUBs/FUNCTIONs with global arrays (BUG-053)"
PRINT "================================================================"

END
