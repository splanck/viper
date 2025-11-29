'==============================================================================
' CENTIPEDE
' A recreation of the classic Atari arcade game
' Written in Viper BASIC using OOP classes
'==============================================================================

' Screen constants
CONST SCREEN_WIDTH AS INTEGER = 80
CONST SCREEN_HEIGHT AS INTEGER = 25
CONST PLAY_LEFT AS INTEGER = 1
CONST PLAY_RIGHT AS INTEGER = 78
CONST PLAY_TOP AS INTEGER = 2
CONST PLAY_BOTTOM AS INTEGER = 24
CONST PLAYER_ZONE_TOP AS INTEGER = 20
CONST FRAME_TIME AS INTEGER = 50

' Entity limits
CONST MAX_SEG AS INTEGER = 12
CONST MAX_MUSH AS INTEGER = 30

' Game constants
CONST CENTIPEDE_LEN AS INTEGER = 10
CONST MUSH_COUNT AS INTEGER = 20
CONST MUSH_HP AS INTEGER = 4
CONST CENT_SPEED AS INTEGER = 8
CONST BULLET_SPEED AS INTEGER = 25
CONST LIVES_START AS INTEGER = 3

' Colors
CONST CLR_BLACK AS INTEGER = 0
CONST CLR_RED AS INTEGER = 1
CONST CLR_GREEN AS INTEGER = 2
CONST CLR_YELLOW AS INTEGER = 3
CONST CLR_CYAN AS INTEGER = 6
CONST CLR_WHITE AS INTEGER = 7
CONST CLR_BRED AS INTEGER = 9
CONST CLR_BGREEN AS INTEGER = 10
CONST CLR_BYELLOW AS INTEGER = 11
CONST CLR_BWHITE AS INTEGER = 15

' Game states
CONST ST_TITLE AS INTEGER = 0
CONST ST_PLAY AS INTEGER = 1
CONST ST_PAUSE AS INTEGER = 2
CONST ST_OVER AS INTEGER = 3
CONST ST_WIN AS INTEGER = 4

'==============================================================================
' Segment class - represents one segment of the centipede
'==============================================================================
CLASS Segment
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM dx AS INTEGER
    DIM isHead AS INTEGER
    DIM active AS INTEGER
    DIM dropCount AS INTEGER
    DIM moveTimer AS INTEGER

    SUB Init(startX AS INTEGER, startY AS INTEGER, dir AS INTEGER, head AS INTEGER)
        x = startX
        y = startY
        dx = dir
        isHead = head
        active = 1
        dropCount = 0
        moveTimer = 0
    END SUB

    FUNCTION IsActive() AS INTEGER
        IsActive = active
    END FUNCTION

    SUB Deactivate()
        active = 0
    END SUB

    SUB SetHead(h AS INTEGER)
        isHead = h
    END SUB

    SUB Move(speed AS INTEGER)
        IF active = 0 THEN EXIT SUB
        moveTimer = moveTimer + speed
        IF moveTimer >= 10 THEN
            moveTimer = moveTimer - 10
            IF dropCount > 0 THEN
                y = y + 1
                dropCount = dropCount - 1
                IF dropCount = 0 THEN dx = -dx
            ELSE
                x = x + dx
            END IF
            ' Boundary checks
            IF x < PLAY_LEFT THEN
                x = PLAY_LEFT
                dropCount = 1
            END IF
            IF x > PLAY_RIGHT THEN
                x = PLAY_RIGHT
                dropCount = 1
            END IF
            IF y > PLAY_BOTTOM THEN
                y = PLAY_TOP
                dx = -dx
            END IF
        END IF
    END SUB

    SUB StartDrop()
        IF dropCount = 0 THEN dropCount = 1
    END SUB
END CLASS

'==============================================================================
' Mushroom class
'==============================================================================
CLASS Mushroom
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM hp AS INTEGER
    DIM active AS INTEGER

    SUB Init(startX AS INTEGER, startY AS INTEGER)
        x = startX
        y = startY
        hp = MUSH_HP
        active = 1
    END SUB

    FUNCTION IsActive() AS INTEGER
        IsActive = active
    END FUNCTION

    FUNCTION Hit() AS INTEGER
        hp = hp - 1
        IF hp <= 0 THEN
            active = 0
            Hit = 1
        ELSE
            Hit = 0
        END IF
    END FUNCTION
END CLASS

'==============================================================================
' Player class
'==============================================================================
CLASS Player
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM lives AS INTEGER
    DIM invulnTimer AS INTEGER

    SUB Init()
        x = SCREEN_WIDTH \ 2
        y = PLAY_BOTTOM - 1
        lives = LIVES_START
        invulnTimer = 0
    END SUB

    SUB Respawn()
        x = SCREEN_WIDTH \ 2
        y = PLAY_BOTTOM - 1
        invulnTimer = 60
    END SUB

    SUB UpdateInvuln()
        IF invulnTimer > 0 THEN invulnTimer = invulnTimer - 1
    END SUB

    FUNCTION IsInvulnerable() AS INTEGER
        IF invulnTimer > 0 THEN
            IsInvulnerable = 1
        ELSE
            IsInvulnerable = 0
        END IF
    END FUNCTION

    SUB LoseLife()
        lives = lives - 1
    END SUB
END CLASS

'==============================================================================
' Bullet class
'==============================================================================
CLASS Bullet
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM active AS INTEGER
    DIM moveTimer AS INTEGER

    SUB Fire(startX AS INTEGER, startY AS INTEGER)
        x = startX
        y = startY - 1
        active = 1
        moveTimer = 0
    END SUB

    FUNCTION IsActive() AS INTEGER
        IsActive = active
    END FUNCTION

    SUB Deactivate()
        active = 0
    END SUB

    SUB Move(speed AS INTEGER)
        IF active = 0 THEN EXIT SUB
        moveTimer = moveTimer + speed
        IF moveTimer >= 10 THEN
            moveTimer = moveTimer - 10
            y = y - 1
            IF y < PLAY_TOP THEN active = 0
        END IF
    END SUB
END CLASS

'==============================================================================
' Game state
'==============================================================================
DIM gSegments(11) AS Segment
DIM gMushrooms(29) AS Mushroom
DIM gPlayer AS Player
DIM gBullet AS Bullet
DIM gSegCount AS INTEGER
DIM gMushCount AS INTEGER
DIM gGameState AS INTEGER
DIM gScore AS INTEGER
DIM gLevel AS INTEGER
DIM gLastColor AS INTEGER

'==============================================================================
' Drawing functions
'==============================================================================
SUB DrawAt(row AS INTEGER, col AS INTEGER, ch AS STRING, clr AS INTEGER)
    IF row >= 1 THEN
        IF row <= SCREEN_HEIGHT THEN
            IF col >= 1 THEN
                IF col <= SCREEN_WIDTH THEN
                    Viper.Terminal.SetPosition(row, col)
                    IF clr <> gLastColor THEN
                        Viper.Terminal.SetColor(clr, CLR_BLACK)
                        gLastColor = clr
                    END IF
                    PRINT ch;
                END IF
            END IF
        END IF
    END IF
END SUB

SUB DrawStr(row AS INTEGER, col AS INTEGER, txt AS STRING, clr AS INTEGER)
    DIM i AS INTEGER
    DIM tlen AS INTEGER
    tlen = LEN(txt)
    Viper.Terminal.SetPosition(row, col)
    IF clr <> gLastColor THEN
        Viper.Terminal.SetColor(clr, CLR_BLACK)
        gLastColor = clr
    END IF
    FOR i = 1 TO tlen
        PRINT MID$(txt, i, 1);
    NEXT i
END SUB

SUB DrawCenter(row AS INTEGER, txt AS STRING, clr AS INTEGER)
    DIM col AS INTEGER
    col = (SCREEN_WIDTH - LEN(txt)) \ 2 + 1
    DrawStr(row, col, txt, clr)
END SUB

'==============================================================================
' Mushroom collision check
'==============================================================================
FUNCTION MushAt(mx AS INTEGER, my AS INTEGER) AS INTEGER
    DIM i AS INTEGER
    FOR i = 0 TO gMushCount - 1
        IF gMushrooms(i).active = 1 THEN
            IF gMushrooms(i).x = mx THEN
                IF gMushrooms(i).y = my THEN
                    MushAt = 1
                    EXIT FUNCTION
                END IF
            END IF
        END IF
    NEXT i
    MushAt = 0
END FUNCTION

'==============================================================================
' Add a new mushroom
'==============================================================================
SUB AddMush(x AS INTEGER, y AS INTEGER)
    DIM i AS INTEGER
    ' Find inactive slot
    FOR i = 0 TO gMushCount - 1
        IF gMushrooms(i).active = 0 THEN
            gMushrooms(i).Init(x, y)
            EXIT SUB
        END IF
    NEXT i
    ' Expand if room
    IF gMushCount < MAX_MUSH THEN
        gMushrooms(gMushCount) = NEW Mushroom()
        gMushrooms(gMushCount).Init(x, y)
        gMushCount = gMushCount + 1
    END IF
END SUB

'==============================================================================
' Count active segments
'==============================================================================
FUNCTION ActiveSegs() AS INTEGER
    DIM i AS INTEGER
    DIM cnt AS INTEGER
    cnt = 0
    FOR i = 0 TO gSegCount - 1
        IF gSegments(i).active = 1 THEN cnt = cnt + 1
    NEXT i
    ActiveSegs = cnt
END FUNCTION

'==============================================================================
' Kill a segment
'==============================================================================
SUB KillSeg(idx AS INTEGER)
    DIM nextIdx AS INTEGER
    IF gSegments(idx).isHead = 1 THEN
        gScore = gScore + 100
    ELSE
        gScore = gScore + 10
    END IF
    ' Leave mushroom
    IF MushAt(gSegments(idx).x, gSegments(idx).y) = 0 THEN
        AddMush(gSegments(idx).x, gSegments(idx).y)
    END IF
    gSegments(idx).Deactivate()
    ' Promote next segment to head
    nextIdx = idx + 1
    IF nextIdx < gSegCount THEN
        IF gSegments(nextIdx).active = 1 THEN
            gSegments(nextIdx).SetHead(1)
        END IF
    END IF
    IF ActiveSegs() = 0 THEN
        gGameState = ST_WIN
    END IF
END SUB

'==============================================================================
' Initialize level
'==============================================================================
SUB InitLevel()
    DIM i AS INTEGER
    DIM sx AS INTEGER
    DIM mx AS INTEGER
    DIM my AS INTEGER
    DIM placed AS INTEGER
    DIM randVal AS INTEGER

    ' Init segments
    gSegCount = CENTIPEDE_LEN
    IF gSegCount > MAX_SEG THEN gSegCount = MAX_SEG
    sx = PLAY_RIGHT - 3
    FOR i = 0 TO gSegCount - 1
        gSegments(i) = NEW Segment()
        IF i = 0 THEN
            gSegments(i).Init(sx - i, PLAY_TOP, -1, 1)
        ELSE
            gSegments(i).Init(sx - i, PLAY_TOP, -1, 0)
        END IF
    NEXT i

    ' Init mushrooms (only on level 1)
    IF gLevel = 1 THEN
        gMushCount = MUSH_COUNT
        IF gMushCount > MAX_MUSH THEN gMushCount = MAX_MUSH
        FOR i = 0 TO gMushCount - 1
            placed = 0
            DO WHILE placed = 0
                randVal = INT(RND() * (PLAY_RIGHT - PLAY_LEFT))
                mx = PLAY_LEFT + randVal
                randVal = INT(RND() * 12)
                my = PLAY_TOP + 2 + randVal
                IF MushAt(mx, my) = 0 THEN
                    gMushrooms(i) = NEW Mushroom()
                    gMushrooms(i).Init(mx, my)
                    placed = 1
                END IF
            LOOP
        NEXT i
    END IF

    ' Init player
    gPlayer = NEW Player()
    gPlayer.Init()

    ' Init bullet
    gBullet = NEW Bullet()
    gBullet.active = 0
END SUB

'==============================================================================
' Update segments
'==============================================================================
SUB UpdateSegs()
    DIM i AS INTEGER
    DIM speed AS INTEGER
    speed = CENT_SPEED + gLevel * 2
    FOR i = 0 TO gSegCount - 1
        IF gSegments(i).active = 1 THEN
            gSegments(i).Move(speed)
            ' Check mushroom collision
            IF MushAt(gSegments(i).x, gSegments(i).y) = 1 THEN
                gSegments(i).StartDrop()
            END IF
        END IF
    NEXT i
END SUB

'==============================================================================
' Update bullet
'==============================================================================
SUB UpdateBullet()
    DIM i AS INTEGER
    IF gBullet.active = 0 THEN EXIT SUB
    gBullet.Move(BULLET_SPEED)
    IF gBullet.active = 0 THEN EXIT SUB

    ' Check segment hits
    FOR i = 0 TO gSegCount - 1
        IF gSegments(i).active = 1 THEN
            IF gBullet.x = gSegments(i).x THEN
                IF gBullet.y = gSegments(i).y THEN
                    KillSeg(i)
                    gBullet.Deactivate()
                    EXIT SUB
                END IF
            END IF
        END IF
    NEXT i

    ' Check mushroom hits
    FOR i = 0 TO gMushCount - 1
        IF gMushrooms(i).active = 1 THEN
            IF gBullet.x = gMushrooms(i).x THEN
                IF gBullet.y = gMushrooms(i).y THEN
                    IF gMushrooms(i).Hit() = 1 THEN
                        gScore = gScore + 1
                    END IF
                    gBullet.Deactivate()
                    EXIT SUB
                END IF
            END IF
        END IF
    NEXT i
END SUB

'==============================================================================
' Check player collisions
'==============================================================================
SUB CheckCollisions()
    DIM i AS INTEGER
    gPlayer.UpdateInvuln()
    IF gPlayer.IsInvulnerable() = 1 THEN EXIT SUB

    FOR i = 0 TO gSegCount - 1
        IF gSegments(i).active = 1 THEN
            IF gPlayer.x = gSegments(i).x THEN
                IF gPlayer.y = gSegments(i).y THEN
                    gPlayer.LoseLife()
                    Viper.Terminal.Bell()
                    IF gPlayer.lives <= 0 THEN
                        gGameState = ST_OVER
                    ELSE
                        gPlayer.Respawn()
                        gBullet.Deactivate()
                    END IF
                    EXIT SUB
                END IF
            END IF
        END IF
    NEXT i
END SUB

'==============================================================================
' Draw game
'==============================================================================
SUB DrawGame()
    DIM i AS INTEGER
    DIM sp AS STRING
    DIM clr AS INTEGER
    DIM sb AS Viper.Text.StringBuilder

    ' HUD
    sb = NEW Viper.Text.StringBuilder()
    sb.Append("Score:")
    sb.Append(STR$(gScore))
    DrawStr(1, 2, sb.ToString(), CLR_BYELLOW)

    sb = NEW Viper.Text.StringBuilder()
    sb.Append("Lives:")
    sb.Append(STR$(gPlayer.lives))
    DrawStr(1, 25, sb.ToString(), CLR_BWHITE)

    sb = NEW Viper.Text.StringBuilder()
    sb.Append("Level:")
    sb.Append(STR$(gLevel))
    DrawStr(1, 45, sb.ToString(), CLR_WHITE)

    ' Mushrooms
    FOR i = 0 TO gMushCount - 1
        IF gMushrooms(i).active = 1 THEN
            IF gMushrooms(i).hp >= 3 THEN
                sp = "#"
                clr = CLR_GREEN
            ELSEIF gMushrooms(i).hp = 2 THEN
                sp = "@"
                clr = CLR_YELLOW
            ELSE
                sp = "+"
                clr = CLR_RED
            END IF
            DrawAt(gMushrooms(i).y, gMushrooms(i).x, sp, clr)
        END IF
    NEXT i

    ' Segments
    FOR i = 0 TO gSegCount - 1
        IF gSegments(i).active = 1 THEN
            IF gSegments(i).isHead = 1 THEN
                DrawAt(gSegments(i).y, gSegments(i).x, "O", CLR_BRED)
            ELSE
                DrawAt(gSegments(i).y, gSegments(i).x, "o", CLR_BGREEN)
            END IF
        END IF
    NEXT i

    ' Bullet
    IF gBullet.active = 1 THEN
        DrawAt(gBullet.y, gBullet.x, "|", CLR_BWHITE)
    END IF

    ' Player
    IF gPlayer.invulnTimer > 0 THEN
        IF (gPlayer.invulnTimer MOD 4) < 2 THEN
            DrawAt(gPlayer.y, gPlayer.x, "A", CLR_BYELLOW)
        END IF
    ELSE
        DrawAt(gPlayer.y, gPlayer.x, "A", CLR_BYELLOW)
    END IF
END SUB

'==============================================================================
' Handle input
'==============================================================================
SUB HandleInput(key AS STRING)
    DIM nx AS INTEGER
    DIM ny AS INTEGER

    IF key = "a" THEN
        nx = gPlayer.x - 1
        IF nx >= PLAY_LEFT THEN
            IF MushAt(nx, gPlayer.y) = 0 THEN gPlayer.x = nx
        END IF
    ELSEIF key = "d" THEN
        nx = gPlayer.x + 1
        IF nx <= PLAY_RIGHT THEN
            IF MushAt(nx, gPlayer.y) = 0 THEN gPlayer.x = nx
        END IF
    ELSEIF key = "w" THEN
        ny = gPlayer.y - 1
        IF ny >= PLAYER_ZONE_TOP THEN
            IF MushAt(gPlayer.x, ny) = 0 THEN gPlayer.y = ny
        END IF
    ELSEIF key = "s" THEN
        ny = gPlayer.y + 1
        IF ny <= PLAY_BOTTOM THEN
            IF MushAt(gPlayer.x, ny) = 0 THEN gPlayer.y = ny
        END IF
    ELSEIF key = " " THEN
        IF gBullet.active = 0 THEN
            gBullet.Fire(gPlayer.x, gPlayer.y)
        END IF
    ELSEIF key = "p" THEN
        gGameState = ST_PAUSE
    END IF
END SUB

'==============================================================================
' Main game
'==============================================================================
Main()

SUB Main()
    DIM running AS INTEGER
    DIM key AS STRING

    ' Init terminal
    Viper.Terminal.SetCursorVisible(0)
    Viper.Terminal.SetAltScreen(1)
    RANDOMIZE
    gLastColor = -1

    gGameState = ST_TITLE
    gScore = 0
    gLevel = 1
    running = 1

    DO WHILE running = 1
        Viper.Terminal.Clear()
        gLastColor = -1
        key = Viper.Terminal.InKey()

        IF key = "q" THEN running = 0

        IF gGameState = ST_TITLE THEN
            DrawCenter(5, "=== CENTIPEDE ===", CLR_BGREEN)
            DrawCenter(10, "W/A/S/D - Move", CLR_YELLOW)
            DrawCenter(11, "SPACE - Shoot", CLR_YELLOW)
            DrawCenter(12, "P - Pause  Q - Quit", CLR_YELLOW)
            DrawCenter(16, "Press SPACE to start", CLR_BWHITE)
            IF key = " " THEN
                gScore = 0
                gLevel = 1
                InitLevel()
                gGameState = ST_PLAY
            END IF

        ELSEIF gGameState = ST_PLAY THEN
            HandleInput(key)
            UpdateSegs()
            UpdateBullet()
            CheckCollisions()
            DrawGame()

        ELSEIF gGameState = ST_PAUSE THEN
            DrawGame()
            DrawCenter(12, "PAUSED - Press P", CLR_CYAN)
            IF key = "p" THEN gGameState = ST_PLAY

        ELSEIF gGameState = ST_OVER THEN
            DrawGame()
            DrawCenter(10, "GAME OVER", CLR_BRED)
            DrawCenter(12, "Press SPACE", CLR_WHITE)
            IF key = " " THEN gGameState = ST_TITLE

        ELSEIF gGameState = ST_WIN THEN
            DrawGame()
            DrawCenter(10, "LEVEL COMPLETE!", CLR_BGREEN)
            DrawCenter(12, "Press SPACE", CLR_WHITE)
            IF key = " " THEN
                gLevel = gLevel + 1
                InitLevel()
                gGameState = ST_PLAY
            END IF
        END IF

        Viper.Time.SleepMs(FRAME_TIME)
    LOOP

    Viper.Terminal.SetCursorVisible(1)
    Viper.Terminal.SetAltScreen(0)
    Viper.Terminal.Clear()
END SUB
