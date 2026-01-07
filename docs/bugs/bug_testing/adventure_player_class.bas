REM ╔════════════════════════════════════════════════════════╗
REM ║     TEXT ADVENTURE - Player Class Test                ║
REM ╚════════════════════════════════════════════════════════╝
REM
REM Testing: Player with inventory (scalar workaround)
REM Workarounds: BUG-067 (no array fields in classes)

CLASS Player
    name AS STRING
    health AS INTEGER
    item1 AS STRING
    item2 AS STRING
    item3 AS STRING
    itemCount AS INTEGER

    SUB Init(playerName AS STRING)
        ME.name = playerName
        ME.health = 100
        ME.item1 = ""
        ME.item2 = ""
        ME.item3 = ""
        ME.itemCount = 0
    END SUB

    FUNCTION AddItem(item AS STRING) AS INTEGER
        IF ME.itemCount >= 3 THEN
            COLOR 12, 0
            PRINT "Inventory full!"
            COLOR 15, 0
            RETURN 0
        END IF

        ME.itemCount = ME.itemCount + 1

        IF ME.itemCount = 1 THEN
            ME.item1 = item
        ELSEIF ME.itemCount = 2 THEN
            ME.item2 = item
        ELSEIF ME.itemCount = 3 THEN
            ME.item3 = item
        END IF

        COLOR 10, 0
        PRINT "Added "; item; " to inventory"
        COLOR 15, 0
        RETURN 1
    END FUNCTION

    SUB ShowInventory()
        COLOR 14, 0
        PRINT "═══ "; ME.name; "'s Inventory ═══"
        COLOR 15, 0

        IF ME.itemCount = 0 THEN
            COLOR 8, 0
            PRINT "  (empty)"
            COLOR 15, 0
        ELSE
            IF ME.itemCount >= 1 AND ME.item1 <> "" THEN
                PRINT "  1. "; ME.item1
            END IF
            IF ME.itemCount >= 2 AND ME.item2 <> "" THEN
                PRINT "  2. "; ME.item2
            END IF
            IF ME.itemCount >= 3 AND ME.item3 <> "" THEN
                PRINT "  3. "; ME.item3
            END IF
        END IF
        PRINT
    END SUB

    SUB TakeDamage(amount AS INTEGER)
        ME.health = ME.health - amount
        IF ME.health < 0 THEN
            ME.health = 0
        END IF

        COLOR 12, 0
        PRINT ME.name; " takes "; amount; " damage! Health: "; ME.health
        COLOR 15, 0
    END SUB

    SUB Heal(amount AS INTEGER)
        ME.health = ME.health + amount
        IF ME.health > 100 THEN
            ME.health = 100
        END IF

        COLOR 10, 0
        PRINT ME.name; " heals "; amount; " HP! Health: "; ME.health
        COLOR 15, 0
    END SUB

    FUNCTION IsAlive() AS INTEGER
        IF ME.health > 0 THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION
END CLASS

REM ═══ TEST PLAYER CLASS ═══

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         PLAYER CLASS STRESS TEST                       ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

DIM hero AS Player
hero = NEW Player()
hero.Init("Sir Galahad")

PRINT "Player created: "; hero.name
PRINT "Starting health: "; hero.health
PRINT

REM Test inventory
PRINT "─── Testing Inventory ───"
hero.ShowInventory()

DIM success AS INTEGER
success = hero.AddItem("Rusty Sword")
success = hero.AddItem("Wooden Shield")
hero.ShowInventory()

success = hero.AddItem("Health Potion")
hero.ShowInventory()

REM Try to add when full
success = hero.AddItem("Gold Ring")
PRINT

REM Test health
PRINT "─── Testing Health System ───"
hero.TakeDamage(30)
hero.TakeDamage(25)
hero.Heal(20)
hero.TakeDamage(80)
PRINT

IF hero.IsAlive() THEN
    COLOR 10, 0
    PRINT "✓ Player is still alive"
    COLOR 15, 0
ELSE
    COLOR 12, 0
    PRINT "✗ Player is dead"
    COLOR 15, 0
END IF

PRINT
PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  PLAYER CLASS TEST COMPLETE!                           ║"
PRINT "╚════════════════════════════════════════════════════════╝"
