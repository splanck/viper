' File: examples/basic/sine_cosine.bas
' Purpose: Demonstrates SIN, COS, and POW runtime helpers.
' Prints a small table of angles for 0 and pi/2.

LET HALFPI# = 1.57079632679#
PRINT "ANGLE","SIN","COS"
PRINT 0#; SIN(0#); COS(0#)
PRINT HALFPI#; SIN(HALFPI#); COS(HALFPI#)
PRINT POW(2#, 10#)
