PRINT "=== CURSOR ON/OFF Test ==="
PRINT ""
CURSOR OFF
PRINT "1. Cursor is now hidden (invisible)"
PRINT "2. You should not see a blinking cursor"
FOR i = 1 TO 3
    PRINT "   Processing step "; i
NEXT i
PRINT ""
CURSOR ON
PRINT "3. Cursor is now visible again"
PRINT "4. You should see the cursor blinking"
PRINT ""
PRINT "Test complete!"
