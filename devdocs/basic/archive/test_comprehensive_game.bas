REM Comprehensive Test: Number Guessing Game
REM Tests: RANDOMIZE, RND, SGN, ABS, math, loops, SWAP, CONST
PRINT "=== NUMBER GUESSING GAME ==="
PRINT "I'm thinking of a number between 1 and 100"
PRINT ""

CONST MIN% = 1
CONST MAX% = 100
CONST MAX_GUESSES% = 7

RANDOMIZE TIMER()
secret% = INT(RND() * 100) + 1

guesses% = 0
won% = 0

DO WHILE guesses% < MAX_GUESSES%
    guesses% = guesses% + 1
    PRINT "Guess #"; guesses%; ": ";
    
    REM Simulate guess (in real game would use INPUT)
    REM For testing, use a search strategy
    IF guesses% = 1 THEN guess% = 50
    IF guesses% = 2 THEN
        IF guess% < secret% THEN guess% = 75 ELSE guess% = 25
    END IF
    IF guesses% = 3 THEN
        IF guess% < secret% THEN guess% = guess% + 12 ELSE guess% = guess% - 12
    END IF
    IF guesses% = 4 THEN
        IF guess% < secret% THEN guess% = guess% + 6 ELSE guess% = guess% - 6
    END IF
    IF guesses% >= 5 THEN
        IF guess% < secret% THEN guess% = guess% + 3 ELSE guess% = guess% - 3
    END IF
    
    PRINT guess%
    
    difference% = secret% - guess%
    absDiff% = ABS(difference%)
    sign% = SGN(difference%)
    
    IF difference% = 0 THEN
        PRINT "*** CORRECT! ***"
        won% = 1
        EXIT DO
    ELSEIF absDiff% <= 5 THEN
        PRINT "Very close! ";
        IF sign% > 0 THEN PRINT "Higher" ELSE PRINT "Lower"
    ELSEIF absDiff% <= 15 THEN
        PRINT "Close! ";
        IF sign% > 0 THEN PRINT "Higher" ELSE PRINT "Lower"
    ELSE
        PRINT "Not close. ";
        IF sign% > 0 THEN PRINT "Much higher" ELSE PRINT "Much lower"
    END IF
    PRINT ""
LOOP

PRINT ""
IF won% = 1 THEN
    PRINT "You won in "; guesses%; " guesses!"
    score% = 100 - (guesses% * 10)
    PRINT "Score: "; score%
ELSE
    PRINT "Out of guesses! The number was "; secret%
END IF

PRINT ""
PRINT "=== Game Statistics ==="
PRINT "Secret number: "; secret%
PRINT "Total guesses: "; guesses%
PRINT "Won: "; won%

PRINT ""
PRINT "Comprehensive game test completed!"
