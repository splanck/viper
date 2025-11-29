REM ============================================================================
REM VIPER ROGUELIKE - A simple roguelike demo
REM ============================================================================
REM This is a simplified version that demonstrates OOP with Viper.* classes
REM ============================================================================

REM Screen dimensions
DIM SCREEN_WIDTH AS INTEGER
DIM SCREEN_HEIGHT AS INTEGER
DIM MAP_WIDTH AS INTEGER
DIM MAP_HEIGHT AS INTEGER

REM Tile types
DIM T_VOID AS INTEGER
DIM T_FLOOR AS INTEGER
DIM T_WALL AS INTEGER
DIM T_STAIRS AS INTEGER

REM Colors
DIM C_BLACK AS INTEGER
DIM C_WHITE AS INTEGER
DIM C_GRAY AS INTEGER
DIM C_YELLOW AS INTEGER
DIM C_RED AS INTEGER
DIM C_GREEN AS INTEGER
DIM C_BLUE AS INTEGER

REM Game state
DIM gRunning AS INTEGER
DIM gPlayerX AS INTEGER
DIM gPlayerY AS INTEGER
DIM gPlayerHP AS INTEGER
DIM gPlayerMaxHP AS INTEGER
DIM gPlayerGold AS INTEGER
DIM gFloor AS INTEGER
DIM gTurns AS INTEGER

REM Map data (using simple 2D array)
DIM gMap(80, 24) AS INTEGER
DIM gVisible(80, 24) AS INTEGER
DIM gExplored(80, 24) AS INTEGER

REM Monster data (simple arrays)
DIM gMonsterX(20) AS INTEGER
DIM gMonsterY(20) AS INTEGER
DIM gMonsterHP(20) AS INTEGER
DIM gMonsterType(20) AS INTEGER
DIM gMonsterCount AS INTEGER

REM Message log (using simple string for last message)
DIM gLastMsg AS STRING
DIM gLastMsgClr AS INTEGER

REM Initialize constants
SUB InitConstants()
    SCREEN_WIDTH = 80
    SCREEN_HEIGHT = 24
    MAP_WIDTH = 60
    MAP_HEIGHT = 20

    T_VOID = 0
    T_FLOOR = 1
    T_WALL = 2
    T_STAIRS = 3

    C_BLACK = 0
    C_WHITE = 7
    C_GRAY = 8
    C_YELLOW = 3
    C_RED = 1
    C_GREEN = 2
    C_BLUE = 4
END SUB

REM Add message to log
SUB AddMsg(msg AS STRING, clr AS INTEGER)
    gLastMsg = msg
    gLastMsgClr = clr
END SUB

REM Generate a simple dungeon
SUB GenerateMap()
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM roomCount AS INTEGER
    DIM rx AS INTEGER
    DIM ry AS INTEGER
    DIM rw AS INTEGER
    DIM rh AS INTEGER
    DIM i AS INTEGER
    DIM lastCX AS INTEGER
    DIM lastCY AS INTEGER
    DIM cx AS INTEGER
    DIM cy AS INTEGER

    REM Fill with walls
    FOR y = 0 TO MAP_HEIGHT - 1
        FOR x = 0 TO MAP_WIDTH - 1
            gMap(x, y) = T_WALL
            gVisible(x, y) = 0
            gExplored(x, y) = 0
        NEXT x
    NEXT y

    REM Generate rooms
    roomCount = 5 + INT(RND() * 5)
    lastCX = 0
    lastCY = 0

    FOR i = 1 TO roomCount
        rw = 4 + INT(RND() * 6)
        rh = 3 + INT(RND() * 4)
        rx = 1 + INT(RND() * (MAP_WIDTH - rw - 2))
        ry = 1 + INT(RND() * (MAP_HEIGHT - rh - 2))

        REM Carve room
        FOR y = ry TO ry + rh - 1
            FOR x = rx TO rx + rw - 1
                gMap(x, y) = T_FLOOR
            NEXT x
        NEXT y

        cx = rx + INT(rw / 2)
        cy = ry + INT(rh / 2)

        REM Connect to previous room
        IF i > 1 THEN
            REM Horizontal tunnel
            IF cx < lastCX THEN
                FOR x = cx TO lastCX
                    gMap(x, cy) = T_FLOOR
                NEXT x
            ELSE
                FOR x = lastCX TO cx
                    gMap(x, cy) = T_FLOOR
                NEXT x
            END IF

            REM Vertical tunnel
            IF cy < lastCY THEN
                FOR y = cy TO lastCY
                    gMap(lastCX, y) = T_FLOOR
                NEXT y
            ELSE
                FOR y = lastCY TO cy
                    gMap(lastCX, y) = T_FLOOR
                NEXT y
            END IF
        ELSE
            REM Place player in first room
            gPlayerX = cx
            gPlayerY = cy
        END IF

        lastCX = cx
        lastCY = cy
    NEXT i

    REM Place stairs in last room
    gMap(lastCX, lastCY) = T_STAIRS
END SUB

REM Spawn monsters
SUB SpawnMonsters()
    DIM i AS INTEGER
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM attempts AS INTEGER

    gMonsterCount = 3 + gFloor
    IF gMonsterCount > 20 THEN
        gMonsterCount = 20
    END IF

    FOR i = 0 TO gMonsterCount - 1
        attempts = 0
        WHILE attempts < 100
            x = 1 + INT(RND() * (MAP_WIDTH - 2))
            y = 1 + INT(RND() * (MAP_HEIGHT - 2))

            IF gMap(x, y) = T_FLOOR THEN
                IF x <> gPlayerX THEN
                    gMonsterX(i) = x
                    gMonsterY(i) = y
                    gMonsterHP(i) = 2 + gFloor
                    gMonsterType(i) = 1 + INT(RND() * 3)
                    EXIT WHILE
                END IF
            END IF
            attempts = attempts + 1
        WEND
    NEXT i
END SUB

REM Compute simple FOV
SUB ComputeFOV()
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM dx AS INTEGER
    DIM dy AS INTEGER
    DIM dist AS INTEGER
    DIM radius AS INTEGER

    radius = 8

    REM Clear visibility
    FOR y = 0 TO MAP_HEIGHT - 1
        FOR x = 0 TO MAP_WIDTH - 1
            gVisible(x, y) = 0
        NEXT x
    NEXT y

    REM Simple circular FOV
    FOR dy = -radius TO radius
        FOR dx = -radius TO radius
            x = gPlayerX + dx
            y = gPlayerY + dy

            IF x >= 0 THEN
                IF x < MAP_WIDTH THEN
                    IF y >= 0 THEN
                        IF y < MAP_HEIGHT THEN
                            dist = dx * dx + dy * dy
                            IF dist <= radius * radius THEN
                                gVisible(x, y) = 1
                                gExplored(x, y) = 1
                            END IF
                        END IF
                    END IF
                END IF
            END IF
        NEXT dx
    NEXT dy
END SUB

REM Render the game
SUB Render()
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM tile AS INTEGER
    DIM sym AS STRING
    DIM clr AS INTEGER
    DIM i AS INTEGER
    DIM sb AS OBJECT
    DIM row AS INTEGER
    DIM col AS INTEGER

    sb = NEW Viper.Text.StringBuilder()

    CLS

    REM Draw map
    FOR y = 0 TO MAP_HEIGHT - 1
        row = y + 1
        LOCATE row, 1

        FOR x = 0 TO MAP_WIDTH - 1
            sym = " "
            clr = C_BLACK

            IF gVisible(x, y) = 1 THEN
                tile = gMap(x, y)
                IF tile = T_FLOOR THEN
                    sym = "."
                    clr = C_WHITE
                ELSEIF tile = T_WALL THEN
                    sym = "#"
                    clr = C_GRAY
                ELSEIF tile = T_STAIRS THEN
                    sym = ">"
                    clr = C_YELLOW
                END IF
            ELSEIF gExplored(x, y) = 1 THEN
                tile = gMap(x, y)
                IF tile = T_FLOOR THEN
                    sym = "."
                    clr = C_GRAY
                ELSEIF tile = T_WALL THEN
                    sym = "#"
                    clr = C_GRAY
                ELSEIF tile = T_STAIRS THEN
                    sym = ">"
                    clr = C_GRAY
                END IF
            END IF

            COLOR clr, C_BLACK
            PRINT sym;
        NEXT x
    NEXT y

    REM Draw monsters
    FOR i = 0 TO gMonsterCount - 1
        IF gMonsterHP(i) > 0 THEN
            IF gVisible(gMonsterX(i), gMonsterY(i)) = 1 THEN
                row = gMonsterY(i) + 1
                col = gMonsterX(i) + 1
                LOCATE row, col
                IF gMonsterType(i) = 1 THEN
                    COLOR C_GREEN, C_BLACK
                    PRINT "g";
                ELSEIF gMonsterType(i) = 2 THEN
                    COLOR C_RED, C_BLACK
                    PRINT "o";
                ELSE
                    COLOR C_YELLOW, C_BLACK
                    PRINT "r";
                END IF
            END IF
        END IF
    NEXT i

    REM Draw player
    row = gPlayerY + 1
    col = gPlayerX + 1
    LOCATE row, col
    COLOR C_WHITE, C_BLACK
    PRINT "@";

    REM Draw status bar
    row = MAP_HEIGHT + 2
    LOCATE row, 1
    COLOR C_WHITE, C_BLACK
    sb.Clear()
    sb.Append("HP: ")
    sb.Append(STR$(gPlayerHP))
    sb.Append("/")
    sb.Append(STR$(gPlayerMaxHP))
    sb.Append("  Gold: ")
    sb.Append(STR$(gPlayerGold))
    sb.Append("  Floor: ")
    sb.Append(STR$(gFloor))
    sb.Append("  Turns: ")
    sb.Append(STR$(gTurns))
    PRINT sb.ToString()

    REM Draw last message
    row = MAP_HEIGHT + 3
    LOCATE row, 1
    IF LEN(gLastMsg) > 0 THEN
        COLOR gLastMsgClr, C_BLACK
        PRINT gLastMsg
    END IF

    REM Draw help
    row = MAP_HEIGHT + 4
    LOCATE row, 1
    COLOR C_GRAY, C_BLACK
    PRINT "Move: hjkl/arrows  >: stairs  q: quit"
END SUB

REM Get monster at position
FUNCTION GetMonsterAt(x AS INTEGER, y AS INTEGER) AS INTEGER
    DIM i AS INTEGER

    FOR i = 0 TO gMonsterCount - 1
        IF gMonsterHP(i) > 0 THEN
            IF gMonsterX(i) = x THEN
                IF gMonsterY(i) = y THEN
                    GetMonsterAt = i
                    EXIT FUNCTION
                END IF
            END IF
        END IF
    NEXT i

    GetMonsterAt = -1
END FUNCTION

REM Attack a monster
SUB AttackMonster(idx AS INTEGER)
    DIM damage AS INTEGER
    DIM sb AS OBJECT

    sb = NEW Viper.Text.StringBuilder()

    damage = 1 + INT(RND() * 4)
    gMonsterHP(idx) = gMonsterHP(idx) - damage

    sb.Append("You hit the monster for ")
    sb.Append(STR$(damage))
    sb.Append(" damage!")
    AddMsg(sb.ToString(), C_WHITE)

    IF gMonsterHP(idx) <= 0 THEN
        AddMsg("The monster dies!", C_GREEN)
        gPlayerGold = gPlayerGold + 5 + INT(RND() * 10)
    END IF
END SUB

REM Monster attacks player
SUB MonsterAttacks(idx AS INTEGER)
    DIM damage AS INTEGER
    DIM sb AS OBJECT

    sb = NEW Viper.Text.StringBuilder()

    damage = 1 + INT(RND() * 3)
    gPlayerHP = gPlayerHP - damage

    sb.Append("The monster hits you for ")
    sb.Append(STR$(damage))
    sb.Append(" damage!")
    AddMsg(sb.ToString(), C_RED)
END SUB

REM Move monsters
SUB MoveMonsters()
    DIM i AS INTEGER
    DIM dx AS INTEGER
    DIM dy AS INTEGER
    DIM nx AS INTEGER
    DIM ny AS INTEGER
    DIM distX AS INTEGER
    DIM distY AS INTEGER

    FOR i = 0 TO gMonsterCount - 1
        IF gMonsterHP(i) > 0 THEN
            distX = gPlayerX - gMonsterX(i)
            distY = gPlayerY - gMonsterY(i)

            REM Only move if player visible
            IF gVisible(gMonsterX(i), gMonsterY(i)) = 1 THEN
                REM Adjacent to player? Attack!
                DIM attacked AS INTEGER
                attacked = 0
                IF distX >= -1 THEN
                    IF distX <= 1 THEN
                        IF distY >= -1 THEN
                            IF distY <= 1 THEN
                                MonsterAttacks(i)
                                attacked = 1
                            END IF
                        END IF
                    END IF
                END IF

                IF attacked = 0 THEN
                    REM Move toward player
                    dx = 0
                    dy = 0
                    IF distX > 0 THEN
                        dx = 1
                    ELSEIF distX < 0 THEN
                        dx = -1
                    END IF
                    IF distY > 0 THEN
                        dy = 1
                    ELSEIF distY < 0 THEN
                        dy = -1
                    END IF

                    nx = gMonsterX(i) + dx
                    ny = gMonsterY(i) + dy

                    IF gMap(nx, ny) = T_FLOOR THEN
                        IF GetMonsterAt(nx, ny) = -1 THEN
                            gMonsterX(i) = nx
                            gMonsterY(i) = ny
                        END IF
                    END IF
                END IF
            END IF
        END IF
    NEXT i
END SUB

REM Try to move player
FUNCTION TryMove(dx AS INTEGER, dy AS INTEGER) AS INTEGER
    DIM nx AS INTEGER
    DIM ny AS INTEGER
    DIM tile AS INTEGER
    DIM monIdx AS INTEGER

    nx = gPlayerX + dx
    ny = gPlayerY + dy

    IF nx < 0 THEN
        TryMove = 0
        EXIT FUNCTION
    END IF
    IF nx >= MAP_WIDTH THEN
        TryMove = 0
        EXIT FUNCTION
    END IF
    IF ny < 0 THEN
        TryMove = 0
        EXIT FUNCTION
    END IF
    IF ny >= MAP_HEIGHT THEN
        TryMove = 0
        EXIT FUNCTION
    END IF

    tile = gMap(nx, ny)

    IF tile = T_WALL THEN
        TryMove = 0
        EXIT FUNCTION
    END IF

    REM Check for monster
    monIdx = GetMonsterAt(nx, ny)
    IF monIdx >= 0 THEN
        AttackMonster(monIdx)
        TryMove = 1
        EXIT FUNCTION
    END IF

    REM Move player
    gPlayerX = nx
    gPlayerY = ny
    TryMove = 1
END FUNCTION

REM Use stairs
SUB UseStairs()
    IF gMap(gPlayerX, gPlayerY) = T_STAIRS THEN
        gFloor = gFloor + 1
        AddMsg("You descend to floor " + STR$(gFloor) + "...", C_YELLOW)
        GenerateMap()
        SpawnMonsters()
        gPlayerHP = gPlayerHP + 5
        IF gPlayerHP > gPlayerMaxHP THEN
            gPlayerHP = gPlayerMaxHP
        END IF
    ELSE
        AddMsg("There are no stairs here.", C_GRAY)
    END IF
END SUB

REM Process one turn
SUB ProcessTurn()
    gTurns = gTurns + 1
    MoveMonsters()
    ComputeFOV()
END SUB

REM Main game loop
SUB GameLoop()
    DIM key AS STRING
    DIM moved AS INTEGER

    gRunning = 1

    WHILE gRunning = 1
        Render()

        key = INKEY$()

        IF LEN(key) > 0 THEN
            moved = 0

            IF key = "h" THEN
                moved = TryMove(-1, 0)
            ELSEIF key = "l" THEN
                moved = TryMove(1, 0)
            ELSEIF key = "k" THEN
                moved = TryMove(0, -1)
            ELSEIF key = "j" THEN
                moved = TryMove(0, 1)
            ELSEIF key = "y" THEN
                moved = TryMove(-1, -1)
            ELSEIF key = "u" THEN
                moved = TryMove(1, -1)
            ELSEIF key = "b" THEN
                moved = TryMove(-1, 1)
            ELSEIF key = "n" THEN
                moved = TryMove(1, 1)
            ELSEIF key = "." THEN
                moved = 1
            ELSEIF key = ">" THEN
                UseStairs()
                moved = 1
            ELSEIF key = "q" THEN
                gRunning = 0
            END IF

            IF moved = 1 THEN
                ProcessTurn()
            END IF

            IF gPlayerHP <= 0 THEN
                AddMsg("You have died! Press any key...", C_RED)
                Render()
                WHILE LEN(INKEY$()) = 0
                    SLEEP 10
                WEND
                gRunning = 0
            END IF
        END IF

        SLEEP 16
    WEND
END SUB

REM Main entry point
SUB Main()
    RANDOMIZE TIMER()

    InitConstants()

    REM Initialize message log
    gLastMsg = ""
    gLastMsgClr = C_WHITE

    REM Initialize player
    gPlayerHP = 20
    gPlayerMaxHP = 20
    gPlayerGold = 0
    gFloor = 1
    gTurns = 0

    REM Generate first level
    GenerateMap()
    SpawnMonsters()
    ComputeFOV()

    AddMsg("Welcome to the dungeon! Find the stairs to descend.", C_YELLOW)

    REM Hide cursor and set up terminal
    CURSOR OFF

    REM Run game
    GameLoop()

    REM Cleanup
    CURSOR ON
    CLS

    PRINT "Thanks for playing!"
    PRINT "Final score: Floor "; gFloor; ", Gold "; gPlayerGold; ", Turns "; gTurns
END SUB

Main()
