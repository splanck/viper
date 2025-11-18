CLASS Board
    DIM cells(7, 7) AS INTEGER

    SUB New()
        cells(7, 0) = 10
        cells(7, 1) = 20
        cells(7, 2) = 30
    END SUB

    SUB Show()
        PRINT cells(7, 0)
        PRINT cells(7, 1)
        PRINT cells(7, 2)
    END SUB
END CLASS

DIM b AS Board
b = NEW Board()
b.Show()

