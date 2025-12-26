DIM name$ AS STRING
DIM age AS INTEGER

PRINT "What is your name?"
INPUT name$
PRINT "What is your age?"
INPUT age

PRINT "Hello, " + name$
PRINT "You are " + STR$(age) + " years old"
