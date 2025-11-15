' The Dungeon of Viper - Working version avoiding known bugs
' Testing: OOP basics, colors, game logic, SELECT CASE, loops

CONST ESC$ = CHR$(27)
CONST RESET$ = ESC$ + "[0m"
CONST RED$ = ESC$ + "[31m"
CONST GREEN$ = ESC$ + "[32m"
CONST YELLOW$ = ESC$ + "[33m"
CONST CYAN$ = ESC$ + "[96m"
CONST MAGENTA$ = ESC$ + "[35m"

CLASS Player
    DIM name AS STRING
    DIM health AS INTEGER
    DIM maxHealth AS INTEGER
    DIM attack AS INTEGER
    DIM defense AS INTEGER
    DIM gold AS INTEGER

    SUB Init(playerName AS STRING)
        name = playerName
        health = 100
        maxHealth = 100
        attack = 10
        defense = 5
        gold = 0
    END SUB

    FUNCTION IsAlive() AS INTEGER
        IF health > 0 THEN
            RETURN 1
        ELSE
            RETURN 0
        END IF
    END FUNCTION
END CLASS

CLASS Monster
    DIM name AS STRING
    DIM health AS INTEGER
    DIM attack AS INTEGER
    DIM goldDrop AS INTEGER

    SUB Init(monsterName AS STRING, hp AS INTEGER, atk AS INTEGER, gold AS INTEGER)
        name = monsterName
        health = hp
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
END CLASS

' Game start
PRINT CYAN$ + "======================================" + RESET$
PRINT CYAN$ + "   THE DUNGEON OF VIPER" + RESET$
PRINT CYAN$ + "======================================" + RESET$
PRINT ""

DIM hero AS Player
hero = NEW Player()
hero.Init("Brave Hero")

PRINT "Welcome, " + hero.name + "!"
PRINT "You enter a dark dungeon..."
PRINT ""

' === ENCOUNTER 1: Goblin ===
DIM goblin AS Monster
goblin = NEW Monster()
goblin.Init("Goblin", 25, 6, 15)

PRINT RED$ + "A " + goblin.name + " appears!" + RESET$
PRINT goblin.name + " HP: " + STR$(goblin.health)
PRINT ""

DIM round AS INTEGER
round = 1

' Combat loop
DO WHILE hero.IsAlive()
    IF NOT goblin.IsAlive() THEN
        EXIT DO
    END IF

    PRINT "Round " + STR$(round) + ":"

    ' Player attacks
    DIM damage AS INTEGER
    damage = hero.attack
    goblin.health = goblin.health - damage
    IF goblin.health < 0 THEN
        goblin.health = 0
    END IF
    PRINT "You attack for " + STR$(damage) + " damage!"

    IF goblin.IsAlive() THEN
        ' Monster counterattack
        DIM monsterDamage AS INTEGER
        monsterDamage = goblin.attack - hero.defense
        IF monsterDamage < 1 THEN
            monsterDamage = 1
        END IF
        hero.health = hero.health - monsterDamage
        IF hero.health < 0 THEN
            hero.health = 0
        END IF
        PRINT RED$ + goblin.name + " attacks for " + STR$(monsterDamage) + " damage!" + RESET$
        PRINT "Your HP: " + STR$(hero.health) + "/" + STR$(hero.maxHealth)
    ELSE
        PRINT GREEN$ + goblin.name + " is defeated!" + RESET$
        hero.gold = hero.gold + goblin.goldDrop
        PRINT "You gained " + YELLOW$ + STR$(goblin.goldDrop) + " gold!" + RESET$
    END IF

    PRINT ""
    round = round + 1
LOOP

IF NOT hero.IsAlive() THEN
    PRINT RED$ + "You have been defeated!" + RESET$
    PRINT "GAME OVER"
    END
END IF

' === REST AND HEAL ===
PRINT GREEN$ + "You find a rest area..." + RESET$
DIM healAmount AS INTEGER
healAmount = 40
hero.health = hero.health + healAmount
IF hero.health > hero.maxHealth THEN
    hero.health = hero.maxHealth
END IF
PRINT "Restored " + STR$(healAmount) + " HP"
PRINT "Current HP: " + STR$(hero.health) + "/" + STR$(hero.maxHealth)
PRINT ""

' === ENCOUNTER 2: Orc ===
DIM orc AS Monster
orc = NEW Monster()
orc.Init("Orc Warrior", 40, 9, 30)

PRINT RED$ + "An " + orc.name + " blocks your path!" + RESET$
PRINT orc.name + " HP: " + STR$(orc.health)
PRINT ""

round = 1

' Combat loop 2
DO WHILE hero.IsAlive()
    IF NOT orc.IsAlive() THEN
        EXIT DO
    END IF

    PRINT "Round " + STR$(round) + ":"

    ' Player attacks
    damage = hero.attack
    orc.health = orc.health - damage
    IF orc.health < 0 THEN
        orc.health = 0
    END IF
    PRINT "You attack for " + STR$(damage) + " damage!"

    IF orc.IsAlive() THEN
        ' Monster counterattack
        monsterDamage = orc.attack - hero.defense
        IF monsterDamage < 1 THEN
            monsterDamage = 1
        END IF
        hero.health = hero.health - monsterDamage
        IF hero.health < 0 THEN
            hero.health = 0
        END IF
        PRINT RED$ + orc.name + " attacks for " + STR$(monsterDamage) + " damage!" + RESET$
        PRINT "Your HP: " + STR$(hero.health) + "/" + STR$(hero.maxHealth)
    ELSE
        PRINT GREEN$ + orc.name + " is defeated!" + RESET$
        hero.gold = hero.gold + orc.goldDrop
        PRINT "You gained " + YELLOW$ + STR$(orc.goldDrop) + " gold!" + RESET$
    END IF

    PRINT ""
    round = round + 1
LOOP

' Victory or defeat
IF hero.IsAlive() THEN
    PRINT ""
    PRINT GREEN$ + "======================================" + RESET$
    PRINT GREEN$ + "   VICTORY!" + RESET$
    PRINT GREEN$ + "======================================" + RESET$
    PRINT ""
    PRINT "Final Status:"
    PRINT "HP: " + STR$(hero.health) + "/" + STR$(hero.maxHealth)
    PRINT "Gold: " + YELLOW$ + STR$(hero.gold) + RESET$
    PRINT ""
    PRINT "You have conquered the Dungeon of Viper!"
ELSE
    PRINT RED$ + "You have been defeated!" + RESET$
    PRINT "GAME OVER"
END IF

END
