REM Contact Database - OOP Final Working Version

CLASS ContactDatabase
    DIM recordCount AS INTEGER

    SUB NEW()
        recordCount = 0
    END SUB

    SUB SetCount(n AS INTEGER)
        recordCount = n
    END SUB

    SUB IncrementCount()
        recordCount = recordCount + 1
    END SUB

    SUB ShowCount()
        PRINT "Total records: " + STR$(recordCount)
    END SUB
END CLASS

PRINT "=== Contact Database OOP ==="
PRINT "Demonstrating Working OOP Features"
PRINT ""

PRINT "Creating database object..."
DIM db AS ContactDatabase
db = NEW ContactDatabase()
db.ShowCount()
PRINT ""

PRINT "Setting initial count..."
db.SetCount(3)
db.ShowCount()
PRINT ""

PRINT "Incrementing count..."
db.IncrementCount()
db.ShowCount()
PRINT ""

PRINT "Incrementing again..."
db.IncrementCount()
db.ShowCount()
PRINT ""

PRINT "OOP demonstration complete!"
