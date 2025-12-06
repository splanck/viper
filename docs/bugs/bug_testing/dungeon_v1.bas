REM Dungeon of Viper - V1: Testing basic OOP
REM Workaround: No array fields (BUG-NEW)
REM Workaround: Using ME.field for field access (BUG-065)

REM Item class
CLASS Item
    name AS STRING
    description AS STRING
    value AS INTEGER

    SUB Init(itemName AS STRING, desc AS STRING, val AS INTEGER)
        ME.name = itemName
        ME.description = desc
        ME.value = val
    END SUB

    FUNCTION GetName() AS STRING
        GetName = ME.name
    END FUNCTION

    FUNCTION GetDescription() AS STRING
        GetDescription = ME.description
    END FUNCTION

    SUB Display()
        PRINT ME.name; " ("; ME.value; " gold)"
        PRINT "  "; ME.description
    END SUB
END CLASS

REM Player class
CLASS Player
    name AS STRING
    health AS INTEGER
    maxHealth AS INTEGER
    gold AS INTEGER

    SUB Init(playerName AS STRING)
        ME.name = playerName
        ME.health = 100
        ME.maxHealth = 100
        ME.gold = 0
    END SUB

    SUB TakeDamage(amount AS INTEGER)
        ME.health = ME.health - amount
        IF ME.health < 0 THEN
            ME.health = 0
        END IF
    END SUB

    SUB Heal(amount AS INTEGER)
        ME.health = ME.health + amount
        IF ME.health > ME.maxHealth THEN
            ME.health = ME.maxHealth
        END IF
    END SUB

    FUNCTION IsAlive() AS BOOLEAN
        IsAlive = ME.health > 0
    END FUNCTION

    FUNCTION GetHealthPercent() AS INTEGER
        IF ME.maxHealth = 0 THEN
            GetHealthPercent = 0
        ELSE
            GetHealthPercent = (ME.health * 100) / ME.maxHealth
        END IF
    END FUNCTION

    SUB DisplayStatus()
        PRINT "=== "; ME.name; " ==="
        PRINT "Health: [";
        DIM pct AS INTEGER
        pct = ME.GetHealthPercent()
        REM Draw health bar
        DIM bars AS INTEGER
        bars = pct / 10
        DIM i AS INTEGER
        FOR i = 1 TO 10
            IF i <= bars THEN
                PRINT "#";
            ELSE
                PRINT "-";
            END IF
        NEXT i
        PRINT "] "; ME.health; "/"; ME.maxHealth
        PRINT "Gold: "; ME.gold
    END SUB
END CLASS

REM Test the classes
PRINT "=== DUNGEON OF VIPER - Class Test ==="
PRINT

DIM sword AS Item
sword.Init("Iron Sword", "A sturdy iron blade", 50)
PRINT "Created item:"
sword.Display()
PRINT

DIM hero AS Player
hero.Init("Brave Adventurer")
PRINT "Created player:"
hero.DisplayStatus()
PRINT

PRINT ">>> Taking 30 damage!"
hero.TakeDamage(30)
hero.DisplayStatus()
PRINT

PRINT ">>> Healing 20 HP!"
hero.Heal(20)
hero.DisplayStatus()
PRINT

PRINT ">>> Taking fatal damage!"
hero.TakeDamage(150)
hero.DisplayStatus()
PRINT "Alive? "; hero.IsAlive()
PRINT

PRINT "=== Test Complete! ==="
