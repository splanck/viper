CLASS Contact
    DIM name$ AS STRING
END CLASS

DIM c AS Contact
c = NEW Contact()
c.name$ = "Alice"
PRINT c.name$
END
