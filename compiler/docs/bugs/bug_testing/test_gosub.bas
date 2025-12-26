REM Test GOSUB/RETURN
DIM score AS INTEGER
DIM lives AS INTEGER

score = 0
lives = 3

PRINT "=== GAME START ==="
PRINT ""

GOSUB ShowStatus
PRINT ""

PRINT "Level 1..."
score = score + 100
GOSUB ShowStatus
PRINT ""

PRINT "Died!"
lives = lives - 1
GOSUB ShowStatus
PRINT ""

PRINT "Level 2..."
score = score + 250
GOSUB ShowStatus
PRINT ""

PRINT "=== GAME OVER ==="
END

ShowStatus:
    PRINT "┌────────────────┐"
    PRINT "│ SCORE: "; score
    PRINT "│ LIVES: "; lives
    PRINT "└────────────────┘"
    RETURN
