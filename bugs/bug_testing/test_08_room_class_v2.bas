' Test 08b: Room class (simplified)
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
    END SUB

    SUB MarkVisited()
        visited = 1
    END SUB
END CLASS

DIM entrance AS Room

entrance = NEW Room()
entrance.Init("Castle Entrance", "A grand stone archway leads into darkness.")
entrance.Describe()
entrance.MarkVisited()
PRINT "Visited: "; entrance.visited
END
