REM ====================================================================
REM CENTIPEDE - Classic Arcade Clone for Viper BASIC
REM ====================================================================
REM Features:
REM - Multi-segment centipede that splits when shot
REM - Mushroom field using Viper.Collections.List
REM - Spider, Flea, and Scorpion enemies
REM - Progressive difficulty with levels
REM - High score system with persistence
REM - ANSI terminal graphics
REM ====================================================================
REM Uses Viper.* Runtime:
REM - Viper.Collections.List for dynamic entity management
REM - Viper.Random (via RND()) for enemy spawning
REM - Viper.Math (via INT, ABS) for calculations
REM - Viper.String for score display
REM ====================================================================

AddFile "centipede_ansi.bas"
AddFile "centipede_classes.bas"
AddFile "centipede_scores.bas"

REM ====================================================================
REM Game Constants
REM ====================================================================
DIM GAME_WIDTH AS INTEGER
DIM GAME_HEIGHT AS INTEGER
DIM PLAYER_AREA_TOP AS INTEGER
DIM MAX_SEGMENTS AS INTEGER
DIM MAX_BULLETS AS INTEGER
DIM MAX_EXPLOSIONS AS INTEGER

GAME_WIDTH = 80
GAME_HEIGHT = 24
PLAYER_AREA_TOP = 18
MAX_SEGMENTS = 24
MAX_BULLETS = 3
MAX_EXPLOSIONS = 10

REM ====================================================================
REM Game Objects - Using Viper.Collections.List where dynamic
REM ====================================================================
DIM player AS Player

REM Static arrays for fixed-size collections
DIM segments(23) AS Segment
DIM bullets(2) AS Bullet
DIM explosions(9) AS Explosion

REM Viper.Collections.List for dynamic mushroom management
DIM mushroomList AS Viper.Collections.List

DIM spider AS Spider
DIM flea AS Flea
DIM scorpion AS Scorpion

REM Game state
DIM gameLevel AS INTEGER
DIM gameRunning AS INTEGER
DIM gamePaused AS INTEGER
DIM segmentCount AS INTEGER
DIM frameCount AS INTEGER
DIM centipedeSpeed AS INTEGER
DIM spiderSpawnTimer AS INTEGER
DIM fleaSpawnTimer AS INTEGER
DIM scorpionSpawnTimer AS INTEGER

REM ====================================================================
REM Initialize Mushroom Field using Viper.Collections.List
REM ====================================================================
SUB InitMushrooms()
    DIM i AS INTEGER
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM count AS INTEGER
    DIM maxMush AS INTEGER
    DIM m AS Mushroom
    DIM valid AS INTEGER
    DIM j AS INTEGER
    DIM existing AS Mushroom

    REM Create new list for mushrooms
    mushroomList = NEW Viper.Collections.List()

    maxMush = 30 + (gameLevel * 2)
    IF maxMush > 50 THEN
        maxMush = 50
    END IF

    count = 0
    WHILE count < maxMush
        x = INT(RND() * 76) + 3
        y = INT(RND() * 14) + 4

        REM Check if position is free
        valid = 1
        FOR j = 0 TO mushroomList.Count - 1
            existing = mushroomList.get_Item(j)
            IF existing.GetX() = x THEN
                IF existing.GetY() = y THEN
                    valid = 0
                    EXIT FOR
                END IF
            END IF
        NEXT j

        IF valid = 1 THEN
            m = NEW Mushroom()
            m.Init(x, y)
            mushroomList.Add(m)
            count = count + 1
        END IF
    WEND
END SUB

REM ====================================================================
REM Initialize Centipede
REM ====================================================================
SUB InitCentipede()
    DIM i AS INTEGER
    DIM numSegs AS INTEGER

    numSegs = 12
    segmentCount = numSegs

    FOR i = 0 TO MAX_SEGMENTS - 1
        segments(i) = NEW Segment()
        IF i < numSegs THEN
            segments(i).Init(40 - i, 3, 1, 0, i MOD 6)
        ELSE
            segments(i).Init(0, 0, 1, 0, 0)
            segments(i).Kill()
        END IF
    NEXT i

    REM First segment is the head
    segments(0).SetHead(1)
END SUB

REM ====================================================================
REM Initialize Game
REM ====================================================================
SUB InitGame()
    DIM i AS INTEGER

    gameLevel = 1
    gameRunning = 1
    gamePaused = 0
    frameCount = 0
    centipedeSpeed = 3
    spiderSpawnTimer = 0
    fleaSpawnTimer = 0
    scorpionSpawnTimer = 0

    REM Create player
    player = NEW Player()
    player.Init(40, 22)

    REM Initialize bullets
    FOR i = 0 TO MAX_BULLETS - 1
        bullets(i) = NEW Bullet()
        bullets(i).Init(0, 0)
        bullets(i).Deactivate()
    NEXT i

    REM Initialize explosions
    FOR i = 0 TO MAX_EXPLOSIONS - 1
        explosions(i) = NEW Explosion()
        explosions(i).Init(0, 0)
    NEXT i

    REM Initialize enemies
    spider = NEW Spider()
    spider.Init(0, 0)
    spider.Kill()

    flea = NEW Flea()
    flea.Init(0)
    flea.Kill()

    scorpion = NEW Scorpion()
    scorpion.Init(0, 0, 1)
    scorpion.Kill()

    REM Initialize mushrooms using Viper.Collections.List and centipede
    InitMushrooms()
    InitCentipede()
END SUB

REM ====================================================================
REM Check for mushroom at position using List
REM ====================================================================
FUNCTION GetMushroomAt(x AS INTEGER, y AS INTEGER) AS INTEGER
    DIM i AS INTEGER
    DIM m AS Mushroom

    GetMushroomAt = -1
    FOR i = 0 TO mushroomList.Count - 1
        m = mushroomList.get_Item(i)
        IF m.IsActive() = 1 THEN
            IF m.GetX() = x THEN
                IF m.GetY() = y THEN
                    GetMushroomAt = i
                    EXIT FUNCTION
                END IF
            END IF
        END IF
    NEXT i
END FUNCTION

REM ====================================================================
REM Add explosion effect
REM ====================================================================
SUB AddExplosion(x AS INTEGER, y AS INTEGER)
    DIM i AS INTEGER
    FOR i = 0 TO MAX_EXPLOSIONS - 1
        IF explosions(i).IsActive() = 0 THEN
            explosions(i) = NEW Explosion()
            explosions(i).Init(x, y)
            EXIT SUB
        END IF
    NEXT i
END SUB

REM ====================================================================
REM Add mushroom at position using List
REM ====================================================================
SUB AddMushroom(x AS INTEGER, y AS INTEGER)
    DIM m AS Mushroom

    REM Check if mushroom already exists
    IF GetMushroomAt(x, y) >= 0 THEN
        EXIT SUB
    END IF

    REM Add new mushroom to list
    m = NEW Mushroom()
    m.Init(x, y)
    mushroomList.Add(m)
END SUB

REM ====================================================================
REM Update Centipede Movement
REM ====================================================================
SUB UpdateCentipede()
    DIM i AS INTEGER
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM dir AS INTEGER
    DIM needDrop AS INTEGER
    DIM mushIdx AS INTEGER
    DIM m AS Mushroom
    DIM nextX AS INTEGER

    REM Only move every few frames based on speed
    IF (frameCount MOD centipedeSpeed) <> 0 THEN
        EXIT SUB
    END IF

    FOR i = 0 TO MAX_SEGMENTS - 1
        IF segments(i).IsActive() = 1 THEN
            x = segments(i).GetX()
            y = segments(i).GetY()
            dir = segments(i).GetDirection()
            needDrop = 0

            REM Check for wall or mushroom ahead
            nextX = x + dir

            IF nextX < 2 THEN
                needDrop = 1
            END IF
            IF nextX > 79 THEN
                needDrop = 1
            END IF
            IF needDrop = 0 THEN
                mushIdx = GetMushroomAt(nextX, y)
                IF mushIdx >= 0 THEN
                    m = mushroomList.get_Item(mushIdx)
                    REM Check if poisoned mushroom
                    IF m.IsPoisoned() = 1 THEN
                        REM Drop straight down through poisoned mushrooms
                        segments(i).SetPosition(x, y + 1)
                        IF y + 1 > 22 THEN
                            segments(i).SetPosition(x, 3)
                        END IF
                    ELSE
                        needDrop = 1
                    END IF
                END IF
            END IF

            IF needDrop = 1 THEN
                segments(i).MoveDown()
                IF segments(i).GetY() > 22 THEN
                    REM Wrap to top
                    segments(i).SetPosition(segments(i).GetX(), 3)
                END IF
            ELSE
                segments(i).MoveHorizontal()
            END IF
        END IF
    NEXT i
END SUB

REM ====================================================================
REM Update Spider
REM ====================================================================
SUB UpdateSpider()
    DIM mushIdx AS INTEGER
    DIM m AS Mushroom
    DIM startX AS INTEGER

    IF spider.IsActive() = 0 THEN
        spiderSpawnTimer = spiderSpawnTimer + 1
        IF spiderSpawnTimer > 200 - (gameLevel * 10) THEN
            IF RND() > 0.5 THEN
                startX = 2
            ELSE
                startX = 78
            END IF
            spider.Spawn(startX, 20)
            spiderSpawnTimer = 0
        END IF
    ELSE
        spider.Move(PLAYER_AREA_TOP, 22)

        REM Spider eats mushrooms
        mushIdx = GetMushroomAt(spider.GetX(), spider.GetY())
        IF mushIdx >= 0 THEN
            IF RND() > 0.7 THEN
                m = mushroomList.get_Item(mushIdx)
                m.Damage()
                m.Damage()
                m.Damage()
                m.Damage()
            END IF
        END IF
    END IF
END SUB

REM ====================================================================
REM Update Flea
REM ====================================================================
SUB UpdateFlea()
    DIM playerAreaMushrooms AS INTEGER
    DIM i AS INTEGER
    DIM m AS Mushroom

    IF flea.IsActive() = 0 THEN
        REM Count mushrooms in player area using List iteration
        playerAreaMushrooms = 0
        FOR i = 0 TO mushroomList.Count - 1
            m = mushroomList.get_Item(i)
            IF m.IsActive() = 1 THEN
                IF m.GetY() >= PLAYER_AREA_TOP THEN
                    playerAreaMushrooms = playerAreaMushrooms + 1
                END IF
            END IF
        NEXT i

        REM Spawn flea if few mushrooms in player area
        IF playerAreaMushrooms < 5 THEN
            fleaSpawnTimer = fleaSpawnTimer + 1
            IF fleaSpawnTimer > 100 THEN
                DIM fleaX AS INTEGER
                fleaX = INT(RND() * 76) + 3
                flea = NEW Flea()
                flea.Init(fleaX)
                fleaSpawnTimer = 0
            END IF
        END IF
    ELSE
        flea.Move()

        REM Drop mushrooms as flea falls
        IF RND() > 0.7 THEN
            AddMushroom(flea.GetX(), flea.GetY())
        END IF
    END IF
END SUB

REM ====================================================================
REM Update Scorpion
REM ====================================================================
SUB UpdateScorpion()
    DIM mushIdx AS INTEGER
    DIM m AS Mushroom
    DIM startX AS INTEGER
    DIM dir AS INTEGER
    DIM startY AS INTEGER

    IF scorpion.IsActive() = 0 THEN
        scorpionSpawnTimer = scorpionSpawnTimer + 1
        IF scorpionSpawnTimer > 300 - (gameLevel * 15) THEN
            IF RND() > 0.5 THEN
                startX = 1
                dir = 1
            ELSE
                startX = 80
                dir = -1
            END IF
            startY = INT(RND() * 12) + 4

            scorpion = NEW Scorpion()
            scorpion.Init(startX, startY, dir)
            scorpionSpawnTimer = 0
        END IF
    ELSE
        scorpion.Move()

        REM Poison mushrooms in path
        mushIdx = GetMushroomAt(scorpion.GetX(), scorpion.GetY())
        IF mushIdx >= 0 THEN
            m = mushroomList.get_Item(mushIdx)
            m.Poison()
        END IF
    END IF
END SUB

REM ====================================================================
REM Update Bullets
REM ====================================================================
SUB UpdateBullets()
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM bx AS INTEGER
    DIM by AS INTEGER
    DIM mushIdx AS INTEGER
    DIM m AS Mushroom
    DIM dist AS INTEGER

    FOR i = 0 TO MAX_BULLETS - 1
        IF bullets(i).IsActive() = 1 THEN
            bullets(i).Move()

            bx = bullets(i).GetX()
            by = bullets(i).GetY()

            REM Check mushroom collision
            mushIdx = GetMushroomAt(bx, by)
            IF mushIdx >= 0 THEN
                m = mushroomList.get_Item(mushIdx)
                m.Damage()
                IF m.IsActive() = 0 THEN
                    player.AddScore(1)
                    AddExplosion(bx, by)
                END IF
                bullets(i).Deactivate()
            END IF

            REM Check centipede collision
            FOR j = 0 TO MAX_SEGMENTS - 1
                IF segments(j).IsActive() = 1 THEN
                    IF segments(j).GetX() = bx THEN
                        IF segments(j).GetY() = by THEN
                            REM Hit!
                            IF segments(j).IsHead() = 1 THEN
                                player.AddScore(100)
                            ELSE
                                player.AddScore(10)
                            END IF

                            AddExplosion(bx, by)
                            AddMushroom(bx, by)

                            REM Split centipede - make next segment a head
                            IF j < MAX_SEGMENTS - 1 THEN
                                IF segments(j + 1).IsActive() = 1 THEN
                                    segments(j + 1).SetHead(1)
                                END IF
                            END IF

                            segments(j).Kill()
                            bullets(i).Deactivate()
                            EXIT FOR
                        END IF
                    END IF
                END IF
            NEXT j

            REM Check spider collision
            IF spider.IsActive() = 1 THEN
                IF spider.GetX() = bx THEN
                    IF spider.GetY() = by THEN
                        REM Score based on distance
                        dist = 22 - by
                        IF dist < 3 THEN
                            player.AddScore(900)
                        ELSEIF dist < 6 THEN
                            player.AddScore(600)
                        ELSE
                            player.AddScore(300)
                        END IF
                        AddExplosion(bx, by)
                        spider.Kill()
                        bullets(i).Deactivate()
                    END IF
                END IF
            END IF

            REM Check flea collision
            IF flea.IsActive() = 1 THEN
                IF flea.GetX() = bx THEN
                    IF flea.GetY() = by THEN
                        player.AddScore(200)
                        AddExplosion(bx, by)
                        flea.Kill()
                        bullets(i).Deactivate()
                    END IF
                END IF
            END IF

            REM Check scorpion collision
            IF scorpion.IsActive() = 1 THEN
                IF scorpion.GetX() = bx THEN
                    IF scorpion.GetY() = by THEN
                        player.AddScore(1000)
                        AddExplosion(bx, by)
                        scorpion.Kill()
                        bullets(i).Deactivate()
                    END IF
                END IF
            END IF
        END IF
    NEXT i
END SUB

REM ====================================================================
REM Fire Bullet
REM ====================================================================
SUB FireBullet()
    DIM i AS INTEGER
    FOR i = 0 TO MAX_BULLETS - 1
        IF bullets(i).IsActive() = 0 THEN
            bullets(i) = NEW Bullet()
            bullets(i).Init(player.GetX(), player.GetY() - 1)
            EXIT SUB
        END IF
    NEXT i
END SUB

REM ====================================================================
REM Check Player Collisions
REM ====================================================================
SUB CheckPlayerCollisions()
    DIM px AS INTEGER
    DIM py AS INTEGER
    DIM i AS INTEGER

    IF player.IsInvincible() = 1 THEN
        EXIT SUB
    END IF

    px = player.GetX()
    py = player.GetY()

    REM Check centipede collision
    FOR i = 0 TO MAX_SEGMENTS - 1
        IF segments(i).IsActive() = 1 THEN
            IF segments(i).GetX() = px THEN
                IF segments(i).GetY() = py THEN
                    player.Die()
                    FlashScreen()
                    EXIT SUB
                END IF
            END IF
        END IF
    NEXT i

    REM Check spider collision
    IF spider.IsActive() = 1 THEN
        IF spider.GetX() = px THEN
            IF spider.GetY() = py THEN
                player.Die()
                FlashScreen()
                EXIT SUB
            END IF
        END IF
    END IF

    REM Check flea collision
    IF flea.IsActive() = 1 THEN
        IF flea.GetX() = px THEN
            IF flea.GetY() = py THEN
                player.Die()
                FlashScreen()
                EXIT SUB
            END IF
        END IF
    END IF
END SUB

REM ====================================================================
REM Check Level Complete
REM ====================================================================
FUNCTION IsLevelComplete() AS INTEGER
    DIM i AS INTEGER
    FOR i = 0 TO MAX_SEGMENTS - 1
        IF segments(i).IsActive() = 1 THEN
            IsLevelComplete = 0
            EXIT FUNCTION
        END IF
    NEXT i
    IsLevelComplete = 1
END FUNCTION

REM ====================================================================
REM Next Level
REM ====================================================================
SUB NextLevel()
    DIM i AS INTEGER
    DIM m AS Mushroom

    gameLevel = gameLevel + 1

    REM Increase difficulty
    centipedeSpeed = centipedeSpeed - 1
    IF centipedeSpeed < 1 THEN
        centipedeSpeed = 1
    END IF

    REM Repair mushrooms using List iteration
    FOR i = 0 TO mushroomList.Count - 1
        m = mushroomList.get_Item(i)
        IF m.IsActive() = 1 THEN
            m.Heal()
        END IF
    NEXT i

    REM Create new centipede
    InitCentipede()

    REM Reset enemies
    spider.Kill()
    flea.Kill()
    scorpion.Kill()

    REM Show level message
    PrintColorAt(12, 30, COLOR_BRIGHT_YELLOW, "LEVEL " + STR$(gameLevel))
    SLEEP 1000
END SUB

REM ====================================================================
REM Draw Game
REM ====================================================================
SUB DrawGame()
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM m AS Mushroom
    DIM mushChar AS STRING
    DIM mushClr AS STRING
    DIM hits AS INTEGER
    DIM segChar AS STRING
    DIM segClr AS STRING
    DIM clrIdx AS INTEGER
    DIM expChar AS STRING
    DIM t AS INTEGER
    DIM playerClr AS STRING
    DIM inBounds AS INTEGER
    DIM flashMod AS INTEGER

    BeginFrame()

    REM Draw border
    FOR i = 1 TO GAME_WIDTH
        BufferColorAt(1, i, COLOR_BLUE, "-")
        BufferColorAt(23, i, COLOR_BLUE, "-")
    NEXT i
    FOR i = 2 TO 22
        BufferColorAt(i, 1, COLOR_BLUE, "|")
        BufferColorAt(i, 80, COLOR_BLUE, "|")
    NEXT i

    REM Clear play area
    FOR i = 2 TO 22
        FOR j = 2 TO 79
            BufferAt(i, j, " ")
        NEXT j
    NEXT i

    REM Draw HUD
    BufferColorAt(1, 3, COLOR_BRIGHT_WHITE, "SCORE:" + STR$(player.GetScore()))
    BufferColorAt(1, 25, COLOR_BRIGHT_CYAN, "=== CENTIPEDE ===")
    BufferColorAt(1, 55, COLOR_BRIGHT_WHITE, "LIVES:" + STR$(player.GetLives()))
    BufferColorAt(1, 70, COLOR_BRIGHT_WHITE, "LVL:" + STR$(gameLevel))

    REM Draw mushrooms from List
    FOR i = 0 TO mushroomList.Count - 1
        m = mushroomList.get_Item(i)
        IF m.IsActive() = 1 THEN
            x = m.GetX()
            y = m.GetY()
            inBounds = 0
            IF x >= 2 THEN
                IF x <= 79 THEN
                    IF y >= 2 THEN
                        IF y <= 22 THEN
                            inBounds = 1
                        END IF
                    END IF
                END IF
            END IF
            IF inBounds = 1 THEN
                IF m.IsPoisoned() = 1 THEN
                    mushClr = COLOR_BRIGHT_MAGENTA
                ELSE
                    mushClr = COLOR_GREEN
                END IF

                hits = m.GetHits()
                IF hits = 4 THEN
                    mushChar = "@"
                ELSEIF hits = 3 THEN
                    mushChar = "O"
                ELSEIF hits = 2 THEN
                    mushChar = "o"
                ELSE
                    mushChar = "."
                END IF

                BufferColorAt(y, x, mushClr, mushChar)
            END IF
        END IF
    NEXT i

    REM Draw centipede
    FOR i = 0 TO MAX_SEGMENTS - 1
        IF segments(i).IsActive() = 1 THEN
            x = segments(i).GetX()
            y = segments(i).GetY()
            inBounds = 0
            IF x >= 2 THEN
                IF x <= 79 THEN
                    IF y >= 2 THEN
                        IF y <= 22 THEN
                            inBounds = 1
                        END IF
                    END IF
                END IF
            END IF
            IF inBounds = 1 THEN
                IF segments(i).IsHead() = 1 THEN
                    segChar = "O"
                    segClr = COLOR_BRIGHT_RED
                ELSE
                    segChar = "o"
                    REM Alternate colors for body
                    clrIdx = segments(i).GetColorIndex()
                    IF clrIdx = 0 THEN
                        segClr = COLOR_RED
                    ELSEIF clrIdx = 1 THEN
                        segClr = COLOR_YELLOW
                    ELSEIF clrIdx = 2 THEN
                        segClr = COLOR_BRIGHT_YELLOW
                    ELSEIF clrIdx = 3 THEN
                        segClr = COLOR_BRIGHT_GREEN
                    ELSEIF clrIdx = 4 THEN
                        segClr = COLOR_CYAN
                    ELSE
                        segClr = COLOR_MAGENTA
                    END IF
                END IF

                BufferColorAt(y, x, segClr, segChar)
            END IF
        END IF
    NEXT i

    REM Draw spider
    IF spider.IsActive() = 1 THEN
        BufferColorAt(spider.GetY(), spider.GetX(), COLOR_BRIGHT_MAGENTA, "X")
    END IF

    REM Draw flea
    IF flea.IsActive() = 1 THEN
        BufferColorAt(flea.GetY(), flea.GetX(), COLOR_BRIGHT_CYAN, "!")
    END IF

    REM Draw scorpion
    IF scorpion.IsActive() = 1 THEN
        BufferColorAt(scorpion.GetY(), scorpion.GetX(), COLOR_BRIGHT_RED, "S")
    END IF

    REM Draw bullets
    FOR i = 0 TO MAX_BULLETS - 1
        IF bullets(i).IsActive() = 1 THEN
            x = bullets(i).GetX()
            y = bullets(i).GetY()
            inBounds = 0
            IF y >= 2 THEN
                IF y <= 22 THEN
                    inBounds = 1
                END IF
            END IF
            IF inBounds = 1 THEN
                BufferColorAt(y, x, COLOR_BRIGHT_WHITE, "|")
            END IF
        END IF
    NEXT i

    REM Draw explosions
    FOR i = 0 TO MAX_EXPLOSIONS - 1
        IF explosions(i).IsActive() = 1 THEN
            x = explosions(i).GetX()
            y = explosions(i).GetY()
            t = explosions(i).GetTimer()
            IF t > 4 THEN
                expChar = "*"
            ELSEIF t > 2 THEN
                expChar = "+"
            ELSE
                expChar = "."
            END IF
            BufferColorAt(y, x, COLOR_BRIGHT_YELLOW, expChar)
        END IF
    NEXT i

    REM Draw player
    playerClr = COLOR_BRIGHT_GREEN
    IF player.IsInvincible() = 1 THEN
        playerClr = COLOR_BLUE
        flashMod = frameCount MOD 4
        IF flashMod < 2 THEN
            playerClr = COLOR_BRIGHT_WHITE
        END IF
    END IF
    BufferColorAt(player.GetY(), player.GetX(), playerClr, "A")

    REM Draw instructions
    BufferAt(24, 2, "A/D=Move  SPACE=Fire  P=Pause  Q=Quit")

    FlushFrame()
END SUB

REM ====================================================================
REM Handle Input
REM ====================================================================
SUB HandleInput()
    DIM key AS STRING
    key = INKEY$()

    IF LEN(key) > 0 THEN
        IF key = "a" THEN
            player.MoveLeft()
        END IF
        IF key = "A" THEN
            player.MoveLeft()
        END IF

        IF key = "d" THEN
            player.MoveRight()
        END IF
        IF key = "D" THEN
            player.MoveRight()
        END IF

        IF key = " " THEN
            FireBullet()
        END IF

        IF key = "p" THEN
            HandlePause()
        END IF
        IF key = "P" THEN
            HandlePause()
        END IF

        IF key = "q" THEN
            gameRunning = 0
        END IF
        IF key = "Q" THEN
            gameRunning = 0
        END IF
    END IF
END SUB

REM ====================================================================
REM Handle Pause
REM ====================================================================
SUB HandlePause()
    DIM key AS STRING
    DIM resumeGame AS INTEGER

    PrintColorAt(12, 30, COLOR_BRIGHT_YELLOW, "*** PAUSED ***")
    PrintColorAt(13, 27, COLOR_WHITE, "Press P to resume")

    resumeGame = 0
    WHILE resumeGame = 0
        key = INKEY$()
        IF LEN(key) > 0 THEN
            IF key = "p" THEN
                resumeGame = 1
            END IF
            IF key = "P" THEN
                resumeGame = 1
            END IF
            IF key = "q" THEN
                gameRunning = 0
                resumeGame = 1
            END IF
            IF key = "Q" THEN
                gameRunning = 0
                resumeGame = 1
            END IF
        END IF
        SLEEP 50
    WEND
END SUB

REM ====================================================================
REM Update Explosions
REM ====================================================================
SUB UpdateExplosions()
    DIM i AS INTEGER
    FOR i = 0 TO MAX_EXPLOSIONS - 1
        IF explosions(i).IsActive() = 1 THEN
            explosions(i).Update()
        END IF
    NEXT i
END SUB

REM ====================================================================
REM Main Game Loop
REM ====================================================================
SUB GameLoop()
    DIM playerName AS STRING

    ClearScreen()
    HideCursor()

    DIM continueGame AS INTEGER
    continueGame = 1
    WHILE continueGame = 1
        IF gameRunning = 0 THEN
            continueGame = 0
        END IF
        IF player.IsAlive() = 0 THEN
            continueGame = 0
        END IF
        IF continueGame = 0 THEN
            EXIT WHILE
        END IF
        frameCount = frameCount + 1

        HandleInput()

        player.Update()
        UpdateBullets()
        UpdateCentipede()
        UpdateSpider()
        UpdateFlea()
        UpdateScorpion()
        UpdateExplosions()

        CheckPlayerCollisions()

        REM Check for level complete
        IF IsLevelComplete() = 1 THEN
            NextLevel()
        END IF

        DrawGame()

        SLEEP 33   REM ~30 FPS
    WEND

    ShowCursor()

    REM Game Over Screen
    ClearScreen()
    PRINT ""
    PRINT ""
    IF player.IsAlive() = 0 THEN
        PrintColorAt(8, 25, COLOR_BRIGHT_RED, "=== GAME OVER ===")
    ELSE
        PrintColorAt(8, 25, COLOR_BRIGHT_YELLOW, "Thanks for playing!")
    END IF
    PRINT ""
    PrintColorAt(10, 25, COLOR_WHITE, "Final Score: " + STR$(player.GetScore()))
    PrintColorAt(11, 25, COLOR_WHITE, "Level Reached: " + STR$(gameLevel))
    PRINT ""

    REM Check for high score
    DIM showHighScore AS INTEGER
    showHighScore = 0
    IF IsHighScore(player.GetScore()) = 1 THEN
        IF player.GetScore() > 0 THEN
            showHighScore = 1
        END IF
    END IF
    IF showHighScore = 1 THEN
        PrintColorAt(13, 20, COLOR_BRIGHT_YELLOW, "*** NEW HIGH SCORE! ***")
        playerName = GetPlayerName()
        AddHighScore(playerName, player.GetScore(), gameLevel)
        SaveHighScores()
    END IF

    PrintColorAt(22, 20, COLOR_WHITE, "Press any key to continue...")
    WHILE LEN(INKEY$()) = 0
        SLEEP 50
    WEND
END SUB

REM ====================================================================
REM Show Title Screen - Returns 1 to play, 0 to quit
REM ====================================================================
FUNCTION ShowTitle() AS INTEGER
    DIM animFrame AS INTEGER
    DIM key AS STRING
    DIM centX AS INTEGER
    DIM titleLoop AS INTEGER
    DIM wantQuit AS INTEGER

    animFrame = 0
    titleLoop = 1
    wantQuit = 0

    WHILE titleLoop = 1
        ClearScreen()

        REM Animated title
        PrintColorAt(3, 15, COLOR_BRIGHT_GREEN, "  ____ _____ _   _ _____ ___ ____  _____ ____  _____")
        PrintColorAt(4, 15, COLOR_GREEN, " / ___| ____| \\ | |_   _|_ _|  _ \\| ____|  _ \\| ____|")
        PrintColorAt(5, 15, COLOR_BRIGHT_GREEN, "| |   |  _| |  \\| | | |  | || |_) |  _| | | | |  _|  ")
        PrintColorAt(6, 15, COLOR_GREEN, "| |___| |___| |\\  | | |  | ||  __/| |___| |_| | |___ ")
        PrintColorAt(7, 15, COLOR_BRIGHT_GREEN, " \\____|_____|_| \\_| |_| |___|_|   |_____|____/|_____|")

        REM Animated centipede
        centX = (animFrame MOD 50) + 15
        PrintColorAt(9, centX, COLOR_BRIGHT_RED, "O")
        PrintColorAt(9, centX + 1, COLOR_RED, "o")
        PrintColorAt(9, centX + 2, COLOR_YELLOW, "o")
        PrintColorAt(9, centX + 3, COLOR_BRIGHT_YELLOW, "o")
        PrintColorAt(9, centX + 4, COLOR_GREEN, "o")
        PrintColorAt(9, centX + 5, COLOR_CYAN, "o")

        PrintColorAt(12, 28, COLOR_BRIGHT_CYAN, "Classic Arcade Action!")

        PrintColorAt(15, 32, COLOR_WHITE, "[1] Start Game")
        PrintColorAt(16, 32, COLOR_WHITE, "[2] High Scores")
        PrintColorAt(17, 32, COLOR_WHITE, "[3] Instructions")
        PrintColorAt(18, 32, COLOR_WHITE, "[Q] Quit")

        PrintColorAt(21, 28, COLOR_BRIGHT_WHITE, "Select option...")

        PrintColorAt(23, 22, COLOR_CYAN, "Viper BASIC Demo - Uses Viper.*")

        key = INKEY$()
        IF LEN(key) > 0 THEN
            IF key = "1" THEN
                titleLoop = 0
            END IF
            IF key = "2" THEN
                DisplayHighScores()
                WHILE LEN(INKEY$()) = 0
                    SLEEP 50
                WEND
            END IF
            IF key = "3" THEN
                ShowInstructions()
            END IF
            IF key = "q" THEN
                wantQuit = 1
                titleLoop = 0
            END IF
            IF key = "Q" THEN
                wantQuit = 1
                titleLoop = 0
            END IF
        END IF

        animFrame = animFrame + 1
        SLEEP 50
    WEND

    IF wantQuit = 1 THEN
        ShowTitle = 0
    ELSE
        ShowTitle = 1
    END IF
END FUNCTION

REM ====================================================================
REM Show Instructions
REM ====================================================================
SUB ShowInstructions()
    ClearScreen()
    PRINT ""
    PrintColorAt(2, 25, COLOR_BRIGHT_YELLOW, "=== HOW TO PLAY ===")
    PRINT ""
    PrintColorAt(4, 5, COLOR_BRIGHT_CYAN, "OBJECTIVE:")
    PrintColorAt(5, 5, COLOR_WHITE, "Destroy the centipede before it reaches the bottom!")
    PRINT ""
    PrintColorAt(7, 5, COLOR_BRIGHT_CYAN, "CONTROLS:")
    PrintColorAt(8, 5, COLOR_WHITE, "  A / D   - Move left/right")
    PrintColorAt(9, 5, COLOR_WHITE, "  SPACE   - Fire")
    PrintColorAt(10, 5, COLOR_WHITE, "  P       - Pause")
    PrintColorAt(11, 5, COLOR_WHITE, "  Q       - Quit")
    PRINT ""
    PrintColorAt(13, 5, COLOR_BRIGHT_CYAN, "ENEMIES:")
    PrintColorAt(14, 5, COLOR_BRIGHT_RED, "  O/o - Centipede (splits when hit)")
    PrintColorAt(15, 5, COLOR_BRIGHT_MAGENTA, "  X   - Spider (eats mushrooms)")
    PrintColorAt(16, 5, COLOR_BRIGHT_CYAN, "  !   - Flea (drops mushrooms)")
    PrintColorAt(17, 5, COLOR_BRIGHT_RED, "  S   - Scorpion (poisons mushrooms)")
    PRINT ""
    PrintColorAt(19, 5, COLOR_BRIGHT_CYAN, "SCORING:")
    PrintColorAt(20, 5, COLOR_WHITE, "  Head: 100  Body: 10  Spider: 300-900")
    PrintColorAt(21, 5, COLOR_WHITE, "  Flea: 200  Scorpion: 1000  Mushroom: 1")
    PRINT ""
    PrintColorAt(23, 20, COLOR_WHITE, "Press any key to return...")

    WHILE LEN(INKEY$()) = 0
        SLEEP 50
    WEND
END SUB

REM ====================================================================
REM Main Program Entry
REM ====================================================================
RANDOMIZE

InitHighScores()
LoadHighScores()

DIM playAgain AS INTEGER
DIM response AS STRING
DIM titleResult AS INTEGER
playAgain = 1

WHILE playAgain = 1
    titleResult = ShowTitle()
    IF titleResult = 0 THEN
        playAgain = 0
    ELSE
        InitGame()
        GameLoop()

        REM Ask to play again
        PrintColorAt(23, 20, COLOR_BRIGHT_CYAN, "Play again? (Y/N)")
        response = ""
        WHILE response = ""
            response = INKEY$()
            SLEEP 50
        WEND

        playAgain = 0
        IF response = "y" THEN
            playAgain = 1
        END IF
        IF response = "Y" THEN
            playAgain = 1
        END IF
    END IF
WEND

ClearScreen()
PRINT ""
PRINT "Thanks for playing CENTIPEDE!"
PRINT "A Viper BASIC Demo using Viper.Collections.List"
PRINT ""
