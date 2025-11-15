CLASS Board
    DIM cells(4) AS INTEGER
END CLASS

DIM b AS Board
b = NEW Board()

b.cells(0) = 1
b.cells(1) = 2

PRINT b.cells(0)
PRINT b.cells(1)
PRINT b.cells(0) + b.cells(1)
