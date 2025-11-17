REM ═══════════════════════════════════════════════════════
REM  FROGGER ENTITIES - Game object classes
REM  Using workarounds: NEW required, INTEGER for booleans
REM ═══════════════════════════════════════════════════════

REM ===== FROG CLASS =====
CLASS Frog
    x AS INTEGER
    y AS INTEGER
    lives AS INTEGER
    score AS INTEGER
    alive AS INTEGER

    SUB Init()
        ME.x = 20
        ME.y = 12
        ME.lives = 3
        ME.score = 0
        ME.alive = 1
    END SUB

    SUB MoveUp()
        IF ME.y > 1 THEN
            ME.y = ME.y - 1
            ME.score = ME.score + 10
        END IF
    END SUB

    SUB MoveDown()
        IF ME.y < 12 THEN
            ME.y = ME.y + 1
        END IF
    END SUB

    SUB MoveLeft()
        IF ME.x > 1 THEN
            ME.x = ME.x - 1
        END IF
    END SUB

    SUB MoveRight()
        IF ME.x < 40 THEN
            ME.x = ME.x + 1
        END IF
    END SUB

    SUB Die()
        ME.lives = ME.lives - 1
        IF ME.lives <= 0 THEN
            ME.alive = 0
        END IF
        REM Reset position
        ME.x = 20
        ME.y = 12
    END SUB

    SUB Draw()
        LOCATE ME.y, ME.x
        COLOR 10, 0
        PRINT "🐸";
    END SUB

    SUB ShowStatus()
        LOCATE 14, 1
        COLOR 15, 0
        PRINT "Lives: ";
        DIM i AS INTEGER
        FOR i = 1 TO ME.lives
            COLOR 12, 0
            PRINT "♥";
        NEXT i
        COLOR 15, 0
        PRINT "  Score: "; ME.score
    END SUB
END CLASS

REM ===== CAR CLASS =====
CLASS Car
    x AS INTEGER
    y AS INTEGER
    speed AS INTEGER
    direction AS INTEGER
    symbol AS STRING

    SUB Init(lane AS INTEGER, spd AS INTEGER, dir AS INTEGER)
        ME.y = lane
        ME.speed = spd
        ME.direction = dir
        IF dir > 0 THEN
            ME.x = 1
            ME.symbol = ">"
        ELSE
            ME.x = 40
            ME.symbol = "<"
        END IF
    END SUB

    SUB Update()
        ME.x = ME.x + (ME.speed * ME.direction)
        REM Wrap around
        IF ME.direction > 0 AND ME.x > 40 THEN
            ME.x = 1
        END IF
        IF ME.direction < 0 AND ME.x < 1 THEN
            ME.x = 40
        END IF
    END SUB

    SUB Draw()
        LOCATE ME.y, ME.x
        COLOR 12, 0
        PRINT "█";
    END SUB

    FUNCTION CheckCollision(frogX AS INTEGER, frogY AS INTEGER) AS INTEGER
        IF ME.y = frogY THEN
            IF ME.x = frogX OR ME.x = frogX - 1 OR ME.x = frogX + 1 THEN
                RETURN 1
            END IF
        END IF
        RETURN 0
    END FUNCTION
END CLASS

REM ===== TEST THE CLASSES =====
PRINT "╔════════════════════════════════════════╗"
PRINT "║     FROGGER ENTITIES TEST              ║"
PRINT "╚════════════════════════════════════════╝"
PRINT

REM Test Frog
DIM frog AS Frog
frog = NEW Frog()
frog.Init()
PRINT "Frog created at ("; frog.x; ", "; frog.y; ")"
PRINT "Lives: "; frog.lives; "  Score: "; frog.score
PRINT

REM Test movement
PRINT "Testing movement..."
frog.MoveUp()
PRINT "After MoveUp: ("; frog.x; ", "; frog.y; ") Score: "; frog.score
frog.MoveLeft()
PRINT "After MoveLeft: ("; frog.x; ", "; frog.y; ")"
frog.MoveRight()
frog.MoveRight()
PRINT "After 2x MoveRight: ("; frog.x; ", "; frog.y; ")"
PRINT

REM Test Car
DIM car1 AS Car
car1 = NEW Car()
car1.Init(5, 1, 1)
PRINT "Car created at lane "; car1.y; ", moving right"
PRINT "Position: "; car1.x

REM Simulate car movement
DIM tick AS INTEGER
FOR tick = 1 TO 5
    car1.Update()
    PRINT "Tick "; tick; ": x = "; car1.x
NEXT tick
PRINT

REM Test collision
DIM hit AS INTEGER
hit = car1.CheckCollision(frog.x, frog.y)
PRINT "Collision test: "; hit; " (should be 0)"

REM Move frog to car's lane
frog.y = car1.y
frog.x = car1.x
hit = car1.CheckCollision(frog.x, frog.y)
PRINT "After moving frog to car position: "; hit; " (should be 1)"
PRINT

PRINT "✓ Entity classes working!"
