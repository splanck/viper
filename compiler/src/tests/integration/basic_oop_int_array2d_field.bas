CLASS Board
    DIM cells(7, 7) AS INTEGER

    SUB New()
        me.cells(7, 0) = 4
        me.cells(7, 1) = 2
        me.cells(7, 2) = 3
    END SUB

    SUB Show()
        PRINT me.cells(7, 0)
        PRINT me.cells(7, 1)
        PRINT me.cells(7, 2)
    END SUB
END CLASS

DIM b AS Board
b = NEW Board()
b.Show()

