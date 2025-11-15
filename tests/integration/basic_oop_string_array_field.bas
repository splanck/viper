CLASS Player
    DIM inventory(2) AS STRING
    SUB AddItem(item AS STRING)
        inventory(0) = item
    END SUB
END CLASS

DIM p AS Player
p = NEW Player()
p.AddItem("Sword")
PRINT p.inventory(0)

