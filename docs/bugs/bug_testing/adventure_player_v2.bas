REM ===================================================================
REM PLAYER CLASS V2 - Player character with stats, inventory, location
REM Testing: OOP, field arrays, methods, boolean returns
REM No color references to avoid global variable bugs
REM ===================================================================

CLASS Player
    DIM name AS STRING
    DIM health AS INTEGER
    DIM maxHealth AS INTEGER
    DIM attack AS INTEGER
    DIM defense AS INTEGER
    DIM gold AS INTEGER
    DIM currentRoom AS INTEGER
    DIM inventory(10) AS STRING
    DIM inventoryCount AS INTEGER

    SUB Init(playerName AS STRING)
        name = playerName
        health = 100
        maxHealth = 100
        attack = 10
        defense = 5
        gold = 0
        currentRoom = 0
        inventoryCount = 0
    END SUB

    FUNCTION IsAlive() AS BOOLEAN
        RETURN health > 0
    END FUNCTION

    SUB TakeDamage(dmg AS INTEGER)
        DIM actual AS INTEGER
        actual = dmg - defense
        IF actual < 1 THEN
            actual = 1
        END IF
        health = health - actual
        IF health < 0 THEN
            health = 0
        END IF
    END SUB

    SUB Heal(amount AS INTEGER)
        health = health + amount
        IF health > maxHealth THEN
            health = maxHealth
        END IF
    END SUB

    FUNCTION AddItem(item AS STRING) AS BOOLEAN
        IF inventoryCount >= 10 THEN
            RETURN FALSE
        END IF
        inventory(inventoryCount) = item
        inventoryCount = inventoryCount + 1
        RETURN TRUE
    END FUNCTION

    SUB ShowStatus()
        PRINT "==== " + name + " ===="
        PRINT "Health: " + STR$(health) + "/" + STR$(maxHealth)
        PRINT "Attack: " + STR$(attack) + " Defense: " + STR$(defense)
        PRINT "Gold: " + STR$(gold)
        PRINT "Items: " + STR$(inventoryCount) + "/10"
    END SUB

    SUB ShowInventory()
        DIM i AS INTEGER
        PRINT "=== Inventory ==="
        IF inventoryCount = 0 THEN
            PRINT "Empty"
        ELSE
            FOR i = 0 TO inventoryCount - 1
                PRINT STR$(i + 1) + ". " + inventory(i)
            NEXT i
        END IF
    END SUB
END CLASS
