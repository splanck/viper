REM ====================================================================
REM FROGGER - Classic Arcade Game in Viper BASIC
REM ====================================================================
REM A complete OOP implementation featuring:
REM - Modular design with AddFile
REM - Object-oriented game objects
REM - ANSI color graphics
REM - Non-blocking input with INKEY$
REM - Collision detection
REM - Score tracking and lives system
REM ====================================================================

REM Include required modules
AddFile "frogger_ansi.bas"
AddFile "frogger_classes.bas"

REM ====================================================================
REM Game Constants
REM ====================================================================
DIM GAME_WIDTH AS INTEGER
DIM GAME_HEIGHT AS INTEGER
DIM NUM_CARS AS INTEGER

GAME_WIDTH = 70
GAME_HEIGHT = 23
NUM_CARS = 8

REM ====================================================================
REM Game Objects and State
REM ====================================================================
DIM frog AS Frog
DIM cars(7) AS Car

DIM score AS INTEGER
DIM gameRunning AS INTEGER
DIM frameCounter AS INTEGER

REM ====================================================================
REM Initialize Game
REM Creates frog and car objects with varied speeds and directions
REM ====================================================================
SUB InitGame()
    DIM i AS INTEGER

    score = 0
    gameRunning = 1
    frameCounter = 0

    REM Create player frog at bottom center
    frog = NEW Frog()
    frog.Init(GAME_HEIGHT - 1, GAME_WIDTH / 2)

    REM Create 8 cars with different speeds, directions, and positions
    cars(0) = NEW Car()
    cars(0).Init(5, 10, 1, 1, ">", 3)

    cars(1) = NEW Car()
    cars(1).Init(7, 30, 1, -1, "<", 4)

    cars(2) = NEW Car()
    cars(2).Init(9, 5, 2, 1, ">", 3)

    cars(3) = NEW Car()
    cars(3).Init(11, 50, 1, -1, "<", 5)

    cars(4) = NEW Car()
    cars(4).Init(13, 15, 2, 1, ">", 3)

    cars(5) = NEW Car()
    cars(5).Init(15, 40, 1, -1, "<", 4)

    cars(6) = NEW Car()
    cars(6).Init(17, 20, 1, 1, ">", 3)

    cars(7) = NEW Car()
    cars(7).Init(19, 60, 2, -1, "<", 4)
END SUB

REM ====================================================================
REM Draw Game Board
REM Renders all game objects using ANSI positioning and colors
REM ====================================================================
SUB DrawBoard()
    DIM i AS INTEGER
    DIM j AS INTEGER

    ClearScreen()

    REM Draw header with score and lives
    PrintColorAt(1, 2, COLOR_CYAN, "=== FROGGER ===")
    PrintColorAt(1, 50, COLOR_WHITE, "Lives: " + STR$(frog.GetLives()))
    PrintColorAt(1, 65, COLOR_YELLOW, "Score: " + STR$(score))

    REM Draw goal line at top
    FOR i = 1 TO GAME_WIDTH
        PrintColorAt(3, i, COLOR_GREEN, "=")
    NEXT i
    PrintColorAt(2, 30, COLOR_GREEN, "GOAL!")

    REM Draw road separator
    PrintColorAt(21, 1, COLOR_YELLOW, "----------------------------------------------")

    REM Draw frog (player)
    PrintColorAt(frog.GetRow(), frog.GetCol(), COLOR_GREEN, "@")

    REM Draw all cars
    FOR i = 0 TO NUM_CARS - 1
        DIM carRow AS INTEGER
        DIM carCol AS INTEGER
        DIM carWidth AS INTEGER
        DIM carSym AS STRING

        carRow = cars(i).GetRow()
        carCol = cars(i).GetCol()
        carWidth = cars(i).GetWidth()
        carSym = cars(i).GetSymbol()

        REM Draw car as multiple characters for its width
        FOR j = 0 TO carWidth - 1
            IF carCol + j >= 1 AND carCol + j <= GAME_WIDTH THEN
                PrintColorAt(carRow, carCol + j, COLOR_RED, carSym)
            END IF
        NEXT j
    NEXT i

    REM Draw control instructions
    PrintAt(GAME_HEIGHT + 1, 1, "WASD to move, Q to quit")
END SUB

REM ====================================================================
REM Update Game State
REM Moves objects, checks collisions, handles scoring
REM ====================================================================
SUB UpdateGame()
    DIM i AS INTEGER

    REM Move all cars
    FOR i = 0 TO NUM_CARS - 1
        cars(i).Move()
    NEXT i

    REM Check collisions with all cars
    FOR i = 0 TO NUM_CARS - 1
        IF cars(i).CheckCollision(frog.GetRow(), frog.GetCol()) = 1 THEN
            frog.Die()
            IF frog.IsAlive() = 1 THEN
                REM Show death animation if lives remain
                PrintColorAt(12, 30, COLOR_RED, "SPLAT!")
                SLEEP 500
            END IF
        END IF
    NEXT i

    REM Check if frog reached goal
    IF frog.GetRow() <= 3 THEN
        score = score + 100
        frog.Reset()
        REM Show goal animation
        PrintColorAt(12, 28, COLOR_GREEN, "SAFE!")
        SLEEP 500
    END IF

    frameCounter = frameCounter + 1
END SUB

REM ====================================================================
REM Handle Player Input
REM Non-blocking input using INKEY$
REM ====================================================================
SUB HandleInput()
    DIM key AS STRING
    key = INKEY$()

    IF LEN(key) > 0 THEN
        IF key = "w" OR key = "W" THEN
            frog.MoveUp()
        END IF

        IF key = "s" OR key = "S" THEN
            frog.MoveDown()
        END IF

        IF key = "a" OR key = "A" THEN
            frog.MoveLeft()
        END IF

        IF key = "d" OR key = "D" THEN
            frog.MoveRight()
        END IF

        IF key = "q" OR key = "Q" THEN
            gameRunning = 0
        END IF
    END IF
END SUB

REM ====================================================================
REM Main Game Loop
REM ====================================================================
SUB GameLoop()
    HideCursor()

    WHILE gameRunning = 1 AND frog.IsAlive() = 1
        DrawBoard()
        HandleInput()
        UpdateGame()
        SLEEP 100  ' 10 FPS game loop
    WEND

    REM Game over screen
    ClearScreen()
    ShowCursor()

    PRINT ""
    IF frog.IsAlive() = 0 THEN
        PRINT "==============================================="
        PRINT "           G A M E   O V E R                  "
        PRINT "==============================================="
        PRINT ""
    ELSE
        PRINT "Thanks for playing!"
    END IF

    PRINT ""
    PRINT "Final Score: "; score
    PRINT "Frames: "; frameCounter
    PRINT ""
END SUB

REM ====================================================================
REM Start the Game
REM ====================================================================
InitGame()
GameLoop()
