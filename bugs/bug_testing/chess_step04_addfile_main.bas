REM Chess Game - Step 4: Testing ADDFILE Keyword
REM Main program that includes separate modules

REM Include module files
ADDFILE "chess_pieces.bas"
ADDFILE "chess_display.bas"

REM Initialize board
DIM board(64) AS ChessPiece
DIM r AS INTEGER
DIM c AS INTEGER
DIM idx AS INTEGER

REM Initialize all squares as empty
FOR r = 1 TO 8
    FOR c = 1 TO 8
        idx = (r - 1) * 8 + c
        board(idx) = NEW ChessPiece()
        board(idx).Init(0, 0, r, c)
    NEXT c
NEXT r

REM Setup starting position
REM White pieces (row 1-2)
board(1).Init(2, 0, 1, 1)   REM Rook
board(2).Init(3, 0, 1, 2)   REM Knight
board(3).Init(4, 0, 1, 3)   REM Bishop
board(4).Init(5, 0, 1, 4)   REM Queen
board(5).Init(6, 0, 1, 5)   REM King
board(6).Init(4, 0, 1, 6)   REM Bishop
board(7).Init(3, 0, 1, 7)   REM Knight
board(8).Init(2, 0, 1, 8)   REM Rook

FOR c = 1 TO 8
    idx = (2 - 1) * 8 + c
    board(idx).Init(1, 0, 2, c)  REM White pawns
NEXT c

REM Black pieces (row 7-8)
FOR c = 1 TO 8
    idx = (7 - 1) * 8 + c
    board(idx).Init(1, 1, 7, c)  REM Black pawns
NEXT c

idx = (8 - 1) * 8 + 1
board(idx).Init(2, 1, 8, 1)   REM Rook

idx = (8 - 1) * 8 + 2
board(idx).Init(3, 1, 8, 2)   REM Knight

idx = (8 - 1) * 8 + 3
board(idx).Init(4, 1, 8, 3)   REM Bishop

idx = (8 - 1) * 8 + 4
board(idx).Init(5, 1, 8, 4)   REM Queen

idx = (8 - 1) * 8 + 5
board(idx).Init(6, 1, 8, 5)   REM King

idx = (8 - 1) * 8 + 6
board(idx).Init(4, 1, 8, 6)   REM Bishop

idx = (8 - 1) * 8 + 7
board(idx).Init(3, 1, 8, 7)   REM Knight

idx = (8 - 1) * 8 + 8
board(idx).Init(2, 1, 8, 8)   REM Rook

REM Display the board using the module function
DisplayBoard(board)

PRINT AnsiColor(92); "Step 4 Complete: ADDFILE works!"; AnsiReset()
