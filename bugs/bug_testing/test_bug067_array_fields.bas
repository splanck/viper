REM Test BUG-067: Array Fields in Classes

CLASS Player
    name AS STRING
    inventory(10) AS STRING
    scores(5) AS INTEGER

    SUB Init(n AS STRING)
        ME.name = n
    END SUB

    SUB AddItem(slot AS INTEGER, item AS STRING)
        ME.inventory(slot) = item
    END SUB

    SUB SetScore(slot AS INTEGER, score AS INTEGER)
        ME.scores(slot) = score
    END SUB

    SUB ShowInventory()
        PRINT ME.name; "'s inventory:"
        DIM i AS INTEGER
        FOR i = 0 TO 2
            IF ME.inventory(i) <> "" THEN
                PRINT "  Slot "; i; ": "; ME.inventory(i)
            END IF
        NEXT i
    END SUB
END CLASS

PRINT "Testing array fields in classes..."
PRINT

DIM player AS Player
player = NEW Player()
player.Init("Hero")

player.AddItem(0, "Sword")
player.AddItem(1, "Shield")
player.AddItem(2, "Potion")

player.SetScore(0, 100)
player.SetScore(1, 200)

player.ShowInventory()

PRINT
PRINT "SUCCESS: Array fields in classes work!"
