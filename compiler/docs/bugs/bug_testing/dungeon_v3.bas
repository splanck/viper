REM === DUNGEON OF VIPER V3 ===
REM Workaround: Using INTEGER instead of BOOLEAN for parameters (BUG-069?)

REM ===== ITEM CLASS =====
CLASS Item
    name AS STRING
    description AS STRING
    value AS INTEGER
    isWeapon AS INTEGER

    SUB Init(itemName AS STRING, desc AS STRING, val AS INTEGER, weapon AS INTEGER)
        ME.name = itemName
        ME.description = desc
        ME.value = val
        ME.isWeapon = weapon
    END SUB

    FUNCTION GetName() AS STRING
        RETURN ME.name
    END FUNCTION

    SUB Display()
        IF ME.isWeapon THEN
            PRINT "[WEAPON] ";
        ELSE
            PRINT "[ITEM] ";
        END IF
        PRINT ME.name; " ("; ME.value; "g)"
        PRINT "   "; ME.description
    END SUB
END CLASS

REM ===== PLAYER CLASS =====
CLASS Player
    name AS STRING
    health AS INTEGER
    maxHealth AS INTEGER

    SUB Init(playerName AS STRING)
        ME.name = playerName
        ME.health = 100
        ME.maxHealth = 100
    END SUB

    SUB TakeDamage(dmg AS INTEGER)
        ME.health = ME.health - dmg
        IF ME.health < 0 THEN
            ME.health = 0
        END IF
    END SUB

    FUNCTION IsAlive() AS INTEGER
        IF ME.health > 0 THEN
            RETURN 1
        ELSE
            RETURN 0
        END IF
    END FUNCTION

    SUB DisplayHealthBar()
        DIM pct AS INTEGER
        DIM bars AS INTEGER
        DIM i AS INTEGER

        pct = (ME.health * 100) / ME.maxHealth
        bars = pct / 10

        PRINT "HP: [";
        FOR i = 1 TO 10
            IF i <= bars THEN
                PRINT "#";
            ELSE
                PRINT "-";
            END IF
        NEXT i
        PRINT "] "; ME.health; "/"; ME.maxHealth
    END SUB
END CLASS

PRINT "╔═══════════════════════════════════╗"
PRINT "║   DUNGEON TEST V3                ║"
PRINT "╚═══════════════════════════════════╝"
PRINT

DIM hero AS Player
hero.Init("Brave Hero")
hero.DisplayHealthBar()
PRINT

DIM sword AS Item
sword.Init("Steel Sword", "A sharp blade", 100, 1)
PRINT "Created: ";
sword.Display()
PRINT

DIM potion AS Item
potion.Init("Health Potion", "Heals 20 HP", 25, 0)
PRINT "Created: ";
potion.Display()
PRINT

PRINT "Testing damage..."
hero.TakeDamage(30)
hero.DisplayHealthBar()
PRINT

PRINT "More damage..."
hero.TakeDamage(50)
hero.DisplayHealthBar()
PRINT "Alive? "; hero.IsAlive()

PRINT
PRINT "=== Test passed! ==="
