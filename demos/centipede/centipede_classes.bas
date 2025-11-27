REM ====================================================================
REM Game Object Classes for Centipede
REM Uses Viper.* runtime where possible
REM ====================================================================

REM ====================================================================
REM Segment Class - A single segment of the centipede
REM ====================================================================
CLASS Segment
    DIM posX AS INTEGER
    DIM posY AS INTEGER
    DIM direction AS INTEGER     REM 1 = right, -1 = left
    DIM headFlag AS INTEGER      REM 1 if this is a head segment
    DIM active AS INTEGER        REM 1 if alive
    DIM colorIndex AS INTEGER    REM Color variation

    SUB Init(x AS INTEGER, y AS INTEGER, dir AS INTEGER, head AS INTEGER, clrIdx AS INTEGER)
        posX = x
        posY = y
        direction = dir
        headFlag = head
        active = 1
        colorIndex = clrIdx
    END SUB

    FUNCTION GetX() AS INTEGER
        GetX = posX
    END FUNCTION

    FUNCTION GetY() AS INTEGER
        GetY = posY
    END FUNCTION

    FUNCTION GetDirection() AS INTEGER
        GetDirection = direction
    END FUNCTION

    FUNCTION IsHead() AS INTEGER
        IsHead = headFlag
    END FUNCTION

    FUNCTION IsActive() AS INTEGER
        IsActive = active
    END FUNCTION

    FUNCTION GetColorIndex() AS INTEGER
        GetColorIndex = colorIndex
    END FUNCTION

    SUB SetHead(h AS INTEGER)
        headFlag = h
    END SUB

    SUB SetPosition(x AS INTEGER, y AS INTEGER)
        posX = x
        posY = y
    END SUB

    SUB SetDirection(dir AS INTEGER)
        direction = dir
    END SUB

    SUB Kill()
        active = 0
    END SUB

    SUB MoveHorizontal()
        posX = posX + direction
    END SUB

    SUB MoveDown()
        posY = posY + 1
        direction = direction * -1
    END SUB

    SUB ReverseDirection()
        direction = direction * -1
    END SUB
END CLASS

REM ====================================================================
REM Mushroom Class - Obstacles that deflect the centipede
REM ====================================================================
CLASS Mushroom
    DIM posX AS INTEGER
    DIM posY AS INTEGER
    DIM hits AS INTEGER          REM 4 hits to destroy
    DIM poisoned AS INTEGER      REM 1 if poisoned by scorpion
    DIM active AS INTEGER

    SUB Init(x AS INTEGER, y AS INTEGER)
        posX = x
        posY = y
        hits = 4
        poisoned = 0
        active = 1
    END SUB

    FUNCTION GetX() AS INTEGER
        GetX = posX
    END FUNCTION

    FUNCTION GetY() AS INTEGER
        GetY = posY
    END FUNCTION

    FUNCTION GetHits() AS INTEGER
        GetHits = hits
    END FUNCTION

    FUNCTION IsPoisoned() AS INTEGER
        IsPoisoned = poisoned
    END FUNCTION

    FUNCTION IsActive() AS INTEGER
        IsActive = active
    END FUNCTION

    SUB Damage()
        hits = hits - 1
        IF hits <= 0 THEN
            active = 0
        END IF
    END SUB

    SUB Poison()
        poisoned = 1
    END SUB

    SUB Heal()
        hits = 4
    END SUB
END CLASS

REM ====================================================================
REM Bullet Class - Player's projectile
REM ====================================================================
CLASS Bullet
    DIM posX AS INTEGER
    DIM posY AS INTEGER
    DIM active AS INTEGER

    SUB Init(x AS INTEGER, y AS INTEGER)
        posX = x
        posY = y
        active = 1
    END SUB

    FUNCTION GetX() AS INTEGER
        GetX = posX
    END FUNCTION

    FUNCTION GetY() AS INTEGER
        GetY = posY
    END FUNCTION

    FUNCTION IsActive() AS INTEGER
        IsActive = active
    END FUNCTION

    SUB Move()
        posY = posY - 1
        IF posY < 2 THEN
            active = 0
        END IF
    END SUB

    SUB Deactivate()
        active = 0
    END SUB
END CLASS

REM ====================================================================
REM Spider Class - Erratic enemy in player area
REM ====================================================================
CLASS Spider
    DIM posX AS INTEGER
    DIM posY AS INTEGER
    DIM dx AS INTEGER
    DIM dy AS INTEGER
    DIM active AS INTEGER
    DIM moveTimer AS INTEGER

    SUB Init(x AS INTEGER, y AS INTEGER)
        posX = x
        posY = y
        dx = 1
        dy = 1
        active = 1
        moveTimer = 0
    END SUB

    FUNCTION GetX() AS INTEGER
        GetX = posX
    END FUNCTION

    FUNCTION GetY() AS INTEGER
        GetY = posY
    END FUNCTION

    FUNCTION IsActive() AS INTEGER
        IsActive = active
    END FUNCTION

    SUB Move(minY AS INTEGER, maxY AS INTEGER)
        moveTimer = moveTimer + 1
        IF moveTimer < 2 THEN
            EXIT SUB
        END IF
        moveTimer = 0

        REM Random direction changes using Viper.Random
        DIM r AS INTEGER
        r = INT(RND() * 10)
        IF r < 2 THEN
            dx = dx * -1
        END IF
        IF r >= 8 THEN
            dy = dy * -1
        END IF

        posX = posX + dx
        posY = posY + dy

        REM Bounce off edges
        IF posX < 2 THEN
            posX = 2
            dx = 1
        END IF
        IF posX > 79 THEN
            posX = 79
            dx = -1
        END IF
        IF posY < minY THEN
            posY = minY
            dy = 1
        END IF
        IF posY > maxY THEN
            posY = maxY
            dy = -1
        END IF
    END SUB

    SUB Kill()
        active = 0
    END SUB

    SUB Spawn(x AS INTEGER, y AS INTEGER)
        posX = x
        posY = y
        IF RND() > 0.5 THEN
            dx = 1
        ELSE
            dx = -1
        END IF
        IF RND() > 0.5 THEN
            dy = 1
        ELSE
            dy = -1
        END IF
        active = 1
    END SUB
END CLASS

REM ====================================================================
REM Flea Class - Drops down creating mushrooms
REM ====================================================================
CLASS Flea
    DIM posX AS INTEGER
    DIM posY AS INTEGER
    DIM active AS INTEGER
    DIM speed AS INTEGER

    SUB Init(x AS INTEGER)
        posX = x
        posY = 2
        active = 1
        speed = 1
    END SUB

    FUNCTION GetX() AS INTEGER
        GetX = posX
    END FUNCTION

    FUNCTION GetY() AS INTEGER
        GetY = posY
    END FUNCTION

    FUNCTION IsActive() AS INTEGER
        IsActive = active
    END FUNCTION

    SUB Move()
        posY = posY + speed
        IF posY > 22 THEN
            active = 0
        END IF
    END SUB

    SUB Kill()
        active = 0
    END SUB

    SUB DoubleSpeed()
        speed = 2
    END SUB
END CLASS

REM ====================================================================
REM Scorpion Class - Poisons mushrooms
REM ====================================================================
CLASS Scorpion
    DIM posX AS INTEGER
    DIM posY AS INTEGER
    DIM direction AS INTEGER
    DIM active AS INTEGER
    DIM moveTimer AS INTEGER

    SUB Init(x AS INTEGER, y AS INTEGER, dir AS INTEGER)
        posX = x
        posY = y
        direction = dir
        active = 1
        moveTimer = 0
    END SUB

    FUNCTION GetX() AS INTEGER
        GetX = posX
    END FUNCTION

    FUNCTION GetY() AS INTEGER
        GetY = posY
    END FUNCTION

    FUNCTION IsActive() AS INTEGER
        IsActive = active
    END FUNCTION

    SUB Move()
        moveTimer = moveTimer + 1
        IF moveTimer < 2 THEN
            EXIT SUB
        END IF
        moveTimer = 0

        posX = posX + direction
        IF posX < 1 THEN
            active = 0
        END IF
        IF posX > 80 THEN
            active = 0
        END IF
    END SUB

    SUB Kill()
        active = 0
    END SUB
END CLASS

REM ====================================================================
REM Player Class - The player's ship
REM ====================================================================
CLASS Player
    DIM posX AS INTEGER
    DIM posY AS INTEGER
    DIM lives AS INTEGER
    DIM score AS INTEGER
    DIM alive AS INTEGER
    DIM invincibleTimer AS INTEGER

    SUB Init(x AS INTEGER, y AS INTEGER)
        posX = x
        posY = y
        lives = 3
        score = 0
        alive = 1
        invincibleTimer = 0
    END SUB

    FUNCTION GetX() AS INTEGER
        GetX = posX
    END FUNCTION

    FUNCTION GetY() AS INTEGER
        GetY = posY
    END FUNCTION

    FUNCTION GetLives() AS INTEGER
        GetLives = lives
    END FUNCTION

    FUNCTION GetScore() AS INTEGER
        GetScore = score
    END FUNCTION

    FUNCTION IsAlive() AS INTEGER
        IsAlive = alive
    END FUNCTION

    FUNCTION IsInvincible() AS INTEGER
        IF invincibleTimer > 0 THEN
            IsInvincible = 1
        ELSE
            IsInvincible = 0
        END IF
    END FUNCTION

    SUB MoveLeft()
        IF posX > 2 THEN
            posX = posX - 1
        END IF
    END SUB

    SUB MoveRight()
        IF posX < 79 THEN
            posX = posX + 1
        END IF
    END SUB

    SUB AddScore(points AS INTEGER)
        DIM oldScore AS INTEGER
        oldScore = score
        score = score + points

        REM Bonus life every 10000 points
        IF INT(score / 10000) > INT(oldScore / 10000) THEN
            lives = lives + 1
        END IF
    END SUB

    SUB Die()
        lives = lives - 1
        IF lives <= 0 THEN
            alive = 0
        ELSE
            invincibleTimer = 60
        END IF
    END SUB

    SUB Update()
        IF invincibleTimer > 0 THEN
            invincibleTimer = invincibleTimer - 1
        END IF
    END SUB

    SUB Reset()
        posX = 40
    END SUB
END CLASS

REM ====================================================================
REM Explosion Class - Visual effect when enemies die
REM ====================================================================
CLASS Explosion
    DIM posX AS INTEGER
    DIM posY AS INTEGER
    DIM timer AS INTEGER
    DIM active AS INTEGER

    SUB Init(x AS INTEGER, y AS INTEGER)
        posX = x
        posY = y
        timer = 6
        active = 1
    END SUB

    FUNCTION GetX() AS INTEGER
        GetX = posX
    END FUNCTION

    FUNCTION GetY() AS INTEGER
        GetY = posY
    END FUNCTION

    FUNCTION GetTimer() AS INTEGER
        GetTimer = timer
    END FUNCTION

    FUNCTION IsActive() AS INTEGER
        IsActive = active
    END FUNCTION

    SUB Update()
        timer = timer - 1
        IF timer <= 0 THEN
            active = 0
        END IF
    END SUB
END CLASS
