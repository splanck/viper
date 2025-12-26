REM Test GOSUB/RETURN with new math features
10 PRINT "=== Testing GOSUB with Math Functions ==="
20 x% = -5
30 GOSUB 100
40 x% = 0
50 GOSUB 100
60 x% = 10
70 GOSUB 100
80 GOTO 200

100 REM Subroutine: Analyze number
110 PRINT "Number: "; x%; " Sign: "; SGN(x%)
120 IF SGN(x%) < 0 THEN PRINT "  Negative - EXP: "; EXP(ABS(x%))
130 IF SGN(x%) = 0 THEN PRINT "  Zero - LOG not applicable"
140 IF SGN(x%) > 0 THEN PRINT "  Positive - LOG: "; LOG(x%)
150 RETURN

200 PRINT ""
210 PRINT "=== Testing SWAP in GOSUB context ==="
220 a% = 100
230 b% = 200
240 PRINT "Before: a="; a%; " b="; b%
250 GOSUB 300
260 PRINT "After: a="; a%; " b="; b%
270 GOTO 400

300 REM Subroutine: Swap variables
310 SWAP a%, b%
320 PRINT "Inside GOSUB: a="; a%; " b="; b%
330 RETURN

400 PRINT ""
410 PRINT "All GOSUB tests passed!"
420 END
