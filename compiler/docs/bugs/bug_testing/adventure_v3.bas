' The Dungeon of Viper - v3 working around array field bugs
' Testing: OOP without array fields, complex logic, colors

CONST ESC$ = CHR$(27)
CONST RESET$ = ESC$ + "[0m"
CONST RED$ = ESC$ + "[31m"
CONST GREEN$ = ESC$ + "[32m"
CONST YELLOW$ = ESC$ + "[33m"
CONST CYAN$ = ESC$ + "[96m"

CLASS Player
    DIM name AS STRING
    DIM health AS INTEGER
    DIM maxHealth AS INTEGER
    DIM attack AS INTEGER
    DIM defense AS INTEGER
    DIM gold AS INTEGER
    DIM level AS INTEGER

    SUB Init(playerName AS STRING)
        name = playerName
        health = 100
        maxHealth = 100
        attack = 10
        defense = 5
        gold = 0
        level = 1
    END SUB

    SUB TakeDamage(damage AS INTEGER)
        DIM actualDamage AS INTEGER
        actualDamage = damage - defense
        IF actualDamage < 1 THEN
            actualDamage = 1
        END IF
        health = health - actualDamage
        IF health < 0 THEN
            health = 0
        END IF
    END SUB

    FUNCTION IsAlive() AS INTEGER
        IF health > 0 THEN
            RETURN 1
        ELSE
            RETURN 0
        END IF
    END FUNCTION

    SUB Heal(amount AS INTEGER)
        health = health + amount
        IF health > maxHealth THEN
            health = maxHealth
        END IF
    END SUB

    SUB AddGold(amount AS INTEGER)
        gold = gold + amount
    END SUB

    SUB LevelUp()
        level = level + 1
        maxHealth = maxHealth + 10
        health = maxHealth
        attack = attack + 2
        defense = defense + 1
    END SUB
END CLASS

CLASS Monster
    DIM name AS STRING
    DIM health AS INTEGER
    DIM maxHealth AS INTEGER
    DIM attack AS INTEGER
    DIM goldDrop AS INTEGER

    SUB Init(monsterName AS STRING, hp AS INTEGER, atk AS INTEGER, gold AS INTEGER)
        name = monsterName
        health = hp
        maxHealth = hp
        attack = atk
        goldDrop = gold
    END SUB

    FUNCTION IsAlive() AS INTEGER
        IF health > 0 THEN
            RETURN 1
        ELSE
            RETURN 0
        END IF
    END FUNCTION

    SUB TakeDamage(damage AS INTEGER)
        health = health - damage
        IF health < 0 THEN
            health = 0
        END IF
    END SUB
END CLASS

SUB ShowPlayerStatus(p AS Player)
    PRINT CYAN$ + "=== Player Status ===" + RESET$
    PRINT "Name: " + p.name + " (Level " + STR$(p.level) + ")"
    PRINT "Health: " + GREEN$ + STR$(p.health) + RESET$ + "/" + STR$(p.maxHealth)
    PRINT "Attack: " + STR$(p.attack) + "  Defense: " + STR$(p.defense)
    PRINT "Gold: " + YELLOW$ + STR$(p.gold) + RESET$
    PRINT ""
END SUB

SUB Combat(p AS Player, m AS Monster)
    PRINT RED$ + "A " + m.name + " appears!" + RESET$
    PRINT ""

    DIM round AS INTEGER
    round = 1

    DO WHILE p.IsAlive()
        IF NOT m.IsAlive() THEN
            EXIT DO
        END IF
        PRINT "Round " + STR$(round) + ":"

        ' Player attacks
        m.TakeDamage(p.attack)
        PRINT "You attack for " + STR$(p.attack) + " damage!"

        IF m.IsAlive() THEN
            ' Monster attacks back
            p.TakeDamage(m.attack)
            PRINT RED$ + m.name + " attacks for " + STR$(m.attack) + " damage!" + RESET$
        ELSE
            PRINT GREEN$ + m.name + " is defeated!" + RESET$
        END IF

        PRINT "Your HP: " + STR$(p.health) + "  " + m.name + " HP: " + STR$(p.health)
        PRINT ""

        round = round + 1
    LOOP

    IF p.IsAlive() THEN
        PRINT GREEN$ + "Victory!" + RESET$
        p.AddGold(m.goldDrop)
        PRINT "You gained " + YELLOW$ + STR$(m.goldDrop) + " gold" + RESET$
    ELSE
        PRINT RED$ + "You have been defeated!" + RESET$
    END IF
    PRINT ""
END SUB

' Main game
PRINT CYAN$ + "======================================" + RESET$
PRINT CYAN$ + "   THE DUNGEON OF VIPER" + RESET$
PRINT CYAN$ + "======================================" + RESET$
PRINT ""

DIM hero AS Player
hero = NEW Player()
hero.Init("Brave Adventurer")

ShowPlayerStatus(hero)

' First encounter
DIM goblin AS Monster
goblin = NEW Monster()
goblin.Init("Goblin", 20, 5, 10)
Combat(hero, goblin)

ShowPlayerStatus(hero)

' Heal
PRINT "You find a healing potion!"
hero.Heal(30)
PRINT GREEN$ + "Restored 30 HP" + RESET$
PRINT ""

ShowPlayerStatus(hero)

' Second encounter
DIM orc AS Monster
orc = NEW Monster()
orc.Init("Orc Warrior", 35, 8, 25)
Combat(hero, orc)

IF hero.IsAlive() THEN
    ShowPlayerStatus(hero)
    PRINT GREEN$ + "You have survived the dungeon!" + RESET$
    PRINT "Final gold: " + YELLOW$ + STR$(hero.gold) + RESET$
END IF

END
