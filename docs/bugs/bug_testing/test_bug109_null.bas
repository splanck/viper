REM BUG-109 Test: Null object in array
REM Access null object's field - should this crash?

CLASS Hand
    Public count AS Integer

    Sub New()
        LET ME.count = 0
    End Sub
END CLASS

CLASS Player
    Public name AS String
    Public hand AS Hand

    Sub New(pname AS String)
        LET ME.name = pname
        LET ME.hand = NEW Hand()
    End Sub
END CLASS

REM Create array but only initialize first element
DIM players(2) AS Player
LET players(0) = NEW Player("Alice")
REM players(1) and players(2) are NULL!

PRINT "Alice's hand count: "; players(0).hand.count

PRINT "Trying to access null player's hand..."
REM This should trigger the bug - players(1) is null
PRINT "Bob's hand count: "; players(1).hand.count

PRINT "Should not reach here!"
