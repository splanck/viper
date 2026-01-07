REM ╔════════════════════════════════════════════════════════╗
REM ║          DUNGEON OF VIPER V4                          ║
REM ║      OOP Stress Test for VIPER BASIC                  ║
REM ╚════════════════════════════════════════════════════════╝
REM
REM Workarounds applied:
REM - BUG-067: No array fields in classes
REM - BUG-068: Must use RETURN keyword in functions
REM - BUG-069: Must use NEW to allocate objects
REM - BUG-070: Use INTEGER instead of BOOLEAN for parameters

REM ===== ITEM CLASS =====
CLASS Item
    name AS STRING
    description AS STRING
    goldValue AS INTEGER
    attackBonus AS INTEGER

    SUB SetData(itemName AS STRING, desc AS STRING, gold AS INTEGER, atk AS INTEGER)
        ME.name = itemName
        ME.description = desc
        ME.goldValue = gold
        ME.attackBonus = atk
    END SUB

    SUB Display()
        PRINT "  ["; ME.name; "]"
        PRINT "  "; ME.description
        IF ME.attackBonus > 0 THEN
            PRINT "  Attack: +"; ME.attackBonus
        END IF
        PRINT "  Value: "; ME.goldValue; " gold"
    END SUB
END CLASS

REM ===== MONSTER CLASS =====
CLASS Monster
    name AS STRING
    health AS INTEGER
    maxHealth AS INTEGER
    attack AS INTEGER

    SUB SetData(monsterName AS STRING, hp AS INTEGER, atk AS INTEGER)
        ME.name = monsterName
        ME.health = hp
        ME.maxHealth = hp
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

    SUB DisplayStatus()
        DIM pct AS INTEGER
        DIM bars AS INTEGER
        DIM i AS INTEGER

        pct = (ME.health * 100) / ME.maxHealth
        bars = pct / 5

        PRINT ME.name; " [";
        FOR i = 1 TO 20
            IF i <= bars THEN
                PRINT "█";
            ELSE
                PRINT "░";
            END IF
        NEXT i
        PRINT "] "; ME.health; "/"; ME.maxHealth
    END SUB
END CLASS

REM ===== PLAYER CLASS =====
CLASS Player
    name AS STRING
    health AS INTEGER
    maxHealth AS INTEGER
    gold AS INTEGER
    weaponPower AS INTEGER
    armorClass AS INTEGER

    SUB SetData(playerName AS STRING)
        ME.name = playerName
        ME.health = 100
        ME.maxHealth = 100
        ME.gold = 0
        ME.weaponPower = 5
        ME.armorClass = 1
    END SUB

    SUB TakeDamage(dmg AS INTEGER)
        DIM actualDamage AS INTEGER
        actualDamage = dmg - ME.armorClass
        IF actualDamage < 1 THEN
            actualDamage = 1
        END IF
        ME.health = ME.health - actualDamage
        IF ME.health < 0 THEN
            ME.health = 0
        END IF
        PRINT "    >> "; ME.name; " takes "; actualDamage; " damage!"
        IF dmg > actualDamage THEN
            PRINT "    >> (Armor blocked "; ME.armorClass; " damage)"
        END IF
    END SUB

    SUB Heal(amount AS INTEGER)
        ME.health = ME.health + amount
        IF ME.health > ME.maxHealth THEN
            ME.health = ME.maxHealth
        END IF
    END SUB

    FUNCTION IsAlive() AS INTEGER
        IF ME.health > 0 THEN
            RETURN 1
        ELSE
            RETURN 0
        END IF
    END FUNCTION

    FUNCTION GetAttack() AS INTEGER
        REM Random variance: base damage +/- 2
        DIM variance AS INTEGER
        variance = (RND() * 5) - 2
        RETURN ME.weaponPower + variance
    END FUNCTION

    SUB DisplayHealthBar()
        DIM pct AS INTEGER
        DIM bars AS INTEGER
        DIM i AS INTEGER

        pct = (ME.health * 100) / ME.maxHealth
        bars = pct / 5

        PRINT "  HP: [";
        FOR i = 1 TO 20
            IF i <= bars THEN
                IF pct > 60 THEN
                    PRINT "█";
                ELSEIF pct > 30 THEN
                    PRINT "▓";
                ELSE
                    PRINT "▒";
                END IF
            ELSE
                PRINT "░";
            END IF
        NEXT i
        PRINT "] "; ME.health; "/"; ME.maxHealth
    END SUB

    SUB DisplayStatus()
        PRINT "╔═══════════════════════════════════════╗"
        PRINT "║ HERO: "; ME.name
        PRINT "╠═══════════════════════════════════════╣"
        ME.DisplayHealthBar()
        PRINT "  Gold: "; ME.gold; "g  │  Weapon: +"; ME.weaponPower; "  │  Armor: "; ME.armorClass
        PRINT "╚═══════════════════════════════════════╝"
    END SUB
END CLASS

REM ╔════════════════════════════════════════════════════════╗
REM ║                    MAIN PROGRAM                        ║
REM ╚════════════════════════════════════════════════════════╝

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║                DUNGEON OF VIPER                        ║"
PRINT "║             A Text Adventure Demo                      ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

REM Create player using NEW (BUG-069 workaround)
DIM hero AS Player
hero = NEW Player()
hero.SetData("Sir Codealot the Brave")

PRINT "Welcome, "; hero.name; "!"
PRINT "You enter a dark dungeon..."
PRINT

REM Create items
DIM sword AS Item
sword = NEW Item()
sword.SetData("Steel Longsword", "A finely crafted blade", 150, 3)

DIM potion AS Item
potion = NEW Item()
potion.SetData("Health Potion", "Restores 30 HP instantly", 25, 0)

PRINT "You find some items scattered on the floor:"
sword.Display()
PRINT
potion.Display()
PRINT

PRINT "You take the sword! (Attack +3)"
hero.weaponPower = hero.weaponPower + sword.attackBonus
PRINT

REM Create monsters
DIM goblin1 AS Monster
goblin1 = NEW Monster()
goblin1.SetData("Goblin Warrior", 40, 12)

DIM goblin2 AS Monster
goblin2 = NEW Monster()
goblin2.SetData("Goblin Archer", 30, 10)

REM ===== COMBAT SIMULATION =====
PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║              ⚔️  COMBAT BEGINS!  ⚔️                    ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

DIM currentEnemy AS Monster
currentEnemy = goblin1

PRINT "A "; currentEnemy.name; " blocks your path!"
PRINT

DIM round AS INTEGER
round = 1
DIM maxRounds AS INTEGER
maxRounds = 15

WHILE hero.IsAlive() AND currentEnemy.IsAlive() AND round <= maxRounds
    PRINT "┌─────────────[ Round "; round; " ]─────────────┐"
    PRINT

    REM Player's turn
    DIM playerDmg AS INTEGER
    playerDmg = hero.GetAttack()
    PRINT "  ⚔ "; hero.name; " attacks!"
    PRINT "    >> Deals "; playerDmg; " damage!"
    currentEnemy.TakeDamage(playerDmg)
    PRINT "  ";
    currentEnemy.DisplayStatus()
    PRINT

    IF currentEnemy.IsAlive() THEN
        REM Enemy's turn
        DIM enemyDmg AS INTEGER
        enemyDmg = currentEnemy.attack
        PRINT "  ⚔ "; currentEnemy.name; " counter-attacks!"
        hero.TakeDamage(enemyDmg)
        PRINT "  ";
        hero.DisplayHealthBar()
    ELSE
        PRINT "  ═══ "; currentEnemy.name; " defeated! ═══"
        DIM goldDrop AS INTEGER
        goldDrop = 15
        hero.gold = hero.gold + goldDrop
        PRINT "  💰 Gained "; goldDrop; " gold!"
    END IF

    PRINT
    round = round + 1
WEND

REM Check for second enemy if first defeated
IF hero.IsAlive() AND currentEnemy.IsAlive() = 0 THEN
    PRINT
    PRINT "But wait! Another enemy appears!"
    PRINT

    REM Use potion before next fight
    PRINT "You quickly use the Health Potion!"
    hero.Heal(30)
    hero.DisplayHealthBar()
    PRINT

    currentEnemy = goblin2
    PRINT "A "; currentEnemy.name; " emerges from the shadows!"
    PRINT

    round = 1
    WHILE hero.IsAlive() AND currentEnemy.IsAlive() AND round <= maxRounds
        PRINT "┌─────────────[ Round "; round; " ]─────────────┐"
        PRINT

        DIM dmg2 AS INTEGER
        dmg2 = hero.GetAttack()
        PRINT "  ⚔ "; hero.name; " attacks!"
        PRINT "    >> Deals "; dmg2; " damage!"
        currentEnemy.TakeDamage(dmg2)
        PRINT "  ";
        currentEnemy.DisplayStatus()
        PRINT

        IF currentEnemy.IsAlive() THEN
            DIM edmg2 AS INTEGER
            edmg2 = currentEnemy.attack
            PRINT "  ⚔ "; currentEnemy.name; " shoots an arrow!"
            hero.TakeDamage(edmg2)
            PRINT "  ";
            hero.DisplayHealthBar()
        ELSE
            PRINT "  ═══ "; currentEnemy.name; " defeated! ═══"
            DIM gold2 AS INTEGER
            gold2 = 20
            hero.gold = hero.gold + gold2
            PRINT "  💰 Gained "; gold2; " gold!"
        END IF

        PRINT
        round = round + 1
    WEND
END IF

REM Final result
PRINT
PRINT "╔════════════════════════════════════════════════════════╗"
IF hero.IsAlive() = 0 THEN
    PRINT "║              💀 GAME OVER 💀                          ║"
    PRINT "║         You have been defeated...                     ║"
ELSE
    PRINT "║              🎉 VICTORY! 🎉                           ║"
    PRINT "║     You have cleared the dungeon!                     ║"
END IF
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

hero.DisplayStatus()

PRINT
PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║           OOP STRESS TEST COMPLETE!                    ║"
PRINT "║   Features Tested:                                     ║"
PRINT "║   ✓ Classes with multiple fields                       ║"
PRINT "║   ✓ Methods and functions with RETURN                  ║"
PRINT "║   ✓ Object creation with NEW keyword                   ║"
PRINT "║   ✓ ME keyword for field access                        ║"
PRINT "║   ✓ String concatenation in PRINT                      ║"
PRINT "║   ✓ Integer arithmetic and conditions                  ║"
PRINT "║   ✓ FOR loops with variable iteration                  ║"
PRINT "║   ✓ WHILE loops with complex conditions                ║"
PRINT "║   ✓ Multiple object instances                          ║"
PRINT "║   ✓ IF/ELSEIF/ELSE conditionals                        ║"
PRINT "║   ✓ Box-drawing characters (UTF-8)                     ║"
PRINT "╚════════════════════════════════════════════════════════╝"
