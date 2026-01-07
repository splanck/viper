RANDOMIZE 7
LET N = 8
DIM A(N)

LET I = 0
WHILE I < N
  LET A(I) = INT(RND() * 100#)
  LET I = I + 1
WEND

FOR I = 0 TO N - 2
  FOR J = 0 TO N - 2
    IF A(J) > A(J + 1) THEN LET T = A(J) : LET A(J) = A(J + 1) : LET A(J + 1) = T
  NEXT J
NEXT I

FOR I = 0 TO N - 1
  PRINT A(I)
NEXT I
END
