REM ============================================================================
REM CENTIPEDE - Arcade Clone
REM ============================================================================
REM Controls: WASD/Arrows=Move, Space=Fire, P=Pause, Q=Quit
REM ============================================================================

REM Screen dimensions
DIM SCREEN_WIDTH AS INTEGER
DIM SCREEN_HEIGHT AS INTEGER
DIM PLAY_TOP AS INTEGER
DIM PLAYER_TOP AS INTEGER
SCREEN_WIDTH = 80
SCREEN_HEIGHT = 24
PLAY_TOP = 2
PLAYER_TOP = 19

REM Colors
DIM C_BLACK AS INTEGER
DIM C_RED AS INTEGER
DIM C_GREEN AS INTEGER
DIM C_YELLOW AS INTEGER
DIM C_CYAN AS INTEGER
DIM C_WHITE AS INTEGER
DIM C_BRED AS INTEGER
DIM C_BGREEN AS INTEGER
DIM C_BYELLOW AS INTEGER
DIM C_BCYAN AS INTEGER
C_BLACK = 0
C_RED = 1
C_GREEN = 2
C_YELLOW = 3
C_CYAN = 6
C_WHITE = 7
C_BRED = 9
C_BGREEN = 10
C_BYELLOW = 11
C_BCYAN = 14

REM Game constants
DIM MAX_MUSH AS INTEGER
DIM MAX_SEG AS INTEGER
DIM MAX_BULL AS INTEGER
DIM INIT_MUSH AS INTEGER
DIM INIT_SEG AS INTEGER
DIM MUSH_HP AS INTEGER
MAX_MUSH = 50
MAX_SEG = 16
MAX_BULL = 2
INIT_MUSH = 25
INIT_SEG = 10
MUSH_HP = 4

REM Direction
DIM D_LEFT AS INTEGER
DIM D_RIGHT AS INTEGER
D_LEFT = 0
D_RIGHT = 1

REM Mushroom data
DIM mX(50) AS INTEGER
DIM mY(50) AS INTEGER
DIM mHP(50) AS INTEGER
DIM mPoison(50) AS INTEGER
DIM mCount AS INTEGER

REM Centipede data
DIM sX(16) AS SINGLE
DIM sY(16) AS SINGLE
DIM sDir(16) AS INTEGER
DIM sHead(16) AS INTEGER
DIM sOn(16) AS INTEGER
DIM sCount AS INTEGER

REM Bullet data
DIM bX(2) AS INTEGER
DIM bY(2) AS SINGLE
DIM bOn(2) AS INTEGER

REM Player data
DIM pX AS INTEGER
DIM pY AS INTEGER
DIM pLives AS INTEGER
DIM pInvuln AS INTEGER

REM Game state
DIM gScore AS INTEGER
DIM gLevel AS INTEGER
DIM gOver AS INTEGER
DIM gPaused AS INTEGER
DIM gRunning AS INTEGER
DIM gFrame AS INTEGER
DIM gLastTime AS INTEGER

REM ============================================================================
REM TERMINAL HELPERS
REM ============================================================================

SUB TSetPos(row AS INTEGER, col AS INTEGER)
    Viper.Terminal.SetPosition(row, col)
END SUB

SUB TSetClr(fg AS INTEGER)
    Viper.Terminal.SetColor(fg, C_BLACK)
END SUB

SUB TClear()
    Viper.Terminal.Clear()
END SUB

SUB THideCursor()
    Viper.Terminal.SetCursorVisible(0)
END SUB

SUB TShowCursor()
    Viper.Terminal.SetCursorVisible(1)
END SUB

FUNCTION TGetKey() AS STRING
    TGetKey = Viper.Terminal.InKey()
END FUNCTION

SUB TBeep()
    Viper.Terminal.Bell()
END SUB

SUB TWait(ms AS INTEGER)
    Viper.Time.SleepMs(ms)
END SUB

FUNCTION TGetTicks() AS INTEGER
    TGetTicks = Viper.Time.GetTickCount()
END FUNCTION

REM ============================================================================
REM RANDOM HELPERS - using RND() builtin
REM ============================================================================

FUNCTION RndInt(maxVal AS INTEGER) AS INTEGER
    RndInt = INT(RND() * maxVal)
END FUNCTION

REM ============================================================================
REM GAME INIT
REM ============================================================================

SUB InitGame()
    gScore = 0
    gLevel = 1
    gOver = 0
    gPaused = 0
    gRunning = 1
    gFrame = 0
    
    pX = SCREEN_WIDTH / 2
    pY = SCREEN_HEIGHT - 2
    pLives = 3
    pInvuln = 0
    
    mCount = 0
    sCount = 0
    
    DIM i AS INTEGER
    FOR i = 0 TO MAX_BULL - 1
        bOn(i) = 0
    NEXT i
    
    PlaceMushrooms()
    SpawnCentipede()
END SUB

SUB PlaceMushrooms()
    DIM i AS INTEGER
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM ok AS INTEGER
    DIM j AS INTEGER
    
    mCount = 0
    FOR i = 0 TO INIT_MUSH - 1
        ok = 0
        WHILE ok = 0
            x = RndInt(SCREEN_WIDTH - 4) + 2
            y = RndInt(PLAYER_TOP - PLAY_TOP - 2) + PLAY_TOP + 1
            ok = 1
            FOR j = 0 TO mCount - 1
                IF mX(j) = x THEN
                    IF mY(j) = y THEN
                        ok = 0
                    END IF
                END IF
            NEXT j
        WEND
        mX(mCount) = x
        mY(mCount) = y
        mHP(mCount) = MUSH_HP
        mPoison(mCount) = 0
        mCount = mCount + 1
    NEXT i
END SUB

SUB SpawnCentipede()
    DIM i AS INTEGER
    DIM len AS INTEGER
    DIM startX AS INTEGER
    
    len = INIT_SEG + (gLevel - 1) * 2
    IF len > MAX_SEG THEN
        len = MAX_SEG
    END IF
    startX = SCREEN_WIDTH - 2
    
    FOR i = 0 TO len - 1
        sX(i) = startX - i
        sY(i) = PLAY_TOP
        sDir(i) = D_LEFT
        IF i = 0 THEN
            sHead(i) = 1
        ELSE
            sHead(i) = 0
        END IF
        sOn(i) = 1
    NEXT i
    sCount = len
END SUB

REM ============================================================================
REM COLLISION
REM ============================================================================

FUNCTION FindMush(x AS INTEGER, y AS INTEGER) AS INTEGER
    DIM i AS INTEGER
    FindMush = -1
    FOR i = 0 TO mCount - 1
        IF mX(i) = x THEN
            IF mY(i) = y THEN
                IF mHP(i) > 0 THEN
                    FindMush = i
                    EXIT FUNCTION
                END IF
            END IF
        END IF
    NEXT i
END FUNCTION

FUNCTION HitPlayer(x AS INTEGER, y AS INTEGER) AS INTEGER
    HitPlayer = 0
    IF pInvuln > 0 THEN
        EXIT FUNCTION
    END IF
    IF x = pX THEN
        IF y = pY THEN
            HitPlayer = 1
        END IF
    END IF
END FUNCTION

REM ============================================================================
REM UPDATES
REM ============================================================================

SUB UpdateCentipede()
    DIM i AS INTEGER
    DIM newX AS SINGLE
    DIM blocked AS INTEGER
    DIM mi AS INTEGER
    DIM spd AS SINGLE
    
    spd = 0.4 + gLevel * 0.05
    
    FOR i = 0 TO sCount - 1
        IF sOn(i) = 0 THEN
            GOTO NextSeg
        END IF
        
        IF sDir(i) = D_LEFT THEN
            newX = sX(i) - spd
        ELSE
            newX = sX(i) + spd
        END IF
        
        blocked = 0
        IF INT(newX) < 1 THEN
            blocked = 1
        ELSEIF INT(newX) >= SCREEN_WIDTH - 1 THEN
            blocked = 1
        END IF
        
        IF blocked = 0 THEN
            mi = FindMush(INT(newX), INT(sY(i)))
            IF mi >= 0 THEN
                IF mPoison(mi) = 1 THEN
                    sY(i) = sY(i) + 1
                    IF INT(sY(i)) >= SCREEN_HEIGHT - 1 THEN
                        sY(i) = PLAY_TOP
                    END IF
                    GOTO NextSeg
                END IF
                blocked = 1
            END IF
        END IF
        
        IF blocked = 1 THEN
            sY(i) = sY(i) + 1
            IF INT(sY(i)) >= SCREEN_HEIGHT - 1 THEN
                sY(i) = SCREEN_HEIGHT - 2
            END IF
            IF sDir(i) = D_LEFT THEN
                sDir(i) = D_RIGHT
            ELSE
                sDir(i) = D_LEFT
            END IF
        ELSE
            sX(i) = newX
        END IF
        
        IF HitPlayer(INT(sX(i)), INT(sY(i))) = 1 THEN
            PlayerDeath()
        END IF
NextSeg:
    NEXT i
END SUB

SUB UpdateBullets()
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM hX AS INTEGER
    DIM hY AS INTEGER
    DIM mi AS INTEGER
    
    FOR i = 0 TO MAX_BULL - 1
        IF bOn(i) = 0 THEN
            GOTO NextBull
        END IF
        
        bY(i) = bY(i) - 2
        hX = bX(i)
        hY = INT(bY(i))
        
        IF hY < PLAY_TOP THEN
            bOn(i) = 0
            GOTO NextBull
        END IF
        
        mi = FindMush(hX, hY)
        IF mi >= 0 THEN
            mHP(mi) = mHP(mi) - 1
            IF mHP(mi) <= 0 THEN
                gScore = gScore + 1
            END IF
            bOn(i) = 0
            TBeep()
            GOTO NextBull
        END IF
        
        FOR j = 0 TO sCount - 1
            IF sOn(j) = 1 THEN
                IF INT(sX(j)) = hX THEN
                    IF INT(sY(j)) = hY THEN
                        HitSeg(j)
                        bOn(i) = 0
                        GOTO NextBull
                    END IF
                END IF
            END IF
        NEXT j
NextBull:
    NEXT i
END SUB

SUB HitSeg(idx AS INTEGER)
    DIM pts AS INTEGER
    
    IF sHead(idx) = 1 THEN
        pts = 100
    ELSE
        pts = 10
    END IF
    gScore = gScore + pts
    
    IF mCount < MAX_MUSH THEN
        mX(mCount) = INT(sX(idx))
        mY(mCount) = INT(sY(idx))
        mHP(mCount) = MUSH_HP
        mPoison(mCount) = 0
        mCount = mCount + 1
    END IF
    
    TBeep()
    
    IF idx < sCount - 1 THEN
        IF sOn(idx + 1) = 1 THEN
            sHead(idx + 1) = 1
        END IF
    END IF
    
    sOn(idx) = 0
    CheckWin()
END SUB

SUB CheckWin()
    DIM i AS INTEGER
    DIM any AS INTEGER
    
    any = 0
    FOR i = 0 TO sCount - 1
        IF sOn(i) = 1 THEN
            any = 1
            EXIT FOR
        END IF
    NEXT i
    
    IF any = 0 THEN
        gLevel = gLevel + 1
        SpawnCentipede()
        TBeep()
        TBeep()
    END IF
END SUB

REM ============================================================================
REM PLAYER
REM ============================================================================

SUB MovePlayer(dx AS INTEGER, dy AS INTEGER)
    DIM nx AS INTEGER
    DIM ny AS INTEGER
    
    nx = pX + dx
    ny = pY + dy
    
    IF nx < 1 THEN
        nx = 1
    END IF
    IF nx >= SCREEN_WIDTH - 1 THEN
        nx = SCREEN_WIDTH - 2
    END IF
    IF ny < PLAYER_TOP THEN
        ny = PLAYER_TOP
    END IF
    IF ny >= SCREEN_HEIGHT - 1 THEN
        ny = SCREEN_HEIGHT - 2
    END IF
    
    IF FindMush(nx, ny) < 0 THEN
        pX = nx
        pY = ny
    END IF
END SUB

SUB Fire()
    DIM i AS INTEGER
    FOR i = 0 TO MAX_BULL - 1
        IF bOn(i) = 0 THEN
            bX(i) = pX
            bY(i) = pY - 1
            bOn(i) = 1
            TBeep()
            EXIT SUB
        END IF
    NEXT i
END SUB

SUB PlayerDeath()
    IF pInvuln > 0 THEN
        EXIT SUB
    END IF
    
    pLives = pLives - 1
    TBeep()
    TBeep()
    
    IF pLives <= 0 THEN
        gOver = 1
    ELSE
        pX = SCREEN_WIDTH / 2
        pY = SCREEN_HEIGHT - 2
        pInvuln = 60
    END IF
END SUB

REM ============================================================================
REM RENDERING
REM ============================================================================

SUB Render()
    DIM i AS INTEGER
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM sp AS STRING
    
    REM Clear play area
    FOR y = PLAY_TOP TO SCREEN_HEIGHT - 1
        TSetPos(y, 1)
        TSetClr(C_BLACK)
        PRINT STRING$(SCREEN_WIDTH, " ");
    NEXT y
    
    REM Draw mushrooms
    FOR i = 0 TO mCount - 1
        IF mHP(i) > 0 THEN
            TSetPos(mY(i), mX(i))
            IF mPoison(i) = 1 THEN
                TSetClr(C_BGREEN)
                PRINT "%";
            ELSEIF mHP(i) >= 3 THEN
                TSetClr(C_RED)
                PRINT "#";
            ELSE
                TSetClr(C_YELLOW)
                PRINT "*";
            END IF
        END IF
    NEXT i
    
    REM Draw centipede
    FOR i = 0 TO sCount - 1
        IF sOn(i) = 1 THEN
            x = INT(sX(i))
            y = INT(sY(i))
            TSetPos(y, x)
            IF sHead(i) = 1 THEN
                TSetClr(C_BGREEN)
                IF sDir(i) = D_LEFT THEN
                    PRINT "<";
                ELSE
                    PRINT ">";
                END IF
            ELSE
                TSetClr(C_GREEN)
                IF gFrame MOD 2 = 0 THEN
                    PRINT "O";
                ELSE
                    PRINT "o";
                END IF
            END IF
        END IF
    NEXT i
    
    REM Draw bullets
    FOR i = 0 TO MAX_BULL - 1
        IF bOn(i) = 1 THEN
            TSetPos(INT(bY(i)), bX(i))
            TSetClr(C_BCYAN)
            PRINT "!";
        END IF
    NEXT i
    
    REM Draw player
    IF pInvuln > 0 THEN
        IF gFrame MOD 4 < 2 THEN
            TSetPos(pY, pX)
            TSetClr(C_BCYAN)
            PRINT "A";
        END IF
    ELSE
        TSetPos(pY, pX)
        TSetClr(C_BCYAN)
        PRINT "A";
    END IF
    
    REM Draw HUD
    TSetPos(1, 1)
    TSetClr(C_BYELLOW)
    PRINT "SCORE: "; gScore; "  LIVES: "; pLives; "  LEVEL: "; gLevel; "     ";
END SUB

REM ============================================================================
REM INPUT
REM ============================================================================

SUB HandleInput()
    DIM k AS STRING
    k = TGetKey()
    
    IF k = "" THEN
        EXIT SUB
    END IF
    
    IF k = CHR$(27) + "[A" THEN
        MovePlayer(0, -1)
    ELSEIF k = CHR$(27) + "[B" THEN
        MovePlayer(0, 1)
    ELSEIF k = CHR$(27) + "[C" THEN
        MovePlayer(1, 0)
    ELSEIF k = CHR$(27) + "[D" THEN
        MovePlayer(-1, 0)
    ELSEIF k = "w" THEN
        MovePlayer(0, -1)
    ELSEIF k = "W" THEN
        MovePlayer(0, -1)
    ELSEIF k = "s" THEN
        MovePlayer(0, 1)
    ELSEIF k = "S" THEN
        MovePlayer(0, 1)
    ELSEIF k = "a" THEN
        MovePlayer(-1, 0)
    ELSEIF k = "A" THEN
        MovePlayer(-1, 0)
    ELSEIF k = "d" THEN
        MovePlayer(1, 0)
    ELSEIF k = "D" THEN
        MovePlayer(1, 0)
    ELSEIF k = " " THEN
        Fire()
    ELSEIF k = "p" THEN
        gPaused = 1 - gPaused
    ELSEIF k = "P" THEN
        gPaused = 1 - gPaused
    ELSEIF k = "q" THEN
        gRunning = 0
    ELSEIF k = "Q" THEN
        gRunning = 0
    ELSEIF k = CHR$(27) THEN
        gRunning = 0
    END IF
END SUB

REM ============================================================================
REM GAME LOOP
REM ============================================================================

SUB GameLoop()
    DIM curTime AS INTEGER
    DIM elapsed AS INTEGER
    
    gLastTime = TGetTicks()
    
    WHILE gRunning = 1
        curTime = TGetTicks()
        elapsed = curTime - gLastTime
        
        IF elapsed >= 33 THEN
            gLastTime = curTime
            gFrame = gFrame + 1
            
            HandleInput()
            
            IF gPaused = 0 THEN
                IF gOver = 0 THEN
                    UpdateCentipede()
                    UpdateBullets()
                    IF pInvuln > 0 THEN
                        pInvuln = pInvuln - 1
                    END IF
                END IF
            END IF
            
            Render()
            
            IF gPaused = 1 THEN
                TSetPos(12, 30)
                TSetClr(C_WHITE)
                PRINT " PAUSED - P to Resume ";
            END IF
            
            IF gOver = 1 THEN
                TSetPos(10, 30)
                TSetClr(C_BRED)
                PRINT " GAME OVER ";
                TSetPos(12, 25)
                TSetClr(C_BYELLOW)
                PRINT " Final Score: "; gScore; " ";
                TSetPos(14, 28)
                TSetClr(C_WHITE)
                PRINT " Press Q to Quit ";
            END IF
        ELSE
            TWait(1)
        END IF
    WEND
END SUB

REM ============================================================================
REM TITLE SCREEN
REM ============================================================================

SUB ShowTitle()
    DIM k AS STRING
    DIM f AS INTEGER
    DIM i AS INTEGER
    DIM x AS INTEGER
    
    TClear()
    THideCursor()
    f = 0
    
    WHILE 1 = 1
        REM Title
        TSetPos(5, 25)
        TSetClr((f / 8) MOD 8 + 9)
        PRINT "*** CENTIPEDE ***"
        
        REM Animated centipede
        x = (f MOD 90) - 10
        FOR i = 0 TO 9
            IF x - i >= 1 THEN
                IF x - i < 80 THEN
                    TSetPos(10, x - i)
                    IF i = 0 THEN
                        TSetClr(C_BGREEN)
                        PRINT ">";
                    ELSE
                        TSetClr(C_GREEN)
                        IF f MOD 2 = 0 THEN
                            PRINT "O";
                        ELSE
                            PRINT "o";
                        END IF
                    END IF
                END IF
            END IF
        NEXT i
        
        REM Clear trail
        IF x - 10 >= 1 THEN
            TSetPos(10, x - 10)
            PRINT " ";
        END IF
        
        TSetPos(14, 22)
        TSetClr(C_WHITE)
        PRINT "WASD or Arrow Keys = Move"
        TSetPos(15, 30)
        PRINT "SPACE = Fire"
        TSetPos(16, 30)
        PRINT "P = Pause"
        
        TSetPos(19, 26)
        TSetClr(C_BYELLOW)
        PRINT "Press SPACE to Start"
        
        TSetPos(21, 28)
        TSetClr(C_CYAN)
        PRINT "Q or ESC to Quit"
        
        k = TGetKey()
        IF k = " " THEN
            EXIT WHILE
        END IF
        IF k = "q" THEN
            gRunning = 0
            EXIT WHILE
        END IF
        IF k = "Q" THEN
            gRunning = 0
            EXIT WHILE
        END IF
        IF k = CHR$(27) THEN
            gRunning = 0
            EXIT WHILE
        END IF
        
        f = f + 1
        TWait(50)
    WEND
END SUB

REM ============================================================================
REM MAIN
REM ============================================================================

SUB Main()
    gRunning = 1
    ShowTitle()
    
    IF gRunning = 0 THEN
        TClear()
        TShowCursor()
        TSetPos(1, 1)
        PRINT "Thanks for playing!"
        EXIT SUB
    END IF
    
    TClear()
    InitGame()
    GameLoop()
    
    TClear()
    TShowCursor()
    TSetPos(1, 1)
    PRINT "Thanks for playing Centipede!"
    PRINT "Final Score: "; gScore
    PRINT "Level Reached: "; gLevel
END SUB

Main()
