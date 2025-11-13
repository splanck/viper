REM Test TIMER and SLEEP with math computations
PRINT "=== Timing Math Computations ==="

start# = TIMER()
PRINT "Starting time: "; start#

PRINT "Computing factorial using loop..."
n% = 10
result% = 1
FOR i% = 1 TO n%
    result% = result% * i%
NEXT i%
PRINT "10! = "; result%

PRINT "Computing 1000 trigonometric operations..."
FOR i% = 1 TO 1000
    x# = SIN(i%) + COS(i%)
NEXT i%

finish# = TIMER()
PRINT "Finishing time: "; finish#
elapsed# = finish# - start#
PRINT "Elapsed time: "; elapsed#; " seconds"

PRINT ""
PRINT "=== Testing SLEEP with SWAP ==="
a% = 100
b% = 200
PRINT "Before SWAP: a="; a%; " b="; b%

PRINT "Swapping..."
SWAP a%, b%

PRINT "Sleeping for 1 second..."
SLEEP 1

PRINT "After SWAP and SLEEP: a="; a%; " b="; b%

PRINT ""
PRINT "Timing tests completed!"
