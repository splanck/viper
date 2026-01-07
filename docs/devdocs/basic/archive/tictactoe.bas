REM ========================================
REM    TIC-TAC-TOE GAME WITH AI
REM    Version 1.0 - Full Featured
REM ========================================
REM
REM Features:
REM - Two player mode
REM - Computer AI opponent (3 difficulty levels)
REM - Score tracking
REM - Game statistics
REM - Colorful display (simulated with ASCII art)
REM
REM NOTE: Due to BUG-2003 (globals not working in SUB/FUNCTION),
REM       this game uses a monolithic structure.
REM ========================================

REM === GAME STATE VARIABLES ===
DIM c1$, c2$, c3$, c4$, c5$, c6$, c7$, c8$, c9$
DIM move%, player$, validMove%, gameOver%
DIM gameMode%, aiDifficulty%
DIM xWins%, oWins%, ties%, gamesPlayed%
DIM playAgain$, menuChoice%
DIM aiMove%, tempMove%, aiScore%, bestScore%
DIM aiCheckPos%, aiCheckMark$
DIM winner$, moveCount%
DIM i%, j%, k%

REM === INITIALIZE GAME ===
xWins% = 0%
oWins% = 0%
ties% = 0%
gamesPlayed% = 0%

REM ====================================
REM    MAIN MENU
REM ====================================
mainMenuLoop:
PRINT ""
PRINT "========================================"
PRINT "         TIC-TAC-TOE GAME"
PRINT "========================================"
PRINT ""
PRINT "SCOREBOARD:"
PRINT "  Games Played: "; gamesPlayed%
PRINT "  Player X Wins: "; xWins%
PRINT "  Player O Wins: "; oWins%
PRINT "  Ties: "; ties%
PRINT ""
PRINT "MAIN MENU:"
PRINT "  1. Play vs Human"
PRINT "  2. Play vs Computer (Easy)"
PRINT "  3. Play vs Computer (Medium)"
PRINT "  4. Play vs Computer (Hard)"
PRINT "  5. View Statistics"
PRINT "  6. Reset Scores"
PRINT "  0. Quit"
PRINT ""
PRINT "Enter your choice: ";
INPUT menuChoice%

IF menuChoice% = 0% THEN
    PRINT ""
    PRINT "Thanks for playing! Final Statistics:"
    PRINT "  Total Games: "; gamesPlayed%
    PRINT "  X Wins: "; xWins%
    PRINT "  O Wins: "; oWins%
    PRINT "  Ties: "; ties%
    PRINT ""
    PRINT "Goodbye!"
    END
END IF

IF menuChoice% = 5% THEN
    PRINT ""
    PRINT "====== GAME STATISTICS ======"
    PRINT "Total Games Played: "; gamesPlayed%
    PRINT ""
    IF gamesPlayed% > 0% THEN
        PRINT "Win Statistics:"
        PRINT "  X Wins: "; xWins%; " ("; INT(xWins% * 100 / gamesPlayed%); "%)"
        PRINT "  O Wins: "; oWins%; " ("; INT(oWins% * 100 / gamesPlayed%); "%)"
        PRINT "  Ties: "; ties%; " ("; INT(ties% * 100 / gamesPlayed%); "%)"
    ELSE
        PRINT "No games played yet!"
    END IF
    PRINT ""
    PRINT "Press Enter to continue...";
    INPUT playAgain$
    GOTO mainMenuLoop
END IF

IF menuChoice% = 6% THEN
    PRINT ""
    PRINT "Resetting all scores..."
    xWins% = 0%
    oWins% = 0%
    ties% = 0%
    gamesPlayed% = 0%
    PRINT "Scores reset!"
    PRINT ""
    GOTO mainMenuLoop
END IF

IF menuChoice% = 1% THEN
    gameMode% = 1%
    GOTO startGame
END IF

IF menuChoice% = 2% THEN
    gameMode% = 2%
    aiDifficulty% = 1%
    GOTO startGame
END IF

IF menuChoice% = 3% THEN
    gameMode% = 2%
    aiDifficulty% = 2%
    GOTO startGame
END IF

IF menuChoice% = 4% THEN
    gameMode% = 2%
    aiDifficulty% = 3%
    GOTO startGame
END IF

PRINT "Invalid choice! Try again."
GOTO mainMenuLoop

REM ====================================
REM    START NEW GAME
REM ====================================
startGame:
gamesPlayed% = gamesPlayed% + 1%

REM Initialize board with position numbers
c1$ = "1": c2$ = "2": c3$ = "3"
c4$ = "4": c5$ = "5": c6$ = "6"
c7$ = "7": c8$ = "8": c9$ = "9"

player$ = "X"
gameOver% = 0%

PRINT ""
PRINT "========================================"
IF gameMode% = 1% THEN
    PRINT "       PLAYER vs PLAYER MODE"
ELSE
    PRINT "       PLAYER vs COMPUTER MODE"
    IF aiDifficulty% = 1% THEN PRINT "           (Easy Difficulty)"
    IF aiDifficulty% = 2% THEN PRINT "         (Medium Difficulty)"
    IF aiDifficulty% = 3% THEN PRINT "           (Hard Difficulty)"
END IF
PRINT "========================================"
PRINT ""

REM ====================================
REM    MAIN GAME LOOP
REM ====================================
gameLoop:
IF gameOver% <> 0% THEN GOTO endGame

REM === DRAW BOARD ===
PRINT ""
PRINT "      |     |     "
PRINT "   "; c1$; "  |  "; c2$; "  |  "; c3$
PRINT " _____|_____|_____"
PRINT "      |     |     "
PRINT "   "; c4$; "  |  "; c5$; "  |  "; c6$
PRINT " _____|_____|_____"
PRINT "      |     |     "
PRINT "   "; c7$; "  |  "; c8$; "  |  "; c9$
PRINT "      |     |     "
PRINT ""

REM === GET MOVE ===
validMove% = 0%

REM Check if AI turn
IF gameMode% = 2% AND player$ = "O" THEN
    PRINT "Computer is thinking..."

    REM Easy AI: Random moves
    IF aiDifficulty% = 1% THEN
        aiMove% = 0%
        DO WHILE validMove% = 0%
            aiMove% = INT(RND() * 9%) + 1%
            IF aiMove% = 1% AND c1$ <> "X" AND c1$ <> "O" THEN validMove% = 1%
            IF aiMove% = 2% AND c2$ <> "X" AND c2$ <> "O" THEN validMove% = 1%
            IF aiMove% = 3% AND c3$ <> "X" AND c3$ <> "O" THEN validMove% = 1%
            IF aiMove% = 4% AND c4$ <> "X" AND c4$ <> "O" THEN validMove% = 1%
            IF aiMove% = 5% AND c5$ <> "X" AND c5$ <> "O" THEN validMove% = 1%
            IF aiMove% = 6% AND c6$ <> "X" AND c6$ <> "O" THEN validMove% = 1%
            IF aiMove% = 7% AND c7$ <> "X" AND c7$ <> "O" THEN validMove% = 1%
            IF aiMove% = 8% AND c8$ <> "X" AND c8$ <> "O" THEN validMove% = 1%
            IF aiMove% = 9% AND c9$ <> "X" AND c9$ <> "O" THEN validMove% = 1%
        LOOP
        move% = aiMove%
    END IF

    REM Medium AI: Block and take center
    IF aiDifficulty% = 2% THEN
        aiMove% = 0%

        REM Check for winning move
        FOR aiCheckPos% = 1% TO 9%
            IF aiCheckPos% = 1% AND c1$ <> "X" AND c1$ <> "O" THEN
                c1$ = "O"
                REM Check if this wins
                IF c1$ = c2$ AND c2$ = c3$ THEN aiMove% = 1%
                IF c1$ = c4$ AND c4$ = c7$ THEN aiMove% = 1%
                IF c1$ = c5$ AND c5$ = c9$ THEN aiMove% = 1%
                c1$ = "1"
            END IF
            IF aiCheckPos% = 2% AND c2$ <> "X" AND c2$ <> "O" THEN
                c2$ = "O"
                IF c1$ = c2$ AND c2$ = c3$ THEN aiMove% = 2%
                IF c2$ = c5$ AND c5$ = c8$ THEN aiMove% = 2%
                c2$ = "2"
            END IF
            IF aiCheckPos% = 3% AND c3$ <> "X" AND c3$ <> "O" THEN
                c3$ = "O"
                IF c1$ = c2$ AND c2$ = c3$ THEN aiMove% = 3%
                IF c3$ = c6$ AND c6$ = c9$ THEN aiMove% = 3%
                IF c3$ = c5$ AND c5$ = c7$ THEN aiMove% = 3%
                c3$ = "3"
            END IF
            IF aiCheckPos% = 5% AND c5$ <> "X" AND c5$ <> "O" THEN
                c5$ = "O"
                IF c1$ = c5$ AND c5$ = c9$ THEN aiMove% = 5%
                IF c3$ = c5$ AND c5$ = c7$ THEN aiMove% = 5%
                IF c2$ = c5$ AND c5$ = c8$ THEN aiMove% = 5%
                IF c4$ = c5$ AND c5$ = c6$ THEN aiMove% = 5%
                c5$ = "5"
            END IF
        NEXT aiCheckPos%

        REM If no winning move, block opponent
        IF aiMove% = 0% THEN
            FOR aiCheckPos% = 1% TO 9%
                IF aiCheckPos% = 1% AND c1$ <> "X" AND c1$ <> "O" THEN
                    c1$ = "X"
                    IF c1$ = c2$ AND c2$ = c3$ THEN aiMove% = 1%
                    IF c1$ = c4$ AND c4$ = c7$ THEN aiMove% = 1%
                    IF c1$ = c5$ AND c5$ = c9$ THEN aiMove% = 1%
                    c1$ = "1"
                END IF
                IF aiCheckPos% = 3% AND c3$ <> "X" AND c3$ <> "O" THEN
                    c3$ = "X"
                    IF c1$ = c2$ AND c2$ = c3$ THEN aiMove% = 3%
                    IF c3$ = c5$ AND c5$ = c7$ THEN aiMove% = 3%
                    c3$ = "3"
                END IF
                IF aiCheckPos% = 5% AND c5$ <> "X" AND c5$ <> "O" THEN
                    c5$ = "X"
                    IF c1$ = c5$ AND c5$ = c9$ THEN aiMove% = 5%
                    IF c3$ = c5$ AND c5$ = c7$ THEN aiMove% = 5%
                    c5$ = "5"
                END IF
            NEXT aiCheckPos%
        END IF

        REM If no block needed, take center or random
        IF aiMove% = 0% THEN
            IF c5$ <> "X" AND c5$ <> "O" THEN
                aiMove% = 5%
            ELSE
                DO WHILE validMove% = 0%
                    aiMove% = INT(RND() * 9%) + 1%
                    IF aiMove% = 1% AND c1$ <> "X" AND c1$ <> "O" THEN validMove% = 1%
                    IF aiMove% = 2% AND c2$ <> "X" AND c2$ <> "O" THEN validMove% = 1%
                    IF aiMove% = 3% AND c3$ <> "X" AND c3$ <> "O" THEN validMove% = 1%
                    IF aiMove% = 4% AND c4$ <> "X" AND c4$ <> "O" THEN validMove% = 1%
                    IF aiMove% = 5% AND c5$ <> "X" AND c5$ <> "O" THEN validMove% = 1%
                    IF aiMove% = 6% AND c6$ <> "X" AND c6$ <> "O" THEN validMove% = 1%
                    IF aiMove% = 7% AND c7$ <> "X" AND c7$ <> "O" THEN validMove% = 1%
                    IF aiMove% = 8% AND c8$ <> "X" AND c8$ <> "O" THEN validMove% = 1%
                    IF aiMove% = 9% AND c9$ <> "X" AND c9$ <> "O" THEN validMove% = 1%
                LOOP
            END IF
        END IF

        move% = aiMove%
        validMove% = 1%
    END IF

    REM Hard AI: Same as medium for now (would need minimax for true hard)
    IF aiDifficulty% = 3% THEN
        REM Use same logic as medium
        aiMove% = 0%

        REM Take center first if available
        IF c5$ <> "X" AND c5$ <> "O" THEN
            aiMove% = 5%
        END IF

        REM Take corners
        IF aiMove% = 0% THEN
            IF c1$ <> "X" AND c1$ <> "O" THEN aiMove% = 1%
            IF aiMove% = 0% AND c3$ <> "X" AND c3$ <> "O" THEN aiMove% = 3%
            IF aiMove% = 0% AND c7$ <> "X" AND c7$ <> "O" THEN aiMove% = 7%
            IF aiMove% = 0% AND c9$ <> "X" AND c9$ <> "O" THEN aiMove% = 9%
        END IF

        REM Otherwise random
        IF aiMove% = 0% THEN
            DO WHILE validMove% = 0%
                aiMove% = INT(RND() * 9%) + 1%
                IF aiMove% = 1% AND c1$ <> "X" AND c1$ <> "O" THEN validMove% = 1%
                IF aiMove% = 2% AND c2$ <> "X" AND c2$ <> "O" THEN validMove% = 1%
                IF aiMove% = 3% AND c3$ <> "X" AND c3$ <> "O" THEN validMove% = 1%
                IF aiMove% = 4% AND c4$ <> "X" AND c4$ <> "O" THEN validMove% = 1%
                IF aiMove% = 5% AND c5$ <> "X" AND c5$ <> "O" THEN validMove% = 1%
                IF aiMove% = 6% AND c6$ <> "X" AND c6$ <> "O" THEN validMove% = 1%
                IF aiMove% = 7% AND c7$ <> "X" AND c7$ <> "O" THEN validMove% = 1%
                IF aiMove% = 8% AND c8$ <> "X" AND c8$ <> "O" THEN validMove% = 1%
                IF aiMove% = 9% AND c9$ <> "X" AND c9$ <> "O" THEN validMove% = 1%
            LOOP
        END IF

        move% = aiMove%
        validMove% = 1%
    END IF

    PRINT "Computer chooses position "; move%
ELSE
    REM Human player input
    DO WHILE validMove% = 0%
        IF gameMode% = 1% THEN
            PRINT "Player "; player$; ", enter position (1-9): ";
        ELSE
            PRINT "Your turn (X), enter position (1-9): ";
        END IF
        INPUT move%

        IF move% < 1% OR move% > 9% THEN
            PRINT "Invalid position! Enter 1-9."
            validMove% = 0%
        ELSE
            REM Check if position is available
            IF move% = 1% AND c1$ <> "X" AND c1$ <> "O" THEN validMove% = 1%
            IF move% = 2% AND c2$ <> "X" AND c2$ <> "O" THEN validMove% = 1%
            IF move% = 3% AND c3$ <> "X" AND c3$ <> "O" THEN validMove% = 1%
            IF move% = 4% AND c4$ <> "X" AND c4$ <> "O" THEN validMove% = 1%
            IF move% = 5% AND c5$ <> "X" AND c5$ <> "O" THEN validMove% = 1%
            IF move% = 6% AND c6$ <> "X" AND c6$ <> "O" THEN validMove% = 1%
            IF move% = 7% AND c7$ <> "X" AND c7$ <> "O" THEN validMove% = 1%
            IF move% = 8% AND c8$ <> "X" AND c8$ <> "O" THEN validMove% = 1%
            IF move% = 9% AND c9$ <> "X" AND c9$ <> "O" THEN validMove% = 1%

            IF validMove% = 0% THEN
                PRINT "That position is taken! Try again."
            END IF
        END IF
    LOOP
END IF

REM === MAKE MOVE ===
IF move% = 1% THEN c1$ = player$
IF move% = 2% THEN c2$ = player$
IF move% = 3% THEN c3$ = player$
IF move% = 4% THEN c4$ = player$
IF move% = 5% THEN c5$ = player$
IF move% = 6% THEN c6$ = player$
IF move% = 7% THEN c7$ = player$
IF move% = 8% THEN c8$ = player$
IF move% = 9% THEN c9$ = player$

REM === CHECK FOR WINNER ===
winner$ = ""

REM Check rows
IF c1$ = c2$ AND c2$ = c3$ AND (c1$ = "X" OR c1$ = "O") THEN winner$ = c1$
IF c4$ = c5$ AND c5$ = c6$ AND (c4$ = "X" OR c4$ = "O") THEN winner$ = c4$
IF c7$ = c8$ AND c8$ = c9$ AND (c7$ = "X" OR c7$ = "O") THEN winner$ = c7$

REM Check columns
IF c1$ = c4$ AND c4$ = c7$ AND (c1$ = "X" OR c1$ = "O") THEN winner$ = c1$
IF c2$ = c5$ AND c5$ = c8$ AND (c2$ = "X" OR c2$ = "O") THEN winner$ = c2$
IF c3$ = c6$ AND c6$ = c9$ AND (c3$ = "X" OR c3$ = "O") THEN winner$ = c3$

REM Check diagonals
IF c1$ = c5$ AND c5$ = c9$ AND (c1$ = "X" OR c1$ = "O") THEN winner$ = c1$
IF c3$ = c5$ AND c5$ = c7$ AND (c3$ = "X" OR c3$ = "O") THEN winner$ = c3$

IF winner$ <> "" THEN
    REM Draw final board
    PRINT ""
    PRINT "      |     |     "
    PRINT "   "; c1$; "  |  "; c2$; "  |  "; c3$
    PRINT " _____|_____|_____"
    PRINT "      |     |     "
    PRINT "   "; c4$; "  |  "; c5$; "  |  "; c6$
    PRINT " _____|_____|_____"
    PRINT "      |     |     "
    PRINT "   "; c7$; "  |  "; c8$; "  |  "; c9$
    PRINT "      |     |     "
    PRINT ""

    PRINT ""
    PRINT "******************************"
    PRINT "*                            *"
    IF gameMode% = 2% AND winner$ = "O" THEN
        PRINT "*   COMPUTER WINS!           *"
    ELSE
        PRINT "*   PLAYER "; winner$; " WINS!        *"
    END IF
    PRINT "*                            *"
    PRINT "******************************"
    PRINT ""

    IF winner$ = "X" THEN xWins% = xWins% + 1%
    IF winner$ = "O" THEN oWins% = oWins% + 1%

    gameOver% = 1%
    GOTO gameLoop
END IF

REM === CHECK FOR TIE ===
moveCount% = 0%
IF c1$ = "X" OR c1$ = "O" THEN moveCount% = moveCount% + 1%
IF c2$ = "X" OR c2$ = "O" THEN moveCount% = moveCount% + 1%
IF c3$ = "X" OR c3$ = "O" THEN moveCount% = moveCount% + 1%
IF c4$ = "X" OR c4$ = "O" THEN moveCount% = moveCount% + 1%
IF c5$ = "X" OR c5$ = "O" THEN moveCount% = moveCount% + 1%
IF c6$ = "X" OR c6$ = "O" THEN moveCount% = moveCount% + 1%
IF c7$ = "X" OR c7$ = "O" THEN moveCount% = moveCount% + 1%
IF c8$ = "X" OR c8$ = "O" THEN moveCount% = moveCount% + 1%
IF c9$ = "X" OR c9$ = "O" THEN moveCount% = moveCount% + 1%

IF moveCount% = 9% THEN
    REM Draw final board
    PRINT ""
    PRINT "      |     |     "
    PRINT "   "; c1$; "  |  "; c2$; "  |  "; c3$
    PRINT " _____|_____|_____"
    PRINT "      |     |     "
    PRINT "   "; c4$; "  |  "; c5$; "  |  "; c6$
    PRINT " _____|_____|_____"
    PRINT "      |     |     "
    PRINT "   "; c7$; "  |  "; c8$; "  |  "; c9$
    PRINT "      |     |     "
    PRINT ""

    PRINT ""
    PRINT "******************************"
    PRINT "*                            *"
    PRINT "*       IT'S A TIE!          *"
    PRINT "*                            *"
    PRINT "******************************"
    PRINT ""

    ties% = ties% + 1%
    gameOver% = 1%
    GOTO gameLoop
END IF

REM === SWITCH PLAYERS ===
IF player$ = "X" THEN
    player$ = "O"
ELSE
    player$ = "X"
END IF

GOTO gameLoop

REM ====================================
REM    END GAME
REM ====================================
endGame:
PRINT "Play again? (Y/N): ";
INPUT playAgain$

IF playAgain$ = "Y" THEN
    GOTO startGame
END IF
IF playAgain$ = "y" THEN
    GOTO startGame
END IF
GOTO mainMenuLoop
