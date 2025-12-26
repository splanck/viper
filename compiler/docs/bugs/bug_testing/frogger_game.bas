REM ╔════════════════════════════════════════════════════════╗
REM ║                 VIPER FROGGER                          ║
REM ║          Full Game with OOP & ANSI Graphics            ║
REM ╚════════════════════════════════════════════════════════╝
REM
REM Workarounds applied:
REM - BUG-067: No array fields in classes
REM - BUG-068: Using RETURN keyword
REM - BUG-069: Using NEW to allocate objects
REM - BUG-070: Using INTEGER instead of BOOLEAN
REM - BUG-071: No string arrays (integer arrays only)

REM ===== FROG CLASS =====
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

    SUB MoveDown()
        IF ME.y < 13 THEN
            ME.y = ME.y + 1
        END IF
    END SUB

    SUB MoveLeft()
        IF ME.x > 2 THEN
            ME.x = ME.x - 2
        END IF
    END SUB

    SUB MoveRight()
        IF ME.x < 38 THEN
            ME.x = ME.x + 2
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

REM ===== CAR CLASS =====
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
            REM Car is 2 chars wide
            IF frogX >= ME.x AND frogX <= ME.x + 1 THEN
                RETURN 1
            END IF
        END IF
        RETURN 0
    END FUNCTION
END CLASS

REM ╔════════════════════════════════════════════════════════╗
REM ║                    MAIN GAME                           ║
REM ╚════════════════════════════════════════════════════════╝

REM Create frog
DIM frog AS Frog
frog = NEW Frog()
frog.Init()

REM Create cars (using separate variables since no arrays in classes)
DIM car1 AS Car
DIM car2 AS Car
DIM car3 AS Car
DIM car4 AS Car
DIM car5 AS Car

car1 = NEW Car()
car2 = NEW Car()
car3 = NEW Car()
car4 = NEW Car()
car5 = NEW Car()

REM Initialize cars at different lanes
car1.Init(4, 1, 1, 1)
car2.Init(6, 1, -1, 40)
car3.Init(8, 2, 1, 10)
car4.Init(10, 1, -1, 30)
car5.Init(12, 2, 1, 20)

REM Game loop simulation
DIM gameRunning AS INTEGER
DIM frame AS INTEGER
DIM maxFrames AS INTEGER

gameRunning = 1
frame = 0
maxFrames = 30

WHILE gameRunning AND frame < maxFrames
    REM Clear screen area (in real game would use LOCATE)
    PRINT
    PRINT "╔════════════════════════════════════════╗"
    PRINT "║         VIPER FROGGER  GAME            ║"
    PRINT "╚════════════════════════════════════════╝"

    REM Display status
    COLOR 15, 0
    PRINT "Lives: ";
    DIM i AS INTEGER
    FOR i = 1 TO frog.lives
        COLOR 12, 0
        PRINT "♥";
    NEXT i
    COLOR 15, 0
    PRINT "  Score: "; frog.score; "  Frame: "; frame
    PRINT

    REM Draw game board
    COLOR 11, 0
    PRINT "GOAL:  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

    REM Draw road lanes with cars
    DIM lane AS INTEGER
    FOR lane = 3 TO 13
        COLOR 8, 0
        PRINT "ROAD"; lane; ": ";

        REM Draw cars on this lane
        DIM drawn AS INTEGER
        drawn = 0

        IF lane = car1.y THEN
            DIM pos AS INTEGER
            FOR pos = 1 TO 40
                IF pos = car1.x OR pos = car1.x + 1 THEN
                    COLOR 12, 0
                    PRINT "█";
                    drawn = 1
                ELSEIF pos = frog.x AND lane = frog.y THEN
                    COLOR 10, 0
                    PRINT "F";
                    drawn = 1
                ELSE
                    IF drawn = 0 THEN
                        COLOR 8, 0
                        PRINT "▓";
                    ELSE
                        drawn = 0
                        COLOR 8, 0
                        PRINT "▓";
                    END IF
                END IF
            NEXT pos
        ELSEIF lane = car2.y THEN
            FOR pos = 1 TO 40
                IF pos = car2.x OR pos = car2.x + 1 THEN
                    COLOR 12, 0
                    PRINT "█";
                ELSEIF pos = frog.x AND lane = frog.y THEN
                    COLOR 10, 0
                    PRINT "F";
                ELSE
                    COLOR 8, 0
                    PRINT "▓";
                END IF
            NEXT pos
        ELSEIF lane = frog.y THEN
            REM Draw frog's lane
            FOR pos = 1 TO 40
                IF pos = frog.x THEN
                    COLOR 10, 0
                    PRINT "F";
                ELSE
                    COLOR 8, 0
                    PRINT "▓";
                END IF
            NEXT pos
        ELSE
            REM Empty lane
            FOR pos = 1 TO 40
                COLOR 8, 0
                PRINT "▓";
            NEXT pos
        END IF
        PRINT
    NEXT lane

    COLOR 10, 0
    PRINT "START: ════════════════════════════════"
    PRINT

    REM Update cars
    car1.Update()
    car2.Update()
    car3.Update()
    car4.Update()
    car5.Update()

    REM Check collisions
    IF car1.Hits(frog.x, frog.y) OR car2.Hits(frog.x, frog.y) OR car3.Hits(frog.x, frog.y) OR car4.Hits(frog.x, frog.y) OR car5.Hits(frog.x, frog.y) THEN
        COLOR 12, 0
        PRINT ">>> CRASH! Frog hit by car! <<<"
        frog.Die()
        IF frog.Alive() = 0 THEN
            gameRunning = 0
        END IF
    END IF

    REM Check win condition
    IF frog.Won() THEN
        COLOR 10, 0
        PRINT ">>> VICTORY! Frog reached the goal! <<<"
        frog.score = frog.score + 100
        REM Reset frog position
        frog.x = 20
        frog.y = 13
    END IF

    REM Simulate player movement (auto-move up for demo)
    IF frame MOD 3 = 0 THEN
        frog.MoveUp()
    END IF

    frame = frame + 1
    PRINT "Press any key to continue..."
    PRINT "─────────────────────────────────────────"
    PRINT
WEND

REM Game over screen
PRINT
PRINT "╔════════════════════════════════════════╗"
IF frog.Alive() = 0 THEN
    COLOR 12, 0
    PRINT "║          GAME OVER!                    ║"
ELSE
    COLOR 10, 0
    PRINT "║       DEMO COMPLETE!                   ║"
END IF
COLOR 15, 0
PRINT "╠════════════════════════════════════════╣"
PRINT "║  Final Score: "; frog.score
PRINT "║  Lives Remaining: "; frog.lives
PRINT "╚════════════════════════════════════════╝"

PRINT
PRINT "╔════════════════════════════════════════╗"
PRINT "║     FROGGER STRESS TEST COMPLETE!      ║"
PRINT "║                                        ║"
PRINT "║  ✓ OOP classes (Frog, Car x5)         ║"
PRINT "║  ✓ Game loop with state management    ║"
PRINT "║  ✓ Collision detection                ║"
PRINT "║  ✓ ANSI graphics & colors              ║"
PRINT "║  ✓ Multiple object instances           ║"
PRINT "║  ✓ Complex conditionals                ║"
PRINT "║  ✓ Animation (car movement)            ║"
PRINT "╚════════════════════════════════════════╝"
