' The Dungeon of Viper - v2 with workarounds
' Testing: OOP, integer arrays, complex game logic

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

    SUB TakeDamage(damage AS INTEGER)
        health = health - damage
        IF health < 0 THEN
            health = 0
        END IF
    END SUB
END CLASS

CLASS Room
    DIM id AS INTEGER
    DIM description AS STRING
    DIM hasMonster AS INTEGER
    DIM hasTreasure AS INTEGER
    DIM visited AS INTEGER
    DIM exits(4) AS INTEGER  ' 0=north, 1=south, 2=east, 3=west (-1 = no exit)

    SUB Init(roomId AS INTEGER, desc AS STRING)
        id = roomId
        description = desc
        hasMonster = 0
        hasTreasure = 0
        visited = 0
        exits(0) = -1
        exits(1) = -1
        exits(2) = -1
        exits(3) = -1
    END SUB

    SUB SetExit(direction AS INTEGER, targetRoom AS INTEGER)
        exits(direction) = targetRoom
    END SUB

    FUNCTION GetExit(direction AS INTEGER) AS INTEGER
        DIM result AS INTEGER
        result = exits(direction)
        RETURN result
    END FUNCTION
END CLASS

' Test Monster class
DIM goblin AS Monster
goblin = NEW Monster()
goblin.Init("Goblin", 30, 8, 10)

PRINT "Monster: "; goblin.name
PRINT "Health: "; goblin.health
PRINT "Attack: "; goblin.attack
PRINT "Gold: "; goblin.goldDrop
PRINT "Is Alive: "; goblin.IsAlive()

goblin.TakeDamage(35)
PRINT "After 35 damage: "; goblin.health
PRINT "Is Alive: "; goblin.IsAlive()

PRINT ""

' Test Room class
DIM room1 AS Room
room1 = NEW Room()
room1.Init(1, "A dark dungeon entrance")

PRINT "Room ID: "; room1.id
PRINT "Description: "; room1.description
PRINT "Visited: "; room1.visited

room1.SetExit(0, 2)  ' North leads to room 2
room1.SetExit(2, 3)  ' East leads to room 3

PRINT "North exit: "; room1.GetExit(0)
PRINT "South exit: "; room1.GetExit(1)
PRINT "East exit: "; room1.GetExit(2)
PRINT "West exit: "; room1.GetExit(3)

END
