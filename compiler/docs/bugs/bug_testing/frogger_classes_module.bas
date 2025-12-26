REM ═══════════════════════════════════════════════════════
REM  FROGGER CLASSES MODULE - For AddFile testing
REM ═══════════════════════════════════════════════════════

CLASS Frog
    x AS INTEGER
    y AS INTEGER
    score AS INTEGER

    SUB Init()
        ME.x = 20
        ME.y = 10
        ME.score = 0
    END SUB

    SUB MoveUp()
        IF ME.y > 1 THEN
            ME.y = ME.y - 1
            ME.score = ME.score + 5
        END IF
    END SUB

    FUNCTION AtGoal() AS INTEGER
        IF ME.y <= 1 THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION
END CLASS

CLASS Obstacle
    x AS INTEGER
    y AS INTEGER
    speed AS INTEGER

    SUB Init(lane AS INTEGER, spd AS INTEGER, startPos AS INTEGER)
        ME.y = lane
        ME.speed = spd
        ME.x = startPos
    END SUB

    SUB Move()
        ME.x = ME.x + ME.speed
        IF ME.x > 30 THEN
            ME.x = 1
        END IF
        IF ME.x < 1 THEN
            ME.x = 30
        END IF
    END SUB

    FUNCTION HitsFrog(frogX AS INTEGER, frogY AS INTEGER) AS INTEGER
        IF ME.y = frogY AND ME.x = frogX THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION
END CLASS

PRINT "✓ Frogger classes loaded via AddFile"
