REM ====================================================================
REM Enhanced Game Object Classes for Classic Frogger
REM ====================================================================

REM ====================================================================
REM Position Class - Represents a 2D coordinate
REM ====================================================================
CLASS Position
    DIM row AS INTEGER
    DIM col AS INTEGER

    SUB Init(r AS INTEGER, c AS INTEGER)
        row = r
        col = c
    END SUB

    SUB MoveTo(r AS INTEGER, c AS INTEGER)
        row = r
        col = c
    END SUB

    SUB MoveBy(dr AS INTEGER, dc AS INTEGER)
        row = row + dr
        col = col + dc
    END SUB

    FUNCTION GetRow() AS INTEGER
        GetRow = row
    END FUNCTION

    FUNCTION GetCol() AS INTEGER
        GetCol = col
    END FUNCTION
END CLASS

REM ====================================================================
REM Frog Class - The player character
REM ====================================================================
CLASS Frog
    DIM pos AS Position
    DIM lives AS INTEGER
    DIM alive AS INTEGER
    DIM startRow AS INTEGER
    DIM startCol AS INTEGER
    DIM onPlatform AS INTEGER
    DIM platformSpeed AS INTEGER

    SUB Init(r AS INTEGER, c AS INTEGER)
        pos = NEW Position()
        pos.Init(r, c)
        startRow = r
        startCol = c
        lives = 3
        alive = 1
        onPlatform = 0
        platformSpeed = 0
    END SUB

    SUB MoveUp()
        DIM newRow AS INTEGER
        newRow = pos.GetRow() - 1
        IF newRow >= 1 THEN
            pos.MoveTo(newRow, pos.GetCol())
        END IF
    END SUB

    SUB MoveDown()
        DIM newRow AS INTEGER
        newRow = pos.GetRow() + 1
        IF newRow <= 24 THEN
            pos.MoveTo(newRow, pos.GetCol())
        END IF
    END SUB

    SUB MoveLeft()
        DIM newCol AS INTEGER
        newCol = pos.GetCol() - 1
        IF newCol >= 1 THEN
            pos.MoveTo(pos.GetRow(), newCol)
        END IF
    END SUB

    SUB MoveRight()
        DIM newCol AS INTEGER
        newCol = pos.GetCol() + 1
        IF newCol <= 70 THEN
            pos.MoveTo(pos.GetRow(), newCol)
        END IF
    END SUB

    SUB UpdateOnPlatform()
        REM Move with platform if riding one
        IF onPlatform = 1 THEN
            DIM newCol AS INTEGER
            newCol = pos.GetCol() + platformSpeed
            IF newCol >= 1 AND newCol <= 70 THEN
                pos.MoveTo(pos.GetRow(), newCol)
            END IF
        END IF
    END SUB

    SUB SetOnPlatform(speed AS INTEGER)
        onPlatform = 1
        platformSpeed = speed
    END SUB

    SUB ClearPlatform()
        onPlatform = 0
        platformSpeed = 0
    END SUB

    FUNCTION GetRow() AS INTEGER
        GetRow = pos.GetRow()
    END FUNCTION

    FUNCTION GetCol() AS INTEGER
        GetCol = pos.GetCol()
    END FUNCTION

    SUB Die()
        lives = lives - 1
        IF lives <= 0 THEN
            alive = 0
        ELSE
            pos.MoveTo(startRow, startCol)
        END IF
        onPlatform = 0
        platformSpeed = 0
    END SUB

    FUNCTION GetLives() AS INTEGER
        GetLives = lives
    END FUNCTION

    FUNCTION IsAlive() AS INTEGER
        IsAlive = alive
    END FUNCTION

    SUB Reset()
        pos.MoveTo(startRow, startCol)
        onPlatform = 0
        platformSpeed = 0
    END SUB
END CLASS

REM ====================================================================
REM Vehicle Class - Cars and trucks on the road
REM ====================================================================
CLASS Vehicle
    DIM pos AS Position
    DIM speed AS INTEGER
    DIM direction AS INTEGER
    DIM symbol AS STRING
    DIM width AS INTEGER

    SUB Init(r AS INTEGER, c AS INTEGER, spd AS INTEGER, dir AS INTEGER, sym AS STRING, w AS INTEGER)
        pos = NEW Position()
        pos.Init(r, c)
        speed = spd
        direction = dir
        symbol = sym
        width = w
    END SUB

    SUB Move()
        DIM newCol AS INTEGER
        newCol = pos.GetCol() + (speed * direction)
        IF newCol > 75 THEN
            newCol = 1 - width
        END IF
        IF newCol < (0 - width) THEN
            newCol = 75
        END IF
        pos.MoveTo(pos.GetRow(), newCol)
    END SUB

    FUNCTION GetRow() AS INTEGER
        GetRow = pos.GetRow()
    END FUNCTION

    FUNCTION GetCol() AS INTEGER
        GetCol = pos.GetCol()
    END FUNCTION

    FUNCTION GetWidth() AS INTEGER
        GetWidth = width
    END FUNCTION

    FUNCTION GetSymbol() AS STRING
        GetSymbol = symbol
    END FUNCTION

    FUNCTION GetSpeed() AS INTEGER
        GetSpeed = speed
    END FUNCTION

    FUNCTION GetDirection() AS INTEGER
        GetDirection = direction
    END FUNCTION

    FUNCTION CheckCollision(frogRow AS INTEGER, frogCol AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        CheckCollision = 0
        IF frogRow = pos.GetRow() THEN
            FOR i = 0 TO width - 1
                IF frogCol = pos.GetCol() + i THEN
                    CheckCollision = 1
                    EXIT FUNCTION
                END IF
            NEXT i
        END IF
    END FUNCTION
END CLASS

REM ====================================================================
REM Platform Class - Logs and turtles in the river
REM ====================================================================
CLASS Platform
    DIM pos AS Position
    DIM speed AS INTEGER
    DIM direction AS INTEGER
    DIM symbol AS STRING
    DIM width AS INTEGER

    SUB Init(r AS INTEGER, c AS INTEGER, spd AS INTEGER, dir AS INTEGER, sym AS STRING, w AS INTEGER)
        pos = NEW Position()
        pos.Init(r, c)
        speed = spd
        direction = dir
        symbol = sym
        width = w
    END SUB

    SUB Move()
        DIM newCol AS INTEGER
        newCol = pos.GetCol() + (speed * direction)
        IF newCol > 75 THEN
            newCol = 1 - width
        END IF
        IF newCol < (0 - width) THEN
            newCol = 75
        END IF
        pos.MoveTo(pos.GetRow(), newCol)
    END SUB

    FUNCTION GetRow() AS INTEGER
        GetRow = pos.GetRow()
    END FUNCTION

    FUNCTION GetCol() AS INTEGER
        GetCol = pos.GetCol()
    END FUNCTION

    FUNCTION GetWidth() AS INTEGER
        GetWidth = width
    END FUNCTION

    FUNCTION GetSymbol() AS STRING
        GetSymbol = symbol
    END FUNCTION

    FUNCTION GetSpeed() AS INTEGER
        GetSpeed = speed
    END FUNCTION

    FUNCTION GetDirection() AS INTEGER
        GetDirection = direction
    END FUNCTION

    FUNCTION CheckOnPlatform(frogRow AS INTEGER, frogCol AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        CheckOnPlatform = 0
        IF frogRow = pos.GetRow() THEN
            FOR i = 0 TO width - 1
                IF frogCol = pos.GetCol() + i THEN
                    CheckOnPlatform = 1
                    EXIT FUNCTION
                END IF
            NEXT i
        END IF
    END FUNCTION
END CLASS

REM ====================================================================
REM Home Class - Goal slots at the top
REM ====================================================================
CLASS Home
    DIM col AS INTEGER
    DIM filled AS INTEGER

    SUB Init(c AS INTEGER)
        col = c
        filled = 0
    END SUB

    FUNCTION GetCol() AS INTEGER
        GetCol = col
    END FUNCTION

    FUNCTION IsFilled() AS INTEGER
        IsFilled = filled
    END FUNCTION

    SUB Fill()
        filled = 1
    END SUB

    SUB Reset()
        filled = 0
    END SUB
END CLASS
