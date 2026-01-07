' Test 08: Room class for adventure game
CLASS Room
    DIM name AS STRING
    DIM description AS STRING
    DIM visited AS INTEGER

    SUB Init(n AS STRING, d AS STRING)
        name = n
        description = d
        visited = 0
    END SUB

    SUB Describe()
        PRINT ""
        PRINT "=== "; name; " ==="
        PRINT description
        IF visited = 0 THEN
            PRINT "(This is your first time here)"
            visited = 1
        END IF
    END SUB
END CLASS

DIM entrance AS Room
DIM hallway AS Room

entrance = NEW Room()
entrance.Init("Castle Entrance", "A grand stone archway leads into darkness.")

hallway = NEW Room()
hallway.Init("Long Hallway", "Torches flicker along moss-covered walls.")

entrance.Describe()
hallway.Describe()
entrance.Describe()
END
