REM === DUNGEON OF VIPER V2 ===
REM Stress-testing VIPER BASIC OOP features
REM Workarounds: Using RETURN keyword (BUG-068), no array fields (BUG-067)

REM ===== ITEM CLASS =====
CLASS Item
    name AS STRING
    description AS STRING
    value AS INTEGER
    isWeapon AS BOOLEAN

    SUB Init(itemName AS STRING, desc AS STRING, val AS INTEGER, weapon AS BOOLEAN)
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

REM ===== MONSTER CLASS =====
CLASS Monster
    name AS STRING
    health AS INTEGER
    maxHealth AS INTEGER
    attack AS INTEGER
    isAlive AS BOOLEAN

    SUB Init(monsterName AS STRING, hp AS INTEGER, atk AS INTEGER)
        ME.name = monsterName
        ME.health = hp
        ME.maxHealth = hp
        ME.attack = atk
        ME.isAlive = 1
    END SUB

    SUB TakeDamage(dmg AS INTEGER)
        ME.health = ME.health - dmg
        IF ME.health <= 0 THEN
            ME.health = 0
            ME.isAlive = 0
        END IF
    END SUB

    FUNCTION GetAttack() AS INTEGER
        RETURN ME.attack
    END FUNCTION

    SUB DisplayStatus()
        PRINT ME.name; " [HP: "; ME.health; "/"; ME.maxHealth; "]";
        IF ME.isAlive = 0 THEN
            PRINT " **DEAD**"
        ELSE
            PRINT ""
        END IF
    END SUB
END CLASS

REM ===== PLAYER CLASS =====
CLASS Player
    name AS STRING
    health AS INTEGER
    maxHealth AS INTEGER
    gold AS INTEGER
    weapon AS INTEGER
    armor AS INTEGER

    SUB Init(playerName AS STRING)
        ME.name = playerName
        ME.health = 100
        ME.maxHealth = 100
        ME.gold = 0
        ME.weapon = 5
        ME.armor = 2
    END SUB

    SUB TakeDamage(dmg AS INTEGER)
        DIM actualDamage AS INTEGER
        actualDamage = dmg - ME.armor
        IF actualDamage < 1 THEN
            actualDamage = 1
        END IF
        ME.health = ME.health - actualDamage
        IF ME.health < 0 THEN
            ME.health = 0
        END IF
        PRINT ">> "; ME.name; " takes "; actualDamage; " damage! (Armor blocked "; ME.armor; ")"
    END SUB

    SUB Heal(amount AS INTEGER)
        ME.health = ME.health + amount
        IF ME.health > ME.maxHealth THEN
            ME.health = ME.maxHealth
        END IF
        PRINT ">> Healed "; amount; " HP!"
    END SUB

    FUNCTION IsAlive() AS BOOLEAN
        RETURN ME.health > 0
    END FUNCTION

    FUNCTION GetAttackPower() AS INTEGER
        RETURN ME.weapon + 5
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

    SUB DisplayStatus()
        PRINT "==================================="
        PRINT "HERO: "; ME.name
        ME.DisplayHealthBar()
        PRINT "Gold: "; ME.gold; "g | Weapon: +"; ME.weapon; " | Armor: "; ME.armor
        PRINT "==================================="
    END SUB
END CLASS

REM ===== TESTING =====
PRINT "╔═══════════════════════════════════╗"
PRINT "║   DUNGEON OF VIPER - TEST V2     ║"
PRINT "╚═══════════════════════════════════╝"
PRINT

REM Create player
DIM hero AS Player
hero.Init("Sir Codealot")
hero.DisplayStatus()
PRINT

REM Create items
PRINT ">>> Found items in the dungeon:"
DIM sword AS Item
sword.Init("Steel Longsword", "A well-forged blade", 150, 1)
sword.Display()
PRINT

DIM potion AS Item
potion.Init("Health Potion", "Restores 20 HP", 25, 0)
potion.Display()
PRINT

REM Create monster
PRINT ">>> A monster appears!"
DIM goblin AS Monster
goblin.Init("Goblin Scout", 30, 8)
goblin.DisplayStatus()
PRINT

REM Combat simulation
PRINT "╔═══════════════════════════════════╗"
PRINT "║         COMBAT BEGINS!            ║"
PRINT "╚═══════════════════════════════════╝"
PRINT

DIM round AS INTEGER
round = 1

WHILE hero.IsAlive() AND goblin.isAlive
    PRINT "--- Round "; round; " ---"

    REM Player attacks
    DIM playerDmg AS INTEGER
    playerDmg = hero.GetAttackPower()
    PRINT hero.name; " attacks for "; playerDmg; " damage!"
    goblin.TakeDamage(playerDmg)
    goblin.DisplayStatus()

    IF goblin.isAlive THEN
        REM Monster attacks
        DIM monsterDmg AS INTEGER
        monsterDmg = goblin.GetAttack()
        PRINT goblin.name; " attacks!"
        hero.TakeDamage(monsterDmg)
        hero.DisplayHealthBar()
    ELSE
        PRINT ">>> "; goblin.name; " defeated!"
        hero.gold = hero.gold + 10
        PRINT ">>> Gained 10 gold!"
    END IF

    PRINT
    round = round + 1

    IF round > 20 THEN
        PRINT ">>> Combat timeout!"
        END
    END IF
WEND

IF hero.IsAlive() = 0 THEN
    PRINT "╔═══════════════════════════════════╗"
    PRINT "║          GAME OVER!               ║"
    PRINT "╚═══════════════════════════════════╝"
ELSE
    PRINT "╔═══════════════════════════════════╗"
    PRINT "║          VICTORY!                 ║"
    PRINT "╚═══════════════════════════════════╝"
    hero.DisplayStatus()
END IF

PRINT
PRINT "=== Test Complete ==="
