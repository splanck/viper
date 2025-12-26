' Othello Test 02b: OOP classes (without arrays in classes)

CLASS GameState
    DIM currentPlayer AS INTEGER
    DIM blackCount AS INTEGER
    DIM whiteCount AS INTEGER
    DIM moveCount AS INTEGER
END CLASS

CLASS Move
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM player AS INTEGER
    DIM isValid AS INTEGER
END CLASS

CLASS GameStats
    DIM totalMoves AS INTEGER
    DIM blackPasses AS INTEGER
    DIM whitePasses AS INTEGER
    DIM winner AS INTEGER
END CLASS

' Board array separate from class (workaround)
DIM board(64) AS INTEGER

' Test creating objects
DIM state AS GameState
DIM testMove AS Move
DIM stats AS GameStats

state = NEW GameState()
testMove = NEW Move()
stats = NEW GameStats()

' Initialize
state.currentPlayer = 1  ' Black starts
state.blackCount = 2
state.whiteCount = 2
state.moveCount = 0

testMove.row = 2
testMove.col = 3
testMove.player = 1
testMove.isValid = 0

stats.totalMoves = 0
stats.blackPasses = 0
stats.whitePasses = 0

PRINT "Othello OOP Classes Test (v2)"
PRINT "Current player: "; state.currentPlayer
PRINT "Black pieces: "; state.blackCount
PRINT "White pieces: "; state.whiteCount
PRINT "Test move: ("; testMove.row; ","; testMove.col; ")"
PRINT "Total moves: "; stats.totalMoves

END
