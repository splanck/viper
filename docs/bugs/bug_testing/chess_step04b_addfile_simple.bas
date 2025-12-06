REM Chess Game - Step 4b: Simplified ADDFILE Test
REM Testing ADDFILE without array parameters

REM Include module files
ADDFILE "chess_pieces.bas"
ADDFILE "chess_display_simple.bas"

REM Test 1: Create piece from included class
DIM piece AS ChessPiece
piece = NEW ChessPiece()
piece.Init(6, 0, 1, 5)  REM White King

PRINT AnsiClear();
PRINT AnsiColor(96); "=== ADDFILE Test ===" ; AnsiReset()
PRINT ""

PRINT "Testing included ChessPiece class:"
PRINT "Piece type: "; piece.pieceType
PRINT "Piece color: "; piece.pieceColor
PRINT "Symbol: "; piece.GetSymbol()
PRINT "Colored symbol: "; piece.GetColoredSymbol()
PRINT ""

REM Test 2: Test ANSI functions from included module
PRINT "Testing included ANSI functions:"
PRINT AnsiColor(91); "Red text"; AnsiReset()
PRINT AnsiColor(92); "Green text"; AnsiReset()
PRINT AnsiColor(93); "Yellow text"; AnsiReset()
PRINT AnsiColor(94); "Blue text"; AnsiReset()
PRINT ""

PRINT AnsiColor(92); "Step 4b Complete: ADDFILE includes modules correctly!"; AnsiReset()
