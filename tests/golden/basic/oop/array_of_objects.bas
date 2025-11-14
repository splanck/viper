CLASS Node
  DIM v AS INT
END CLASS
DIM a(2) AS Node
DIM n AS Node
n = NEW Node()
a(0) = n
a(0).v = 7
PRINT a(0).v
