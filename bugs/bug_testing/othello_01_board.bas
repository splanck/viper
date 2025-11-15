' Othello Test 01: Board representation
' Test basic board array and display

DIM board(64) AS INTEGER  ' 8x8 = 64 squares
DIM i AS INTEGER
DIM row AS INTEGER
DIM col AS INTEGER

' Initialize empty board (0 = empty, 1 = black, 2 = white)
FOR i = 0 TO 63
    board(i) = 0
NEXT i

' Set starting position (center 4 squares)
' Row 3 (index 3), Col 3 (index 3) = White = 2
board(27) = 2  ' (3,3) = 3*8 + 3 = 27
' Row 3, Col 4 = Black = 1
board(28) = 1  ' (3,4) = 3*8 + 4 = 28
' Row 4, Col 3 = Black = 1
board(35) = 1  ' (4,3) = 4*8 + 3 = 35
' Row 4, Col 4 = White = 2
board(36) = 2  ' (4,4) = 4*8 + 4 = 36

' Display board
PRINT "  0 1 2 3 4 5 6 7"
FOR row = 0 TO 7
    PRINT row; " ";
    FOR col = 0 TO 7
        DIM index AS INTEGER
        index = row * 8 + col
        DIM piece AS INTEGER
        piece = board(index)

        IF piece = 0 THEN
            PRINT ". ";
        ELSE
            IF piece = 1 THEN
                PRINT "B ";
            ELSE
                PRINT "W ";
            END IF
        END IF
    NEXT col
    PRINT ""
NEXT row

END
