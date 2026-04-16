' ============================================================================
' MODULE: frogger_classes.bas
' PURPOSE: Defines the four game-object classes: Position (2D coord),
'          Frog (player), Vehicle (road obstacle), Platform (river
'          platform), and Home (goal slot).
'
' WHERE-THIS-FITS: Loaded after frogger_ansi.bas. Consumed by frogger.bas
'          which allocates a Frog, 10 Vehicles, 10 Platforms, and 5 Homes
'          in `InitGame`, then ticks them each frame.
'
' KEY-DESIGN-CHOICES:
'   * POSITION IS A VALUE RECORD. Tiny class with just row/col plus
'     mutator helpers. Every entity holds its own Position rather than
'     inheriting from it so the entity classes can evolve independently
'     — for example, Vehicle and Platform both hold a Position but they
'     also carry direction, width, and symbol that Frog doesn't need.
'   * TWO PARALLEL OBSTACLE CLASSES. `Vehicle` (road) and `Platform`
'     (river) look nearly identical: both hold position, speed,
'     direction, symbol, and width; both have a `Move` method with
'     identical wraparound logic; both have a "does the frog overlap me"
'     test. They stay separate because their SEMANTIC role differs:
'     vehicles KILL the frog on contact, platforms CARRY the frog.
'     Keeping them as distinct classes makes the collision code in
'     frogger.bas self-documenting ("is the frog on a platform?" reads
'     differently from "did a vehicle hit the frog?").
'   * WIDTH-AWARE COLLISION. `CheckCollision` and `CheckOnPlatform` both
'     walk the width of the object cell-by-cell testing if the frog's
'     single cell overlaps any of the obstacle's cells. A frog is always
'     1 cell wide; a truck might be 6. The loop compares `frogCol =
'     pos.GetCol() + i` rather than doing a half-open range check so the
'     logic is dead simple.
'   * WRAPAROUND ON MOVE. `Move` wraps from the right edge back to
'     `1 - width` (so a right-going object re-enters smoothly from the
'     left) and from the left edge to column 75. This creates the
'     conveyor-belt feel of Frogger's traffic and river without needing
'     an explicit spawn system.
'   * PLATFORM-RIDING AS EXPLICIT STATE. The Frog class carries
'     `onPlatform` + `platformSpeed` flags. The game loop calls
'     `SetOnPlatform` when it finds the frog overlapping a platform and
'     `UpdateOnPlatform` to apply the drift. If the frog moves off the
'     platform or the water, `ClearPlatform` resets the state. This
'     keeps the river-physics logic out of the frog and leaves the
'     decision "am I on something?" to the caller — which makes it
'     easy to extend with new platform types (e.g., diving turtles).
'
' HOW-TO-READ: Position (trivial) -> Frog (movement + life handling) ->
'   Vehicle (wraparound + collision) -> Platform (same shape, different
'   semantics) -> Home (goal-slot state).
' ============================================================================

' ===========================================================================
' Class Position
'   A 2D coordinate record. Used as a component inside Frog, Vehicle, and
'   Platform — each of those classes owns one.
' ===========================================================================
CLASS Position
    DIM row AS INTEGER
    DIM col AS INTEGER

    ' Set row/col explicitly. The conventional "constructor" for this
    ' record. Called by owning classes right after `NEW Position()`.
    SUB Init(r AS INTEGER, c AS INTEGER)
        row = r
        col = c
    END SUB

    ' Overwrite the coordinate. Preferred over `Init` inside update code
    ' because the name implies "move" not "construct".
    SUB MoveTo(r AS INTEGER, c AS INTEGER)
        row = r
        col = c
    END SUB

    ' Relative motion: add `(dr, dc)` to the current coordinate.
    ' Not currently used — `MoveTo` + arithmetic is idiomatic in
    ' frogger.bas — but kept for extensibility.
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

' ===========================================================================
' Class Frog
'   The player avatar. Owns its current position, the starting position
'   to respawn at, lives count, alive flag, and the "am I riding a
'   platform?" state that binds the frog to river logs/turtles.
' ===========================================================================
CLASS Frog
    DIM pos AS Position
    DIM lives AS INTEGER
    DIM alive AS INTEGER
    DIM startRow AS INTEGER
    DIM startCol AS INTEGER
    DIM onPlatform AS INTEGER
    DIM platformSpeed AS INTEGER

    ' Construct a frog at (r, c) with 3 lives and no platform attached.
    ' Stores (r, c) separately as start coords so `Die` can respawn.
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

    ' One-cell movement with bounds clamping. The bounds (1..24 rows,
    ' 1..70 cols) match the playfield dimensions used by frogger.bas.
    ' Each call is a try-and-maybe-reject — no "try then undo" pattern
    ' needed because we check BEFORE mutating.
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

    ' Apply platform drift. Called by UpdateGame each frame when the frog
    ' is riding a log or turtle. The platform's speed * direction is
    ' baked into `platformSpeed` so we just add it to the column. If the
    ' drift would push the frog off-screen, the move is skipped — the
    ' caller's bounds check in UpdateGame then detects the frog sitting
    ' at the edge and applies a death.
    SUB UpdateOnPlatform()
        IF onPlatform = 1 THEN
            DIM newCol AS INTEGER
            newCol = pos.GetCol() + platformSpeed
            IF newCol >= 1 AND newCol <= 70 THEN
                pos.MoveTo(pos.GetRow(), newCol)
            END IF
        END IF
    END SUB

    ' Attach the frog to a platform with the given signed drift speed.
    ' Called from UpdateGame when a river-row collision check succeeds.
    SUB SetOnPlatform(speed AS INTEGER)
        onPlatform = 1
        platformSpeed = speed
    END SUB

    ' Detach the frog from any platform. Called when the frog leaves the
    ' river, when it dies, or when the frame's platform search finds
    ' nothing.
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

    ' Death handler. Decrements lives; on zero, flips `alive` so the main
    ' loop can exit. Otherwise respawns at the starting coords and clears
    ' the platform state. The main loop calls this on collision, drown,
    ' or bad-home-landing.
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

    ' Respawn WITHOUT losing a life. Called on a successful home landing
    ' to set the frog back at the bottom for another crossing.
    SUB Reset()
        pos.MoveTo(startRow, startCol)
        onPlatform = 0
        platformSpeed = 0
    END SUB
END CLASS

' ===========================================================================
' Class Vehicle
'   A car or truck on the road. Moves horizontally at `speed * direction`
'   and wraps from one edge to the other. Kills the frog on overlap.
' ===========================================================================
CLASS Vehicle
    DIM pos AS Position
    DIM speed AS INTEGER     ' cells per tick, always positive
    DIM direction AS INTEGER ' +1 = right, -1 = left
    DIM symbol AS STRING     ' glyph used for rendering ("=", "#", etc.)
    DIM width AS INTEGER     ' number of cells the vehicle occupies

    SUB Init(r AS INTEGER, c AS INTEGER, spd AS INTEGER, dir AS INTEGER, sym AS STRING, w AS INTEGER)
        pos = NEW Position()
        pos.Init(r, c)
        speed = spd
        direction = dir
        symbol = sym
        width = w
    END SUB

    ' Advance the vehicle one tick. Wraps around:
    '   * If `newCol > 75` (moving right off the field), reappear at
    '     `1 - width` so the vehicle slides back on smoothly.
    '   * If `newCol < 0 - width` (moving left off the field), reappear
    '     at column 75.
    ' The 75 and (0 - width) magic numbers bracket a 70-col playfield
    ' with a small buffer so off-screen vehicles get one tick of
    ' invisible travel before returning, which looks more natural than
    ' snapping straight to the opposite edge.
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

    ' Return 1 if the frog at (frogRow, frogCol) overlaps any of this
    ' vehicle's width cells. Tests row equality first (cheap) before
    ' scanning the width. Returns as soon as an overlap is found.
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

' ===========================================================================
' Class Platform
'   A log or turtle floating in the river. Shape is IDENTICAL to Vehicle
'   but the semantics flip: overlapping the frog SAVES it rather than
'   killing it. Kept as a separate class so call sites in frogger.bas
'   read as "is the frog on a platform?" rather than "is the frog on a
'   moving-thing-at-this-row?" — a small clarity win that costs a bit of
'   duplicated code.
' ===========================================================================
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

    ' Same wraparound rule as Vehicle.Move.
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

    ' Same shape as Vehicle.CheckCollision, renamed for legibility at the
    ' call site. Returns 1 if the frog is riding this platform.
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

' ===========================================================================
' Class Home
'   One of the five goal slots at the top of the screen. Tracks only its
'   column (the row is always HOME_ROW) and a filled flag. The main loop
'   calls `Fill` when the frog lands and `Reset` between games.
' ===========================================================================
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
