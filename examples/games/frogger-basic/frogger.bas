' ============================================================================
' MODULE: frogger.bas
' PURPOSE: Program entry point and main game loop for the Frogger demo.
'          Owns the playfield layout, the global game state, the menu
'          state machine, and the per-frame orchestration.
'
' WHERE-THIS-FITS: Top of the dependency tree. Loads frogger_ansi.bas
'          (terminal helpers), frogger_classes.bas (game entities), and
'          frogger_scores.bas (high scores). Instantiates the Frog,
'          ten Vehicles, ten Platforms, and five Homes, then ticks them
'          each frame.
'
' KEY-DESIGN-CHOICES:
'   * PLAYFIELD LAYOUT IS ROW-BAND-BASED. The screen is vertically
'     divided into named bands:
'         HOME_ROW     (row 2)   - the five goal slots
'         RIVER_START..RIVER_END (rows 4-8)  - the water
'         MEDIAN_ROW   (row 10)  - safe zone between river and road
'         ROAD_START..ROAD_END   (rows 12-16) - the traffic
'         START_ROW    (row 18)  - the frog's spawn row
'     Collision logic branches on which band the frog is in. This is
'     cleaner than per-row collision tables and matches the arcade
'     original's structure.
'   * WATER = DEATH UNLESS ON A PLATFORM. `IsInRiver` flags a row as
'     water; if the frog is there, we search for a platform it's
'     riding. No platform match -> drown. This is the core Frogger
'     mechanic inverted from the road: in the river you MUST be on a
'     moving object; on the road you must NOT be.
'   * TICK ORDER. Draw -> HandleInput -> UpdateGame -> sleep. Input
'     before update means the frog responds to the current frame's
'     keypress but the world ticks afterwards, giving "snappy" controls
'     that feel synchronous. Reversed order would feel sluggish.
'   * WIN CONDITION. `homesFilledCount = 5` clears `gameRunning` and
'     exits the loop with `frog.IsAlive() = 1`, which the game-over
'     screen detects to show "CONGRATULATIONS!" instead of "GAME OVER".
'   * FLICKER SUPPRESSION. Every draw cell goes through `BufferColorAt`
'     / `BufferAt` from frogger_ansi.bas. Under ALTSCREEN batch mode
'     those writes coalesce into a single flush at frame end — no
'     partial-paint tearing. The "Clear gap rows" loop at the top of
'     `DrawBoard` wipes the narrow no-obstacles rows that would
'     otherwise retain trail pixels from the frog passing through them.
'
' HOW-TO-READ: Constants / layout bands -> InitGame (entity layout) ->
'   DrawBoard (per-frame paint) -> IsInRiver (tiny helper) ->
'   UpdateGame (collision resolution: river drown, road splat, home
'   fill, bounds) -> HandleInput -> GameLoop -> pause / menu /
'   instructions -> main program.
' ============================================================================

' Load helper modules BEFORE any DIM references their classes. Order:
' ansi (terminal primitives) -> classes (game objects) -> scores (table).
AddFile "frogger_ansi.bas"
AddFile "frogger_classes.bas"
AddFile "frogger_scores.bas"

REM ====================================================================
REM Game Constants
REM ====================================================================
DIM GAME_WIDTH AS INTEGER
DIM GAME_HEIGHT AS INTEGER

GAME_WIDTH = 70
GAME_HEIGHT = 24

REM Row definitions
DIM HOME_ROW AS INTEGER
DIM RIVER_START AS INTEGER
DIM RIVER_END AS INTEGER
DIM MEDIAN_ROW AS INTEGER
DIM ROAD_START AS INTEGER
DIM ROAD_END AS INTEGER
DIM START_ROW AS INTEGER

HOME_ROW = 2
RIVER_START = 4
RIVER_END = 8
MEDIAN_ROW = 10
ROAD_START = 12
ROAD_END = 16
START_ROW = 18

REM ====================================================================
REM Game Objects
REM ====================================================================
DIM frog AS Frog
DIM vehicles(9) AS Vehicle
DIM platforms(9) AS Platform
DIM homes(4) AS Home

DIM score AS INTEGER
DIM level AS INTEGER
DIM gameRunning AS INTEGER
DIM homesFilledCount AS INTEGER

' ====================================================================
' Initialize Game
' --------------------------------------------------------------------
' Build every per-game entity from scratch. Five blocks:
'   1. Score / level / game-flag / homes-filled counter reset.
'   2. Frog constructed at the bottom-centre spawn (row 18, col 35).
'   3. Five Home slots laid out across the top at cols 8/20/32/44/56.
'   4. Ten road vehicles arranged in five lanes (rows 12-16) with
'      alternating direction and a mix of car/truck widths. The
'      pattern — slow right, slow left, faster right, slow left, fast
'      right — mirrors the classic arcade traffic feel.
'   5. Ten river platforms arranged in five rows (rows 4-8) with
'      logs ("=") going right and turtles ("O") going left. Long
'      logs on row 6 give the player breathing room; shorter turtle
'      clusters on rows 5 and 7 force more careful timing.
'
' All entities are constructed with NEW; the array slots hold
' references. This is the only place the game allocates — the main
' loop re-uses these instances for the entire session.
' ====================================================================
SUB InitGame()
    DIM i AS INTEGER

    score = 0
    level = 1
    gameRunning = 1
    homesFilledCount = 0

    REM Create frog at start position
    frog = NEW Frog()
    frog.Init(START_ROW, 35)

    REM Create 5 homes at top
    homes(0) = NEW Home()
    homes(0).Init(8)
    homes(1) = NEW Home()
    homes(1).Init(20)
    homes(2) = NEW Home()
    homes(2).Init(32)
    homes(3) = NEW Home()
    homes(3).Init(44)
    homes(4) = NEW Home()
    homes(4).Init(56)

    REM Create road vehicles (5 lanes)
    REM Lane 1: Cars going right
    vehicles(0) = NEW Vehicle()
    vehicles(0).Init(12, 5, 1, 1, "=", 4)
    vehicles(1) = NEW Vehicle()
    vehicles(1).Init(12, 35, 1, 1, "=", 4)

    REM Lane 2: Trucks going left
    vehicles(2) = NEW Vehicle()
    vehicles(2).Init(13, 15, 1, -1, "#", 6)
    vehicles(3) = NEW Vehicle()
    vehicles(3).Init(13, 50, 1, -1, "#", 6)

    REM Lane 3: Cars going right
    vehicles(4) = NEW Vehicle()
    vehicles(4).Init(14, 10, 2, 1, "=", 4)
    vehicles(5) = NEW Vehicle()
    vehicles(5).Init(14, 45, 2, 1, "=", 4)

    REM Lane 4: Trucks going left
    vehicles(6) = NEW Vehicle()
    vehicles(6).Init(15, 20, 1, -1, "#", 6)

    REM Lane 5: Fast cars going right
    vehicles(7) = NEW Vehicle()
    vehicles(7).Init(16, 8, 2, 1, "=", 4)
    vehicles(8) = NEW Vehicle()
    vehicles(8).Init(16, 40, 2, 1, "=", 4)
    vehicles(9) = NEW Vehicle()
    vehicles(9).Init(16, 60, 2, 1, "=", 4)

    REM Create river platforms (5 rows)
    REM Row 1: Logs going right
    platforms(0) = NEW Platform()
    platforms(0).Init(4, 5, 1, 1, "=", 8)
    platforms(1) = NEW Platform()
    platforms(1).Init(4, 35, 1, 1, "=", 8)

    REM Row 2: Turtles going left
    platforms(2) = NEW Platform()
    platforms(2).Init(5, 15, 1, -1, "O", 5)
    platforms(3) = NEW Platform()
    platforms(3).Init(5, 50, 1, -1, "O", 5)

    REM Row 3: Long log going right
    platforms(4) = NEW Platform()
    platforms(4).Init(6, 10, 1, 1, "=", 12)
    platforms(5) = NEW Platform()
    platforms(5).Init(6, 45, 1, 1, "=", 10)

    REM Row 4: Turtles going left
    platforms(6) = NEW Platform()
    platforms(6).Init(7, 20, 1, -1, "O", 4)
    platforms(7) = NEW Platform()
    platforms(7).Init(7, 55, 1, -1, "O", 4)

    REM Row 5: Logs going right
    platforms(8) = NEW Platform()
    platforms(8).Init(8, 12, 2, 1, "=", 7)
    platforms(9) = NEW Platform()
    platforms(9).Init(8, 48, 2, 1, "=", 7)
END SUB

' ====================================================================
' Draw Game Board
' --------------------------------------------------------------------
' Full-frame paint. Bands painted bottom-up in the game world (top-
' down on screen):
'   1. Clear the four gap rows (3, 9, 11, 17) — these are the
'      borderless transitions between bands. The frog leaves "trail"
'      artifacts in these rows when it moves so we wipe them fresh.
'   2. Title / lives / score on row 1.
'   3. Home row: baseline of blue water, then each home slot as
'      either "[F]" (filled) or "[ ]" (empty).
'   4. River: fill the RIVER_START..RIVER_END band with blue "~".
'   5. Platforms: draw logs (yellow "=") and turtles (green "O") on
'      top of the river. Platform draws override the water below.
'   6. Median: solid green "-" row with "SAFE ZONE" label.
'   7. Road: white "." filler.
'   8. Vehicles: red glyphs on top of the road.
'   9. Start row: green "-" strip with "START" label.
'  10. Frog: green "@" at its current row/col.
'  11. Bottom-of-screen instructions banner.
'
' The entire frame is batch-mode under ALTSCREEN, so none of these
' writes actually flush until `FlushFrame` — keeping the paint
' atomic and flicker-free.
' ====================================================================
SUB DrawBoard()
    DIM i AS INTEGER
    DIM j AS INTEGER

    REM Begin buffered frame (flicker-free rendering)
    BeginFrame()

    REM Clear gap rows to prevent frog trails (rows 3, 9, 11, 17)
    FOR i = 1 TO GAME_WIDTH
        BufferColorAt(3, i, COLOR_WHITE, " ")
    NEXT i
    FOR i = 1 TO GAME_WIDTH
        BufferColorAt(9, i, COLOR_WHITE, " ")
    NEXT i
    FOR i = 1 TO GAME_WIDTH
        BufferColorAt(11, i, COLOR_WHITE, " ")
    NEXT i
    FOR i = 1 TO GAME_WIDTH
        BufferColorAt(17, i, COLOR_WHITE, " ")
    NEXT i

    REM Draw title and status
    BufferColorAt(1, 25, COLOR_CYAN, "*** CLASSIC FROGGER ***")
    BufferColorAt(1, 2, COLOR_WHITE, "Lives:" + STR$(frog.GetLives()))
    BufferColorAt(1, 60, COLOR_YELLOW, "Score:" + STR$(score))

    REM Draw homes row
    FOR i = 1 TO GAME_WIDTH
        BufferColorAt(HOME_ROW, i, COLOR_BLUE, "~")
    NEXT i

    REM Draw each home
    FOR i = 0 TO 4
        DIM homeCol AS INTEGER
        homeCol = homes(i).GetCol()
        IF homes(i).IsFilled() = 1 THEN
            BufferColorAt(HOME_ROW, homeCol - 1, COLOR_GREEN, "[")
            BufferColorAt(HOME_ROW, homeCol, COLOR_GREEN, "F")
            BufferColorAt(HOME_ROW, homeCol + 1, COLOR_GREEN, "]")
        ELSE
            BufferColorAt(HOME_ROW, homeCol - 1, COLOR_WHITE, "[")
            BufferColorAt(HOME_ROW, homeCol, COLOR_WHITE, " ")
            BufferColorAt(HOME_ROW, homeCol + 1, COLOR_WHITE, "]")
        END IF
    NEXT i

    REM Draw river section
    FOR i = RIVER_START TO RIVER_END
        FOR j = 1 TO GAME_WIDTH
            BufferColorAt(i, j, COLOR_BLUE, "~")
        NEXT j
    NEXT i

    REM Draw river platforms (logs and turtles)
    FOR i = 0 TO 9
        DIM platRow AS INTEGER
        DIM platCol AS INTEGER
        DIM platWidth AS INTEGER
        DIM platSym AS STRING

        platRow = platforms(i).GetRow()
        platCol = platforms(i).GetCol()
        platWidth = platforms(i).GetWidth()
        platSym = platforms(i).GetSymbol()

        FOR j = 0 TO platWidth - 1
            IF platCol + j >= 1 AND platCol + j <= GAME_WIDTH THEN
                IF platSym = "O" THEN
                    BufferColorAt(platRow, platCol + j, COLOR_GREEN, platSym)
                ELSE
                    BufferColorAt(platRow, platCol + j, COLOR_YELLOW, platSym)
                END IF
            END IF
        NEXT j
    NEXT i

    REM Draw median (safe zone)
    FOR i = 1 TO GAME_WIDTH
        BufferColorAt(MEDIAN_ROW, i, COLOR_GREEN, "-")
    NEXT i
    BufferColorAt(MEDIAN_ROW, 28, COLOR_GREEN, "SAFE ZONE")

    REM Draw road section
    FOR i = ROAD_START TO ROAD_END
        FOR j = 1 TO GAME_WIDTH
            BufferColorAt(i, j, COLOR_WHITE, ".")
        NEXT j
    NEXT i

    REM Draw road vehicles
    FOR i = 0 TO 9
        DIM vehRow AS INTEGER
        DIM vehCol AS INTEGER
        DIM vehWidth AS INTEGER
        DIM vehSym AS STRING

        vehRow = vehicles(i).GetRow()
        vehCol = vehicles(i).GetCol()
        vehWidth = vehicles(i).GetWidth()
        vehSym = vehicles(i).GetSymbol()

        FOR j = 0 TO vehWidth - 1
            IF vehCol + j >= 1 AND vehCol + j <= GAME_WIDTH THEN
                BufferColorAt(vehRow, vehCol + j, COLOR_RED, vehSym)
            END IF
        NEXT j
    NEXT i

    REM Draw start area
    FOR i = 1 TO GAME_WIDTH
        BufferColorAt(START_ROW, i, COLOR_GREEN, "-")
    NEXT i
    BufferColorAt(START_ROW, 30, COLOR_GREEN, "START")

    REM Draw frog
    BufferColorAt(frog.GetRow(), frog.GetCol(), COLOR_GREEN, "@")

    REM Draw instructions
    BufferAt(20, 1, "WASD=Move  P=Pause  Q=Quit  Goal: Fill all 5 homes!")

    REM Flush entire frame to screen at once (eliminates flicker)
    FlushFrame()
END SUB

' Boolean predicate: is the given row inside the river band? Used by
' UpdateGame to decide whether "no platform overlap" means drown or
' just continue.
FUNCTION IsInRiver(row AS INTEGER) AS INTEGER
    IF row >= RIVER_START AND row <= RIVER_END THEN
        IsInRiver = 1
    ELSE
        IsInRiver = 0
    END IF
END FUNCTION

' ====================================================================
' Update Game State
' --------------------------------------------------------------------
' Per-tick simulation. Runs after HandleInput has applied the
' player's move. Order of operations:
'   1. Tick every vehicle (horizontal movement + wraparound).
'   2. Tick every platform (same shape as vehicles).
'   3. If the frog is in the river band, scan platforms to see if
'      it's riding one. If yes, drift with the platform; if no,
'      drown (Die + "SPLASH!" banner).
'   4. If the frog is in the road band, test every vehicle for
'      overlap. Any hit -> Die + "SPLAT!" banner.
'   5. If the frog reached HOME_ROW, test each of the five home
'      slots. Overlap with an empty slot -> fill + +200 + reset;
'      overlap with a filled slot OR no overlap -> Die. Filling
'      all five triggers win-condition (exit loop with alive=1).
'   6. Off-screen horizontally -> Die.
'
' The SLEEP 800 after a death is the "feedback beat" — the player
' sees what killed them before the respawn.
' ====================================================================
SUB UpdateGame()
    DIM i AS INTEGER
    DIM frogRow AS INTEGER
    DIM frogCol AS INTEGER

    frogRow = frog.GetRow()
    frogCol = frog.GetCol()

    REM Move all vehicles
    FOR i = 0 TO 9
        vehicles(i).Move()
    NEXT i

    REM Move all platforms
    FOR i = 0 TO 9
        platforms(i).Move()
    NEXT i

    REM Check if frog is in river
    IF IsInRiver(frogRow) = 1 THEN
        DIM onPlatform AS INTEGER
        onPlatform = 0

        REM Check each platform
        FOR i = 0 TO 9
            IF platforms(i).CheckOnPlatform(frogRow, frogCol) = 1 THEN
                onPlatform = 1
                REM Move frog with platform
                DIM platSpeed AS INTEGER
                DIM platDir AS INTEGER
                platSpeed = platforms(i).GetSpeed() * platforms(i).GetDirection()
                frog.SetOnPlatform(platSpeed)
                frog.UpdateOnPlatform()
            END IF
        NEXT i

        REM If not on platform, frog drowns
        IF onPlatform = 0 THEN
            frog.Die()
            frog.ClearPlatform()
            IF frog.IsAlive() = 1 THEN
                PrintColorAt(12, 28, COLOR_RED, "SPLASH!")
                SLEEP 800
            END IF
            RETURN
        END IF
    ELSE
        frog.ClearPlatform()
    END IF

    REM Check road collisions
    IF frogRow >= ROAD_START AND frogRow <= ROAD_END THEN
        FOR i = 0 TO 9
            IF vehicles(i).CheckCollision(frogRow, frogCol) = 1 THEN
                frog.Die()
                IF frog.IsAlive() = 1 THEN
                    PrintColorAt(12, 28, COLOR_RED, "SPLAT!")
                    SLEEP 800
                END IF
                RETURN
            END IF
        NEXT i
    END IF

    REM Check if frog reached a home
    IF frogRow = HOME_ROW THEN
        DIM foundHome AS INTEGER
        foundHome = 0

        FOR i = 0 TO 4
            DIM homeCol AS INTEGER
            homeCol = homes(i).GetCol()
            IF frogCol >= homeCol - 1 AND frogCol <= homeCol + 1 THEN
                IF homes(i).IsFilled() = 0 THEN
                    homes(i).Fill()
                    homesFilledCount = homesFilledCount + 1
                    score = score + 200
                    foundHome = 1

                    REM Check if all homes filled
                    IF homesFilledCount = 5 THEN
                        gameRunning = 0
                    ELSE
                        frog.Reset()
                        PrintColorAt(12, 25, COLOR_GREEN, "HOME SAFE! +200")
                        SLEEP 500
                    END IF
                ELSE
                    REM Home already filled
                    frog.Die()
                END IF
            END IF
        NEXT i

        REM If didn't land in a home slot, die
        IF foundHome = 0 THEN
            frog.Die()
        END IF
    END IF

    REM Check if frog went off screen
    IF frogCol < 1 OR frogCol > GAME_WIDTH THEN
        frog.Die()
    END IF
END SUB

' Non-blocking input dispatch. Reads ONE queued keypress (returns ""
' if the queue is empty) and maps WASD to frog movement, Q to quit,
' P to pause. Called once per frame before UpdateGame.
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

        IF key = "p" OR key = "P" THEN
            HandlePause()
        END IF
    END IF
END SUB

' ====================================================================
' Main Game Loop
' --------------------------------------------------------------------
' Clears screen once, hides the cursor, then iterates
' DrawBoard/HandleInput/UpdateGame/SLEEP 100 until either the
' gameRunning flag is cleared (win condition or quit) or the frog
' runs out of lives. After the loop:
'   * Re-show cursor and print the appropriate end screen:
'       - homesFilledCount == 5 -> WIN
'       - frog.IsAlive() == 0   -> GAME OVER
'       - else (quit)           -> "Thanks for playing!"
'   * Print final score + homes-filled stat.
'   * If the score qualifies for the high-score table, prompt for
'     name and insert via `AddHighScore`.
'   * Block on ANY key before returning to the menu.
'
' ~100 ms per tick gives roughly 10 Hz gameplay, which matches the
' arcade Frogger's pace. Tighten SLEEP to speed the game up at
' higher levels if a level system is added.
' ====================================================================
SUB GameLoop()
    REM Initial screen setup - clear once at start
    ClearScreen()
    HideCursor()

    WHILE gameRunning = 1 AND frog.IsAlive() = 1
        DrawBoard()
        HandleInput()
        UpdateGame()
        SLEEP 100
    WEND

    REM Game over screen
    ClearScreen()
    ShowCursor()

    PRINT ""
    IF homesFilledCount = 5 THEN
        PRINT "╔════════════════════════════════════════════════╗"
        PRINT "║      ★ CONGRATULATIONS! YOU WIN! ★            ║"
        PRINT "╚════════════════════════════════════════════════╝"
        PRINT ""
        PRINT "  You successfully filled all 5 homes!"
        PRINT ""
    ELSE IF frog.IsAlive() = 0 THEN
        PRINT "╔════════════════════════════════════════════════╗"
        PRINT "║           G A M E   O V E R                   ║"
        PRINT "╚════════════════════════════════════════════════╝"
        PRINT ""
    ELSE
        PRINT "  Thanks for playing!"
        PRINT ""
    END IF

    PRINT "  Final Score: "; score
    PRINT "  Homes Filled: "; homesFilledCount; " / 5"
    PRINT ""

    REM Check for high score
    IF IsHighScore(score) = 1 AND score > 0 THEN
        DIM playerName AS STRING
        playerName = GetPlayerName()
        AddHighScore(playerName, score)
        PRINT ""
        PRINT "  Your score has been added to the high score list!"
        PRINT ""
    END IF

    PRINT "  Press any key to return to menu..."
    WHILE LEN(INKEY$()) = 0
        SLEEP 50
    WEND
END SUB

' ====================================================================
' Handle Pause
' --------------------------------------------------------------------
' Paint a "PAUSED" overlay on top of the current frame and spin
' waiting for P to be pressed again. All other keys are ignored.
' The overlay overwrites two rows of the playfield — when the pause
' releases, the next DrawBoard repaints those cells cleanly.
' ====================================================================
SUB HandlePause()
    DIM key AS STRING

    PrintColorAt(12, 25, COLOR_YELLOW, "*** PAUSED ***")
    PrintColorAt(13, 20, COLOR_WHITE, "Press P to resume")

    WHILE 1 = 1
        key = INKEY$()
        IF LEN(key) > 0 THEN
            IF key = "p" OR key = "P" THEN
                EXIT WHILE
            END IF
        END IF
        SLEEP 50
    WEND
END SUB

' ====================================================================
' Main Menu
' --------------------------------------------------------------------
' W/S-driven vertical menu. Four choices cycle around (choice 1 goes
' to choice 4 if W is pressed at the top; vice versa at the bottom).
' ENTER commits. The ENTER branch SLEEPs 200ms after the action to
' debounce — otherwise a held ENTER from the game-over screen would
' instantly start a new game.
'
' The flow from here:
'   [1] -> InitGame() -> "press any key" -> GameLoop() -> back here
'   [2] -> DisplayHighScores -> wait for key -> back here
'   [3] -> ShowInstructions -> back here
'   [4] -> EXIT WHILE -> exit program
' ====================================================================
SUB ShowMainMenu()
    DIM choice AS INTEGER
    DIM key AS STRING

    choice = 1

    WHILE 1 = 1
        ClearScreen()
        PRINT ""
        PRINT "╔════════════════════════════════════════════════════════╗"
        PRINT "║                                                        ║"
        PRINT "║   ███████╗██████╗  ██████╗  ██████╗  ██████╗ ███████╗║"
        PRINT "║   ██╔════╝██╔══██╗██╔═══██╗██╔════╝ ██╔════╝ ██╔════╝║"
        PRINT "║   █████╗  ██████╔╝██║   ██║██║  ███╗██║  ███╗█████╗  ║"
        PRINT "║   ██╔══╝  ██╔══██╗██║   ██║██║   ██║██║   ██║██╔══╝  ║"
        PRINT "║   ██║     ██║  ██║╚██████╔╝╚██████╔╝╚██████╔╝███████╗║"
        PRINT "║   ╚═╝     ╚═╝  ╚═╝ ╚═════╝  ╚═════╝  ╚═════╝ ╚══════╝║"
        PRINT "║                                                        ║"
        PRINT "║              Classic Arcade Action!                   ║"
        PRINT "╚════════════════════════════════════════════════════════╝"
        PRINT ""

        PRINT ""
        IF choice = 1 THEN
            PRINT "                > START GAME"
        ELSE
            PRINT "                  START GAME"
        END IF

        IF choice = 2 THEN
            PRINT "                > HIGH SCORES"
        ELSE
            PRINT "                  HIGH SCORES"
        END IF

        IF choice = 3 THEN
            PRINT "                > INSTRUCTIONS"
        ELSE
            PRINT "                  INSTRUCTIONS"
        END IF

        IF choice = 4 THEN
            PRINT "                > QUIT"
        ELSE
            PRINT "                  QUIT"
        END IF
        PRINT ""

        PRINT ""
        PRINT "         Use W/S to navigate, ENTER to select"
        PRINT ""

        key = INKEY$()
        IF LEN(key) > 0 THEN
            IF key = "w" OR key = "W" THEN
                choice = choice - 1
                IF choice < 1 THEN
                    choice = 4
                END IF
            END IF

            IF key = "s" OR key = "S" THEN
                choice = choice + 1
                IF choice > 4 THEN
                    choice = 1
                END IF
            END IF

            IF key = CHR(13) OR key = CHR(10) THEN
                REM Enter key pressed - wait for release
                SLEEP 200

                IF choice = 1 THEN
                    REM Start game
                    ClearScreen()
                    PRINT "Initializing game..."
                    InitGame()
                    PRINT "gameRunning = "; gameRunning
                    PRINT "frog.IsAlive() = "; frog.IsAlive()
                    PRINT "Press any key to start..."
                    WHILE LEN(INKEY$()) = 0
                        SLEEP 50
                    WEND
                    GameLoop()
                    SLEEP 200
                END IF

                IF choice = 2 THEN
                    REM View high scores
                    DisplayHighScores()
                    DIM tempKey AS STRING
                    tempKey = ""
                    WHILE LEN(tempKey) = 0
                        tempKey = INKEY$()
                        SLEEP 50
                    WEND
                    SLEEP 200
                END IF

                IF choice = 3 THEN
                    REM View instructions
                    ShowInstructions()
                    SLEEP 200
                END IF

                IF choice = 4 THEN
                    REM Quit
                    EXIT WHILE
                END IF
            END IF
        END IF

        SLEEP 50
    WEND
END SUB

' ====================================================================
' Show Instructions
' --------------------------------------------------------------------
' Static help screen: objective, controls, gameplay tips. Blocks on
' any key before returning to the menu.
' ====================================================================
SUB ShowInstructions()
    ClearScreen()
    PRINT ""
    PRINT "╔════════════════════════════════════════════════════════╗"
    PRINT "║                  HOW TO PLAY                          ║"
    PRINT "╚════════════════════════════════════════════════════════╝"
    PRINT ""
    PRINT "  OBJECTIVE:"
    PRINT "  Guide your frog safely across the road and river"
    PRINT "  to fill all 5 homes at the top of the screen."
    PRINT ""
    PRINT "  CONTROLS:"
    PRINT "    W - Move Up"
    PRINT "    S - Move Down"
    PRINT "    A - Move Left"
    PRINT "    D - Move Right"
    PRINT "    P - Pause Game"
    PRINT "    Q - Quit to Menu"
    PRINT ""
    PRINT "  GAMEPLAY:"
    PRINT "  • Avoid cars and trucks on the road"
    PRINT "  • Jump on logs and turtles in the river"
    PRINT "  • You drown if you fall in the water!"
    PRINT "  • Land in the home slots [ ] at the top"
    PRINT "  • Each home filled = +200 points"
    PRINT "  • You have 3 lives - use them wisely!"
    PRINT ""
    PRINT "  Press any key to return to menu..."

    WHILE LEN(INKEY$()) = 0
        SLEEP 50
    WEND
END SUB

' ============================================================================
' Main Program Entry Point
' ----------------------------------------------------------------------------
' Bootstrap sequence:
'   1. InitHighScores() — zero the in-memory table with "---" placeholders.
'   2. SaveHighScores() — writes the placeholders to disk if no file
'      exists yet. If a file already exists this is destructive, so the
'      follow-up LoadHighScores reloads the real data. This pair-dance is
'      the dialect's "touch + read" pattern.
'   3. LoadHighScores() — read the authoritative table from disk.
'   4. ShowMainMenu() — enter the menu state machine, which only returns
'      when the player chooses QUIT.
' After the menu exits: clear screen, print goodbye message, return.
' ============================================================================

InitHighScores()
SaveHighScores()
LoadHighScores()
ShowMainMenu()

ClearScreen()
PRINT ""
PRINT "Thanks for playing CLASSIC FROGGER!"
PRINT ""
