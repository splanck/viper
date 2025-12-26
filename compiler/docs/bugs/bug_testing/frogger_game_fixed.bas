REM ╔════════════════════════════════════════════════════════╗
REM ║                 VIPER FROGGER                          ║
REM ╚════════════════════════════════════════════════════════╝

CLASS Frog
    x AS INTEGER
    y AS INTEGER
    lives AS INTEGER
    score AS INTEGER

    SUB Init()
        ME.x = 20
        ME.y = 13
        ME.lives = 3
        ME.score = 0
    END SUB

    SUB MoveUp()
        IF ME.y > 2 THEN
            ME.y = ME.y - 1
            ME.score = ME.score + 10
        END IF
    END SUB

    SUB Die()
        ME.lives = ME.lives - 1
        ME.x = 20
        ME.y = 13
    END SUB

    FUNCTION Won() AS INTEGER
        IF ME.y <= 2 THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION

    FUNCTION Alive() AS INTEGER
        IF ME.lives > 0 THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION
END CLASS

CLASS Car
    x AS INTEGER
    y AS INTEGER
    speed AS INTEGER
    direction AS INTEGER

    SUB Init(lane AS INTEGER, spd AS INTEGER, dir AS INTEGER, startX AS INTEGER)
        ME.y = lane
        ME.speed = spd
        ME.direction = dir
        ME.x = startX
    END SUB

    SUB Update()
        ME.x = ME.x + (ME.speed * ME.direction)
        IF ME.direction > 0 AND ME.x > 40 THEN
            ME.x = 1
        END IF
        IF ME.direction < 0 AND ME.x < 1 THEN
            ME.x = 40
        END IF
    END SUB

    FUNCTION Hits(frogX AS INTEGER, frogY AS INTEGER) AS INTEGER
        IF ME.y = frogY THEN
            IF frogX >= ME.x AND frogX <= ME.x + 1 THEN
                RETURN 1
            END IF
        END IF
        RETURN 0
    END FUNCTION
END CLASS

REM ═══ MAIN GAME ═══

DIM frog AS Frog
frog = NEW Frog()
frog.Init()

DIM car1 AS Car
DIM car2 AS Car
DIM car3 AS Car

car1 = NEW Car()
car2 = NEW Car()
car3 = NEW Car()

car1.Init(5, 1, 1, 1)
car2.Init(7, 1, -1, 35)
car3.Init(9, 2, 1, 15)

DIM frame AS INTEGER
frame = 0

PRINT "╔════════════════════════════════════════╗"
PRINT "║         VIPER FROGGER  DEMO            ║"
PRINT "╚════════════════════════════════════════╝"
PRINT

WHILE frog.Alive() AND frame < 25
    PRINT "── Frame "; frame; " ──"
    
    REM Status
    COLOR 15, 0
    PRINT "Lives: ";
    DIM i AS INTEGER
    FOR i = 1 TO frog.lives
        COLOR 12, 0
        PRINT "♥";
    NEXT i
    COLOR 15, 0
    PRINT "  Score: "; frog.score
    
    REM Simple board
    COLOR 11, 0
    PRINT "GOAL [Y=2]:  ~~~~~~~~~~~~~~"
    COLOR 8, 0
    PRINT "ROAD [Y=5]:  ";
    IF car1.y = 5 THEN
        COLOR 12, 0
        PRINT "██";
    END IF
    PRINT
    COLOR 8, 0
    PRINT "ROAD [Y=7]:  ";
    IF car2.y = 7 THEN
        COLOR 12, 0
        PRINT "██";
    END IF
    PRINT
    COLOR 8, 0
    PRINT "ROAD [Y=9]:  ";
    IF car3.y = 9 THEN
        COLOR 12, 0
        PRINT "██";
    END IF
    PRINT
    COLOR 10, 0
    PRINT "FROG [Y="; frog.y; ", X="; frog.x; "]  🐸"
    COLOR 10, 0
    PRINT "START [Y=13]: ═════════════"
    PRINT
    
    REM Update cars
    car1.Update()
    car2.Update()
    car3.Update()
    
    REM Check collisions
    IF car1.Hits(frog.x, frog.y) THEN
        COLOR 12, 0
        PRINT ">>> CAR 1 HIT! <<<"
        frog.Die()
    END IF
    IF car2.Hits(frog.x, frog.y) THEN
        COLOR 12, 0
        PRINT ">>> CAR 2 HIT! <<<"
        frog.Die()
    END IF
    IF car3.Hits(frog.x, frog.y) THEN
        COLOR 12, 0
        PRINT ">>> CAR 3 HIT! <<<"
        frog.Die()
    END IF
    
    REM Check win
    IF frog.Won() THEN
        COLOR 10, 0
        PRINT ">>> VICTORY! +100 points! <<<"
        frog.score = frog.score + 100
        frog.x = 20
        frog.y = 13
    END IF
    
    REM Auto-move frog up
    IF frame MOD 2 = 0 THEN
        frog.MoveUp()
    END IF
    
    frame = frame + 1
    PRINT
WEND

PRINT "╔════════════════════════════════════════╗"
IF frog.Alive() THEN
    COLOR 10, 0
    PRINT "║       GAME COMPLETE!                   ║"
ELSE
    COLOR 12, 0
    PRINT "║          GAME OVER!                    ║"
END IF
COLOR 15, 0
PRINT "╠════════════════════════════════════════╣"
PRINT "║  Final Score: "; frog.score
PRINT "║  Lives: "; frog.lives
PRINT "╚════════════════════════════════════════╝"
PRINT
PRINT "✓ Frogger stress test complete!"
