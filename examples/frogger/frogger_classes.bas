REM ====================================================================
REM Game Object Classes for Frogger
REM Demonstrates OOP principles in Viper BASIC
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

    SUB SetRow(r AS INTEGER)
        row = r
    END SUB

    SUB SetCol(c AS INTEGER)
        col = c
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
REM Demonstrates: nested objects, state management, boundary checking
REM ====================================================================
CLASS Frog
    DIM pos AS Position
    DIM lives AS INTEGER
    DIM alive AS INTEGER  ' BUG-106 workaround: can't name same as method
    DIM startRow AS INTEGER
    DIM startCol AS INTEGER

    SUB Init(r AS INTEGER, c AS INTEGER)
        pos = NEW Position()
        pos.Init(r, c)
        startRow = r
        startCol = c
        lives = 3
        alive = 1
    END SUB

    SUB MoveUp()
        DIM newRow AS INTEGER
        newRow = pos.GetRow() - 1
        IF newRow >= 0 THEN
            pos.SetRow(newRow)
        END IF
    END SUB

    SUB MoveDown()
        DIM newRow AS INTEGER
        newRow = pos.GetRow() + 1
        IF newRow <= 23 THEN
            pos.SetRow(newRow)
        END IF
    END SUB

    SUB MoveLeft()
        DIM newCol AS INTEGER
        newCol = pos.GetCol() - 1
        IF newCol >= 1 THEN
            pos.SetCol(newCol)
        END IF
    END SUB

    SUB MoveRight()
        DIM newCol AS INTEGER
        newCol = pos.GetCol() + 1
        IF newCol <= 70 THEN
            pos.SetCol(newCol)
        END IF
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
            REM Reset to start position when lives remain
            pos.MoveTo(startRow, startCol)
        END IF
    END SUB

    FUNCTION GetLives() AS INTEGER
        GetLives = lives
    END FUNCTION

    FUNCTION IsAlive() AS INTEGER
        IsAlive = alive
    END FUNCTION

    SUB Reset()
        pos.MoveTo(startRow, startCol)
    END SUB
END CLASS

REM ====================================================================
REM Car Class - Moving obstacle
REM Demonstrates: object arrays, collision detection, wrapping behavior
REM ====================================================================
CLASS Car
    DIM pos AS Position
    DIM speed AS INTEGER
    DIM direction AS INTEGER  ' 1 = right, -1 = left
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

        REM Wrap around screen edges
        IF newCol > 75 THEN
            newCol = 1 - width
        END IF
        IF newCol < (0 - width) THEN
            newCol = 75
        END IF

        pos.SetCol(newCol)
    END SUB

    FUNCTION GetRow() AS INTEGER
        GetRow = pos.GetRow()
    END FUNCTION

    FUNCTION GetCol() AS INTEGER
        GetCol = pos.GetCol()
    END FUNCTION

    FUNCTION GetSymbol() AS STRING
        GetSymbol = symbol
    END FUNCTION

    FUNCTION GetWidth() AS INTEGER
        GetWidth = width
    END FUNCTION

    REM Check if car collides with given position
    FUNCTION CheckCollision(frogRow AS INTEGER, frogCol AS INTEGER) AS INTEGER
        DIM carRow AS INTEGER
        DIM carCol AS INTEGER
        DIM i AS INTEGER
        DIM hit AS INTEGER

        carRow = pos.GetRow()
        carCol = pos.GetCol()
        hit = 0

        REM Check if frog is on same row
        IF frogRow = carRow THEN
            REM Check each cell of car's width
            FOR i = 0 TO width - 1
                IF frogCol = carCol + i THEN
                    hit = 1
                END IF
            NEXT i
        END IF

        CheckCollision = hit
    END FUNCTION
END CLASS
