CLASS Database
    SUB ListAll()
        DIM line$ AS STRING
        PRINT line$
    END SUB
END CLASS

DIM d AS Database
d = NEW Database()
d.ListAll()
END
