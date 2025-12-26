' Othello Test 02: OOP classes for game

CLASS Board
    DIM cells(64) AS INTEGER
    DIM currentPlayer AS INTEGER
    DIM blackCount AS INTEGER
    DIM whiteCount AS INTEGER
END CLASS

CLASS Move
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM player AS INTEGER
    DIM isValid AS INTEGER
    DIM flipsCount AS INTEGER
END CLASS

CLASS GameStats
    DIM totalMoves AS INTEGER
    DIM blackPasses AS INTEGER
    DIM whitePasses AS INTEGER
    DIM winner AS INTEGER
END CLASS

' Test creating objects
DIM gameBoard AS Board
DIM testMove AS Move
DIM stats AS GameStats

gameBoard = NEW Board()
testMove = NEW Move()
stats = NEW GameStats()

' Initialize
gameBoard.currentPlayer = 1  ' Black starts
gameBoard.blackCount = 2
gameBoard.whiteCount = 2

testMove.row = 2
testMove.col = 3
testMove.player = 1
testMove.isValid = 0

stats.totalMoves = 0
stats.blackPasses = 0
stats.whitePasses = 0

PRINT "Othello OOP Classes Test"
PRINT "Current player: "; gameBoard.currentPlayer
PRINT "Black pieces: "; gameBoard.blackCount
PRINT "White pieces: "; gameBoard.whiteCount
PRINT "Test move: ("; testMove.row; ","; testMove.col; ")"
PRINT "Total moves: "; stats.totalMoves

END
