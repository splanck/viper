' Othello Test 03: Move validation
' Check if a move would flip pieces in a direction

DIM board(64) AS INTEGER
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

' Test: Can Black play at (2,3)?
' This should flip the white piece at (3,3)
DIM testRow AS INTEGER
DIM testCol AS INTEGER
DIM testPlayer AS INTEGER

testRow = 2
testCol = 3
testPlayer = 1  ' Black

PRINT "Testing if Black can play at ("; testRow; ","; testCol; ")"
PRINT ""

' Check direction: down (row+1, col+0)
DIM checkRow AS INTEGER
DIM checkCol AS INTEGER
DIM foundOpponent AS INTEGER
DIM foundOwn AS INTEGER
DIM steps AS INTEGER

checkRow = testRow + 1
checkCol = testCol
foundOpponent = 0
foundOwn = 0
steps = 0

' Walk down until we find our own piece or empty/edge
WHILE checkRow <= 7
    DIM index AS INTEGER
    index = checkRow * 8 + checkCol

    DIM piece AS INTEGER
    piece = board(index)

    IF piece = 0 THEN
        ' Empty - no flip possible
        checkRow = 99  ' Break
    ELSE
        IF piece = testPlayer THEN
            ' Found our own piece
            foundOwn = 1
            checkRow = 99  ' Break
        ELSE
            ' Found opponent piece
            foundOpponent = 1
            steps = steps + 1
            checkRow = checkRow + 1
        END IF
    END IF
WEND

' Valid if we found opponent(s) then own piece
DIM isValid AS INTEGER
isValid = 0

IF foundOpponent = 1 THEN
    IF foundOwn = 1 THEN
        isValid = 1
    END IF
END IF

PRINT "Direction DOWN:"
PRINT "  Found opponent: "; foundOpponent
PRINT "  Found own piece: "; foundOwn
PRINT "  Steps: "; steps
PRINT "  Valid: "; isValid

END
