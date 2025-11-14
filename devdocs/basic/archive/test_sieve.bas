' simple sieve using 0/1 flags
DIM N AS INTEGER
N = 50000
DIM isPrime(N + 1)
DIM i AS INTEGER, j AS INTEGER, count AS INTEGER
FOR i = 0 TO N
  isPrime(i) = 1
NEXT i
isPrime(0) = 0: isPrime(1) = 0
FOR i = 2 TO SQR(N)
  IF isPrime(i) <> 0 THEN
    j = i * i
    WHILE j <= N
      isPrime(j) = 0
      j = j + i
    WEND
  END IF
NEXT i
count = 0
FOR i = 2 TO N
  IF isPrime(i) <> 0 THEN count = count + 1
NEXT i
PRINT "primes up to"; N; "="; count
