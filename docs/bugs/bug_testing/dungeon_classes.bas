REM ═══════════════════════════════════════════════════════
REM  DUNGEON CLASSES - Shared class definitions
REM ═══════════════════════════════════════════════════════

REM ===== ITEM CLASS =====
CLASS Item
    name AS STRING
    description AS STRING
    goldValue AS INTEGER

    SUB SetData(itemName AS STRING, desc AS STRING, gold AS INTEGER)
        ME.name = itemName
        ME.description = desc
        ME.goldValue = gold
    END SUB

    SUB Display()
        PRINT "  🗡 "; ME.name
        PRINT "     "; ME.description
        PRINT "     Value: "; ME.goldValue; " gold"
    END SUB
END CLASS

REM ===== MONSTER CLASS =====
CLASS Monster
    name AS STRING
    health AS INTEGER
    attack AS INTEGER

    SUB SetData(monsterName AS STRING, hp AS INTEGER, atk AS INTEGER)
        ME.name = monsterName
        ME.health = hp
        ME.attack = atk
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
END CLASS

REM ===== PLAYER CLASS =====
CLASS Player
    name AS STRING
    health AS INTEGER
    gold AS INTEGER
    weapon AS INTEGER

    SUB SetData(playerName AS STRING)
        ME.name = playerName
        ME.health = 100
        ME.gold = 0
        ME.weapon = 5
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

    SUB DisplayStatus()
        PRINT "╔════════════════════════════╗"
        PRINT "║ "; ME.name
        PRINT "╠════════════════════════════╣"
        PRINT "║ HP: "; ME.health; "  │  Gold: "; ME.gold
        PRINT "║ Weapon: +"; ME.weapon
        PRINT "╚════════════════════════════╝"
    END SUB
END CLASS

PRINT "✓ Classes loaded from dungeon_classes.bas"
